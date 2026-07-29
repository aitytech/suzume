"""Server bootstrap coverage for complete MCP tool registration."""

from __future__ import annotations

import asyncio
from pathlib import Path

from suzume_mcp import config, server


def test_project_root_is_repository_root() -> None:
    assert (config.PROJECT_ROOT / "CMakeLists.txt").is_file()
    assert server.PROJECT_ROOT == config.PROJECT_ROOT


def test_every_decorated_tool_module_is_loaded() -> None:
    registered = {tool.name for tool in asyncio.run(server.mcp.list_tools())}
    tool_dir = Path(server.__file__).with_name("tools")
    for module_path in tool_dir.glob("*.py"):
        source = module_path.read_text(encoding="utf-8")
        if "@mcp.tool()" not in source:
            continue
        assert module_path.stem in __import__("sys").modules[f"suzume_mcp.tools.{module_path.stem}"].__name__
    assert {"test_show", "dict_add", "defect_list", "thread_scan"} <= registered
