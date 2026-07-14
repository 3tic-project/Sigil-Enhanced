import json
import os
import socket
import struct
import time


DEFAULT_MAX_MESSAGE_SIZE = 8 * 1024 * 1024


class _UnixSocket:
    def __init__(self, name, timeout):
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._socket.settimeout(timeout)
        self._socket.connect(name)

    def read(self, size):
        return self._socket.recv(size)

    def write(self, data):
        self._socket.sendall(data)

    def close(self):
        self._socket.close()


class _WindowsPipe:
    ERROR_PIPE_BUSY = 231
    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    INVALID_HANDLE_VALUE = -1

    def __init__(self, name, timeout):
        import ctypes
        from ctypes import wintypes

        self._ctypes = ctypes
        self._wintypes = wintypes
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        self._kernel32.CreateFileW.restype = wintypes.HANDLE
        self._kernel32.ReadFile.argtypes = [
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.LPDWORD,
            wintypes.LPVOID,
        ]
        self._kernel32.ReadFile.restype = wintypes.BOOL
        self._kernel32.WriteFile.argtypes = self._kernel32.ReadFile.argtypes
        self._kernel32.WriteFile.restype = wintypes.BOOL
        self._kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        self._kernel32.CloseHandle.restype = wintypes.BOOL
        self._kernel32.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        self._kernel32.WaitNamedPipeW.restype = wintypes.BOOL
        self._path = name if name.startswith("\\\\.\\pipe\\") else "\\\\.\\pipe\\" + name
        self._timeout_ms = max(1, int(timeout * 1000))
        self._handle = None
        self._connect()

    def _connect(self):
        deadline = time.monotonic() + self._timeout_ms / 1000.0
        while True:
            handle = self._kernel32.CreateFileW(
                self._path,
                self.GENERIC_READ | self.GENERIC_WRITE,
                0,
                None,
                self.OPEN_EXISTING,
                0,
                None,
            )
            if handle != self._wintypes.HANDLE(self.INVALID_HANDLE_VALUE).value:
                self._handle = handle
                return
            error = self._ctypes.get_last_error()
            if error != self.ERROR_PIPE_BUSY or time.monotonic() >= deadline:
                raise OSError(error, "could not connect to Sigil plugin pipe")
            self._kernel32.WaitNamedPipeW(self._path, min(100, self._timeout_ms))

    def read(self, size):
        buffer = self._ctypes.create_string_buffer(size)
        read = self._wintypes.DWORD()
        if not self._kernel32.ReadFile(self._handle, buffer, size, self._ctypes.byref(read), None):
            raise self._ctypes.WinError(self._ctypes.get_last_error())
        return buffer.raw[: read.value]

    def write(self, data):
        offset = 0
        while offset < len(data):
            chunk = self._ctypes.create_string_buffer(data[offset:])
            written = self._wintypes.DWORD()
            if not self._kernel32.WriteFile(
                self._handle, chunk, len(data) - offset, self._ctypes.byref(written), None
            ):
                raise self._ctypes.WinError(self._ctypes.get_last_error())
            if written.value == 0:
                raise OSError("short write to Sigil plugin pipe")
            offset += written.value

    def close(self):
        if self._handle is not None:
            self._kernel32.CloseHandle(self._handle)
            self._handle = None


class LocalSocketTransport:
    def __init__(self, socket_name, timeout_ms=10000, max_message_size=DEFAULT_MAX_MESSAGE_SIZE):
        self.socket_name = socket_name
        self.timeout_ms = timeout_ms
        self.max_message_size = max_message_size
        self._connection = None

    def connect(self):
        timeout = self.timeout_ms / 1000.0
        self._connection = (
            _WindowsPipe(self.socket_name, timeout)
            if os.name == "nt"
            else _UnixSocket(self.socket_name, timeout)
        )

    def close(self):
        if self._connection is not None:
            self._connection.close()
            self._connection = None

    def send(self, message):
        if self._connection is None:
            raise ConnectionError("plugin transport is not connected")
        payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if not payload or len(payload) > self.max_message_size:
            raise ValueError("plugin message exceeds the configured limit")
        self._connection.write(struct.pack(">I", len(payload)) + payload)

    def receive(self):
        header = self._read_exact(4)
        size = struct.unpack(">I", header)[0]
        if size == 0 or size > self.max_message_size:
            raise ValueError("invalid plugin message length")
        payload = self._read_exact(size)
        message = json.loads(payload.decode("utf-8"))
        if not isinstance(message, dict):
            raise ValueError("plugin message must be a JSON object")
        return message

    def _read_exact(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self._connection.read(size - len(data))
            if not chunk:
                raise ConnectionError("plugin connection closed")
            data.extend(chunk)
        return bytes(data)
