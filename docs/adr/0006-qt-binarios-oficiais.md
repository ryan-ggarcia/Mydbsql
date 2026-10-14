# ADR-0006: Qt pelos binários oficiais; vcpkg para as demais dependências

- **Status:** aceito
- **Data:** 2026-10-14

## Contexto

A CLAUDE.md §3 prevê o vcpkg para todas as dependências. Compilar o `qtbase` pelo vcpkg leva de 1 a 3 horas por plataforma e ocupa de 10 a 15 GB temporários. Na máquina de desenvolvimento Windows (7,7 GB de RAM, disco 91% ocupado) isso não é viável, e na CI cada mudança de baseline repetiria o custo. A Qt Company publica binários oficiais pré-compilados em `download.qt.io`, com SHA-1 por arquivo. O aqtinstall 3.3.0 não entende o layout novo do repositório no Windows (`qt6_6113/qt6_6113_msvc2022_64/`).

## Decisão

- O Qt (por ora só o `qtbase`) vem dos **binários oficiais**, baixados por `tools/fetch_qt.py`. O script entende os dois layouts do repositório, confere o SHA-1 de cada arquivo e roda igual no desenvolvimento e na CI.
- Versão fixa: **Qt 6.11.3** (build de 2026-09-24). Para mudar a versão, altere a CI e o handoff no mesmo PR.
- O CMake encontra o Qt por `CMAKE_PREFIX_PATH`, que vem da variável de ambiente `QT_ROOT_DIR` (`CMakePresets.json`). Os caminhos locais ficam no `CMakeUserPresets.json`, que não é versionado.
- As demais dependências (spdlog, GoogleTest, Google Benchmark e as próximas) continuam no **vcpkg em modo manifesto**. A baseline fica em `vcpkg-configuration.json` (tag `2026.07.29`).

## Consequências

- O Qt deixa de ser reprodutível pelo vcpkg, mas a versão e os checksums ficam fixados pelo script.
- Builds com sanitizers não instrumentam o Qt. Por isso o job de sanitizers da CI compila sem o executável Qt (`-DMYDB_BUILD_APP=OFF`).
- No Linux, os binários do Qt 6.11 são compilados no RHEL 9.6 (glibc 2.34). Isso não atende à meta de glibc ≥ 2.31 da §2.6 para os **pacotes distribuídos**. O empacotamento (Fase 12) precisa compilar o Qt numa base mais antiga ou usar o Qt da distribuição.
