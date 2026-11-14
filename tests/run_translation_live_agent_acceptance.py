"""Opt-in real Agent acceptance; --smoke exercises the harness without a paid endpoint."""
import argparse
import configparser
import hashlib
import http.server
import json
import os
import shutil
from pathlib import Path
import tempfile
import threading
import zipfile
from xml.etree import ElementTree as ET

from run_opf_resource_integration import epub_fixture, run
from run_translation_window_integration import Mock


def fixture(scratch):
    path, source, members = epub_fixture(scratch)
    source = source.replace('<dc:language>en</dc:language>', '<dc:language>ja</dc:language>')
    source = source.replace('</manifest>', "<item id='b' href='b.xhtml' media-type='application/xhtml+xml'/></manifest>")
    source = source.replace("<itemref idref='a'/>", "<itemref idref='a'/><itemref idref='b'/>")
    members['OEBPS/content.opf'] = b'\xff\xfe' + source.encode('utf-16-le')
    header = '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Original synthetic scene</title></head><body>'
    members['OEBPS/a.xhtml'] = (header + '''<h1 id="start">雨の駅</h1>
<p id="a1"><ruby>ミナ<rt>みな</rt></ruby>は窓の外を見た。「リク、雨が止んだら、約束の場所へ行こう。」</p>
<p id="a2">リクは頷いた。彼がポケットから取り出したのは、昨日ミナが渡した小さな鍵だった。</p>
<p id="a3">町の時計台が六時を告げた。しかし、二人が待っていた列車は、まだ姿を見せなかった。<a epub:type="noteref" href="b.xhtml#note">注</a></p>
</body></html>''').encode()
    members['OEBPS/b.xhtml'] = (header + '''<h1 id="promise">約束の鍵</h1>
<p id="b1">「怖くないと言えば嘘になる。でも、一人で待つよりはずっといい」とミナは笑った。</p>
<p id="b2">その笑顔を見て、リクはようやく気づいた。守るべき約束は、遠い昔のものだけではない。</p>
<p id="b3">雨音が少しずつ弱くなった。ミナは鍵を受け取り、もう一度だけ駅の入り口を振り返った。</p>
<aside epub:type="footnote" id="note"><p>時計台は駅の東側にある。<a href="a.xhtml#a3">本文に戻る</a></p></aside>
</body></html>''').encode()
    members['OEBPS/nav.xhtml'] = (header + '''<nav epub:type="toc"><h1>目次</h1><ol><li><a href="a.xhtml#start">雨の駅</a></li><li><a href="b.xhtml#promise">約束の鍵</a></li></ol></nav></body></html>''').encode()
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, source, members


class Smoke(Mock):
    def respond(self, response):
        for choice in response.get('choices', []):
            message = choice['message']
            if any(c['function']['name'] == 'translation.prepare' for c in message.get('tool_calls', [])):
                message['content'] = '先扫描和整理人物术语，再翻译、校正，最后保存词表。'
            try:
                content = json.loads(message.get('content', ''))
            except (ValueError, TypeError):
                continue
            if 'terms' in content:
                content['terms'] = [{'source': 'ミナ', 'target': '米娜'}, {'source': 'リク', 'target': '陆'}]
            for item in content.get('items', []):
                item['text'] = item['text'].replace('ミナ', '米娜').replace('リク', '陆')
            message['content'] = json.dumps(content, ensure_ascii=False)
        super().respond(response)


def verify(path, source, members):
    for output in Path(path).parent.glob('fixture.epub.*.epub'):
        with zipfile.ZipFile(output) as archive:
            for name in ('OEBPS/a.xhtml', 'OEBPS/b.xhtml', 'OEBPS/nav.xhtml'):
                original, translated = ET.fromstring(members[name]), ET.fromstring(archive.read(name))
                for attribute in ('id', 'href'):
                    before = [node.get(attribute) for node in original.iter() if node.get(attribute)]
                    after = [node.get(attribute) for node in translated.iter() if node.get(attribute)
                             and not node.get(attribute).startswith('sigil-translation-')]
                    assert before == after, f'{name}: original {attribute} changed'
            opf = ET.fromstring(archive.read('OEBPS/content.opf'))
            glossaries = [n for n in opf.iter() if n.get('media-type') == 'application/json']
            assert len(glossaries) == 1, 'Agent did not reuse a single glossary'
        shutil.copy2(output, Path(os.environ['SIGIL_LIVE_AGENT_REPORT_DIR']) / output.name)
    shutil.copy2(path, Path(os.environ['SIGIL_LIVE_AGENT_REPORT_DIR']) / 'source.epub')


def summarize_report(path):
    report = json.loads(path.read_text())
    assert report['passed'], f'{path.stem} did not pass; inspect sanitized report'
    calls = {}
    full = False; full_runs = 0
    for event in report['events']:
        payload = event['payload']
        if event['type'] == 'tool_requested' and payload.get('name') == 'translation.run':
            arguments = payload['arguments']; stages = arguments.get('stages', [])
            calls[payload['tool_call_id']] = isinstance(stages, list) and (not stages or {'scan', 'aggregate', 'translate', 'review'} <= set(stages))
        if event['type'] == 'tool_completed' and payload.get('name') == 'translation.run' and payload.get('ok'):
            full |= calls.get(payload['tool_call_id'], False)
            full_runs += calls.get(payload['tool_call_id'], False)
    assert full, 'Agent omitted a requested scan/aggregate/translate/review stage'
    assert full_runs == 1, 'Agent unnecessarily restarted full scan/translation workflow'
    report['full_workflow_selected'] = full
    report['full_workflow_runs'] = full_runs
    report['main_model_requests'] = sum(e['type'] == 'model_request_started' for e in report['events'])
    usage = {}
    for event in report['events']:
        if event['type'] == 'model_request_completed':
            for key, value in event['payload'].get('usage', {}).items():
                if isinstance(value, (int, float)) and value >= 0:
                    usage[key] = usage.get(key, 0) + value
    report['main_model_usage'] = usage
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2))
    return report


def execute(args, configuration, live):
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    before = hashlib.sha256(configuration.read_bytes()).digest()
    os.environ['SIGIL_LIVE_AGENT_CONFIG'] = str(configuration.resolve())
    os.environ['SIGIL_LIVE_AGENT_REPORT_DIR'] = str(output)
    try:
        mode = ('live' if args.case == 'all' else 'live-' + args.case) if live else 'smoke'
        run(args.build.resolve(), 'translation_live_agent_acceptance.cpp', fixture, verify,
            direct_app_executable=True, run_arguments=[(mode,)], run_timeout=1000 if live else 60)
    finally:
        assert hashlib.sha256(configuration.read_bytes()).digest() == before, 'Original Agent settings changed'
    cases = ('normal', 'recovery') if live and args.case == 'all' else (args.case,) if live else ('normal',)
    for name in cases:
        report = summarize_report(output / f'{name}.json')
        print(json.dumps({k: report[k] for k in ('scenario', 'transport', 'agent_model', 'elapsed_ms', 'worker_requests', 'background_notifications', 'injected_failures', 'tools')}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--live', action='store_true')
    mode.add_argument('--smoke', action='store_true')
    parser.add_argument('--settings', type=Path, default=Path.home() / 'Library/Application Support/sigil-ebook/Sigil/sigil_v6.ini')
    parser.add_argument('--case', choices=('all', 'normal', 'recovery'), default='all')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output is None:
        args.output = args.build / ('translation-live-agent-acceptance' if args.live else 'translation-agent-smoke')
    if args.live:
        if not args.settings.is_file():
            parser.error('Existing Sigil settings file is required; no request sent')
        execute(args, args.settings, True)
    else:
        with tempfile.TemporaryDirectory(prefix='translation-agent-smoke-') as directory:
            with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Smoke) as server:
                thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
                configuration = Path(directory) / 'smoke.ini'
                settings = configparser.ConfigParser()
                settings['native_agent'] = {'provider': 'custom', 'base_url': f'http://127.0.0.1:{server.server_port}/chat/completions',
                    'api_key': 'synthetic-loopback-key', 'model': 'test-planner', 'thinking': 'false', 'translation_model_custom': 'test-translator'}
                with configuration.open('w') as file:
                    settings.write(file)
                try:
                    execute(args, configuration, False)
                finally:
                    server.shutdown()
