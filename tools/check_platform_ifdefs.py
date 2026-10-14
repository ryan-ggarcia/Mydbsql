#!/usr/bin/env python3
"""Falha se houver #if/#ifdef de sistema operacional fora de src/infra/platform/ (CLAUDE.md §2.6, regra 1).

Diferenças entre SOs vão atrás da interface IPlatform; a escolha do arquivo de cada SO é feita no CMake.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

OS_MACROS = r"(_WIN32|_WIN64|WIN32|_WINDOWS|__linux__|__linux|__gnu_linux__|__APPLE__|__MACH__|__unix__|__unix|__FreeBSD__|__OpenBSD__|__NetBSD__|__CYGWIN__|__MINGW32__|__MINGW64__|Q_OS_[A-Z0-9_]+)"
DIRECTIVE = re.compile(rf"^\s*#\s*(if|ifdef|ifndef|elif|elifdef|elifndef)\b.*\b{OS_MACROS}\b")
ALLOWED = Path("src") / "infra" / "platform"
EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".inl"}


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # acentos legíveis também no console do Windows
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()

    root = args.root.resolve()
    violations: list[str] = []
    for top in ("src", "tests", "bench"):
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in EXTENSIONS:
                continue
            relative = path.relative_to(root)
            if ALLOWED in relative.parents:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if DIRECTIVE.search(line):
                    violations.append(f"{relative.as_posix()}:{number}: {line.strip()}")

    if violations:
        print("#ifdef de SO fora de src/infra/platform/ (use IPlatform):", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("ok: nenhum #ifdef de SO fora de src/infra/platform/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
