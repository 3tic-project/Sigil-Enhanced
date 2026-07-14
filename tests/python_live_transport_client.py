import pathlib
import sys


sys.path.insert(0, sys.argv[2])

from sigil_live.transport import LocalSocketTransport


transport = LocalSocketTransport(sys.argv[1])
transport.connect()
request = transport.receive()
if request.get("method") != "session.ping" or request.get("params", {}).get("text") != "line 1\nline 2":
    raise RuntimeError("unexpected transport integration request")
transport.send({"jsonrpc": "2.0", "id": request["id"], "result": {"pong": True}})
transport.close()
