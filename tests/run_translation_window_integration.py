"""Real Sigil window and model adapter, synthetic EPUB and loopback HTTP only."""
import argparse
import http.server
import json
import threading
import zipfile
import posixpath
from urllib.parse import unquote
from xml.etree import ElementTree as ET

from run_opf_resource_integration import epub_fixture, run


def fixture(scratch):
    path, source, members = epub_fixture(scratch)
    body = '<p>Original paragraph</p>'
    body += ''.join(f'<p id="p{i}">第{i}段の<ruby>名前<rt>なまえ</rt></ruby>です。文章を読み、翻訳します。</p>' for i in range(220))
    members['OEBPS/a.xhtml'] = ('<html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
                               + body + '</body></html>').encode()
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, source, members


class Mock(http.server.BaseHTTPRequestHandler):
    requests = []
    lock = threading.Lock()

    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert not request.get('tools'), 'Translation received irrelevant Agent tool schemas'
        assert len(request['messages']) == 2, 'Translation received main conversation history'
        prompt = request['messages'][0]['content']
        payload = json.loads(request['messages'][1]['content'])
        if '完整阅读本批' in prompt or '整理候选词表' in prompt:
            result = {'terms': [{'source': '名前', 'target': '名字'}], 'memory': '角色称呼保持一致。'}
        elif '校正全部' in prompt:
            result = {'changes': []}
        else:
            assert request['model'] == 'test-translator'
            result = {'items': [{'n': row['n'], 'text': '译：' + row['source']} for row in payload['items']]}
        with self.lock:
            self.requests.append((request['model'], len(payload['items'])))
        body = json.dumps({'choices': [{'message': {'role': 'assistant', 'content': json.dumps(result, ensure_ascii=False)},
                                       'finish_reason': 'stop'}],
                           'usage': {'prompt_tokens': 100, 'completion_tokens': 20, 'prompt_cache_hit_tokens': 40}}, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def verify(path, source, members):
    with zipfile.ZipFile(str(path) + '.translated.epub') as archive:
        opf = ET.fromstring(archive.read('OEBPS/content.opf'))
        glossaries = [node for node in opf.iter() if node.tag.endswith('}item') and node.get('href', '').endswith('.json')]
        assert len(glossaries) == 1 and glossaries[0].get('media-type') == 'application/json'
        name = posixpath.normpath(posixpath.join('OEBPS', unquote(glossaries[0].get('href'))))
        terms = json.loads(archive.read(name))
        assert terms['format'] == 'sigil-translation-glossary-v1' and terms['terms']
        chapter = ET.fromstring(archive.read('OEBPS/a.xhtml'))
        translations = [n for n in chapter.iter() if 'se-translation' in n.get('class', '').split()]
        assert len(translations) == 221, 'Saved EPUB lost or duplicated translation nodes'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build')
    args = parser.parse_args()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Mock) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f'http://127.0.0.1:{server.server_port}/chat/completions'
            import pathlib
            run(pathlib.Path(args.build).resolve(), 'translation_window_integration_test.cpp', fixture,
                verify, direct_app_executable=True, run_arguments=[(url,)], run_timeout=60)
            assert any(model == 'test-planner' for model, _ in Mock.requests)
            assert any(model == 'test-translator' for model, _ in Mock.requests)
            assert len(Mock.requests) < 15, 'Request count grew per sentence'
        finally:
            server.shutdown()
