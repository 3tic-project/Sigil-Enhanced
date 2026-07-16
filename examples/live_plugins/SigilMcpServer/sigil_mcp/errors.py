import json

from sigil_live.errors import PluginApiError


RECOVERY = {
    "BookClosed": (False, "Stop and discover a new Sigil Book session."),
    "ResourceNotFound": (True, "List resources again and resolve the current resource ID."),
    "RevisionConflict": (True, "Read the resource again and rebuild the edit."),
    "InvalidPatch": (True, "Correct the UTF-16 ranges and submit non-overlapping edits."),
    "TransactionRequired": (True, "Begin a transaction before the requested write."),
    "ValidationFailed": (True, "Inspect validation details, revise staged changes, and retry."),
    "PayloadTooLarge": (True, "Use pagination, smaller batches, or a bounded text operation."),
    "Busy": (True, "Finish or roll back the current transaction and retry."),
    "UnsupportedOperation": (False, "Query capabilities and choose a supported operation."),
    "TransactionNotFound": (True, "Begin a new transaction and restage the changes."),
    "SessionEnding": (False, "Stop using this endpoint and discover another session."),
}


class BackendError(RuntimeError):
    def __init__(self, code, message, retryable=False, recovery="", data=None):
        super().__init__(message)
        self.error_code = code
        self.retryable = retryable
        self.recovery = recovery
        self.data = data


def error_payload(error):
    if isinstance(error, BackendError):
        return {
            "code": error.error_code,
            "message": str(error),
            "retryable": error.retryable,
            "recovery": error.recovery,
            "data": error.data,
        }
    if isinstance(error, PluginApiError):
        code = type(error).__name__
        retryable, recovery = RECOVERY.get(
            code, (False, "Inspect the host error and retry only after correcting the request.")
        )
        return {
            "code": code,
            "message": str(error),
            "retryable": retryable,
            "recovery": recovery,
            "data": error.data,
        }
    return {
        "code": "InternalError",
        "message": str(error) or type(error).__name__,
        "retryable": False,
        "recovery": "Inspect the Sigil plugin session console.",
        "data": None,
    }


def format_tool_error(error):
    return json.dumps(error_payload(error), ensure_ascii=False, sort_keys=True)
