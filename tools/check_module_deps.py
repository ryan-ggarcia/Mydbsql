#!/usr/bin/env python3
"""Falha se um módulo de src/modules/ incluir headers de outro módulo ou de camadas que não são core/ e sdk/.

CLAUDE.md §2.4: um módulo depende só de core/ e sdk/; a comunicação entre módulos é pelo EventBus.
O CMake já impede o link (mydb_check_module_dependencies); este script impede o #include.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
ALLOWED_PREFIXES = ("core/", "sdk/")
EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".inl"}


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()

    modules_dir = args.root.resolve() / "src" / "modules"
    violations: list[str] = []
    for module in sorted(p for p in modules_dir.iterdir() if p.is_dir()) if modules_dir.is_dir() else []:
        own_prefix = f"modules/{module.name}/"
        for path in sorted(module.rglob("*")):
            if path.suffix not in EXTENSIONS:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                match = INCLUDE.match(line)
                if not match:
                    continue
                target = match.group(1)
                local = "/" not in target  # header da própria pasta
                if local or target.startswith(ALLOWED_PREFIXES) or target.startswith(own_prefix):
                    continue
                relative = path.relative_to(args.root.resolve()).as_posix()
                violations.append(f"{relative}:{number}: {line.strip()}")

    if violations:
        print("Módulos só podem incluir core/, sdk/ e os próprios headers:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("ok: módulos só incluem core/, sdk/ e os próprios headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
