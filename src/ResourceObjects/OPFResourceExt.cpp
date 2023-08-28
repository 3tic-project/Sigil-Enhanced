#include "ResourceObjects/OPFResource.h"
#include "BookManipulation/CleanSource.h"
#include "Misc/Utility.h"

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