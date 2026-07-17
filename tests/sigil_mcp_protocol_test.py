import asyncio
import contextlib
import hashlib
import json
import pathlib
import socket
import sys
import tempfile
import unittest
from unittest import mock

from sigil_mcp_test_support import FakePlugin, ROOT

try:
    import httpx
    import uvicorn
    from mcp import ClientSession
    from mcp.client.streamable_http import streamable_http_client
    from sigil_mcp.catalog import PROMPT_NAMES, TOOL_NAMES
    from sigil_mcp.backend import SigilMcpBackend
    from sigil_mcp.server import BearerTokenMiddleware, LiveCallGate, create_mcp
    from sigil_mcp.server import create_http_app
    from sigil_mcp_upload import (
        UploadError,
        _manifest_specs,
        main as upload_main,
        normalize_spec,
        upload_file,
    )
except ImportError as error:
    raise unittest.SkipTest("bundled MCP SDK is unavailable: {0}".format(error))


class SigilMcpCatalogTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.gate = LiveCallGate()
        self.backend = SigilMcpBackend(FakePlugin())
        self.mcp = create_mcp(self.backend, self.gate, 34567)

    async def asyncTearDown(self):
        self.gate.close()

    async def test_tools_are_deterministic_and_revision_schemas_are_required(self):
        tools = await self.mcp.list_tools()
        self.assertEqual(tuple(tool.name for tool in tools), TOOL_NAMES)
        by_name = {tool.name: tool for tool in tools}
        editor_schema = by_name["sigil.editor.edit"].inputSchema
        self.assertIn("expected_revision", editor_schema["required"])
        self.assertIn("resource_id", editor_schema["required"])
        commit = by_name["sigil.transaction.commit"]
        self.assertTrue(commit.annotations.destructiveHint)
        self.assertFalse(commit.annotations.readOnlyHint)
        self.assertTrue(by_name["sigil.book.info"].annotations.readOnlyHint)
        binary_schema = by_name["sigil.transaction.add_binary_resource"].inputSchema
        self.assertIn("data_base64", binary_schema["required"])
        self.assertIn("media_type", binary_schema["required"])

    async def test_resources_and_prompts_match_the_public_catalog(self):
        resources = await self.mcp.list_resources()
        templates = await self.mcp.list_resource_templates()
        prompts = await self.mcp.list_prompts()
        self.assertEqual(
            {str(resource.uri) for resource in resources},
            {
                "sigil://book/info",
                "sigil://book/metadata",
                "sigil://book/manifest",
                "sigil://book/spine",
                "sigil://editor/state",
            },
        )
        self.assertEqual(
            [template.uriTemplate for template in templates],
            ["sigil://resource/{resource_id}"],
        )
        self.assertEqual(tuple(prompt.name for prompt in prompts), PROMPT_NAMES)

    async def test_batch_upload_manifest_uses_relative_sources_and_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "chapter.xhtml").write_text("<p>chapter</p>", encoding="utf-8")
            manifest = root / "imports.json"
            manifest.write_text(json.dumps({
                "transaction_id": "transaction-1",
                "resources": [{
                    "source": "chapter.xhtml",
                    "book_path": "Text/chapter.xhtml",
                    "manifest_id": "chapter",
                }],
            }), encoding="utf-8")
            specs = _manifest_specs(manifest)
        self.assertEqual(specs[0]["source"], root / "chapter.xhtml")
        self.assertEqual(specs[0]["media_type"], "application/xhtml+xml")
        self.assertEqual(specs[0]["kind"], "text")
        self.assertTrue(specs[0]["add_to_spine"])

    async def test_batch_upload_manifest_rejects_duplicate_targets_before_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("one.css", "two.css"):
                (root / name).write_text("body {}", encoding="utf-8")
            base_resources = [
                {
                    "source": "one.css",
                    "book_path": "Styles/one.css",
                    "manifest_id": "one",
                },
                {
                    "source": "two.css",
                    "book_path": "Styles/two.css",
                    "manifest_id": "two",
                },
            ]
            cases = {
                "book_path": {"book_path": "Styles/one.css"},
                "manifest_id": {"manifest_id": "one"},
                "transaction_id": {"transaction_id": "transaction-2"},
            }
            for label, change in cases.items():
                resources = [dict(item) for item in base_resources]
                resources[1].update(change)
                manifest = root / (label + ".json")
                manifest.write_text(json.dumps({
                    "transaction_id": "transaction-1",
                    "resources": resources,
                }), encoding="utf-8")
                with self.subTest(label=label), self.assertRaises(UploadError):
                    _manifest_specs(manifest)

    async def test_batch_upload_manifest_rejects_duplicate_replace_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("one.css", "two.css"):
                (root / name).write_text("body {}", encoding="utf-8")
            manifest = root / "replace.json"
            manifest.write_text(json.dumps({
                "transaction_id": "transaction-1",
                "resources": [
                    {
                        "source": "one.css",
                        "operation": "replace",
                        "resource_id": "resource-1",
                        "expected_revision": 1,
                    },
                    {
                        "source": "two.css",
                        "operation": "replace",
                        "resource_id": "resource-1",
                        "expected_revision": 1,
                    },
                ],
            }), encoding="utf-8")
            with self.assertRaises(UploadError):
                _manifest_specs(manifest)

    async def test_batch_uploader_reports_a_resumable_failure_index(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("one.css", "two.css"):
                (root / name).write_text("body {}", encoding="utf-8")
            manifest = root / "imports.json"
            manifest.write_text(json.dumps({
                "transaction_id": "transaction-1",
                "resources": [
                    {
                        "source": "one.css",
                        "book_path": "Styles/one.css",
                        "manifest_id": "one",
                    },
                    {
                        "source": "two.css",
                        "book_path": "Styles/two.css",
                        "manifest_id": "two",
                    },
                ],
            }), encoding="utf-8")
            with (
                mock.patch(
                    "sigil_mcp_upload.discover_metadata",
                    return_value=(root / "session.json", {"token": "secret"}),
                ),
                mock.patch(
                    "sigil_mcp_upload.upload_file",
                    side_effect=[{"staged": True}, UploadError("network lost")],
                ),
                mock.patch("sys.stderr") as stderr,
            ):
                result = upload_main(["--manifest", str(manifest)])
        self.assertEqual(result, 2)
        failure = json.loads(stderr.write.call_args_list[0].args[0])
        self.assertEqual(failure["completed_indices"], [0])
        self.assertEqual(failure["next_index"], 1)


class SigilMcpHttpIntegrationTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.plugin = FakePlugin()
        self.backend = SigilMcpBackend(self.plugin)
        self.gate = LiveCallGate()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(socket.SOMAXCONN)
        self.port = self.listener.getsockname()[1]
        self.endpoint = "http://127.0.0.1:{0}/mcp".format(self.port)
        self.token = "integration-secret"
        mcp = create_mcp(self.backend, self.gate, self.port)
        app = create_http_app(mcp, self.backend, self.gate, self.token)
        self.server = uvicorn.Server(uvicorn.Config(
            app,
            host="127.0.0.1",
            port=self.port,
            log_level="error",
            access_log=False,
        ))
        self.server_task = asyncio.create_task(
            self.server.serve(sockets=[self.listener])
        )
        for _ in range(200):
            if self.server.started:
                break
            if self.server_task.done():
                await self.server_task
            await asyncio.sleep(0.01)
        self.assertTrue(self.server.started)

    async def asyncTearDown(self):
        self.server.should_exit = True
        with contextlib.suppress(Exception):
            await asyncio.wait_for(self.server_task, timeout=5)
        with contextlib.suppress(OSError):
            self.listener.close()
        self.gate.close()

    def initialize_payload(self):
        return {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "security-test", "version": "1"},
            },
        }

    async def test_bearer_host_and_origin_checks_reject_untrusted_requests(self):
        headers = {
            "Accept": "application/json, text/event-stream",
            "Content-Type": "application/json",
        }
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.post(
                self.endpoint, headers=headers, json=self.initialize_payload()
            )
            self.assertEqual(response.status_code, 401)

            authorized = dict(headers, Authorization="bearer " + self.token)
            response = await client.post(
                self.endpoint,
                headers=dict(authorized, Origin="https://attacker.example"),
                json=self.initialize_payload(),
            )
            self.assertEqual(response.status_code, 403)

            response = await client.post(
                self.endpoint,
                headers=dict(authorized, Host="attacker.example"),
                json=self.initialize_payload(),
            )
            self.assertEqual(response.status_code, 421)

    async def test_official_client_initializes_lists_and_calls_tool(self):
        async with httpx.AsyncClient(
            headers={"Authorization": "Bearer " + self.token}, timeout=5
        ) as http_client:
            async with streamable_http_client(
                self.endpoint, http_client=http_client
            ) as (read_stream, write_stream, _):
                async with ClientSession(read_stream, write_stream) as session:
                    initialized = await session.initialize()
                    self.assertEqual(initialized.protocolVersion, "2025-11-25")
                    tools = await session.list_tools()
                    self.assertEqual(tuple(tool.name for tool in tools.tools), TOOL_NAMES)
                    resources = await session.list_resources()
                    self.assertIn(
                        "sigil://book/info",
                        {str(resource.uri) for resource in resources.resources},
                    )
                    prompts = await session.list_prompts()
                    self.assertEqual(
                        tuple(prompt.name for prompt in prompts.prompts), PROMPT_NAMES
                    )
                    result = await session.call_tool("sigil.book.info", {})
                    self.assertFalse(result.isError)
                    structured = result.structuredContent
                    self.assertEqual(structured["revision"], 9)

                    resource = await session.read_resource(
                        "sigil://resource/chapter"
                    )
                    self.assertEqual(
                        resource.contents[0].text, "<p>Current editor text</p>"
                    )
                    prompt = await session.get_prompt(
                        "layout_epub", {"goal": "readable typography"}
                    )
                    self.assertIn("readable typography", prompt.messages[0].content.text)

                    started = await session.call_tool(
                        "sigil.transaction.begin", {"label": "Integration"}
                    )
                    transaction_id = started.structuredContent["transaction_id"]
                    staged = await session.call_tool(
                        "sigil.transaction.replace_text",
                        {
                            "transaction_id": transaction_id,
                            "resource_id": "chapter",
                            "expected_revision": 3,
                            "text": "updated",
                        },
                    )
                    self.assertEqual(staged.structuredContent["operation"], "replace_text")
                    preview = await session.call_tool(
                        "sigil.transaction.preview",
                        {"transaction_id": transaction_id},
                    )
                    self.assertTrue(preview.structuredContent["valid"])
                    rolled_back = await session.call_tool(
                        "sigil.transaction.rollback",
                        {"transaction_id": transaction_id},
                    )
                    self.assertTrue(rolled_back.structuredContent["rolled_back"])
                    stale = await session.call_tool(
                        "sigil.transaction.preview",
                        {"transaction_id": transaction_id},
                    )
                    self.assertTrue(stale.isError)
                    error_text = stale.content[0].text
                    self.assertIn("TransactionNotFound", error_text)
                    error = json.loads(error_text[error_text.index("{"):])
                    self.assertEqual(error["code"], "TransactionNotFound")

    async def test_external_import_accepts_raw_bytes_without_base64(self):
        transaction_id = self.backend.transaction_begin("External import")["transaction_id"]
        content = b"\xff\xd8raw-image-bytes\xff\xd9"
        query = {
            "transaction_id": transaction_id,
            "kind": "binary",
            "operation": "add",
            "book_path": "Images/cover.jpg",
            "media_type": "image/jpeg",
            "manifest_id": "cover_image",
            "add_to_spine": "false",
        }
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.post(
                "http://127.0.0.1:{0}/api/v1/imports".format(self.port),
                params=query,
                headers={
                    "Authorization": "Bearer " + self.token,
                    "X-Content-SHA256": hashlib.sha256(content).hexdigest(),
                },
                content=content,
            )
        self.assertEqual(response.status_code, 201, response.text)
        result = response.json()
        self.assertEqual(result["external_import"]["size"], len(content))
        call = self.plugin.book.transactions[0].calls[-1]
        self.assertEqual(call[0], "add_resource")
        self.assertEqual(call[1][1], content)

    async def test_external_import_rejects_bad_hash_auth_and_host(self):
        transaction_id = self.backend.transaction_begin("Rejected imports")["transaction_id"]
        endpoint = "http://127.0.0.1:{0}/api/v1/imports".format(self.port)
        params = {
            "transaction_id": transaction_id,
            "kind": "binary",
            "book_path": "Images/a.jpg",
            "media_type": "image/jpeg",
        }
        headers = {"X-Content-SHA256": "0" * 64}
        async with httpx.AsyncClient(timeout=5) as client:
            unauthorized = await client.post(endpoint, params=params, headers=headers, content=b"x")
            self.assertEqual(unauthorized.status_code, 401)
            authorized = dict(headers, Authorization="Bearer " + self.token)
            bad_hash = await client.post(endpoint, params=params, headers=authorized, content=b"x")
            self.assertEqual(bad_hash.status_code, 400)
            hostile = await client.post(
                endpoint,
                params=params,
                headers=dict(authorized, Host="attacker.example"),
                content=b"x",
            )
            self.assertEqual(hostile.status_code, 421)
        self.assertEqual(self.plugin.book.transactions[0].calls, [])

    async def test_external_uploader_streams_one_local_file(self):
        transaction_id = self.backend.transaction_begin("Uploader")["transaction_id"]
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "style.css"
            source.write_text("body { color: #222; }", encoding="utf-8")
            spec = normalize_spec(
                {
                    "source": str(source),
                    "transaction_id": transaction_id,
                    "book_path": "Styles/external.css",
                    "manifest_id": "external_css",
                },
                pathlib.Path(directory),
            )
            metadata = {
                "endpoint": self.endpoint,
                "external_import_endpoint": "http://127.0.0.1:{0}/api/v1/imports".format(self.port),
                "token": self.token,
            }
            result = await asyncio.to_thread(upload_file, metadata, spec, 5)
        self.assertEqual(result["operation"], "add_resource")
        call = self.plugin.book.transactions[0].calls[-1]
        self.assertEqual(call[1][1], "body { color: #222; }")

    async def test_stdio_proxy_relays_protocol_without_book_logic(self):
        proxy = (
            ROOT
            / "src"
            / "Resource_Files"
            / "plugin_launchers"
            / "python"
            / "sigil_mcp_stdio_proxy.py"
        )
        with tempfile.TemporaryDirectory() as directory:
            metadata = pathlib.Path(directory) / "sigil-mcp-test.json"
            metadata.write_text(json.dumps({
                "endpoint": self.endpoint,
                "token": self.token,
                "session_id": "proxy-session",
                "transport": "streamable-http",
                "book": {"file_path": "/books/example.epub"},
            }), encoding="utf-8")
            process = await asyncio.create_subprocess_exec(
                sys.executable,
                str(proxy),
                "--metadata",
                str(metadata),
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )

            async def request(message):
                process.stdin.write(
                    (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
                )
                await process.stdin.drain()
                response = await asyncio.wait_for(process.stdout.readline(), timeout=5)
                self.assertTrue(response)
                return json.loads(response)

            initialized = await request(self.initialize_payload())
            self.assertEqual(initialized["result"]["protocolVersion"], "2025-11-25")
            process.stdin.write(
                b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n'
            )
            await process.stdin.drain()
            listed = await request({
                "jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {},
            })
            self.assertEqual(
                tuple(tool["name"] for tool in listed["result"]["tools"]), TOOL_NAMES
            )
            called = await request({
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "sigil.book.info", "arguments": {}},
            })
            self.assertEqual(called["result"]["structuredContent"]["revision"], 9)
            process.stdin.close()
            await process.stdin.wait_closed()
            self.assertEqual(await asyncio.wait_for(process.wait(), timeout=5), 0)
            await asyncio.sleep(0.1)
            stderr = (await process.stderr.read()).decode("utf-8")
            self.assertIn("/books/example.epub", stderr)
            self.assertNotIn(self.token, stderr)


if __name__ == "__main__":
    unittest.main()
