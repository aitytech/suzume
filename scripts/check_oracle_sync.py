#!/usr/bin/env python3
"""Fail when golden expectations drift from the fresh oracle pipeline."""

from __future__ import annotations

import asyncio
import json

import suzume_mcp.server  # noqa: F401 - initialize the registered tool graph first
from suzume_mcp.tools._test_tools_read import test_needs_suzume_update


def main() -> None:
    payload = json.loads(asyncio.run(test_needs_suzume_update(apply=False)))
    if payload.get("error"):
        raise RuntimeError(payload["error"])
    errors = payload.get("errors", [])
    total = payload.get("total", 0)
    if errors or total:
        raise SystemExit(
            "oracle sync failed: "
            f"{total} expectation update(s), {len(errors)} normalization error(s)\n"
            f"{json.dumps(payload, ensure_ascii=False, indent=2)}"
        )
    print("Oracle sync passed: 0 expectation updates, 0 normalization errors")


if __name__ == "__main__":
    main()
