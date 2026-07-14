"""API-compatible container types for the live v1 compatibility runtime.

These classes deliberately inherit the authoritative v1 containers. The
RPC-backed wrapper is supplied separately, so the established method bodies,
signatures, iterator shapes, preferences, and local path helpers are reused
instead of being copied into a divergent second implementation.
"""

from bookcontainer import BookContainer
from inputcontainer import InputContainer
from outputcontainer import OutputContainer
from validationcontainer import ValidationContainer


class CompatBookContainer(BookContainer):
    """V1 edit container backed by a live wrapper and implicit transaction."""

    def flush(self, label="Apply staged plugin changes"):
        """Commit the current implicit transaction and begin another one."""
        return self._w.flush(label)


class CompatOutputContainer(OutputContainer):
    """V1 output container API surface for the live compatibility runtime."""


class CompatInputContainer(InputContainer):
    """V1 input container API surface; live input execution is not wired yet."""


class CompatValidationContainer(ValidationContainer):
    """V1 validation container API surface for the live compatibility runtime."""
