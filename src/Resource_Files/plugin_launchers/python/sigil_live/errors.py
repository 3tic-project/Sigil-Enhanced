class PluginApiError(RuntimeError):
    def __init__(self, message, code=None, data=None):
        super().__init__(message)
        self.code = code
        self.data = data


class PermissionDenied(PluginApiError):
    pass


class BookClosed(PluginApiError):
    pass


class ResourceNotFound(PluginApiError):
    pass


class RevisionConflict(PluginApiError):
    pass


class InvalidPatch(PluginApiError):
    pass


class TransactionRequired(PluginApiError):
    pass


class ValidationFailed(PluginApiError):
    pass


class PayloadTooLarge(PluginApiError):
    pass


class Busy(PluginApiError):
    pass


class UnsupportedOperation(PluginApiError):
    pass


class TransactionNotFound(PluginApiError):
    pass


class SessionEnding(PluginApiError):
    pass


ERROR_TYPES = {
    -32001: PermissionDenied,
    -32002: BookClosed,
    -32003: ResourceNotFound,
    -32004: RevisionConflict,
    -32005: InvalidPatch,
    -32006: TransactionRequired,
    -32007: ValidationFailed,
    -32008: PayloadTooLarge,
    -32009: Busy,
    -32010: UnsupportedOperation,
    -32011: TransactionNotFound,
    -32012: SessionEnding,
}


def error_from_response(error):
    error_type = ERROR_TYPES.get(error.get("code"), PluginApiError)
    return error_type(error.get("message", "Plugin API error"), error.get("code"), error.get("data"))
