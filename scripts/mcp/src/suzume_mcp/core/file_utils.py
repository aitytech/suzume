"""Shared helpers for durable text-file updates."""

import os
import tempfile
from collections.abc import Iterable
from pathlib import Path


def atomic_write_text(path: Path, content: str) -> None:
    """Replace *path* atomically with UTF-8 *content*."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as tmp:
        tmp.write(content)
        tmp_path = Path(tmp.name)
    os.replace(tmp_path, path)


def append_lines_atomic(path: Path, lines: Iterable[str]) -> None:
    """Append complete lines while keeping the replacement atomic."""
    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    if existing and not existing.endswith("\n"):
        existing += "\n"
    atomic_write_text(path, existing + "".join(f"{line}\n" for line in lines))
