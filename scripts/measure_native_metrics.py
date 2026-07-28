#!/usr/bin/env python3
"""Run reproducible native throughput checks, including long unbroken input."""

from __future__ import annotations

import argparse
import platform
import subprocess
import tempfile
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CLI = REPOSITORY_ROOT / "build" / "bin" / "suzume-cli"


def _run(cli: Path, *args: str) -> str:
    completed = subprocess.run(
        [str(cli), "test", "benchmark", *args],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.rstrip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--samples", type=int, default=5)
    args = parser.parse_args()

    print(f"Host: {platform.platform()}")
    print(f"Processor: {platform.processor() or 'unreported'}")
    print("\n[default corpus]")
    print(
        _run(
            args.cli,
            f"--iterations={args.iterations}",
            f"--samples={args.samples}",
            "--warmup=1",
        )
    )

    for length in (1600, 4000):
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".txt") as corpus:
            corpus.write("あ" * length)
            corpus.flush()
            print(f"\n[unbroken hiragana: {length} characters]")
            print(
                _run(
                    args.cli,
                    "--iterations=1",
                    "--samples=3",
                    "--warmup=0",
                    "--file",
                    corpus.name,
                )
            )


if __name__ == "__main__":
    main()
