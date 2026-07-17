#!/usr/bin/env python3
"""Upload local files to one active Sigil MCP transaction without Base64."""

import argparse
import getpass
import glob
import hashlib
import http.client
import json
import mimetypes
import os
import pathlib
import sys
import tempfile
import urllib.parse


IMPORT_PATH = "/api/v1/imports"
MAX_RESPONSE_SIZE = 1024 * 1024
METADATA_PATTERN = "sigil-mcp-*.json"
TEXT_MEDIA_TYPES = {
    "application/javascript",
    "application/json",
    "application/oebps-package+xml",
    "application/smil+xml",
    "application/xhtml+xml",
    "application/xml",
    "image/svg+xml",
}


class UploadError(RuntimeError):
    pass


def _candidate_runtime_directories(explicit=None):
    if explicit:
        return [pathlib.Path(explicit).expanduser()]
    candidates = []
    configured = os.environ.get("SIGIL_MCP_RUNTIME_DIR")
    if configured:
        candidates.append(pathlib.Path(configured))
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR")
    if xdg_runtime:
        candidates.append(pathlib.Path(xdg_runtime) / "sigil-enhanced" / "mcp")
    if sys.platform == "darwin":
        candidates.append(
            pathlib.Path.home()
            / "Library"
            / "Application Support"
            / "sigil-enhanced"
            / "mcp"
        )
    temporary = pathlib.Path(tempfile.gettempdir())
    candidates.append(temporary / "sigil-enhanced" / "mcp")
    candidates.append(
        temporary / ("runtime-" + getpass.getuser()) / "sigil-enhanced" / "mcp"
    )
    candidates.extend(
        pathlib.Path(value)
        for value in glob.glob(
            str(temporary / "runtime-*" / "sigil-enhanced" / "mcp")
        )
    )
    unique = []
    seen = set()
    for path in candidates:
        key = str(path.expanduser())
        if key not in seen:
            seen.add(key)
            unique.append(path.expanduser())
    return unique


def _load_metadata(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError) as error:
        raise UploadError("cannot read session metadata {0}: {1}".format(path, error)) from error
    for name in ("endpoint", "token", "session_id", "transport"):
        if not value.get(name):
            raise UploadError("session metadata is missing {0}".format(name))
    if value["transport"] != "streamable-http":
        raise UploadError("session metadata has an unsupported transport")
    return value


def _process_is_alive(metadata):
    pid = metadata.get("pid")
    if not isinstance(pid, int) or isinstance(pid, bool) or pid <= 0:
        return True
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except (PermissionError, OSError):
        return True
    return True


def discover_metadata(metadata_path=None, runtime_directory=None, session_id=None):
    if metadata_path:
        path = pathlib.Path(metadata_path).expanduser()
        metadata = _load_metadata(path)
        if session_id and metadata["session_id"] != session_id:
            raise UploadError("selected metadata does not match --session-id")
        return path, metadata
    matches = []
    for directory in _candidate_runtime_directories(runtime_directory):
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob(METADATA_PATTERN)):
            try:
                metadata = _load_metadata(path)
            except UploadError:
                continue
            if _process_is_alive(metadata) and (
                not session_id or metadata["session_id"] == session_id
            ):
                matches.append((path, metadata))
    if not matches:
        raise UploadError("no running Sigil MCP Book session was found")
    if len(matches) > 1:
        raise UploadError("multiple Book sessions are active; pass --metadata or --session-id")
    return matches[0]


def _import_endpoint(metadata):
    endpoint = metadata.get("external_import_endpoint")
    if not endpoint:
        parsed = urllib.parse.urlsplit(metadata["endpoint"])
        endpoint = urllib.parse.urlunsplit(
            (parsed.scheme, parsed.netloc, IMPORT_PATH, "", "")
        )
    parsed = urllib.parse.urlsplit(endpoint)
    if (
        parsed.scheme != "http"
        or parsed.hostname not in {"127.0.0.1", "localhost", "::1"}
        or parsed.path != IMPORT_PATH
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.port is None
    ):
        raise UploadError("external import endpoint is not a valid loopback URL")
    return parsed


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _media_type(path, explicit=None):
    if explicit:
        return explicit
    guessed, _ = mimetypes.guess_type(path.name)
    if not guessed:
        raise UploadError("media_type is required for {0}".format(path))
    return guessed


def _kind(media_type, explicit=None):
    if explicit:
        return explicit
    return "text" if media_type.startswith("text/") or media_type in TEXT_MEDIA_TYPES else "binary"


def _default_spine(media_type):
    return media_type == "application/xhtml+xml"


def normalize_spec(value, base_directory, default_transaction=None):
    if not isinstance(value, dict):
        raise UploadError("each manifest resource must be an object")
    source_value = value.get("source") or value.get("source_path")
    if not source_value:
        raise UploadError("resource source is required")
    source = pathlib.Path(source_value).expanduser()
    if not source.is_absolute():
        source = base_directory / source
    if not source.is_file():
        raise UploadError("source file does not exist: {0}".format(source))
    operation = value.get("operation", "add")
    transaction_id = value.get("transaction_id", default_transaction)
    if not transaction_id:
        raise UploadError("transaction_id is required")
    media_type = _media_type(source, value.get("media_type"))
    kind = _kind(media_type, value.get("kind"))
    if kind not in {"text", "binary"}:
        raise UploadError("kind must be text or binary")
    if operation not in {"add", "replace"}:
        raise UploadError("operation must be add or replace")
    spec = dict(value)
    spec.update({
        "source": source,
        "transaction_id": transaction_id,
        "operation": operation,
        "media_type": media_type,
        "kind": kind,
    })
    if operation == "add":
        if not spec.get("book_path"):
            raise UploadError("book_path is required for add")
        spec.setdefault("add_to_spine", _default_spine(media_type))
        spec.setdefault("manifested", True)
        if spec["manifested"] and not spec.get("manifest_id"):
            raise UploadError("manifest_id is required for a manifested resource")
    elif not spec.get("resource_id") or spec.get("expected_revision") is None:
        raise UploadError("replace requires resource_id and expected_revision")
    return spec


def upload_file(metadata, spec, timeout=120):
    source = spec["source"]
    size = source.stat().st_size
    digest = _sha256(source)
    query = {}
    for name in (
        "transaction_id", "kind", "operation", "book_path", "media_type",
        "resource_id", "expected_revision", "manifest_id", "properties",
        "fallback", "overlay", "add_to_spine", "manifested",
    ):
        value = spec.get(name)
        if value is not None:
            query[name] = str(value).lower() if isinstance(value, bool) else str(value)
    endpoint = _import_endpoint(metadata)
    target = endpoint.path + "?" + urllib.parse.urlencode(query)
    connection = http.client.HTTPConnection(endpoint.hostname, endpoint.port, timeout=timeout)
    try:
        connection.putrequest("POST", target)
        connection.putheader("Authorization", "Bearer " + metadata["token"])
        connection.putheader("Content-Type", "application/octet-stream")
        connection.putheader("Content-Length", str(size))
        connection.putheader("X-Content-SHA256", digest)
        connection.endheaders()
        with source.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                connection.send(chunk)
        response = connection.getresponse()
        payload = response.read(MAX_RESPONSE_SIZE + 1)
        if len(payload) > MAX_RESPONSE_SIZE:
            raise UploadError("Sigil external import response exceeds 1 MiB")
        try:
            value = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as error:
            raise UploadError("Sigil returned an invalid JSON response") from error
        if response.status != 201:
            detail = value.get("error", value) if isinstance(value, dict) else value
            raise UploadError("Sigil import failed ({0}): {1}".format(response.status, detail))
        return value
    except (OSError, http.client.HTTPException) as error:
        raise UploadError(
            "cannot reach Sigil external import endpoint: {0}".format(error)
        ) from error
    finally:
        connection.close()


def _manifest_specs(path, transaction_id=None):
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise UploadError("cannot read import manifest: {0}".format(error)) from error
    if not isinstance(manifest, dict) or not isinstance(manifest.get("resources"), list):
        raise UploadError("manifest must contain a resources array")
    default_transaction = transaction_id or manifest.get("transaction_id")
    return [
        normalize_spec(item, path.parent, default_transaction)
        for item in manifest["resources"]
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument("--file", type=pathlib.Path, help="one source file")
    source_group.add_argument("--manifest", type=pathlib.Path, help="JSON batch manifest")
    parser.add_argument("--transaction", help="active MCP transaction ID")
    parser.add_argument("--operation", choices=("add", "replace"), default="add")
    parser.add_argument("--book-path")
    parser.add_argument("--media-type")
    parser.add_argument("--kind", choices=("text", "binary"))
    parser.add_argument("--manifest-id")
    parser.add_argument("--properties")
    parser.add_argument("--resource-id")
    parser.add_argument("--expected-revision", type=int)
    parser.add_argument("--add-to-spine", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--manifested", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--metadata", help="exact sigil-mcp-*.json file")
    parser.add_argument("--runtime-dir")
    parser.add_argument("--session-id")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args(argv)
    try:
        _, metadata = discover_metadata(args.metadata, args.runtime_dir, args.session_id)
        if args.manifest:
            specs = _manifest_specs(args.manifest.expanduser(), args.transaction)
        else:
            raw = {
                "source": str(args.file),
                "transaction_id": args.transaction,
                "operation": args.operation,
                "book_path": args.book_path,
                "media_type": args.media_type,
                "kind": args.kind,
                "manifest_id": args.manifest_id,
                "properties": args.properties,
                "resource_id": args.resource_id,
                "expected_revision": args.expected_revision,
            }
            if args.add_to_spine is not None:
                raw["add_to_spine"] = args.add_to_spine
            if args.manifested is not None:
                raw["manifested"] = args.manifested
            specs = [normalize_spec(raw, pathlib.Path.cwd())]
        results = [upload_file(metadata, spec, max(1, min(args.timeout, 1800))) for spec in specs]
        print(json.dumps({"imported": len(results), "results": results}, ensure_ascii=False))
        return 0
    except UploadError as error:
        print("Sigil MCP upload: {0}".format(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
