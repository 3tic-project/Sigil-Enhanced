import inspect
import pathlib
import sys
import unittest


LAUNCHER_ROOT = pathlib.Path(__file__).parents[1] / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(LAUNCHER_ROOT))

from bookcontainer import BookContainer
from inputcontainer import InputContainer
from outputcontainer import OutputContainer
from sigil_live.compat import (
    CompatBookContainer,
    CompatInputContainer,
    CompatOutputContainer,
    CompatValidationContainer,
)
from validationcontainer import ValidationContainer


def public_api(container):
    result = {}
    for name, value in inspect.getmembers(container):
        if name.startswith("_"):
            continue
        if isinstance(value, property):
            result[name] = ("property", str(inspect.signature(value.fget)))
        elif inspect.isfunction(value):
            result[name] = ("method", str(inspect.signature(value)))
    return result


class LegacyPluginApiSurfaceTest(unittest.TestCase):
    def test_authoritative_container_sizes_are_stable(self):
        self.assertEqual(len(public_api(BookContainer)), 76)
        self.assertEqual(len(public_api(OutputContainer)), 60)
        self.assertEqual(len(public_api(InputContainer)), 12)
        self.assertEqual(
            set(public_api(ValidationContainer)) - set(public_api(OutputContainer)),
            {"add_result", "add_extended_result"},
        )

    def test_edit_compat_surface_preserves_all_v1_signatures(self):
        legacy = public_api(BookContainer)
        compat = public_api(CompatBookContainer)
        self.assertEqual({name: compat[name] for name in legacy}, legacy)
        self.assertEqual(set(compat) - set(legacy), {"flush"})

    def test_other_compat_surfaces_preserve_all_v1_signatures(self):
        for legacy_class, compat_class in (
            (OutputContainer, CompatOutputContainer),
            (InputContainer, CompatInputContainer),
            (ValidationContainer, CompatValidationContainer),
        ):
            self.assertEqual(public_api(compat_class), public_api(legacy_class))


if __name__ == "__main__":
    unittest.main()
