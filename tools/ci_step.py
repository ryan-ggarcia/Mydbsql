#!/usr/bin/env python3
"""Roda um passo da CI e, se ele falhar, publica as linhas de erro como anotação do GitHub Actions.

Por que existe: os logs dos jobs só podem ser baixados com login, mas as anotações são públicas.
Assim, quem não tem acesso ao token (ou um agente sem credenciais) consegue diagnosticar a falha.

Uso: python tools/ci_step.py -- cmake --preset debug
"""

from __future__ import annotations

import re
import subprocess
import sys

ERROR_PATTERN = re.compile(r"error|FAILED|fatal|undefined reference|not found|No such file|Traceback", re.IGNORECASE)
MAX_ERROR_LINES = 40
MAX_TAIL_LINES = 40


def escape(text: str) -> str:
    # Codificação exigida pelos comandos de workflow (::error::).
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    args = sys.argv[1:]
    if args and args[0] == "--":
        args = args[1:]
    if not args:
        print("uso: ci_step.py -- <comando> [args...]", file=sys.stderr)
        return 2

    lines: list[str] = []
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace", bufsize=1) as process:
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            lines.append(line.rstrip("\n"))
        code = process.wait()

    if code != 0:
        errors = [line for line in lines if ERROR_PATTERN.search(line)][:MAX_ERROR_LINES]
        tail = lines[-MAX_TAIL_LINES:]
        message = "\n".join([*errors, "----- fim da saída -----", *tail])
        sys.stdout.flush()
        title = escape(f"Falhou {' '.join(args)}"[:200]).replace(":", "%3A").replace(",", "%2C")
        print(f"::error title={title}::{escape(message)}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
