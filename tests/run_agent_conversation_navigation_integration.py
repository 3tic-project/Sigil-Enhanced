"""Open Agent answer links through the real MainWindow, Code View and live Book.

Default mode builds a synthetic EPUB. ``--sample-export`` rebuilds the cited
chapter lines from a local Native Agent JSON export (never committed) and
replays that session's final answer.
"""

import argparse
import json
import pathlib
import re
import zipfile

from run_opf_resource_integration import run

CHAPTER = "OEBPS/Text/Section001.xhtml"
OTHER = "OEBPS/Text/Section002.xhtml"
LINE_REF = re.compile(r"(?<![A-Za-z0-9_])L([1-9][0-9]{0,6})(?![A-Za-z0-9_])")

SYNTHETIC_ANSWER = """已通读第一章全文。**目前尚未写入任何修改。**

**范围**：`OEBPS/Text/Section001.xhtml`（第一章）
**自动审计**：`proof.audit` 默认规则 **0 项**；章节以 `<p><br /></p>` 分隔。

## 一、建议修改（6 项）

| # | 行 | 原文 | 建议 | 依据 |
|---|---|---|---|---|
| 1 | L25 | 也**帮忙我**发传单 | **帮我**发传单 | 同章 L75、L27 |
| 2 | L477 | 「**暗椿**」 | 「**暗桩**」 | 错字 |
| 3 | L925 | **指是**什么 | 是什么 | 衍字 |
| 4 | L1409 | **并下无意识** | **并下意识** | 错字 |
| 5 | L1897 | **小事一椿** | 小事一**桩** | 错字 |
| 6 | L2219 | **向我地**道歉 | **向我**道歉 | 衍字 |

## 二、需你决定

7. **L1899**「帮忙成香的演讲」
8. **L789**「显着」
9. **L1513 / L1833 / L1895 / L2031**「想像」
10. **L1161**「能在」

## 三、其他观察

- L2043 用「•」表示并列。

---

**下一步**：`transaction.begin → patch → preview → commit`。
"""

OPF = """<?xml version='1.0' encoding='utf-8'?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' version='3.0' unique-identifier='bookid'>
 <metadata>
  <dc:identifier id='bookid'>urn:test:agent-navigation</dc:identifier>
  <dc:title>Agent navigation</dc:title><dc:language>zh-CN</dc:language>
  <meta property='dcterms:modified'>2026-09-26T00:00:00Z</meta>
 </metadata>
 <manifest>
  <item id='ch1' href='Text/Section001.xhtml' media-type='application/xhtml+xml'/>
  <item id='ch2' href='Text/Section002.xhtml' media-type='application/xhtml+xml'/>
  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>
 </manifest>
 <spine><itemref idref='ch1'/><itemref idref='ch2'/></spine>
</package>
"""

CONTAINER = (b'<?xml version="1.0"?><container version="1.0" '
             b'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
             b'<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
             b'</rootfiles></container>')

NAV = ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
       '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
       '<head><title>Contents</title></head><body><nav epub:type="toc"><ol>'
       '<li><a href="Text/Section001.xhtml">One</a></li>'
       '<li><a href="Text/Section002.xhtml">Two</a></li></ol></nav></body></html>')


def xhtml(body_lines, total_lines):
    head = ['<?xml version="1.0" encoding="utf-8"?>', '<!DOCTYPE html>',
            '<html xmlns="http://www.w3.org/1999/xhtml">',
            '<head><title>Chapter</title></head>', '<body>']
    lines = head + body_lines
    while len(lines) < total_lines - 1:
        lines.append(f'  <p>正文第 {len(lines) + 1} 行：𠮷 &amp; <ruby>漢<rt>かん</rt></ruby></p>')
    lines.append('</body></html>')
    return '\n'.join(lines)


def write_fixture(scratch, chapter, answer, expected):
    members = {
        'mimetype': b'application/epub+zip',
        'META-INF/container.xml': CONTAINER,
        'OEBPS/content.opf': OPF.encode(),
        'OEBPS/nav.xhtml': NAV.encode(),
        CHAPTER: chapter.encode(),
        OTHER: xhtml([], 40).encode(),
    }
    path = scratch / 'agent-navigation.epub'
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    pathlib.Path(str(path) + '.answer.md').write_text(answer, encoding='utf-8')
    pathlib.Path(str(path) + '.expect.json').write_text(
        json.dumps(expected, ensure_ascii=False), encoding='utf-8')
    return path, None, members


def expectations(answer, chapter):
    source_lines = chapter.split('\n')
    cited = sorted({int(n) for n in LINE_REF.findall(answer)})
    return {
        'resource_path': CHAPTER,
        'file_links': 1,
        'line_links': len(LINE_REF.findall(answer)),
        'lines': {str(n): source_lines[n - 1] for n in cited},
    }


def synthetic_fixture(scratch):
    chapter = xhtml([], 2300)
    return write_fixture(scratch, chapter, SYNTHETIC_ANSWER,
                         expectations(SYNTHETIC_ANSWER, chapter))


def sample_fixture(export_path):
    export = json.loads(pathlib.Path(export_path).expanduser().read_text(encoding='utf-8'))
    events = export['events']
    answer = [e for e in events if e['type'] == 'assistant_message'][-1]['payload']['content']
    complete = {}
    total_lines = 0
    for event in events:
        payload = event['payload']
        data = payload.get('data') or {}
        if (event['type'] != 'tool_completed' or payload.get('name') != 'resource.read_fragment'
                or data.get('book_path') != CHAPTER):
            continue
        entries = data.get('lines') or []
        ends_at_total = data['offset'] + len(data['text']) >= data['total']
        for index, entry in enumerate(entries):
            if index == 0 and data['offset'] > 0:
                continue
            if index == len(entries) - 1 and not ends_at_total:
                continue
            complete[entry['line']] = entry['text']
        if ends_at_total:
            total_lines = max(total_lines, entries[-1]['line'])
    if not total_lines:
        raise SystemExit('the export does not contain the end of ' + CHAPTER)

    def build(scratch):
        lines = []
        for number in range(1, total_lines + 1):
            lines.append(complete.get(number, f'  <p>（未读取的第 {number} 行）</p>'))
        chapter = '\n'.join(lines)
        expected = expectations(answer, chapter)
        missing = [n for n in expected['lines'] if int(n) not in complete]
        if missing:
            raise SystemExit('cited lines without recorded source text: ' + ', '.join(missing))
        print(f'sample export: {len(complete)} recorded source lines of {total_lines}, '
              f'{expected["line_links"]} line references in the final answer')
        return write_fixture(scratch, chapter, answer, expected)

    return build


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--sample-export', type=pathlib.Path)
    args = parser.parse_args()
    fixture = sample_fixture(args.sample_export) if args.sample_export else synthetic_fixture
    run(args.build.resolve(), 'agent_conversation_navigation_integration_test.cpp',
        fixture, lambda *unused: None, direct_app_executable=True, run_timeout=90)
