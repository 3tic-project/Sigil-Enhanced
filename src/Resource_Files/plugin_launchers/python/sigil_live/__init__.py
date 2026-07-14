"""Sigil Plugin API v2 client library."""

from .client import EditorState, Plugin, Resource, Selection
from .errors import (
    BookClosed,
    InvalidPatch,
    PermissionDenied,
    PluginApiError,
    ResourceNotFound,
    RevisionConflict,
)

__all__ = [
    "BookClosed",
    "EditorState",
    "InvalidPatch",
    "PermissionDenied",
    "Plugin",
    "PluginApiError",
    "Resource",
    "ResourceNotFound",
    "RevisionConflict",
    "Selection",
]

__version__ = "2.0.0"
