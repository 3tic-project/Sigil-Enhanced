#include "ResourceObjects/OPFResource.h"
#include "BookManipulation/CleanSource.h"
#include "Misc/Utility.h"
#include "ResourceObjects/NavProcessor.h"

//---------------------------------------- modified: BulkResourceRenamed ---------------------------------------------------
void OPFResource::BulkResourceRenamed(const QList<Resource*>resources, const QList<QString>old_full_paths)
{
    QWriteLocker locker(&GetLock());
    QString source = CleanSource::ProcessXML(GetText(), "application/oebps-package+xml");
    OPFParser p;
    p.parse(source);
    if (p.m_manifest.isEmpty()) return;

    QString old_id, new_id, old_bkpath, old_href, old_full_path;
    QList<QString> old_bkpaths, old_hrefs;
    QHash<QString, QString> id_maps;

    // first convert old_full_path to old_bkpath
    foreach(QString old_full_path, old_full_paths) {
        old_bkpath = old_full_path.right(old_full_path.length() - GetFullPathToBookFolder().length() - 1);
        old_href = Utility::URLEncodePath(Utility::buildRelativePath(GetRelativePath(), old_bkpath));
        old_bkpaths << old_bkpath;
        old_hrefs << old_href;
    }
    // convert resources to a none const type
    QList<Resource*>_resources;
    foreach(Resource * resource, resources) {
        _resources << resource;
    }

    for (int i = 0; i < p.m_manifest.count(); ++i) {
        QString href = p.m_manifest.at(i).m_href;
        int j = 0;
        bool match = false;
        foreach(Resource * resource, _resources) {
            old_href = old_hrefs.at(j);
            if (href == old_href) {
                ManifestEntry me = p.m_manifest.at(i);
                QString old_me_href = me.m_href;
                me.m_href = Utility::URLEncodePath(GetRelativePathToResource(resource));
                old_id = me.m_id;
                p.m_idpos.remove(old_id);
                new_id = GetUniqueID(GetValidID(resource->Filename()), p);
                me.m_id = new_id;
                p.m_idpos[new_id] = i;
                p.m_hrefpos.remove(old_me_href);
                p.m_hrefpos[me.m_href] = i;
                p.m_manifest.replace(i, me);
                id_maps.insert(old_id, new_id);
                match = true;
                if (resource->Type() == Resource::NCXResourceType) {
                    // handle updating the ncx id on the spine if ncx renamed
                    QString ncx_id = p.m_spineattr.m_atts.value(QString("toc"), "");
                    if (new_id != ncx_id) {
                        p.m_spineattr.m_atts[QString("toc")] = new_id;
                    }
                }
                if (resource->Type() == Resource::ImageResourceType) {
                    if (IsCoverImageCheck(old_id, p)) {
                        AddCoverMetaForImage(resource, p);
                    }
                }
                break;
            }
            ++j;
        }
        if (match == true) {
            old_bkpaths.removeAt(j);
            old_hrefs.removeAt(j);
            _resources.removeAt(j);
        }
    }
    for (int i = 0; i < p.m_spine.count(); ++i) {
        QString idref = p.m_spine.at(i).m_idref;
        if (id_maps.value(idref) != "") {
            SpineEntry se = p.m_spine.at(i);
            new_id = id_maps.value(idref);
            se.m_idref = new_id;
            p.m_spine.replace(i, se);
        }
    }
    UpdateText(p);
}
//-------------------------------------------------------------------------------------------------------------

//-------------------------------- modified: BulkAddResource ----------------------------------------
void OPFResource::BulkAddResource(const QList<Resource*>resources) {
    QWriteLocker locker(&GetLock());
    QString source = CleanSource::ProcessXML(GetText(), "application/oebps-package+xml");
    OPFParser p;
    p.parse(source);
    foreach(Resource * resource, resources) {
        ManifestEntry me;
        me.m_id = GetUniqueID(GetValidID(resource->Filename()), p);
        me.m_href = Utility::URLEncodePath(GetRelativePathToResource(resource));
        me.m_mtype = GetResourceMimetype(resource);
        // Argh! If this is an new blank resource - it will have no content yet
        // so trying to parse it here to check for manifest properties is a mistake
        int n = p.m_manifest.count();
        p.m_manifest.append(me);
        p.m_idpos[me.m_id] = n;
        p.m_hrefpos[me.m_href] = n;
        if (resource->Type() == Resource::HTMLResourceType) {
            SpineEntry se;
            se.m_idref = me.m_id;
            p.m_spine.append(se);
        }
    }
    UpdateText(p);
}
//---------------------------------------------------------------------------------------------------

bool OPFResource::ApplyResourceBatch(const QList<ManifestResourceAddition> &additions,
                                     const QList<Resource *> &removals,
                                     const QHash<QString, Resource *> &relocations,
                                     QString *error)
{
    QWriteLocker locker(&GetLock());
    QString source = CleanSource::ProcessXML(GetText(), "application/oebps-package+xml");
    OPFParser p;
    p.parse(source);

    QSet<QString> removed_hrefs;
    QSet<QString> removed_ids;
    for (Resource *resource : removals) {
        const QString href = Utility::URLEncodePath(GetRelativePathToResource(resource));
        removed_hrefs.insert(href);
        const int position = p.m_hrefpos.value(href, -1);
        if (position >= 0) removed_ids.insert(p.m_manifest.at(position).m_id);
    }
    QSet<QString> relocated_source_hrefs;
    for (auto it = relocations.constBegin(); it != relocations.constEnd(); ++it) {
        const QString old_href = Utility::URLEncodePath(
            Utility::buildRelativePath(GetRelativePath(), it.key()));
        if (p.m_hrefpos.contains(old_href) && !removed_hrefs.contains(old_href)) {
            relocated_source_hrefs.insert(old_href);
        }
    }
    QSet<QString> target_hrefs;
    for (auto it = relocations.constBegin(); it != relocations.constEnd(); ++it) {
        const QString href = Utility::URLEncodePath(GetRelativePathToResource(it.value()));
        if ((p.m_hrefpos.contains(href) && !removed_hrefs.contains(href)
             && !relocated_source_hrefs.contains(href))
            || target_hrefs.contains(href)) {
            if (error) *error = QStringLiteral("A relocated resource path already exists in the manifest");
            return false;
        }
        target_hrefs.insert(href);
    }
    QSet<QString> addition_ids;
    for (const ManifestResourceAddition &addition : additions) {
        if (!addition.resource || addition.manifestId.isEmpty()
            || GetValidID(addition.manifestId) != addition.manifestId
            || (p.m_idpos.contains(addition.manifestId)
                && !removed_ids.contains(addition.manifestId))
            || addition_ids.contains(addition.manifestId)) {
            if (error) *error = QStringLiteral("A manifest ID is invalid or already exists");
            return false;
        }
        addition_ids.insert(addition.manifestId);
        const QString href = Utility::URLEncodePath(GetRelativePathToResource(addition.resource));
        if ((p.m_hrefpos.contains(href) && !removed_hrefs.contains(href)
             && !relocated_source_hrefs.contains(href))
            || target_hrefs.contains(href)) {
            if (error) *error = QStringLiteral("An added resource path already exists in the manifest");
            return false;
        }
        target_hrefs.insert(href);
    }

    for (Resource *resource : removals) {
        const QString href = Utility::URLEncodePath(GetRelativePathToResource(resource));
        int pos = -1;
        for (int index = 0; index < p.m_manifest.size(); ++index) {
            if (p.m_manifest.at(index).m_href == href) {
                pos = index;
                break;
            }
        }
        if (pos < 0) continue;
        const QString item_id = p.m_manifest.at(pos).m_id;
        if (resource->Type() == Resource::ImageResourceType) {
            RemoveCoverMetaForImage(resource, p);
        }
        for (int index = p.m_spine.size() - 1; index >= 0; --index) {
            if (p.m_spine.at(index).m_idref == item_id) p.m_spine.removeAt(index);
        }
        if (resource->Type() == Resource::HTMLResourceType) {
            RemoveAllGuideReferencesForResource(resource, p);
            if (GetEpubVersion().startsWith(QLatin1Char('3')) && GetNavResource()) {
                NavProcessor navproc(GetNavResource());
                navproc.RemoveAllLandmarksForResource(resource);
            }
        }
        p.m_manifest.removeAt(pos);
        p.m_idpos.remove(item_id);
        p.m_hrefpos.remove(href);
    }

    p.m_idpos.clear();
    p.m_hrefpos.clear();
    for (int index = 0; index < p.m_manifest.size(); ++index) {
        p.m_idpos.insert(p.m_manifest.at(index).m_id, index);
        p.m_hrefpos.insert(p.m_manifest.at(index).m_href, index);
    }

    for (auto it = relocations.constBegin(); it != relocations.constEnd(); ++it) {
        const QString old_href = Utility::URLEncodePath(
            Utility::buildRelativePath(GetRelativePath(), it.key()));
        const int pos = p.m_hrefpos.value(old_href, -1);
        if (pos < 0) continue;
        ManifestEntry entry = p.m_manifest.at(pos);
        p.m_hrefpos.remove(entry.m_href);
        entry.m_href = Utility::URLEncodePath(GetRelativePathToResource(it.value()));
        p.m_manifest.replace(pos, entry);
        p.m_hrefpos.insert(entry.m_href, pos);
    }

    for (const ManifestResourceAddition &addition : additions) {
        ManifestEntry entry;
        entry.m_id = addition.manifestId;
        entry.m_href = Utility::URLEncodePath(GetRelativePathToResource(addition.resource));
        entry.m_mtype = GetResourceMimetype(addition.resource);
        if (!addition.properties.isEmpty()) entry.m_atts.insert(QStringLiteral("properties"), addition.properties);
        if (!addition.fallback.isEmpty()) entry.m_atts.insert(QStringLiteral("fallback"), addition.fallback);
        if (!addition.overlay.isEmpty()) entry.m_atts.insert(QStringLiteral("media-overlay"), addition.overlay);
        const int pos = p.m_manifest.size();
        p.m_manifest.append(entry);
        p.m_idpos.insert(entry.m_id, pos);
        p.m_hrefpos.insert(entry.m_href, pos);
        if (addition.addToSpine && addition.resource->Type() == Resource::HTMLResourceType) {
            SpineEntry spine_entry;
            spine_entry.m_idref = entry.m_id;
            p.m_spine.append(spine_entry);
        }
    }

    UpdateText(p);
    return true;
}
