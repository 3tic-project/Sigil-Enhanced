from .errors import error_from_response


class RpcClient:
    def __init__(self, transport):
        self.transport = transport
        self._next_id = 1
        self.notifications = []

    def call(self, method, params=None):
        request_id = self._next_id
        self._next_id += 1
        request = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            request["params"] = params
        self.transport.send(request)
        while True:
            response = self.transport.receive()
            if "id" not in response:
                self.notifications.append(response)
                continue
            if response.get("id") != request_id:
                raise RuntimeError("received an unexpected JSON-RPC response id")
            if "error" in response:
                raise error_from_response(response["error"])
            if "result" not in response:
                raise RuntimeError("JSON-RPC response has no result")
            return response["result"]
