import asyncio
import contextlib
import json
import socket
import unittest

from sigil_mcp_test_support import FakePlugin

try:
    import httpx
    import uvicorn
    from mcp import ClientSession
    from mcp.client.streamable_http import streamable_http_client
    from sigil_mcp.catalog import PROMPT_NAMES, TOOL_NAMES
    from sigil_mcp.backend import SigilMcpBackend
    from sigil_mcp.server import BearerTokenMiddleware, LiveCallGate, create_mcp
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
        app = mcp.streamable_http_app()
        app.add_middleware(BearerTokenMiddleware, token=self.token)
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


if __name__ == "__main__":
    unittest.main()
