"""Run the existing OPF source tests against the C++ patcher and the legacy Python."""

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
import opf_source_legacy as opf_source


class Probe:
    def __init__(self, program):
        self.process = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def call(self, operation, *values):
        payload = operation + "\t" + "\t".join(value.encode().hex() for value in values) + "\n"
        self.process.stdin.write(payload)
        self.process.stdin.flush()
        line = self.process.stdout.readline().strip()
        if line == "E":
            raise ValueError("native package edit failed")
        if not line.startswith("S"):
            raise RuntimeError("malformed native response: " + line)
        return bytes.fromhex(line[1:]).decode()


def compare(probe, operation, original, *values):
    failed = False
    try:
        expected = original(*values)
    except Exception:
        failed = True
        expected = None
    try:
        actual = probe.call(operation, *values)
    except ValueError:
        if not failed:
            raise AssertionError(operation + " native edit failed for input the legacy editor accepts")
        raise
    if failed:
        raise AssertionError(operation + " native edit succeeded where the legacy editor rejects the input")
    if actual != expected:
        raise AssertionError(operation + " differs from the legacy editor")
    return expected


def main():
    probe = Probe(sys.argv[1])
    originals = {
        "apply_model_update": opf_source.apply_model_update,
        "model_xml": opf_source.model_xml,
        "add_navigation_manifest": opf_source.add_navigation_manifest,
    }
    opf_source.apply_model_update = lambda *args: compare(probe, "A", originals["apply_model_update"], *args)
    opf_source.model_xml = lambda *args: compare(probe, "M", originals["model_xml"], *args)
    opf_source.add_navigation_manifest = lambda *args: compare(probe, "N", originals["add_navigation_manifest"], *args)
    spec = importlib.util.spec_from_file_location("opf_source_test", ROOT / "tests/opf_source_test.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(module))
    probe.process.stdin.close()
    probe.process.wait()
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == "__main__":
    main()
