def run(plugin):
    try:
        from sigil_mcp.server import run_server
    except ImportError as error:
        plugin.ui.show_message(
            "The bundled MCP runtime is unavailable: {0}".format(error),
            "Sigil MCP Server",
            level="error",
        )
        return 1
    return run_server(plugin)
