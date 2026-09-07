#include "EmbedPython/EmbeddedPython.h" // Python headers must precede Qt's slots macro.
#include "BookManipulation/NavigationRepair.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/OPFResource.h"
#include "MainUI/TOCModel.h"
#include "Misc/Utility.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "Widgets/TextDocument.h"

namespace {
const QString opfNS = QStringLiteral("http://www.idpf.org/2007/opf");

QString RelativeTarget(const QString &navPath, const QString &target)
{
    const QUrl url = QUrl::fromEncoded(target.toUtf8());
    QString href = Utility::URLEncodePath(Utility::buildRelativePath(navPath, url.path()));
    if (url.hasFragment()) href += "#" + url.fragment(QUrl::FullyEncoded);
    return href;
}

bool WriteEntries(QXmlStreamWriter &writer, const QList<TOCModel::TOCEntry> &entries,
                  const QString &navPath, const Book &book, QHash<QString, QSet<QString>> &targetIds)
{
    writer.writeStartElement("ol");
    for (const auto &entry : entries) {
        writer.writeStartElement("li");
        const QUrl target = QUrl::fromEncoded(entry.target.toUtf8());
        if (!entry.target.isEmpty()) {
            auto *resource = book.GetFolderKeeper()->GetResourceByBookPathNoThrow(target.path());
            if (!target.isRelative() || target.hasQuery()
                || !resource) return false;
            if (!target.fragment().isEmpty()) {
                if (!targetIds.contains(target.path())) {
                    auto *text = qobject_cast<TextResource *>(resource);
                    if (!text) return false;
                    QSet<QString> ids;
                    QXmlStreamReader document(text->GetText());
                    while (!document.atEnd()) {
                        document.readNext();
                        if (!document.isStartElement()) continue;
                        const auto attributes = document.attributes();
                        if (attributes.hasAttribute("id")) ids.insert(attributes.value("id").toString());
                        const auto xmlId = attributes.value("http://www.w3.org/XML/1998/namespace", "id");
                        if (!xmlId.isEmpty()) ids.insert(xmlId.toString());
                    }
                    if (document.hasError()) return false;
                    targetIds.insert(target.path(), ids);
                }
                if (!targetIds.value(target.path()).contains(target.fragment())) return false;
            }
            writer.writeStartElement("a");
            writer.writeAttribute("href", RelativeTarget(navPath, entry.target));
        } else {
            if (entry.children.isEmpty()) return false;
            writer.writeStartElement("span");
        }
        writer.writeCharacters(entry.text);
        writer.writeEndElement();
        if (!entry.children.isEmpty() && !WriteEntries(writer, entry.children, navPath, book, targetIds)) return false;
        writer.writeEndElement();
    }
    writer.writeEndElement();
    return true;
}
}

bool NavigationRepair::Prepare(const QSharedPointer<Book> &book, Plan &plan, QString &error)
{
    plan = Plan();
    error.clear();
    auto *opf = book->GetOPF();
    auto folder = book->GetFolderKeeper();
    if (!opf->GetEpubVersion().startsWith('3') || opf->GetNavResource()) {
        error = tr("Navigation repair is only available for EPUB 3 books without a navigation document.");
        return false;
    }
    plan.opfBefore = opf->GetSourceText();
    QXmlStreamReader reader(plan.opfBefore);
    QSet<QString> ids;
    QString declaredHref;
    int declarations = 0;
    int depth = 0;
    bool inManifest = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            if (depth == 2) inManifest = false;
            --depth;
        }
        if (!reader.isStartElement()) continue;
        ++depth;
        const auto attrs = reader.attributes();
        if (depth == 1 && (reader.namespaceUri() != opfNS || reader.name() != QLatin1String("package")
                          || attrs.value("version") != opf->GetEpubVersion())) {
            reader.raiseError("Expected an OPF package root");
            break;
        }
        if (attrs.hasAttribute("id")) ids.insert(attrs.value("id").toString());
        if (depth == 2 && reader.namespaceUri() == opfNS && reader.name() == QLatin1String("manifest")) inManifest = true;
        if (depth != 3 || !inManifest || reader.namespaceUri() != opfNS || reader.name() != QLatin1String("item")) continue;
        if (attrs.value("properties").toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).contains("nav")) {
            ++declarations;
            declaredHref = attrs.value("href").toString();
            if (attrs.value("media-type") != QLatin1String("application/xhtml+xml")) declarations += 2;
        }
    }
    if (reader.hasError() || declarations > 1) {
        error = tr("Repair requires well-formed OPF and at most one XHTML navigation declaration. Review the OPF first.");
        return false;
    }
    const QString opfFolder = opf->GetFolder();
    if (declarations == 1) {
        const QUrl href = QUrl::fromEncoded(declaredHref.toUtf8());
        if (!href.isRelative() || href.hasFragment() || href.hasQuery() || href.path().isEmpty()) {
            error = tr("The declared navigation path is not a local XHTML path. Review the OPF first.");
            return false;
        }
        plan.navBookPath = Utility::buildBookPath(href.path(), opfFolder);
    } else {
        int suffix = 0;
        do {
            const QString name = suffix ? QString("nav%1.xhtml").arg(suffix) : QString("nav.xhtml");
            plan.navBookPath = opfFolder.isEmpty() ? name : opfFolder + "/" + name;
            ++suffix;
        } while (folder->GetResourceByBookPathNoThrow(plan.navBookPath)
                 || QFileInfo::exists(folder->GetFullPathToMainFolder() + "/" + plan.navBookPath));
    }
    const QString fullPath = folder->GetFullPathToMainFolder() + "/" + plan.navBookPath;
    const QString canonicalRoot = QFileInfo(folder->GetFullPathToMainFolder()).canonicalFilePath();
    const QString canonicalParent = QFileInfo(fullPath).dir().canonicalPath();
    if (plan.navBookPath.startsWith('/') || plan.navBookPath.contains('\\') || plan.navBookPath.split('/').contains("..")
        || QFileInfo::exists(fullPath) || folder->GetResourceByBookPathNoThrow(plan.navBookPath)
        || !QFileInfo(fullPath).dir().exists()
        || (!canonicalParent.startsWith(canonicalRoot + "/") && canonicalParent != canonicalRoot)) {
        error = tr("The navigation destination must be an unused file in an existing book folder.");
        return false;
    }
    plan.opfAfter = plan.opfBefore;
    if (!declarations) {
        QString identifier = "nav";
        for (int suffix = 1; ids.contains(identifier); ++suffix) identifier = QString("nav%1").arg(suffix);
        int rv = 0;
        const QVariant result = EmbeddedPython::instance().runInPython("opf_source", "add_navigation_manifest",
            { plan.opfBefore, Utility::URLEncodePath(Utility::buildRelativePath(opf->GetRelativePath(), plan.navBookPath)), identifier },
            &rv, error, false, false);
        if (rv != 0) return false;
        plan.opfAfter = result.toString();
    }

    TOCModel model;
    if (auto *ncx = book->GetNCX()) {
        QXmlStreamReader document(ncx->GetText());
        while (!document.atEnd()) document.readNext();
        if (document.hasError()) {
            error = tr("The NCX is not well-formed. Review it before generating navigation.");
            return false;
        }
    }
    model.SetBook(book, false);
    auto root = model.GetRootTOCEntry();
    plan.fromNCX = !root.children.isEmpty();
    if (root.children.isEmpty()) {
        for (auto *resource : opf->GetSpineOrderResources(folder->GetResourceList())) {
            if (!qobject_cast<HTMLResource *>(resource)) continue;
            TOCModel::TOCEntry entry;
            entry.text = resource->Filename();
            entry.target = Utility::URLEncodePath(resource->GetRelativePath());
            root.children.append(entry);
        }
    }
    if (root.children.isEmpty()) {
        error = tr("There are no NCX entries or readable spine resources to use for navigation.");
        return false;
    }
    QXmlStreamWriter writer(&plan.navText);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeDTD("<!DOCTYPE html>");
    writer.writeStartElement("html");
    writer.writeDefaultNamespace("http://www.w3.org/1999/xhtml");
    writer.writeNamespace("http://www.idpf.org/2007/ops", "epub");
    const QString language = opf->GetPrimaryBookLanguage();
    writer.writeAttribute("lang", language.isEmpty() ? "en" : language);
    writer.writeStartElement("head");
    writer.writeTextElement("title", tr("Contents"));
    writer.writeEndElement();
    writer.writeStartElement("body");
    writer.writeStartElement("nav");
    writer.writeAttribute("epub:type", "toc");
    writer.writeAttribute("id", "toc");
    writer.writeTextElement("h1", tr("Contents"));
    QHash<QString, QSet<QString>> targetIds;
    if (!WriteEntries(writer, root.children, plan.navBookPath, *book, targetIds)) {
        error = tr("An NCX target is missing or is not a local publication resource. Review the NCX first.");
        return false;
    }
    writer.writeEndDocument();

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << opf->GetIdentifier() << opf->GetTextDocumentForWriting().revision() << plan.opfBefore;
    if (auto *ncx = book->GetNCX()) stream << ncx->GetIdentifier() << ncx->GetTextDocumentForWriting().revision() << ncx->GetText();
    QStringList paths = folder->GetAllBookPaths();
    paths.sort();
    stream << paths;
    plan.baseState = QCryptographicHash::hash(state, QCryptographicHash::Sha256);
    return true;
}

bool NavigationRepair::Apply(const QSharedPointer<Book> &book, const Plan &plan, QString &error)
{
    Plan current;
    if (!Prepare(book, current, error)) return false;
    if (current.baseState != plan.baseState || current.navBookPath != plan.navBookPath
        || current.navText != plan.navText || current.opfBefore != plan.opfBefore
        || current.opfAfter != plan.opfAfter || current.fromNCX != plan.fromNCX) {
        error = tr("The book or repair plan changed after preview. Review a new plan before applying it.");
        return false;
    }
    auto folder = book->GetFolderKeeper();
    const QString target = folder->GetFullPathToMainFolder() + "/" + plan.navBookPath;
    QTemporaryFile file(QFileInfo(target).absolutePath() + "/.sigil-nav-XXXXXX");
    const QByteArray data = plan.navText.toUtf8();
    if (!file.open() || file.write(data) != data.size() || !file.flush()) {
        error = file.errorString();
        return false;
    }
    file.close();
    if (!file.rename(target)) { // QFile rename refuses to replace a concurrent new file.
        error = file.errorString();
        return false;
    }
    Resource *added = nullptr;
    try {
        added = folder->AddContentFileToFolder(target, false, "application/xhtml+xml", plan.navBookPath);
        auto *nav = qobject_cast<HTMLResource *>(added);
        nav->SetText(plan.navText);
        // All fallible parsing/planning and file creation happen before this
        // single OPF edit. No CSS or spine item is generated implicitly.
        // This is a structural operation, not a standalone OPF text undo step:
        // undoing only the manifest would leave the new resource unmanifested.
        if (plan.opfBefore != plan.opfAfter) book->GetOPF()->SetText(plan.opfAfter);
        book->GetOPF()->SetNavResource(nav, false);
        file.setAutoRemove(false);
        book->SetModified(true);
        return true;
    } catch (const std::exception &exception) {
        if (added) folder->RemoveWithoutUpdatingOPF(added);
        error = QString::fromUtf8(exception.what());
        return false;
    }
}
