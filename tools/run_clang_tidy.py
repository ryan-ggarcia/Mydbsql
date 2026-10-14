#!/usr/bin/env python3
"""Roda o clang-tidy em paralelo sobre os arquivos de src/ listados no compile_commands.json.

Ignora fontes gerados (moc, autogen) e de dependências. Sai com código != 0 se houver diagnóstico.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # acentos legíveis também no console do Windows
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 2)))
    parser.add_argument("files", nargs="*", help="restringe a estes arquivos (padrão: todo src/)")
    args = parser.parse_args()

    database = args.build_dir / "compile_commands.json"
    if not database.exists():
        print(f"{database} não existe; configure com CMAKE_EXPORT_COMPILE_COMMANDS=ON", file=sys.stderr)
        return 2

    src_root = (args.source_dir / "src").resolve()
    only = {Path(f).resolve() for f in args.files}
    files: list[Path] = []
    for entry in json.loads(database.read_text(encoding="utf-8")):
        path = Path(entry["directory"], entry["file"]).resolve()
        if src_root not in path.parents or "autogen" in path.parts:
            continue
        if only and path not in only:
            continue
        files.append(path)
    files = sorted(set(files))

    def run(path: Path) -> tuple[Path, int, str]:
        result = subprocess.run(
            [args.clang_tidy, "-p", str(args.build_dir), "--quiet", str(path)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        return path, result.returncode, result.stdout + result.stderr

    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, code, output in pool.map(run, files):
            lines = [line for line in output.splitlines()
                     if line.strip() and "warnings generated" not in line and "Suppressed" not in line
                     and "Use -header-filter" not in line and "Use -system-headers" not in line]
            if code != 0 or any(": warning:" in line or ": error:" in line for line in lines):
                failures += 1
                print("\n".join(lines))
    print(f"clang-tidy: {len(files)} arquivos, {failures} com problemas")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
