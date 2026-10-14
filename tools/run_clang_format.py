#!/usr/bin/env python3
"""Verifica (padrão) ou corrige (--fix) a formatação dos fontes C++ com o clang-format."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

EXTENSIONS = {".cpp", ".hpp", ".h", ".cc"}


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # acentos legíveis também no console do Windows
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("dirs", nargs="+", type=Path)
    args = parser.parse_args()

    files = sorted(p for d in args.dirs if d.exists() for p in d.rglob("*") if p.suffix in EXTENSIONS)
    if not files:
        return 0
    mode = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    result = subprocess.run([args.clang_format, *mode, *map(str, files)], check=False)
    if result.returncode != 0 and not args.fix:
        print("Formatação fora do padrão. Rode: cmake --build --preset debug --target format", file=sys.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
