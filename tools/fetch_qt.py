#!/usr/bin/env python3
"""Baixa os binários oficiais pré-compilados do Qt 6 (download.qt.io) e confere o SHA-1.

Por que existe: o aqtinstall 3.3.0 não entende o layout novo do repositório do Qt
(no Windows, `qt6_6113/qt6_6113_msvc2022_64/`), e compilar o Qt pelo vcpkg leva horas.
Veja docs/adr/0006-qt-binarios-oficiais.md.

Uso:
    python tools/fetch_qt.py --version 6.11.3 --host windows_x86 --arch win64_msvc2022_64 \
        --out C:/Users/<você>/tools/Qt
    # imprime o diretório de instalação (use como QT_ROOT_DIR / CMAKE_PREFIX_PATH)

Requer: py7zr (pip install py7zr).
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

BASE_URL = "https://download.qt.io/online/qtsdkrepository"

# Pasta final dentro de <out>/<versão>/, igual à do instalador oficial.
ARCH_DIRS = {
    "win64_msvc2022_64": "msvc2022_64",
    "win64_msvc2022_arm64_cross_compiled": "msvc2022_arm64",
    "linux_gcc_64": "gcc_64",
    "linux_gcc_arm64": "gcc_arm64",
    "clang_64": "macos",
}


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as response:  # noqa: S310 (URL fixa do Qt)
        return response.read()


def find_repository(host: str, version: str, arch: str) -> tuple[str, ET.Element]:
    compact = version.replace(".", "")
    short_arch = ARCH_DIRS.get(arch, arch)
    # Layout novo (por arquitetura) primeiro; depois o layout antigo.
    candidates = [
        f"{BASE_URL}/{host}/desktop/qt6_{compact}/qt6_{compact}_{short_arch}",
        f"{BASE_URL}/{host}/desktop/qt6_{compact}/qt6_{compact}",
    ]
    for repo in candidates:
        try:
            root = ET.fromstring(fetch(f"{repo}/Updates.xml"))
        except Exception:  # noqa: BLE001 (404 ou XML inválido: tenta o próximo layout)
            continue
        package_name = f"qt.qt6.{compact}.{arch}"
        for package in root.iter("PackageUpdate"):
            if package.findtext("Name") == package_name:
                return repo, package
    sys.exit(f"Pacote qt.qt6.{compact}.{arch} não encontrado em {candidates}")


def wanted(archive: str, modules: list[str]) -> bool:
    if archive.startswith("icu-"):  # dependência do qtbase no Linux
        return True
    return any(archive.startswith(f"{module}-") for module in modules)


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # acentos legíveis também no console do Windows
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--host", required=True, choices=["windows_x86", "linux_x64", "linux_arm64", "mac_x64"])
    parser.add_argument("--arch", required=True, help="ex.: win64_msvc2022_64, linux_gcc_64, linux_gcc_arm64, clang_64")
    parser.add_argument("--modules", default="qtbase", help="lista separada por vírgula (padrão: qtbase)")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    import py7zr  # import tardio: --help funciona sem a dependência

    modules = [m.strip() for m in args.modules.split(",") if m.strip()]
    repo, package = find_repository(args.host, args.version, args.arch)
    package_version = package.findtext("Version")
    archives = [a.strip() for a in (package.findtext("DownloadableArchives") or "").split(",") if a.strip()]
    selected = [a for a in archives if wanted(a, modules)]
    missing = [m for m in modules if not any(a.startswith(f"{m}-") for a in selected)]
    if missing:
        sys.exit(f"Módulos sem arquivo no pacote: {missing}")

    target = args.out / args.version / ARCH_DIRS.get(args.arch, args.arch)
    target.mkdir(parents=True, exist_ok=True)
    package_url = f"{repo}/{package.findtext('Name')}"

    with tempfile.TemporaryDirectory() as tmp:
        for archive in selected:
            file_name = f"{package_version}{archive}"
            data = fetch(f"{package_url}/{file_name}")
            expected = fetch(f"{package_url}/{file_name}.sha1").decode().split()[0].strip().lower()
            actual = hashlib.sha1(data).hexdigest()  # noqa: S324 (é o checksum que o Qt publica)
            if actual != expected:
                sys.exit(f"SHA-1 diferente em {file_name}: esperado {expected}, obtido {actual}")
            local = Path(tmp) / file_name
            local.write_bytes(data)
            with py7zr.SevenZipFile(local) as archive_file:
                archive_file.extractall(target)
            print(f"ok {file_name} ({len(data) // 1024} KiB, sha1 {actual})", file=sys.stderr)

    # O qmake procura o prefixo pelo qt.conf; o CMake não precisa dele.
    (target / "bin" / "qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8")
    print(target.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
