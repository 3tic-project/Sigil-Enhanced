"""Compare the native OPF editor fold-back with the frozen Python behavior."""

import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/Resource_Files/python3lib'))
from opf_source_bytes import editor_text, restore_source_text


def cases():
    yield '', 'new\n'
    yield 'one\r\ntwo\r\nthree\r\n', 'one\nnew\ntwo\nthree\n'
    yield 'one\r\ntwo\r\nthree\r\n', 'one\nthree\n'
    yield 'one\r\ntwo\nthree\rfour\u2028five\u2029six', 'changed\ntwo\nthree\nfour\nchanged\nsix'
    yield '<x/>\r\n' * 300 + '<x/>\n' * 300 + '<x/>\r\n' * 300, '<x/>\n' * 300 + '<new/>\n' + '<x/>\n' * 600
    source = '<x/>\r\n' * 12000
    edited = editor_text(source)
    yield source, edited[:24000] + '<y/>' + edited[24004:]

    randomizer = random.Random(20260925)
    endings = ('\r\n', '\n', '\r', '\u2028', '\u2029')
    words = ('a', 'b', 'b', 'c', '日本', '𠮷', 'e\u0301', '<x/>', '')
    for _ in range(350):
        old = [randomizer.choice(words) + randomizer.choice(endings)
               for _ in range(randomizer.randrange(0, 80))]
        if old and randomizer.randrange(2):
            old[-1] = old[-1].rstrip('\r\n\u2028\u2029')
        source = ''.join(old)
        new = editor_text(source).splitlines(keepends=True)
        for _ in range(randomizer.randrange(1, 5)):
            position = randomizer.randrange(len(new) + 1)
            operation = randomizer.randrange(3)
            if operation == 0 or not new:
                new.insert(position, randomizer.choice(words) + '\n')
            elif operation == 1 and position < len(new):
                new.pop(position)
            elif position < len(new):
                new[position] = randomizer.choice(words) + '\n'
        yield source, ''.join(new)


def main():
    samples = list(cases())
    payload = ''.join(original.encode().hex() + '\t' + edited.encode().hex() + '\n'
                      for original, edited in samples)
    output = subprocess.run([sys.argv[1]], input=payload, text=True,
                            capture_output=True, check=True).stdout.splitlines()
    assert len(output) == len(samples), (len(output), len(samples))
    for index, ((original, edited), actual) in enumerate(zip(samples, output)):
        expected = restore_source_text(original, edited)
        assert bytes.fromhex(actual).decode() == expected, (
            f'Case {index} differs:\noriginal={original!r}\nedited={edited!r}\n'
            f'expected={expected!r}\nactual={bytes.fromhex(actual).decode()!r}')
    print(f'{len(samples)} OPF source text cases match the legacy Python implementation')


if __name__ == '__main__':
    main()
