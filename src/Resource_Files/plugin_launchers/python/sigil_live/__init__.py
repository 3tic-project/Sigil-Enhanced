"""Sigil Plugin API v2 client library."""

from .client import EditorState, Plugin, Resource, Selection, Transaction
from .errors import (
    BookClosed,
    Busy,
    InvalidPatch,
    PayloadTooLarge,
    PermissionDenied,
    PluginApiError,
    ResourceNotFound,
    RevisionConflict,
    SessionEnding,
    TransactionNotFound,
    TransactionRequired,
    UnsupportedOperation,
    ValidationFailed,
)

__all__ = [
    "BookClosed",
    "Busy",
    "EditorState",
    "InvalidPatch",
    "PayloadTooLarge",
    "PermissionDenied",
    "Plugin",
    "PluginApiError",
    "Resource",
    "ResourceNotFound",
    "RevisionConflict",
    "SessionEnding",
    "Selection",
    "Transaction",
    "TransactionNotFound",
    "TransactionRequired",
    "UnsupportedOperation",
    "ValidationFailed",
]

__version__ = "2.0.0"
