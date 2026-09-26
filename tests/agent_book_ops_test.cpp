#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/ContentOps.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Execution/ResourceMutations.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/Typeset/ManuscriptParser.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    using namespace SigilAgent;
    QStandardPaths::setTestModeEnabled(true);

    Require(xmlEscape(QStringLiteral("a<b")) == QStringLiteral("a&lt;b"), "xmlEscape");
    Require(relativeBookHref(QStringLiteral("OEBPS/Text/ch.xhtml"),
                             QStringLiteral("OEBPS/Images/cover.jpg"))
                == QStringLiteral("../Images/cover.jpg"),
            "relative href");
    const QString wrapped = wrapPlainText(
        QStringLiteral("Title line\n\nHello.\n[pic:foo]\n"),
        QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^Title") },
            { QStringLiteral("illustration_pattern"), QStringLiteral("\\[pic:([^\\]]+)\\]") },
            { QStringLiteral("illustration_html"),
              QStringLiteral("<div class=\"pic\"><img alt=\"$1\" src=\"../Images/$1.jpg\"/></div>") }
        });
    Require(wrapped.contains(QStringLiteral("<h1>Title line</h1>")), "wrap_plain heading");
    Require(wrapped.contains(QStringLiteral("<p>Hello.</p>")), "wrap_plain paragraph");
    Require(wrapped.contains(QStringLiteral("src=\"../Images/foo.jpg\"")), "wrap_plain illustration $1");

    ParseOptions options;
    options.headingRegex = QStringLiteral("^Chapter \\d+");
    options.illustrationRegex = QStringLiteral("\\{img:([^}]+)\\}");
    const ParsedManuscript custom = parseManuscriptText(
        QStringLiteral("Chapter 1 One\nChapter 1 One\n{img:a}\nbody one\nChapter 2 Two\nbody two\n"),
        QString(), options);
    Require(custom.chapters.size() == 2, "custom heading regex chapters");
    Require(custom.chapters.at(0).illustrationNames.contains(QStringLiteral("a")),
            "custom illustration regex");

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    MemoryResource img;
    img.id = QStringLiteral("img1");
    img.bookPath = QStringLiteral("OEBPS/Images/heat.jpg");
    img.kind = QStringLiteral("image");
    img.mediaType = QStringLiteral("image/jpeg");
    img.binary = QByteArray("x");
    book.addResource(img);

    AgentSession session;
    ToolRegistry registry;
    registerBookTools(&registry, &book, &session);
    auto run = [&](const QString &name, const QJsonObject &arguments) {
        IAgentTool *tool = registry.find(name);
        Require(tool != nullptr, "missing tool");
        return tool->execute(arguments);
    };

    Require(registry.find(QStringLiteral("book_search_regex")) != nullptr, "search_regex registered");
    Require(registry.find(QStringLiteral("session_task_add")) != nullptr, "session tools registered");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "begin");

    const ToolResult wrapped_res = run(QStringLiteral("content.wrap"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("100 degrees") },
        { QStringLiteral("open"), QStringLiteral("<em>") },
        { QStringLiteral("close"), QStringLiteral("</em>") }
    });
    Require(wrapped_res.ok, "wrap");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "wrap commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<em>100 degrees</em>")),
            "wrap applied");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "regex begin");
    Require(run(QStringLiteral("content.replace_regex"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("HELLO") },
        { QStringLiteral("replacement"), QStringLiteral("Heat") }
    }).ok, "regex no-op still ok");
    const ToolResult searched = run(QStringLiteral("book.search_regex"), QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("Water (\\w+)") },
        { QStringLiteral("resource_id"), QStringLiteral("ch1") }
    });
    Require(searched.ok && searched.data.value(QStringLiteral("match_count")).toInt() >= 1,
            "regex search");
    const QJsonObject short_hit = searched.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    Require(short_hit.value(QStringLiteral("captures")).toArray().first().toString()
                    == QStringLiteral("boils")
                && !short_hit.value(QStringLiteral("preview_truncated")).toBool(),
            "short regex matches and captures must remain intact");

    MemoryBookWorkspace bounded_search_book;
    MemoryResource long_page;
    long_page.id = QStringLiteral("long-regex");
    long_page.bookPath = QStringLiteral("OEBPS/Text/long.xhtml");
    long_page.kind = QStringLiteral("xhtml");
    long_page.mediaType = QStringLiteral("application/xhtml+xml");
    long_page.text = QStringLiteral("<html>") + QString(5000, QLatin1Char('x'))
        + QStringLiteral("REGEX-TAIL-SECRET</html>");
    bounded_search_book.addResource(long_page);
    MemoryResource groups_page;
    groups_page.id = QStringLiteral("many-groups");
    groups_page.bookPath = QStringLiteral("OEBPS/Text/groups.xhtml");
    groups_page.kind = QStringLiteral("xhtml");
    groups_page.mediaType = QStringLiteral("application/xhtml+xml");
    groups_page.text = QStringLiteral("abcdefghijkl");
    bounded_search_book.addResource(groups_page);
    MemoryResource hits_page;
    hits_page.id = QStringLiteral("many-hits");
    hits_page.bookPath = QStringLiteral("OEBPS/Text/hits.xhtml");
    hits_page.kind = QStringLiteral("xhtml");
    hits_page.mediaType = QStringLiteral("application/xhtml+xml");
    hits_page.text = QString(200, QLatin1Char('z'));
    bounded_search_book.addResource(hits_page);
    ToolRegistry bounded_search_registry;
    registerBookTools(&bounded_search_registry, &bounded_search_book);
    auto run_bounded_search = [&](const QJsonObject &arguments) {
        return bounded_search_registry.find(
            QStringLiteral("book.search_regex"))->execute(arguments);
    };
    const ToolResult long_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("(<html>([\\s\\S]+)</html>)") },
        { QStringLiteral("resource_id"), long_page.id }
    });
    const QJsonObject long_hit = long_search.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    const QJsonArray long_captures = long_hit.value(
        QStringLiteral("captures")).toArray();
    const QJsonArray long_capture_lengths = long_hit.value(
        QStringLiteral("capture_lengths")).toArray();
    Require(long_search.ok
                && long_hit.value(QStringLiteral("length")).toInt()
                    == long_page.text.size()
                && long_hit.value(QStringLiteral("match")).toString().size() == 240
                && long_hit.value(QStringLiteral("match_truncated")).toBool()
                && long_hit.value(QStringLiteral("capture_count")).toInt() == 2
                && long_hit.value(QStringLiteral("returned_capture_count")).toInt() == 2
                && long_captures.size() == 2
                && long_captures.at(0).toString().size() == 160
                && long_captures.at(1).toString().size() == 160
                && long_capture_lengths.at(0).toInt() == long_page.text.size()
                && long_hit.value(QStringLiteral("captures_truncated")).toBool()
                && long_hit.value(QStringLiteral("preview_truncated")).toBool()
                && !QJsonDocument(long_search.data).toJson().contains(
                    "REGEX-TAIL-SECRET"),
            "regex search must retain source locations while bounding long match and capture previews");
    const ToolResult grouped_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral(
              "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)") },
        { QStringLiteral("resource_id"), groups_page.id }
    });
    const QJsonObject grouped_hit = grouped_search.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    Require(grouped_hit.value(QStringLiteral("capture_count")).toInt() == 12
                && grouped_hit.value(
                    QStringLiteral("returned_capture_count")).toInt() == 8
                && grouped_hit.value(QStringLiteral("captures")).toArray().size() == 8
                && grouped_hit.value(QStringLiteral("capture_lengths")).toArray().size() == 8
                && grouped_hit.value(QStringLiteral("captures_truncated")).toBool(),
            "regex search must bound the number of returned capture previews");
    const ToolResult limited_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("z") },
        { QStringLiteral("resource_id"), hits_page.id },
        { QStringLiteral("max_matches"), 999 }
    });
    const QJsonObject search_schema = bounded_search_registry.find(
        QStringLiteral("book.search_regex"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("max_matches")).toObject();
    Require(limited_search.data.value(QStringLiteral("match_count")).toInt() == 50
                && limited_search.data.value(QStringLiteral("max_matches")).toInt() == 50
                && limited_search.data.value(
                    QStringLiteral("match_limit_reached")).toBool()
                && search_schema.value(QStringLiteral("minimum")).toInt() == 1
                && search_schema.value(QStringLiteral("default")).toInt() == 40
                && search_schema.value(QStringLiteral("maximum")).toInt() == 50,
            "regex search must clamp and disclose its match-count bound");
    const ToolResult invalid_regex = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("(") }
    });
    Require(!invalid_regex.ok && invalid_regex.code == QStringLiteral("REGEX_INVALID")
                && invalid_regex.data.contains(QStringLiteral("error_offset")),
            "invalid regex must return a syntax error instead of matching empty strings");
    const ToolResult empty_regex = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("(?=z)") },
        { QStringLiteral("resource_id"), hits_page.id }
    });
    Require(!empty_regex.ok && empty_regex.code == QStringLiteral("ZERO_LENGTH_MATCH"),
            "zero-length regex matches must be explicitly rejected");
    const ToolResult regex_count = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("z") },
        { QStringLiteral("resource_id"), hits_page.id },
        { QStringLiteral("count_only"), true }
    });
    Require(regex_count.ok && regex_count.data.value(QStringLiteral("total_count")).toInt() == 200
                && regex_count.data.value(QStringLiteral("matches")).toArray().isEmpty(),
            "regex count-only search must count beyond the page cap");
    const ToolResult regex_first_page = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("z") },
        { QStringLiteral("resource_id"), hits_page.id },
        { QStringLiteral("limit"), 50 }
    });
    const ToolResult regex_second_page = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("z") },
        { QStringLiteral("resource_id"), hits_page.id },
        { QStringLiteral("limit"), 50 },
        { QStringLiteral("cursor"), regex_first_page.data.value(QStringLiteral("next_cursor")) }
    });
    Require(regex_second_page.ok
                && regex_second_page.data.value(QStringLiteral("offset")).toInt() == 50
                && regex_second_page.data.value(QStringLiteral("returned_count")).toInt() == 50
                && regex_second_page.data.value(QStringLiteral("matches")).toArray().first()
                    .toObject().value(QStringLiteral("offset")).toInt() == 50,
            "regex search must continue after the first fifty matches");

    MemoryBookWorkspace audit_book;
    audit_book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Proof Audit Fixture") },
        { QStringLiteral("identifier"), QStringLiteral("urn:test:sigil-proof-audit-fixture-20260925") }
    });
    MemoryResource audit_one;
    audit_one.id = QStringLiteral("audit-one");
    audit_one.bookPath = QStringLiteral("Text/one.xhtml");
    audit_one.kind = QStringLiteral("xhtml");
    audit_one.mediaType = QStringLiteral("application/xhtml+xml");
    audit_one.text = QStringLiteral(
        "<html><head><style>，，</style><script>if (a && b) {}</script></head>"
        "<body><p>甲&amp;乙&copy;&#xFFFD;。<em>。</em>丙😀！！～～　……"
        "<ruby>字<rt>，，</rt></ruby><span hidden=\"hidden\">，，</span>"
        "<span style=\"display:none\">，，</span><!--，，--></p>"
        "<p>尾&#x200B;，，</p></body></html>");
    audit_book.addResource(audit_one);
    MemoryResource audit_two = audit_one;
    audit_two.id = QStringLiteral("audit-two");
    audit_two.bookPath = QStringLiteral("Text/two.xhtml");
    audit_two.text = QStringLiteral("<html><body><p>乙，，甲</p></body></html>");
    audit_book.addResource(audit_two);
    audit_book.setSpine({ audit_one.id, audit_two.id });
    ToolRegistry audit_registry;
    registerBookTools(&audit_registry, &audit_book);
    auto audit = [&](const QJsonObject &args) {
        return audit_registry.find(QStringLiteral("proof.audit"))->execute(args);
    };
    const QJsonObject audit_args {
        { QStringLiteral("scope"), QJsonObject {{ QStringLiteral("kind"), QStringLiteral("whole_book") }} },
        { QStringLiteral("limit"), 1 }
    };
    ToolResult audit_page = audit(audit_args);
    Require(audit_page.ok && audit_page.data.value(QStringLiteral("total_count")).toInt() == 5
                && audit_page.data.value(QStringLiteral("has_more")).toBool()
                && audit_page.data.value(QStringLiteral("scanned_resources")).toInt() == 2
                && audit_page.data.value(QStringLiteral("style_counts_are_errors")).toBool() == false
                && audit_page.data.value(QStringLiteral("style_counts")).toObject()
                    .value(QStringLiteral("double_exclamation")).toInt() == 1
                && audit_page.data.value(QStringLiteral("style_counts")).toObject()
                    .value(QStringLiteral("double_tilde")).toInt() == 1
                && audit_page.data.value(QStringLiteral("style_counts")).toObject()
                    .value(QStringLiteral("fullwidth_space")).toInt() == 1
                && audit_page.data.value(QStringLiteral("style_counts")).toObject()
                    .value(QStringLiteral("double_ellipsis")).toInt() == 1,
            "audit must scan visible body and count all candidates");
    QSet<QString> audit_ids;
    QJsonArray audit_issues;
    do {
        const QJsonArray page_issues = audit_page.data.value(QStringLiteral("issues")).toArray();
        Require(page_issues.size() == 1, "audit should respect page limit");
        const QJsonObject candidate = page_issues.first().toObject();
        const QString id = candidate.value(QStringLiteral("issue_id")).toString();
        Require(!audit_ids.contains(id), "audit pages must not duplicate candidates");
        audit_ids.insert(id);
        audit_issues.append(candidate);
        if (!audit_page.data.value(QStringLiteral("has_more")).toBool()) break;
        QJsonObject next = audit_args;
        next.insert(QStringLiteral("cursor"), audit_page.data.value(QStringLiteral("next_cursor")));
        audit_page = audit(next);
        Require(audit_page.ok, "audit continuation");
    } while (true);
    Require(audit_issues.size() == 5, "audit pages must enumerate the exact total");
    const QJsonObject replacement_issue = audit_issues.at(0).toObject();
    const QJsonObject cross_tag_issue = audit_issues.at(1).toObject();
    Require(replacement_issue.value(QStringLiteral("rule")).toString()
                == QStringLiteral("REPLACEMENT_CHARACTER")
                && !replacement_issue.value(QStringLiteral("auto_fixable")).toBool()
                && replacement_issue.value(QStringLiteral("before")).toString().contains(QChar(0x00a9))
                && audit_one.text.mid(replacement_issue.value(QStringLiteral("start")).toInt(),
                                      replacement_issue.value(QStringLiteral("length")).toInt())
                    == QStringLiteral("&#xFFFD;"),
            "entity candidate must map to its exact XHTML source span");
    Require(cross_tag_issue.value(QStringLiteral("rule")).toString()
                == QStringLiteral("REPEATED_PUNCTUATION")
                && !cross_tag_issue.value(QStringLiteral("auto_fixable")).toBool()
                && audit_one.text.mid(cross_tag_issue.value(QStringLiteral("start")).toInt(),
                                      cross_tag_issue.value(QStringLiteral("length")).toInt())
                    == QStringLiteral("。<em>。"),
            "cross-tag candidate must preserve source coordinates and require manual review");
    const QJsonObject zero_width_issue = audit_issues.at(2).toObject();
    Require(zero_width_issue.value(QStringLiteral("rule")).toString()
                == QStringLiteral("INVISIBLE_OR_CONTROL")
                && audit_one.text.mid(zero_width_issue.value(QStringLiteral("start")).toInt(),
                                      zero_width_issue.value(QStringLiteral("length")).toInt())
                    == QStringLiteral("&#x200B;"),
            "zero-width entity must map to its exact XHTML source span");
    const ToolResult selection_audit = audit(QJsonObject {
        { QStringLiteral("scope"), QJsonObject {
            { QStringLiteral("kind"), QStringLiteral("selection") },
            { QStringLiteral("resource_id"), audit_one.id },
            { QStringLiteral("start"), audit_one.text.indexOf(QStringLiteral("<p>尾")) },
            { QStringLiteral("end"), audit_one.text.indexOf(QStringLiteral("</p></body>")) }
        } }
    });
    Require(selection_audit.ok
                && selection_audit.data.value(QStringLiteral("total_count")).toInt() == 2,
            "selection audit must include only candidates inside the source selection");
    const ToolResult stale_first = audit(audit_args);
    audit_one.text.replace(QStringLiteral("尾"), QStringLiteral("末"));
    audit_book.addResource(audit_one); // no revision bump: source fingerprint must still invalidate cursor
    QJsonObject stale_args = audit_args;
    stale_args.insert(QStringLiteral("cursor"), stale_first.data.value(QStringLiteral("next_cursor")));
    const ToolResult stale_audit = audit(stale_args);
    Require(!stale_audit.ok && stale_audit.code == QStringLiteral("AUDIT_SNAPSHOT_STALE"),
            "audit cursor must reject changed XHTML even without a revision bump");

    const ToolResult review_scan = audit(QJsonObject {{ QStringLiteral("limit"), 50 }});
    Require(review_scan.ok && review_scan.data.value(QStringLiteral("total_count")).toInt() == 5,
            "rescan after source change");
    const QJsonArray current_issues = review_scan.data.value(QStringLiteral("issues")).toArray();
    const QString invisible_id = current_issues.at(2).toObject().value(QStringLiteral("issue_id")).toString();
    const QString punctuation_id = current_issues.at(4).toObject().value(QStringLiteral("issue_id")).toString();
    const QString ignored_id = current_issues.at(0).toObject().value(QStringLiteral("issue_id")).toString();
    const QString cross_tag_id = current_issues.at(1).toObject().value(QStringLiteral("issue_id")).toString();
    const ToolResult invalid_xml_replacement = audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), invisible_id },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("replacement"), QString(QChar(0x0001)) }
    });
    Require(!invalid_xml_replacement.ok
                && invalid_xml_replacement.code == QStringLiteral("AUDIT_REPLACEMENT_INVALID"),
            "proof decisions must reject replacement text that cannot be serialized as XHTML");
    Require(audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), cross_tag_id },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("replacement"), QStringLiteral("。") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    }).ok, "cross-tag candidate may be recorded as a reviewed decision");
    const ToolResult rejected_markup_plan = audit_registry.find(QStringLiteral("proof.plan"))->execute(QJsonObject {
        { QStringLiteral("accepted_issue_ids"), QJsonArray { cross_tag_id } }
    });
    Require(!rejected_markup_plan.ok && rejected_markup_plan.code == QStringLiteral("AUDIT_MARKUP_SPAN"),
            "proof plan must not flatten inline XHTML markup");
    const ToolResult ignored_replacement = audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), ignored_id },
        { QStringLiteral("decision"), QStringLiteral("ignore") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    });
    const ToolResult accepted_invisible = audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), invisible_id },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("replacement"), QStringLiteral("") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    });
    const ToolResult accepted_punctuation = audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), punctuation_id },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    });
    Require(ignored_replacement.ok && accepted_invisible.ok && accepted_punctuation.ok
                && !accepted_invisible.applied && !accepted_punctuation.applied,
            "review decisions must persist locally without editing the book");
    MemoryBookWorkspace reopened_audit_book;
    reopened_audit_book.setMetadata(audit_book.metadata());
    reopened_audit_book.addResource(audit_one);
    reopened_audit_book.addResource(audit_two);
    reopened_audit_book.setSpine({ audit_one.id, audit_two.id });
    ToolRegistry reopened_audit_registry;
    registerBookTools(&reopened_audit_registry, &reopened_audit_book);
    const ToolResult reopened_scan = reopened_audit_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject {
        { QStringLiteral("limit"), 50 }
    });
    const QJsonArray reopened_issues = reopened_scan.data.value(QStringLiteral("issues")).toArray();
    Require(reopened_scan.ok
                && reopened_issues.at(2).toObject().value(QStringLiteral("decision")).toString() == QStringLiteral("accept")
                && reopened_issues.at(0).toObject().value(QStringLiteral("decision")).toString() == QStringLiteral("ignore")
                && reopened_issues.at(4).toObject().value(QStringLiteral("decision")).toString() == QStringLiteral("accept"),
            "same-source review decisions must recover after reopening the book");
    const ToolResult first_plan_page = audit_registry.find(QStringLiteral("proof.plan"))->execute(QJsonObject {
        { QStringLiteral("accepted_issue_ids"), QJsonArray { invisible_id, punctuation_id } },
        { QStringLiteral("limit"), 1 }
    });
    Require(first_plan_page.ok && !first_plan_page.data.value(QStringLiteral("review_complete")).toBool()
                && first_plan_page.data.value(QStringLiteral("total_count")).toInt() == 2,
            "proof plan must paginate accepted changes");
    const QJsonObject apply_args {
        { QStringLiteral("plan_id"), first_plan_page.data.value(QStringLiteral("plan_id")) },
        { QStringLiteral("plan_digest"), first_plan_page.data.value(QStringLiteral("plan_digest")) },
        { QStringLiteral("expected_book_revision"), static_cast<qint64>(audit_book.revision()) }
    };
    const ToolResult early_apply = audit_registry.find(QStringLiteral("proof.apply"))->execute(apply_args);
    Require(!early_apply.ok && early_apply.code == QStringLiteral("AUDIT_PLAN_NOT_REVIEWED")
                && !audit_book.hasOpenTransaction(),
            "proof apply must reject an unread plan page without opening a transaction");
    const ToolResult last_plan_page = audit_registry.find(QStringLiteral("proof.plan"))->execute(QJsonObject {
        { QStringLiteral("cursor"), first_plan_page.data.value(QStringLiteral("next_cursor")) },
        { QStringLiteral("limit"), 1 }
    });
    Require(last_plan_page.ok && last_plan_page.data.value(QStringLiteral("review_complete")).toBool(),
            "proof plan final page must complete review");
    const ToolResult staged_proof = audit_registry.find(QStringLiteral("proof.apply"))->execute(apply_args);
    Require(staged_proof.ok && staged_proof.previewOnly && !staged_proof.applied
                && audit_book.hasOpenTransaction()
                && audit_book.resourceText(audit_one.id).contains(QStringLiteral("&#x200B;"))
                && audit_book.resourceText(audit_two.id).contains(QStringLiteral("，，")),
            "proof apply must stage only, leaving live XHTML untouched");
    const ToolResult proof_preview = audit_registry.find(QStringLiteral("transaction.preview"))->execute(QJsonObject());
    Require(proof_preview.ok && proof_preview.data.value(QStringLiteral("changes")).toArray().size() == 2,
            "proof changes must be visible in the existing transaction preview");
    const ToolResult proof_commit = audit_registry.find(QStringLiteral("transaction.commit"))->execute(QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(audit_book.revision()) }
    });
    Require(proof_commit.ok && proof_commit.applied
                && !audit_book.resourceText(audit_one.id).contains(QStringLiteral("&#x200B;"))
                && audit_book.resourceText(audit_one.id).contains(QStringLiteral("&#xFFFD;"))
                && audit_book.resourceText(audit_two.id).contains(QStringLiteral("乙，甲"))
                && audit_book.resourceText(audit_one.id).contains(QStringLiteral("<rt>，，</rt>")),
            "commit must change only accepted issues, preserving unreviewed and Ruby text");
    const ToolResult old_decision = audit_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), invisible_id },
        { QStringLiteral("decision"), QStringLiteral("ignore") }
    });
    Require(!old_decision.ok && old_decision.code == QStringLiteral("AUDIT_SNAPSHOT_STALE"),
            "old review snapshot must reject decisions after commit");
    const ToolResult rescanned_after_commit = audit(QJsonObject {{ QStringLiteral("limit"), 50 }});
    Require(rescanned_after_commit.ok
                && !rescanned_after_commit.data.value(QStringLiteral("issues")).toArray().first()
                    .toObject().contains(QStringLiteral("decision")),
            "a changed resource must require review again even for a surviving candidate");

    MemoryBookWorkspace conflict_book;
    MemoryResource conflict_page = audit_two;
    conflict_page.id = QStringLiteral("conflict-proof");
    conflict_page.text = QStringLiteral("<html><body><p>甲，，乙</p></body></html>");
    conflict_book.addResource(conflict_page);
    ToolRegistry conflict_registry;
    registerBookTools(&conflict_registry, &conflict_book);
    const ToolResult conflict_scan = conflict_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject());
    const QString conflict_issue = conflict_scan.data.value(QStringLiteral("issues")).toArray().first()
        .toObject().value(QStringLiteral("issue_id")).toString();
    Require(conflict_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), conflict_issue },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    }).ok, "conflict fixture decision");
    const ToolResult conflict_plan = conflict_registry.find(QStringLiteral("proof.plan"))->execute(QJsonObject {
        { QStringLiteral("accepted_issue_ids"), QJsonArray { conflict_issue } }
    });
    Require(conflict_plan.ok && conflict_plan.data.value(QStringLiteral("review_complete")).toBool(),
            "conflict fixture plan");
    conflict_page.text.replace(QStringLiteral("甲"), QStringLiteral("新"));
    conflict_book.addResource(conflict_page);
    const ToolResult conflict_apply = conflict_registry.find(QStringLiteral("proof.apply"))->execute(QJsonObject {
        { QStringLiteral("plan_id"), conflict_plan.data.value(QStringLiteral("plan_id")) },
        { QStringLiteral("plan_digest"), conflict_plan.data.value(QStringLiteral("plan_digest")) },
        { QStringLiteral("expected_book_revision"), static_cast<qint64>(conflict_book.revision()) }
    });
    Require(!conflict_apply.ok && conflict_apply.code == QStringLiteral("AUDIT_PLAN_STALE")
                && conflict_apply.data.value(QStringLiteral("conflicting_issue_ids")).toArray()
                    .contains(conflict_issue)
                && !conflict_book.hasOpenTransaction(),
            "proof apply must reject concurrent source edits without a partial transaction");

    MemoryBookWorkspace cdata_book;
    MemoryResource cdata_page = conflict_page;
    cdata_page.id = QStringLiteral("cdata-proof");
    cdata_page.text = QStringLiteral("<html><body><p><![CDATA[甲，，乙]]></p></body></html>");
    cdata_book.addResource(cdata_page);
    ToolRegistry cdata_registry;
    registerBookTools(&cdata_registry, &cdata_book);
    const ToolResult cdata_scan = cdata_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject());
    const QJsonObject cdata_issue = cdata_scan.data.value(QStringLiteral("issues")).toArray().first().toObject();
    Require(cdata_scan.ok && cdata_scan.data.value(QStringLiteral("total_count")).toInt() == 1
                && cdata_issue.value(QStringLiteral("source_kind")).toString() == QStringLiteral("cdata")
                && !cdata_issue.value(QStringLiteral("auto_fixable")).toBool(),
            "CDATA text may be reported but must never be treated as a direct XHTML replacement");
    MemoryBookWorkspace unknown_entity_book;
    MemoryResource unknown_entity_page = conflict_page;
    unknown_entity_page.id = QStringLiteral("unknown-entity-proof");
    unknown_entity_page.text = QStringLiteral("<html><body><p>甲&bogusproofentity;乙</p></body></html>");
    unknown_entity_book.addResource(unknown_entity_page);
    ToolRegistry unknown_entity_registry;
    registerBookTools(&unknown_entity_registry, &unknown_entity_book);
    const ToolResult unknown_entity_audit = unknown_entity_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject());
    Require(!unknown_entity_audit.ok && unknown_entity_audit.code == QStringLiteral("AUDIT_XHTML_INVALID"),
            "unknown XHTML entities must fail explicitly instead of shifting source offsets");

    MemoryBookWorkspace scale_book;
    for (int chapter = 0; chapter < 22; ++chapter) {
        MemoryResource page = conflict_page;
        page.id = QStringLiteral("scale-%1").arg(chapter);
        page.bookPath = QStringLiteral("Text/scale-%1.xhtml").arg(chapter);
        QString body;
        for (int segment = 0; segment < 20; ++segment) {
            body += QString(248, QChar(0x7532)) + QStringLiteral("，，");
        }
        page.text = QStringLiteral("<html><body><p>") + body + QStringLiteral("</p></body></html>");
        scale_book.addResource(page);
    }
    ToolRegistry scale_registry;
    registerBookTools(&scale_registry, &scale_book);
    QElapsedTimer audit_timer;
    audit_timer.start();
    const ToolResult scale_scan = scale_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject());
    const qint64 audit_ms = audit_timer.elapsed();
    Require(scale_scan.ok && scale_scan.data.value(QStringLiteral("scanned_resources")).toInt() == 22
                && scale_scan.data.value(QStringLiteral("total_count")).toInt() == 440
                && scale_scan.data.value(QStringLiteral("has_more")).toBool()
                && audit_ms < 15000,
            "audit must handle a 22-chapter, 110k-character synthetic book within the test budget");
    std::cout << "Synthetic proof audit 22 chapters / 110k chars: " << audit_ms << " ms\n";

    MemoryBookWorkspace convention_book;
    convention_book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Proof Convention Fixture") },
        { QStringLiteral("identifier"),
          QStringLiteral("urn:test:sigil-proof-conventions-")
              + QUuid::createUuid().toString(QUuid::WithoutBraces) }
    });
    MemoryResource convention_one = conflict_page;
    convention_one.id = QStringLiteral("convention-one");
    convention_one.bookPath = QStringLiteral("Text/convention-one.xhtml");
    convention_one.text = QStringLiteral("<html><body><p>甲，，乙 裏姬 裏地</p></body></html>");
    convention_book.addResource(convention_one);
    MemoryResource convention_two = convention_one;
    convention_two.id = QStringLiteral("convention-two");
    convention_two.bookPath = QStringLiteral("Text/convention-two.xhtml");
    convention_two.text = QStringLiteral("<html><body><p>甲，，乙 裏地</p></body></html>");
    convention_book.addResource(convention_two);
    ToolRegistry convention_registry;
    registerBookTools(&convention_registry, &convention_book);
    auto convention = [&](const QString &name, const QJsonObject &args) {
        return convention_registry.find(name)->execute(args);
    };
    const ToolResult configured_book = convention(QStringLiteral("proof.configure"), QJsonObject {
        { QStringLiteral("scope"), QJsonObject {{ QStringLiteral("kind"), QStringLiteral("book") }} },
        { QStringLiteral("allow_repeats"), QJsonArray { QStringLiteral("，，") } },
        { QStringLiteral("allowed_terms"), QJsonArray { QStringLiteral("裏姬") } },
        { QStringLiteral("variant_pairs"), QJsonArray { QJsonObject {
            { QStringLiteral("observed"), QStringLiteral("裏") },
            { QStringLiteral("preferred"), QStringLiteral("里") }
        } } }
    });
    Require(configured_book.ok && !configured_book.applied && convention_book.revision() == 1,
            "book conventions must be stored locally without mutating the EPUB");
    const ToolResult convention_scan = convention(QStringLiteral("proof.audit"), QJsonObject {
        { QStringLiteral("limit"), 1 }
    });
    Require(convention_scan.ok && convention_scan.data.value(QStringLiteral("total_count")).toInt() == 2
                && convention_scan.data.value(QStringLiteral("style_counts")).toObject()
                    .value(QStringLiteral("chinese_comma")).toInt() == 4
                && convention_scan.data.value(QStringLiteral("issues")).toArray().first()
                    .toObject().value(QStringLiteral("rule")).toString() == QStringLiteral("TERM_VARIANT"),
            "configured punctuation is allowed and proper-name exceptions suppress variant candidates");
    const ToolResult second_convention_page = convention(QStringLiteral("proof.audit"), QJsonObject {
        { QStringLiteral("limit"), 1 },
        { QStringLiteral("cursor"), convention_scan.data.value(QStringLiteral("next_cursor")) }
    });
    const QString second_variant_id = second_convention_page.data.value(QStringLiteral("issues"))
        .toArray().first().toObject().value(QStringLiteral("issue_id")).toString();
    Require(second_convention_page.ok
                && convention(QStringLiteral("proof.decide"), QJsonObject {
                    { QStringLiteral("issue_id"), second_variant_id },
                    { QStringLiteral("decision"), QStringLiteral("ignore") },
                    { QStringLiteral("reviewer"), QStringLiteral("user") }
                }).ok,
            "configured variant can receive a local review decision");
    ToolRegistry second_convention_registry;
    registerBookTools(&second_convention_registry, &convention_book);
    const ToolResult configured_file = second_convention_registry.find(
        QStringLiteral("proof.configure"))->execute(QJsonObject {
        { QStringLiteral("scope"), QJsonObject {
            { QStringLiteral("kind"), QStringLiteral("file") },
            { QStringLiteral("resource_id"), convention_one.id }
        } },
        { QStringLiteral("allowed_terms"), QJsonArray { QStringLiteral("裏地") } }
    });
    Require(configured_file.ok, "chapter convention should save");
    const ToolResult stale_convention_decision = convention(QStringLiteral("proof.decide"), QJsonObject {
        { QStringLiteral("issue_id"), second_variant_id },
        { QStringLiteral("decision"), QStringLiteral("accept") }
    });
    Require(!stale_convention_decision.ok
                && stale_convention_decision.code == QStringLiteral("AUDIT_SNAPSHOT_STALE"),
            "a configuration change from another tool registry must invalidate old decisions");
    const ToolResult old_convention_cursor = convention(QStringLiteral("proof.audit"), QJsonObject {
        { QStringLiteral("limit"), 1 },
        { QStringLiteral("cursor"), convention_scan.data.value(QStringLiteral("next_cursor")) }
    });
    Require(!old_convention_cursor.ok && old_convention_cursor.code == QStringLiteral("AUDIT_SNAPSHOT_STALE"),
            "changing a convention must invalidate previous audit cursors");
    const ToolResult chapter_scan = convention(QStringLiteral("proof.audit"), QJsonObject());
    Require(chapter_scan.ok && chapter_scan.data.value(QStringLiteral("total_count")).toInt() == 1
                && chapter_scan.data.value(QStringLiteral("issues")).toArray().first()
                    .toObject().value(QStringLiteral("resource_id")).toString() == convention_two.id
                && !chapter_scan.data.value(QStringLiteral("issues")).toArray().first()
                    .toObject().contains(QStringLiteral("decision")),
            "chapter convention change must suppress only that chapter and require renewed decisions");
    MemoryBookWorkspace reopened_convention_book;
    reopened_convention_book.setMetadata(convention_book.metadata());
    reopened_convention_book.addResource(convention_one);
    reopened_convention_book.addResource(convention_two);
    ToolRegistry reopened_convention_registry;
    registerBookTools(&reopened_convention_registry, &reopened_convention_book);
    const ToolResult reopened_settings = reopened_convention_registry.find(QStringLiteral("proof.settings"))->execute(QJsonObject());
    const ToolResult reopened_conventions = reopened_convention_registry.find(QStringLiteral("proof.audit"))->execute(QJsonObject());
    Require(reopened_settings.ok && reopened_settings.data.value(QStringLiteral("files")).toObject()
                .contains(convention_one.bookPath)
                && reopened_conventions.ok
                && reopened_conventions.data.value(QStringLiteral("total_count")).toInt() == 1,
            "book and chapter conventions must survive reopening with the same identifier");
    const QString reviewed_variant = reopened_conventions.data.value(QStringLiteral("issues"))
        .toArray().first().toObject().value(QStringLiteral("issue_id")).toString();
    Require(reopened_convention_registry.find(QStringLiteral("proof.decide"))->execute(QJsonObject {
        { QStringLiteral("issue_id"), reviewed_variant },
        { QStringLiteral("decision"), QStringLiteral("accept") },
        { QStringLiteral("reviewer"), QStringLiteral("user") }
    }).ok, "reviewed configured variant should be accepted");
    const ToolResult variant_plan = reopened_convention_registry.find(QStringLiteral("proof.plan"))->execute(QJsonObject {
        { QStringLiteral("accepted_issue_ids"), QJsonArray { reviewed_variant } }
    });
    Require(variant_plan.ok && variant_plan.data.value(QStringLiteral("review_complete")).toBool()
                && variant_plan.data.value(QStringLiteral("items")).toArray().first()
                    .toObject().value(QStringLiteral("after")).toString() == QStringLiteral("里"),
            "configured variant plan must contain the reviewed replacement");
    const ToolResult staged_variant = reopened_convention_registry.find(QStringLiteral("proof.apply"))->execute(QJsonObject {
        { QStringLiteral("plan_id"), variant_plan.data.value(QStringLiteral("plan_id")) },
        { QStringLiteral("plan_digest"), variant_plan.data.value(QStringLiteral("plan_digest")) },
        { QStringLiteral("expected_book_revision"), static_cast<qint64>(reopened_convention_book.revision()) }
    });
    Require(staged_variant.ok && staged_variant.previewOnly
                && reopened_convention_book.resourceText(convention_two.id).contains(QStringLiteral("裏地")),
            "configured variant apply must stage without changing the live book");
    Require(reopened_convention_registry.find(QStringLiteral("transaction.preview"))->execute(QJsonObject()).ok,
            "configured variant transaction preview");
    Require(reopened_convention_registry.find(QStringLiteral("transaction.commit"))->execute(QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(reopened_convention_book.revision()) }
    }).applied
                && reopened_convention_book.resourceText(convention_two.id).contains(QStringLiteral("里地"))
                && reopened_convention_book.resourceText(convention_one.id).contains(QStringLiteral("裏姬")),
            "configured variant commit must preserve the allowed proper name");
    const ToolResult invalid_convention = convention(QStringLiteral("proof.configure"), QJsonObject {
        { QStringLiteral("scope"), QJsonObject {{ QStringLiteral("kind"), QStringLiteral("book") }} },
        { QStringLiteral("variant_pairs"), QJsonArray { QJsonObject {
            { QStringLiteral("observed"), QStringLiteral("裏") },
            { QStringLiteral("preferred"), QStringLiteral("裏") }
        } } }
    });
    Require(!invalid_convention.ok && invalid_convention.code == QStringLiteral("AUDIT_CONFIG_INVALID")
                && convention(QStringLiteral("proof.audit"), QJsonObject()).data
                    .value(QStringLiteral("total_count")).toInt() == 1,
            "invalid conventions must leave the previous rule set intact");
    Require(run(QStringLiteral("content.replace_regex"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("<em>([^<]+)</em>") },
        { QStringLiteral("replacement"), QStringLiteral("<strong>$1</strong>") }
    }).ok, "regex replace");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "regex commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<strong>100 degrees</strong>")),
            "capture replace");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "split begin");
    Require(run(QStringLiteral("resource.replace_text"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") },
        { QStringLiteral("text"), QStringLiteral(
            "<html><head><title>t</title></head><body>"
            "<h1>A</h1><p>one</p><h1>B</h1><p>two</p></body></html>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("ch2"))) }
    }).ok, "seed split source");
    const ToolResult split = run(QStringLiteral("content.split"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") }
    });
    Require(split.ok, "split");
    Require(split.data.value(QStringLiteral("section_count")).toInt() == 2, "two sections");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "split commit");
    Require(book.resourceText(QStringLiteral("ch2")).contains(QStringLiteral("<h1>A</h1>")),
            "first section kept");
    Require(!book.resourceText(QStringLiteral("ch2")).contains(QStringLiteral("<h1>B</h1>")),
            "second heading moved");
    const QJsonArray created = split.data.value(QStringLiteral("created")).toArray();
    Require(!created.isEmpty(), "created copy");
    const QString copy_id = created.first().toObject().value(QStringLiteral("resource_id")).toString();
    Require(book.resourceText(copy_id).contains(QStringLiteral("<h1>B</h1>")), "second file has B");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "image begin");
    Require(run(QStringLiteral("image.insert"), QJsonObject {
        { QStringLiteral("page_id"), QStringLiteral("ch1") },
        { QStringLiteral("image_id"), QStringLiteral("img1") },
        { QStringLiteral("anchor"), QStringLiteral("</h1>") }
    }).ok, "insert image");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "image commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("../Images/heat.jpg")),
            "image href");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "body begin");
    Require(run(QStringLiteral("content.replace_body"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("source_resource_id"), QStringLiteral("ch2") }
    }).ok, "replace_body from source");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "body commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<h1>A</h1>")),
            "body copied from ch2 without model dump");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "merge begin");
    Require(run(QStringLiteral("content.merge"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray { copy_id, QStringLiteral("ch2") } },
        { QStringLiteral("delete_sources"), false }
    }).ok, "merge");
    Require(run(QStringLiteral("toc.generate"), QJsonObject()).ok, "toc generate");
    Require(run(QStringLiteral("metadata.update"), QJsonObject {
        { QStringLiteral("patch"), QJsonObject {
            { QStringLiteral("publisher"), QStringLiteral("Test Press") },
            { QStringLiteral("_remove"), QJsonArray { QStringLiteral("creator") } }
        } }
    }).ok, "metadata crud");
    Require(run(QStringLiteral("spine.set"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray {
            QStringLiteral("ch1"), QStringLiteral("ch2"), copy_id
        } }
    }).ok, "spine set");
    Require(run(QStringLiteral("resource.delete"), QJsonObject {
        { QStringLiteral("resource_id"), copy_id }
    }).ok, "delete staged");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "structure commit");
    Require(book.metadata().value(QStringLiteral("publisher")).toString() == QStringLiteral("Test Press"),
            "publisher set");
    Require(!book.metadata().contains(QStringLiteral("creator")), "creator removed");
    bool copy_gone = true;
    for (const QJsonValue &value : book.resources()) {
        if (value.toObject().value(QStringLiteral("resource_id")).toString() == copy_id) copy_gone = false;
    }
    Require(copy_gone, "deleted resource is gone");

    const ToolResult check = run(QStringLiteral("book.check"), QJsonObject());
    Require(check.ok, "book.check");

    const ToolResult remembered = run(QStringLiteral("session.remember"), QJsonObject {
        { QStringLiteral("key"), QStringLiteral("heading") },
        { QStringLiteral("value"), QStringLiteral("^Chapter") }
    });
    Require(remembered.ok, "remember");
    const ToolResult recalled = run(QStringLiteral("session.recall"), QJsonObject {
        { QStringLiteral("key"), QStringLiteral("heading") }
    });
    Require(recalled.data.value(QStringLiteral("value")).toString() == QStringLiteral("^Chapter"),
            "recall");
    const ToolResult task = run(QStringLiteral("session.task_add"), QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Wrap paragraphs") }
    });
    Require(task.ok && !task.data.value(QStringLiteral("id")).toString().isEmpty(), "task add");
    Require(run(QStringLiteral("session.task_update"), QJsonObject {
        { QStringLiteral("id"), task.data.value(QStringLiteral("id")).toString() },
        { QStringLiteral("status"), QStringLiteral("done") }
    }).ok, "task update");
    Require(run(QStringLiteral("session.tasks"), QJsonObject())
                .data.value(QStringLiteral("tasks")).toArray().first().toObject()
                .value(QStringLiteral("status")).toString() == QStringLiteral("done"),
            "task done");

    AgentSession bounded_session;
    ToolRegistry bounded_session_registry;
    registerBookTools(&bounded_session_registry, &book, &bounded_session);
    auto run_bounded_session = [&](const QString &name,
                                   const QJsonObject &arguments) {
        return bounded_session_registry.find(name)->execute(arguments);
    };
    for (int index = 0; index < MAX_SESSION_MEMORY_ENTRIES; ++index) {
        const ToolResult stored = run_bounded_session(
            QStringLiteral("session.remember"), QJsonObject {
                { QStringLiteral("key"), QStringLiteral("memory-%1").arg(index) },
                { QStringLiteral("value"), QStringLiteral("value-%1").arg(index) }
            });
        Require(stored.ok
                    && stored.data.value(QStringLiteral("memory_count")).toInt()
                        == index + 1
                    && !stored.data.contains(QStringLiteral("memory")),
                "remember must return only the changed bounded note");
    }
    const ToolResult updated_full_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-0") },
            { QStringLiteral("value"), QStringLiteral("updated") }
        });
    const ToolResult overflow_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-overflow") },
            { QStringLiteral("value"), QStringLiteral("overflow") }
        });
    const ToolResult oversized_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("oversized") },
            { QStringLiteral("value"), QString(
                MAX_SESSION_MEMORY_VALUE_LENGTH + 1, QLatin1Char('x')) }
        });
    Require(updated_full_memory.ok
                && updated_full_memory.data.value(
                    QStringLiteral("memory_count")).toInt()
                    == MAX_SESSION_MEMORY_ENTRIES
                && !overflow_memory.ok
                && overflow_memory.code
                    == QStringLiteral("SESSION_MEMORY_LIMIT_REACHED")
                && !oversized_memory.ok
                && oversized_memory.code
                    == QStringLiteral("SESSION_MEMORY_ARGUMENT_INVALID"),
            "session memory must allow updates but reject count and value overflow");
    const ToolResult memory_page = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject());
    const ToolResult memory_tail = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject {
            { QStringLiteral("offset"), 32 },
            { QStringLiteral("limit"), 999 }
        });
    const ToolResult exact_memory = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-0") }
        });
    Require(memory_page.data.value(QStringLiteral("memory")).toObject().size() == 16
                && memory_page.data.value(QStringLiteral("total_count")).toInt() == 64
                && memory_page.data.value(QStringLiteral("has_more")).toBool()
                && memory_page.data.value(QStringLiteral("next_offset")).toInt() == 16
                && !memory_page.data.value(QStringLiteral("memory")).toObject()
                    .contains(QStringLiteral("memory-0"))
                && memory_tail.data.value(QStringLiteral("memory")).toObject().size() == 32
                && memory_tail.data.value(QStringLiteral("memory")).toObject()
                    .contains(QStringLiteral("memory-0"))
                && memory_tail.data.value(QStringLiteral("limit")).toInt() == 32
                && !memory_tail.data.value(QStringLiteral("has_more")).toBool()
                && exact_memory.data.value(QStringLiteral("found")).toBool()
                && exact_memory.data.value(QStringLiteral("value")).toString()
                    == QStringLiteral("updated"),
            "session memory listing must paginate while exact recall remains available");

    QString last_task_id;
    for (int index = 0; index < MAX_SESSION_TASKS; ++index) {
        const ToolResult added = run_bounded_session(
            QStringLiteral("session.task_add"), QJsonObject {
                { QStringLiteral("title"), QStringLiteral("task-%1").arg(index) },
                { QStringLiteral("note"), QStringLiteral("note-%1").arg(index) }
            });
        last_task_id = added.data.value(QStringLiteral("id")).toString();
        Require(added.ok && !last_task_id.isEmpty()
                    && added.data.value(QStringLiteral("task_count")).toInt()
                        == index + 1
                    && added.data.value(QStringLiteral("task")).toObject()
                        .value(QStringLiteral("id")).toString() == last_task_id
                    && !added.data.contains(QStringLiteral("tasks")),
                "task add must return only the changed bounded task");
    }
    const ToolResult overflow_task = run_bounded_session(
        QStringLiteral("session.task_add"), QJsonObject {
            { QStringLiteral("title"), QStringLiteral("task-overflow") }
        });
    const ToolResult invalid_task_status = run_bounded_session(
        QStringLiteral("session.task_update"), QJsonObject {
            { QStringLiteral("id"), last_task_id },
            { QStringLiteral("status"), QStringLiteral("mystery") }
        });
    const ToolResult updated_task = run_bounded_session(
        QStringLiteral("session.task_update"), QJsonObject {
            { QStringLiteral("id"), last_task_id },
            { QStringLiteral("status"), QStringLiteral("done") }
        });
    Require(!overflow_task.ok
                && overflow_task.code == QStringLiteral("SESSION_TASK_LIMIT_REACHED")
                && !invalid_task_status.ok
                && invalid_task_status.code
                    == QStringLiteral("SESSION_TASK_ARGUMENT_INVALID")
                && updated_task.ok
                && updated_task.data.value(QStringLiteral("task")).toObject()
                    .value(QStringLiteral("status")).toString()
                    == QStringLiteral("done")
                && !updated_task.data.contains(QStringLiteral("tasks")),
            "session tasks must reject overflow and invalid status without full-list echoes");
    const ToolResult task_page = run_bounded_session(
        QStringLiteral("session.tasks"), QJsonObject());
    const ToolResult task_tail = run_bounded_session(
        QStringLiteral("session.tasks"), QJsonObject {
            { QStringLiteral("offset"), 100 },
            { QStringLiteral("limit"), 999 }
        });
    const QJsonObject recall_schema = bounded_session_registry.find(
        QStringLiteral("session.recall"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject();
    const QJsonObject tasks_schema = bounded_session_registry.find(
        QStringLiteral("session.tasks"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject();
    Require(task_page.data.value(QStringLiteral("tasks")).toArray().size() == 20
                && task_page.data.value(QStringLiteral("total_count")).toInt() == 128
                && task_page.data.value(QStringLiteral("next_offset")).toInt() == 20
                && task_tail.data.value(QStringLiteral("tasks")).toArray().size() == 28
                && task_tail.data.value(QStringLiteral("limit")).toInt() == 50
                && !task_tail.data.value(QStringLiteral("has_more")).toBool()
                && recall_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("default")).toInt() == 16
                && recall_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("maximum")).toInt() == 32
                && tasks_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("default")).toInt() == 20
                && tasks_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("maximum")).toInt() == 50,
            "session task pages and schemas must expose stable bounded traversal");
    bounded_session.clear();
    const QString after_clear_task = bounded_session.addTask(
        QStringLiteral("after-clear"));
    Require(bounded_session.remember(QStringLiteral("after-clear"),
                                     QStringLiteral("value"))
                && !after_clear_task.isEmpty()
                && bounded_session.memoryKeys().size() == 1
                && bounded_session.tasks().size() == 1
                && !bounded_session.remember(
                    QString(MAX_SESSION_MEMORY_KEY_LENGTH + 1, QLatin1Char('k')),
                    QStringLiteral("value"))
                && bounded_session.addTask(
                    QString(MAX_SESSION_TASK_TITLE_LENGTH + 1,
                            QLatin1Char('t'))).isEmpty()
                && !bounded_session.updateTask(
                    after_clear_task, QStringLiteral("invalid"), QString()),
            "new session must reset bounded memory/task storage and insertion order");

    Require(registry.find(QStringLiteral("resource_rename")) != nullptr, "rename registered");
    Require(registry.find(QStringLiteral("spine_sort")) != nullptr, "spine.sort registered");
    Require(registry.find(QStringLiteral("style_link")) != nullptr, "style.link registered");
    Require(registry.find(QStringLiteral("python_run")) != nullptr, "python.run registered");

    Require(resolveRenameTarget(QStringLiteral("OEBPS/Text/ch1.xhtml"), QStringLiteral("Heat"))
                == QStringLiteral("OEBPS/Text/Heat.xhtml"),
            "filename-only rename keeps folder and suffix");
    Require(resolveRenameTarget(QStringLiteral("OEBPS/Text/ch1.xhtml"),
                                QStringLiteral("OEBPS/Misc/ch1.xhtml"))
                == QStringLiteral("OEBPS/Misc/ch1.xhtml"),
            "full-path rename is used as-is");
    Require(rewriteHrefsForMove(QStringLiteral("<link href=\"../Styles/style.css\"/>"),
                                QStringLiteral("OEBPS/Text/ch1.xhtml"),
                                QStringLiteral("OEBPS/Styles/style.css"),
                                QStringLiteral("OEBPS/Styles/theme.css"))
                .contains(QStringLiteral("../Styles/theme.css")),
            "href rewrite follows a stylesheet rename");

    const ToolResult python_missing = run(QStringLiteral("python.run"), QJsonObject {
        { QStringLiteral("script"), QStringLiteral("print(plugin.book.summary())") }
    });
    Require(!python_missing.ok && python_missing.code == QStringLiteral("LIVE_PYTHON_UNAVAILABLE"),
            "Memory workspace cannot snapshot-run Python");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "sort begin");
    Require(run(QStringLiteral("spine.set"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("ch2"), QStringLiteral("ch1") } }
    }).ok, "unsort spine");
    Require(run(QStringLiteral("spine.sort"), QJsonObject()).ok, "spine.sort");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "sort commit");
    Require(book.spine().first().toObject().value(QStringLiteral("resource_id")).toString()
                == QStringLiteral("ch1"),
            "numeric path sort puts ch1 before ch2");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "rename begin");
    const ToolResult renamed = run(QStringLiteral("resource.rename"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("css") },
        { QStringLiteral("book_path"), QStringLiteral("theme.css") }
    });
    Require(renamed.ok, "rename css");
    Require(book.workingText(QStringLiteral("ch1")).contains(QStringLiteral("../Styles/theme.css")),
            "rename rewrites stylesheet hrefs while staged");
    Require(run(QStringLiteral("resource.rename"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("book_path"), QStringLiteral("OEBPS/ch1.xhtml") }
    }).ok, "move chapter");
    Require(book.workingText(QStringLiteral("ch1")).contains(QStringLiteral("href=\"Styles/theme.css\"")),
            "move rewrites outgoing hrefs for the new folder");
    const ToolResult previewed = run(QStringLiteral("transaction.preview"), QJsonObject());
    Require(previewed.ok, "rename preview");
    bool saw_renamed = false;
    for (const QJsonValue &value : previewed.data.value(QStringLiteral("changes")).toArray()) {
        if (value.toObject().value(QStringLiteral("renamed")).toBool()) saw_renamed = true;
    }
    Require(saw_renamed, "preview lists renamed resources");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "rename commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("Styles/theme.css")),
            "committed move keeps rewritten hrefs");
    QString css_path;
    for (const QJsonValue &value : book.resources()) {
        if (value.toObject().value(QStringLiteral("resource_id")).toString() == QLatin1String("css")) {
            css_path = value.toObject().value(QStringLiteral("book_path")).toString();
        }
    }
    Require(css_path == QStringLiteral("OEBPS/Styles/theme.css"), "css path after rename");

    MemoryResource extra_css;
    extra_css.id = QStringLiteral("css2");
    extra_css.bookPath = QStringLiteral("OEBPS/Styles/extra.css");
    extra_css.kind = QStringLiteral("css");
    extra_css.mediaType = QStringLiteral("text/css");
    extra_css.text = QStringLiteral("p { color: red; }");
    book.addResource(extra_css);
    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "link begin");
    Require(run(QStringLiteral("style.link"), QJsonObject {
        { QStringLiteral("html_ids"), QJsonArray { QStringLiteral("ch1") } },
        { QStringLiteral("css_ids"), QJsonArray { QStringLiteral("css"), QStringLiteral("css2") } }
    }).ok, "style.link");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "link commit");
    const QString linked = book.resourceText(QStringLiteral("ch1"));
    Require(linked.contains(QStringLiteral("Styles/theme.css")), "primary stylesheet linked");
    Require(linked.contains(QStringLiteral("Styles/extra.css")), "second stylesheet linked");

    const ToolResult check_wellformed = run(QStringLiteral("book.check"), QJsonObject());
    Require(check_wellformed.data.contains(QStringLiteral("wellformed")), "book.check wellformed");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "plain begin");
    MemoryResource txt;
    txt.id = QStringLiteral("plain");
    txt.bookPath = QStringLiteral("OEBPS/Misc/plain.txt");
    txt.kind = QStringLiteral("text");
    txt.mediaType = QStringLiteral("text/plain");
    txt.text = QStringLiteral("HEAD\nbody line");
    book.addResource(txt);
    Require(run(QStringLiteral("content.wrap_plain"), QJsonObject {
        { QStringLiteral("source_resource_id"), QStringLiteral("plain") },
        { QStringLiteral("target_id"), QStringLiteral("ch1") },
        { QStringLiteral("rules"), QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^HEAD") }
        } }
    }).ok, "wrap_plain tool");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "wrap_plain commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<h1>HEAD</h1>")),
            "plain wrapped into existing xhtml");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok,
            "staged plain begin");
    const ToolResult staged_page = run(QStringLiteral("resource.create"), QJsonObject {
        { QStringLiteral("book_path"), QStringLiteral("OEBPS/Text/wrapped.xhtml") },
        { QStringLiteral("kind"), QStringLiteral("xhtml") },
        { QStringLiteral("add_to_spine"), false }
    });
    Require(staged_page.ok, "create staged wrap target");
    const QString staged_id = staged_page.data.value(QStringLiteral("resource_id")).toString();
    Require(book.resourceRevision(staged_id) == 1, "new staged resource revision");
    Require(run(QStringLiteral("content.wrap_plain"), QJsonObject {
        { QStringLiteral("source_resource_id"), QStringLiteral("plain") },
        { QStringLiteral("target_id"), staged_id },
        { QStringLiteral("rules"), QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^HEAD") }
        } }
    }).ok, "first staged wrap");
    Require(book.resourceRevision(staged_id) == 2, "first staged wrap advances revision");
    const ToolResult stale_staged_write = run(QStringLiteral("resource.replace_text"),
        QJsonObject {
            { QStringLiteral("resource_id"), staged_id },
            { QStringLiteral("expected_revision"), 1 },
            { QStringLiteral("text"), QStringLiteral("<html/>") }
        });
    Require(!stale_staged_write.ok
                && stale_staged_write.code == QStringLiteral("RESOURCE_REVISION_CONFLICT"),
            "stale staged write reports a resource revision conflict");
    const ToolResult staged_search = run(QStringLiteral("book.search_regex"), QJsonObject {
        { QStringLiteral("resource_id"), staged_id },
        { QStringLiteral("pattern"), QStringLiteral("<h1>HEAD</h1>") }
    });
    Require(staged_search.ok
                && staged_search.data.value(QStringLiteral("total_count")).toInt() == 1,
            "search includes staged new text");
    Require(run(QStringLiteral("content.wrap_plain"), QJsonObject {
        { QStringLiteral("source_resource_id"), QStringLiteral("plain") },
        { QStringLiteral("target_id"), staged_id },
        { QStringLiteral("rules"), QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^body") }
        } }
    }).ok, "repeat wrap of staged new text");
    Require(book.resourceRevision(staged_id) == 3
                && book.workingText(staged_id).contains(QStringLiteral("<h1>body line</h1>")),
            "repeat staged wrap uses current revision");
    Require(run(QStringLiteral("transaction.rollback"), QJsonObject()).ok,
            "staged plain rollback");

    return EXIT_SUCCESS;
}
