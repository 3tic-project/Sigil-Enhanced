import json
import os
import pathlib
import re
import tempfile


SAFE_ID = re.compile(r"[^A-Za-z0-9_.-]")


def runtime_directory(session_info):
    configured = session_info.get("runtime_directory")
    base = pathlib.Path(configured) if configured else pathlib.Path(tempfile.gettempdir())
    directory = base / "mcp"
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        directory.chmod(0o700)
    except OSError:
        pass
    return directory


class RendezvousFile:
    def __init__(self, session_info):
        session_id = SAFE_ID.sub("_", str(session_info.get("session_id", "unknown")))
        self.path = runtime_directory(session_info) / ("sigil-mcp-{0}.json".format(session_id))
        self._owner_token = None

    def write(self, payload):
        data = dict(payload)
        token = data.get("token")
        if not isinstance(token, str) or not token:
            raise ValueError("rendezvous metadata requires a non-empty token")
        self._owner_token = token
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=self.path.name + ".", suffix=".tmp", dir=str(self.path.parent)
        )
        temporary = pathlib.Path(temporary_name)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(data, stream, ensure_ascii=False, indent=2, sort_keys=True)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            try:
                temporary.chmod(0o600)
            except OSError:
                pass
            os.replace(temporary, self.path)
            try:
                self.path.chmod(0o600)
            except OSError:
                pass
        finally:
            if temporary.exists():
                temporary.unlink()
        return self.path

    def remove(self):
        if self._owner_token is None:
            return False
        try:
            current = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, TypeError, ValueError):
            return False
        if current.get("token") != self._owner_token:
            return False
        try:
            self.path.unlink()
        except FileNotFoundError:
            return False
        return True
