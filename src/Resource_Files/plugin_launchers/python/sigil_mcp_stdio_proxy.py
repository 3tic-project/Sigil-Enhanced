#!/usr/bin/env python3

import argparse
import getpass
import glob
import http.client
import json
import os
import pathlib
import sys
import tempfile
import urllib.parse


MAX_MESSAGE_SIZE = 8 * 1024 * 1024
METADATA_PATTERN = "sigil-mcp-*.json"


class ProxyError(RuntimeError):
    pass


def _candidate_runtime_directories(explicit=None):
    candidates = []
    if explicit:
        candidates.append(pathlib.Path(explicit))
    configured = os.environ.get("SIGIL_MCP_RUNTIME_DIR")
    if configured:
        candidates.append(pathlib.Path(configured))
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR")
    if xdg_runtime:
        candidates.append(pathlib.Path(xdg_runtime) / "sigil-enhanced" / "mcp")

    # QStandardPaths::RuntimeLocation resolves to Application Support on macOS.
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
    for path in glob.glob(str(temporary / "runtime-*" / "sigil-enhanced" / "mcp")):
        candidates.append(pathlib.Path(path))

    unique = []
    seen = set()
    for path in candidates:
        try:
            key = str(path.expanduser().resolve())
        except OSError:
            key = str(path.expanduser())
        if key not in seen:
            seen.add(key)
            unique.append(path.expanduser())
    return unique


def _load_metadata(path):
    try:
        metadata = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError) as error:
        raise ProxyError("Cannot read MCP metadata {0}: {1}".format(path, error)) from error
    required = {"endpoint", "token", "session_id", "transport"}
    missing = sorted(required - set(metadata))
    if missing:
        raise ProxyError(
            "MCP metadata {0} is missing: {1}".format(path, ", ".join(missing))
        )
    if metadata["transport"] != "streamable-http":
        raise ProxyError("Unsupported MCP transport in {0}".format(path))
    if not isinstance(metadata["token"], str) or not metadata["token"]:
        raise ProxyError("MCP metadata token is invalid")
    return metadata


def discover_metadata(metadata_path=None, runtime_directory=None, session_id=None):
    if metadata_path:
        path = pathlib.Path(metadata_path).expanduser()
        metadata = _load_metadata(path)
        if session_id and metadata.get("session_id") != session_id:
            raise ProxyError("The selected metadata does not match --session-id")
        return path, metadata

    matches = []
    for directory in _candidate_runtime_directories(runtime_directory):
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob(METADATA_PATTERN)):
            try:
                metadata = _load_metadata(path)
            except ProxyError:
                continue
            if session_id and metadata.get("session_id") != session_id:
                continue
            matches.append((path, metadata))

    if not matches:
        raise ProxyError(
            "No running Sigil MCP Book session was found; start the plugin or pass --metadata"
        )
    if len(matches) > 1:
        descriptions = []
        for path, metadata in matches:
            book = metadata.get("book", {})
            descriptions.append(
                "{0} ({1})".format(metadata.get("session_id"), book.get("file_path") or path)
            )
        raise ProxyError(
            "Multiple Sigil MCP Book sessions are active; pass --metadata or --session-id: "
            + "; ".join(descriptions)
        )
    return matches[0]


def validate_endpoint(endpoint):
    parsed = urllib.parse.urlsplit(endpoint)
    if parsed.scheme != "http":
        raise ProxyError("MCP endpoint must use local HTTP")
    if parsed.hostname not in {"127.0.0.1", "localhost", "::1"}:
        raise ProxyError("MCP endpoint is not loopback")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ProxyError("MCP endpoint contains unsupported URL components")
    if parsed.path != "/mcp":
        raise ProxyError("MCP endpoint path must be /mcp")
    try:
        port = parsed.port
    except ValueError as error:
        raise ProxyError("MCP endpoint port is invalid") from error
    if port is None or not 1 <= port <= 65535:
        raise ProxyError("MCP endpoint port is missing or invalid")
    return parsed


class McpHttpRelay:
    def __init__(self, metadata, timeout=30):
        self.metadata = metadata
        self.endpoint = validate_endpoint(metadata["endpoint"])
        self.timeout = timeout
        self.session_id = None
        self.protocol_version = None

    def send(self, message):
        body = json.dumps(
            message, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        if len(body) > MAX_MESSAGE_SIZE:
            raise ProxyError("MCP message exceeds the 8 MiB proxy limit")
        headers = {
            "Accept": "application/json, text/event-stream",
            "Authorization": "Bearer " + self.metadata["token"],
            "Content-Type": "application/json",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        if self.protocol_version:
            headers["MCP-Protocol-Version"] = self.protocol_version

        connection = self._connection()
        try:
            connection.request("POST", self.endpoint.path, body=body, headers=headers)
            response = connection.getresponse()
            payload = response.read(MAX_MESSAGE_SIZE + 1)
            if len(payload) > MAX_MESSAGE_SIZE:
                raise ProxyError("MCP response exceeds the 8 MiB proxy limit")
            session_id = response.getheader("Mcp-Session-Id")
            if session_id:
                self.session_id = session_id
            if response.status == 202:
                return None
            if response.status != 200:
                raise ProxyError(
                    "Sigil MCP endpoint returned HTTP {0}: {1}".format(
                        response.status,
                        payload.decode("utf-8", errors="replace")[:500],
                    )
                )
            result = self._decode_response(response.getheader("Content-Type", ""), payload)
            if message.get("method") == "initialize":
                version = result.get("result", {}).get("protocolVersion")
                if isinstance(version, str):
                    self.protocol_version = version
            return result
        except (OSError, http.client.HTTPException) as error:
            raise ProxyError("Cannot reach the Sigil MCP endpoint: {0}".format(error)) from error
        finally:
            connection.close()

    def close(self):
        if not self.session_id:
            return
        headers = {
            "Authorization": "Bearer " + self.metadata["token"],
            "Mcp-Session-Id": self.session_id,
        }
        if self.protocol_version:
            headers["MCP-Protocol-Version"] = self.protocol_version
        connection = self._connection()
        try:
            connection.request("DELETE", self.endpoint.path, headers=headers)
            response = connection.getresponse()
            response.read()
        except (OSError, http.client.HTTPException):
            pass
        finally:
            connection.close()
            self.session_id = None

    def _connection(self):
        return http.client.HTTPConnection(
            self.endpoint.hostname, self.endpoint.port, timeout=self.timeout
        )

    @staticmethod
    def _decode_response(content_type, payload):
        text = payload.decode("utf-8")
        if content_type.lower().startswith("text/event-stream"):
            messages = [line[5:].strip() for line in text.splitlines() if line.startswith("data:")]
            if not messages:
                raise ProxyError("Sigil MCP endpoint returned an empty SSE response")
            text = messages[-1]
        try:
            value = json.loads(text)
        except ValueError as error:
            raise ProxyError("Sigil MCP endpoint returned invalid JSON") from error
        if not isinstance(value, dict):
            raise ProxyError("Sigil MCP endpoint returned a non-object message")
        return value


def _write_message(message):
    encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write(encoded + "\n")
    sys.stdout.flush()


def _error_response(message, error):
    if "id" not in message:
        print("Sigil MCP proxy: {0}".format(error), file=sys.stderr, flush=True)
        return None
    return {
        "jsonrpc": "2.0",
        "id": message.get("id"),
        "error": {"code": -32000, "message": str(error)},
    }


def proxy_stdio(relay):
    try:
        for raw_line in sys.stdin.buffer:
            if len(raw_line) > MAX_MESSAGE_SIZE:
                _write_message({
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32600, "message": "MCP stdio message is too large"},
                })
                continue
            try:
                message = json.loads(raw_line.decode("utf-8"))
                if not isinstance(message, dict):
                    raise ValueError("message is not an object")
            except (UnicodeDecodeError, ValueError) as error:
                _write_message({
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": "Invalid MCP JSON: {0}".format(error)},
                })
                continue
            try:
                response = relay.send(message)
            except ProxyError as error:
                response = _error_response(message, error)
            if response is not None:
                _write_message(response)
    finally:
        relay.close()


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Bridge MCP stdio to one running Sigil Enhanced Book session."
    )
    parser.add_argument("--metadata", help="exact sigil-mcp-*.json file")
    parser.add_argument("--runtime-dir", help="directory containing endpoint metadata")
    parser.add_argument("--session-id", help="select one Book session by ID")
    parser.add_argument("--timeout", type=float, default=30, help="HTTP request timeout")
    args = parser.parse_args(argv)
    try:
        path, metadata = discover_metadata(
            metadata_path=args.metadata,
            runtime_directory=args.runtime_dir,
            session_id=args.session_id,
        )
        relay = McpHttpRelay(metadata, timeout=max(1, min(args.timeout, 300)))
    except ProxyError as error:
        print("Sigil MCP proxy: {0}".format(error), file=sys.stderr)
        return 2
    book_path = metadata.get("book", {}).get("file_path") or "untitled Book"
    print(
        "Sigil MCP proxy: using {0} ({1})".format(path, book_path),
        file=sys.stderr,
        flush=True,
    )
    proxy_stdio(relay)
    return 0


if __name__ == "__main__":
    sys.exit(main())
