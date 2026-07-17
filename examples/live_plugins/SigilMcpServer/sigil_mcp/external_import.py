import hashlib
import hmac
import os
import re
import tempfile

from starlette.responses import JSONResponse

from .backend import MAX_EXTERNAL_IMPORT_SIZE
from .errors import error_payload


IMPORT_PATH = "/api/v1/imports"
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")


def _optional(query, name):
    value = query.get(name)
    return value if value else None


def _boolean(query, name, default):
    value = query.get(name)
    if value is None:
        return default
    lowered = value.lower()
    if lowered in {"1", "true", "yes"}:
        return True
    if lowered in {"0", "false", "no"}:
        return False
    raise ValueError("{0} must be true or false".format(name))


def _metadata(request):
    query = request.query_params
    operation = query.get("operation", "add")
    expected_revision = _optional(query, "expected_revision")
    if expected_revision is not None:
        expected_revision = int(expected_revision)
        if expected_revision < 0:
            raise ValueError("expected_revision must be non-negative")
    return {
        "transaction_id": query.get("transaction_id"),
        "kind": query.get("kind"),
        "operation": operation,
        "book_path": _optional(query, "book_path"),
        "media_type": _optional(query, "media_type"),
        "resource_id": _optional(query, "resource_id"),
        "expected_revision": expected_revision,
        "manifest_id": _optional(query, "manifest_id"),
        "properties": _optional(query, "properties"),
        "fallback": _optional(query, "fallback"),
        "overlay": _optional(query, "overlay"),
        "add_to_spine": _boolean(query, "add_to_spine", False),
        "manifested": _boolean(query, "manifested", True),
    }


def _status_for(error):
    code = error_payload(error)["code"]
    if code == "PayloadTooLarge":
        return 413
    if code in {"RevisionConflict", "Busy", "TransactionNotFound"}:
        return 409
    if code == "InternalError":
        return 500
    return 400


def create_external_import_handler(backend, gate):
    async def import_resource(request):
        temporary_path = None
        transaction_id = None
        import_started = False
        try:
            declared_hash = request.headers.get("x-content-sha256", "").lower()
            if not SHA256_PATTERN.fullmatch(declared_hash):
                raise ValueError("X-Content-SHA256 must be a hexadecimal SHA-256")
            raw_length = request.headers.get("content-length")
            if raw_length is None:
                return JSONResponse(
                    {"error": {"code": "LengthRequired", "message": "Content-Length is required"}},
                    status_code=411,
                )
            declared_size = int(raw_length)
            if declared_size < 0 or declared_size > MAX_EXTERNAL_IMPORT_SIZE:
                return JSONResponse(
                    {"error": {"code": "PayloadTooLarge", "message": "upload exceeds 32 MiB"}},
                    status_code=413,
                )
            metadata = _metadata(request)
            if not metadata["transaction_id"] or not metadata["kind"]:
                raise ValueError("transaction_id and kind are required")
            transaction_id = metadata["transaction_id"]
            await gate.call(backend.begin_external_import, transaction_id)
            import_started = True

            descriptor, temporary_path = tempfile.mkstemp(prefix="sigil-mcp-import-")
            digest = hashlib.sha256()
            received = 0
            with os.fdopen(descriptor, "wb") as stream:
                async for chunk in request.stream():
                    if not chunk:
                        continue
                    received += len(chunk)
                    if received > declared_size or received > MAX_EXTERNAL_IMPORT_SIZE:
                        return JSONResponse(
                            {
                                "error": {
                                    "code": "PayloadTooLarge",
                                    "message": "body exceeds declared size",
                                }
                            },
                            status_code=413,
                        )
                    stream.write(chunk)
                    digest.update(chunk)
                stream.flush()
                os.fsync(stream.fileno())
            if received != declared_size:
                raise ValueError("body length does not match Content-Length")
            actual_hash = digest.hexdigest()
            if not hmac.compare_digest(actual_hash, declared_hash):
                raise ValueError("body SHA-256 does not match X-Content-SHA256")

            result = await gate.call(
                backend.transaction_import_file, path=temporary_path, **metadata
            )
            return JSONResponse(result, status_code=201)
        except (TypeError, ValueError) as error:
            return JSONResponse(
                {"error": {"code": "InvalidRequest", "message": str(error)}},
                status_code=400,
            )
        except Exception as error:
            return JSONResponse(
                {"error": error_payload(error)}, status_code=_status_for(error)
            )
        finally:
            if temporary_path:
                try:
                    os.unlink(temporary_path)
                except FileNotFoundError:
                    pass
            if import_started:
                await gate.call(backend.end_external_import, transaction_id)

    return import_resource
