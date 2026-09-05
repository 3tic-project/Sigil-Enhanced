"""Run the real OPFResource with an existing macOS Ninja Debug build.

Reuse the application's objects and link flags with a test entry point. This
avoids mocks of the Python bridge, QTextDocument and resource persistence, and
avoids recompiling the application twice. No existing binary or settings are
overwritten. Run after `cmake --build <build> --target Sigil`.
"""
import argparse
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile


def run(build):
    if sys.platform != 'darwin':
        raise RuntimeError('This integration runner currently supports macOS only')
    root = pathlib.Path(__file__).resolve().parents[1]
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if cache.get('CMAKE_GENERATOR') != 'Ninja' or cache.get('CMAKE_BUILD_TYPE') != 'Debug':
        raise RuntimeError('This integration runner requires a Ninja Debug build')
    if pathlib.Path(cache['CMAKE_HOME_DIRECTORY']).resolve() != root:
        raise RuntimeError('Build directory belongs to a different source worktree')
    ninja = cache['CMAKE_MAKE_PROGRAM']
    commands = subprocess.check_output([ninja, '-t', 'commands', 'Sigil'], cwd=build, text=True).splitlines()
    compile_line = next(line for line in commands if ' -c ' in line
                        and shlex.split(line)[-1].endswith('/src/main.cpp'))
    link_line = next(line for line in reversed(commands) if ' -o bin/Sigil.app/Contents/MacOS/Sigil ' in line)
    with tempfile.TemporaryDirectory(prefix='opf-native-') as directory:
        scratch = pathlib.Path(directory)
        test_object = scratch / 'test.o'
        compile_args = shlex.split(compile_line)
        for flag, path in [('-o', test_object), ('-MF', scratch / 'test.d'),
                           ('-MT', test_object), ('-c', root / 'tests/opf_resource_integration_test.cpp')]:
            compile_args[compile_args.index(flag) + 1] = str(path)
        subprocess.run(compile_args, cwd=build, check=True)
        link_args = shlex.split(link_line)
        # Keep only the compiler invocation, excluding application packaging.
        start = link_args.index('&&') + 1 if link_args[0] == ':' else 0
        end = link_args.index('&&', start) if '&&' in link_args[start:] else len(link_args)
        link_args = link_args[start:end]
        main_object = next(i for i, value in enumerate(link_args) if value.endswith('/main.cpp.o'))
        link_args[main_object] = str(test_object)
        # Keep the harness isolated inside the build; do not replace Sigil.
        executable_dir = build / 'bin/Sigil.app/Contents/MacOS'
        with tempfile.TemporaryDirectory(prefix='opf-test-', dir=executable_dir) as bin_directory:
            executable = pathlib.Path(bin_directory) / 'opf_resource_test'
            link_args[link_args.index('-o') + 1] = str(executable)
            subprocess.run(link_args, cwd=build, check=True)
            environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', SIGIL_PREFS_DIR=str(scratch))
            # Use source modules plus built runtime dependencies for the nested
            # executable; the embedded interpreter uses the same linked Python.
            environment['SIGIL_TEST_PYTHON_ROOT'] = str(build / 'bin/Sigil.app/Contents/python3lib')
            environment.pop('PYTHONPATH', None)
            subprocess.run([executable, root], env=environment, check=True, timeout=30)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve())
