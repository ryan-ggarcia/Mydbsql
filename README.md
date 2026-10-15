# Mydbsql

Cliente gráfico de bancos de dados (MySQL/MariaDB, PostgreSQL, SQLite e, depois, SQL Server e Firebird), inspirado no HeidiSQL, escrito do zero em C++20. Leve, rápido, fácil de estender e nativo em Linux, Windows e macOS.

> **Estado:** Fase 0 (fundação). Ainda não conecta a bancos. O plano completo está em [CLAUDE.md](CLAUDE.md) e o histórico de trabalho em [handoff.md](handoff.md).

## Pré-requisitos

| Ferramenta | Versão |
|---|---|
| CMake | ≥ 3.25 |
| Ninja | qualquer recente |
| Compilador | GCC ≥ 12, Clang ≥ 15, MSVC 2022 (v143) ou 2026 (v145), Apple Clang (Xcode 15+) |
| vcpkg | qualquer; a baseline está em `vcpkg-configuration.json` |
| Qt | 6.11.3, binários oficiais ([ADR-0006](docs/adr/0006-qt-binarios-oficiais.md)) |
| Python | ≥ 3.9 (scripts de `tools/`) |

Instale o Qt com o script do projeto (exemplo para Windows x64):

```bash
python -m pip install -r tools/requirements-dev.txt
python tools/fetch_qt.py --version 6.11.3 --host windows_x86 --arch win64_msvc2022_64 --out ~/tools/Qt
# Linux: --host linux_x64 --arch linux_gcc_64 · macOS: --host mac_x64 --arch clang_64
```

Defina `VCPKG_ROOT` (raiz do vcpkg) e `QT_ROOT_DIR` (a pasta impressa pelo script), ou crie um `CMakeUserPresets.json` local que herde dos presets e defina essas variáveis em `environment`.

## Compilar e testar

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug              # todos; -L unit, -L lint ou -L ui para filtrar
```

No Windows, rode os comandos num "x64 Native Tools Command Prompt" ou pelo atalho `tools\vsdev.cmd`, que carrega o ambiente do MSVC:

```bat
tools\vsdev.cmd cmake --preset debug
```

Outros presets: `release` (LTO, `-O2`, símbolos ocultos), `asan` (AddressSanitizer + UBSan) e `tsan` (só Linux e macOS).

## Medir a inicialização

```bash
build/release/bin/mydbsql --measure-startup            # JSON na saída padrão
build/release/bin/mydbsql --measure-startup=run1.json  # JSON em arquivo
python tools/check_budget.py --startup run1.json run2.json run3.json \
    --budget bench/budget.json --baseline bench/baseline/linux.json
```

O orçamento de memória e tempo está na CLAUDE.md §2.1 e é conferido na CI a cada PR.

## Licença

Reimplementação limpa (clean-room). Nenhum código, ícone ou texto do HeidiSQL nem do MySQL Workbench é copiado (CLAUDE.md §0).
