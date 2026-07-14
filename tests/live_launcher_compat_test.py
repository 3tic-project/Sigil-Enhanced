import io
import os
import pathlib
import sys
import types
import unittest
from unittest import mock


LAUNCHER_ROOT = pathlib.Path(__file__).parents[1] / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(LAUNCHER_ROOT))

import live_launcher
import sigil_live.compat


class FakePlugin:
    def __init__(self):
        self.finished = []
        self.closed = False
        self.validation = types.SimpleNamespace(published=[])
        self.validation.publish_results = self.validation.published.append

    def finish(self, status="success", message=""):
        self.finished.append((status, message))

    def close(self):
        self.closed = True


class FakeWrapper:
    instances = []

    def __init__(self, plugin, plugin_dir, plugin_name, writable=True):
        self.plugin = plugin
        self.plugin_dir = plugin_dir
        self.plugin_name = plugin_name
        self.commits = 0
        self.rollbacks = 0
        self.__class__.instances.append(self)

    def commit(self):
        self.commits += 1

    def rollback(self):
        self.rollbacks += 1


class FakeContainer:
    def __init__(self, wrapper):
        self._w = wrapper
        self.results = ["validation-result"]


class LiveLauncherCompatTest(unittest.TestCase):
    def setUp(self):
        FakeWrapper.instances.clear()
        self.plugin = FakePlugin()
        self.environment = mock.patch.dict(
            os.environ,
            {"SIGIL_PLUGIN_SOCKET": "socket", "SIGIL_PLUGIN_TOKEN": "token"},
            clear=False,
        )
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def launch(self, run, plugin_type="edit"):
        module = types.SimpleNamespace(run=run)
        arguments = [
            "--plugin", "/plugins/Test/plugin.py",
            "--plugin-name", "Test",
            "--compat-v1",
            "--plugin-type", plugin_type,
        ]
        with mock.patch.object(live_launcher.Plugin, "connect", return_value=self.plugin), \
             mock.patch.object(live_launcher, "load_plugin", return_value=module), \
             mock.patch.object(sigil_live.compat, "LiveWrapper", FakeWrapper), \
             mock.patch.object(sigil_live.compat, "CompatBookContainer", FakeContainer), \
             mock.patch.object(sigil_live.compat, "CompatValidationContainer", FakeContainer), \
             mock.patch("sys.stderr", new_callable=io.StringIO):
            return live_launcher.main(arguments)

    def test_success_commits_before_finishing(self):
        result = self.launch(lambda container: 0)
        wrapper = FakeWrapper.instances[0]
        self.assertEqual(result, 0)
        self.assertEqual((wrapper.commits, wrapper.rollbacks), (1, 0))
        self.assertEqual(self.plugin.finished, [("success", "")])
        self.assertTrue(self.plugin.closed)

    def test_nonzero_result_rolls_back(self):
        result = self.launch(lambda container: 2)
        wrapper = FakeWrapper.instances[0]
        self.assertEqual(result, 1)
        self.assertEqual((wrapper.commits, wrapper.rollbacks), (0, 1))
        self.assertEqual(self.plugin.finished[0][0], "failed")

    def test_exception_rolls_back_and_reports_failure(self):
        def fail(container):
            raise RuntimeError("failed run")

        result = self.launch(fail)
        wrapper = FakeWrapper.instances[0]
        self.assertEqual(result, 1)
        self.assertEqual((wrapper.commits, wrapper.rollbacks), (0, 1))
        self.assertEqual(self.plugin.finished, [("failed", "failed run")])

    def test_validation_success_publishes_without_a_write_commit(self):
        result = self.launch(lambda container: 0, plugin_type="validation")
        wrapper = FakeWrapper.instances[0]
        self.assertEqual(result, 0)
        self.assertEqual((wrapper.commits, wrapper.rollbacks), (0, 0))
        self.assertEqual(self.plugin.validation.published, [["validation-result"]])


if __name__ == "__main__":
    unittest.main()
