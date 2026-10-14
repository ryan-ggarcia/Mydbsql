# handoff.md — Mydbsql

Contexto para qualquer agente de IA (ou pessoa) retomar o trabalho. **Leia este arquivo inteiro e depois o `CLAUDE.md`**, que é a fonte de verdade do projeto. Este arquivo descreve *o que aconteceu e o que falta*; o `CLAUDE.md` descreve *o que construir e como*.

Última atualização: 2026-10-14, após o início da **Fase 0** (fundação do código) na máquina Windows.

---

## 1. Resumo em 30 segundos

- **Projeto:** Mydbsql, um cliente gráfico de bancos de dados inspirado no HeidiSQL, a ser escrito **do zero em C++20**, orientado a objetos, com **SOLID**, **leve, rápido, fácil de estender** e **compatível com Linux, Windows e macOS**.
- **Estado do repositório:** toda a análise está commitada (até `e2c2346`). A **Fase 0 está em andamento e não foi commitada**: CMake, presets, vcpkg, `infra/platform`, logging, `DbError`, janela vazia com `--measure-startup`, testes, benchmarks, CI e ADRs 0001–0006 (§3, item 9).
- **Fase atual:** **Fase 0**. No Windows tudo compila sem warnings (`/W4 /WX`), os 26 testes passam, o clang-tidy está limpo e a janela vazia cabe no orçamento. Falta rodar a CI pela primeira vez, que é o único jeito de validar Linux e macOS a partir desta máquina (§6). A análise do HeidiSQL e do MySQL Workbench está concluída (`CLAUDE.md` §1.5 e §1.6).

---

## 2. Quem é o usuário e como trabalhar com ele

- Ryan Fidelis (GitHub `ryan-ggarcia`). Escreve em **português do Brasil**: responda e documente em pt-BR.
- Ambiente (até 2026-10-13): Arch Linux, GNOME no Wayland, VS Code com a extensão do Claude Code, **notebook com 3,6 GB de RAM e 2 núcleos** (veja §5).
- Ambiente (desde 2026-10-14): **Windows 11 Home**, VS Code com a extensão do Claude Code, **7,7 GB de RAM e 8 núcleos lógicos** (na hora da análise, só ~0,9 GB livres). HeidiSQL 12.21 instalado em `%LOCALAPPDATA%\Programs\HeidiSQL`.
- Pediu explicitamente o uso da skill **REA** (`reverse-engineer-anything`) para a engenharia reversa do HeidiSQL.
- Prioridades que ele declarou, em ordem: (1) funcionalidades semelhantes ao HeidiSQL, (2) C++ + SOLID + OO, (3) app **leve e rápido**, (4) **fácil de adicionar funcionalidades**, (5) **compatível com Linux e demais SOs**.
- Não faça commit, push nem mudanças em configurações do usuário (`~/.claude.json` etc.) sem ele pedir.

---

## 3. Linha do tempo da conversa

1. **Pedido inicial:** investigar e analisar o HeidiSQL, planejar funcionalidades semelhantes num plano de implementação completo, colocar tudo num `CLAUDE.md` (stack C++, SOLID, OO) e usar a skill REA.
   - O pacote AUR `heidisql` no cache do yay (`~/.cache/yay/heidisql/`) só tinha arquivos compartilhados, sem binário. O mesmo cache tinha o **código-fonte completo** `v12.21.tar.gz`, que foi analisado com ferramentas normais (a própria skill REA manda fazer isso quando há código-fonte).
   - Criado o `CLAUDE.md` com análise, arquitetura, stack e plano em fases.
2. **"Por que não foi possível fazer a engenharia reversa?"** Explicado: não havia binário instalado, só o pacote de arquivos compartilhados.
3. **Usuário instalou o `heidisql-qt6`** e pediu: usar engenharia reversa + deixar claro que o app deve ser **leve, rápido e fácil de estender**.
   - A REA abriu o binário, mas não havia descompilador (Ghidra, IDA e Hopper ausentes; sem Java). Foi feita análise estática (readelf, strings de RTTI do Free Pascal) e dinâmica (`/proc`). Resultados no `CLAUDE.md` §1.5.
   - Adicionada a §2 do `CLAUDE.md` (orçamento de desempenho, regras de leveza e velocidade, arquitetura de módulos e plugins, processo de medição).
4. **Usuário pediu:** registrar no `CLAUDE.md` a **compatibilidade com Linux e demais SOs**, e avisou que **baixou o Ghidra** para seguir com a descompilação.
   - Adicionados o objetivo 0, uma regra na §0 e a §2.6 (matriz de SOs, 10 regras de portabilidade, CI e empacotamento), além de ajustes na stack, nas pastas e nas fases.
   - A análise com o Ghidra foi iniciada pela CLI da REA e **a sessão caiu**.
5. **"O que você está fazendo agora?"** A análise tinha sido perdida. Foi reiniciada com `nohup setsid` e **a sessão caiu de novo**.
6. Criado este `handoff.md`. Ao investigar, foi descoberta a causa das quedas: **OOM killer** (§5). Documentação commitada em `401c4fb`.
7. **2026-10-14, máquina Windows:** "o Heidi já está na máquina, use todos os recursos da REA". Feito:
   - O servidor MCP `rea` **não conectou** (timeout de 30 s na inicialização); foi usada a CLI `rea` 6.4.0 (instalada globalmente via npm). `rea doctor`: registro do Claude Code saudável; **Ghidra, Hopper e IDA ausentes; Java ausente**.
   - REA sem engine: `inspect_artifact`, `inspect_managed_artifact` (código nativo, não .NET) e `inspect_pe_resources` (241 recursos). A captura de processo da REA (`capture-process`) **não roda em host Windows** na 6.4.0 (limitação declarada no código da REA).
   - Complementos com ferramentas comuns: parse do PE (seções, imports, *delay imports*, overlay), extração de strings e medição em execução com PowerShell (3 execuções).
   - Resultados no `CLAUDE.md` §1.5 ("Build Windows"), §1.4 e §2.1. **Descoberta principal:** no Windows o HeidiSQL usa ~47,6 MB de working set e ~24,6 MB de memória privada, ou seja, **já cumpre o orçamento do Mydbsql**; os ~296 MB do Linux vêm, provavelmente, do port Lazarus/Qt6.
8. **2026-10-14:** "pode instalar o JDK e o Ghidra e fazer a descompilação; analise o MySQL Workbench e a modelagem de banco, algo que o HeidiSQL não traz". Feito:
   - Instalados **sem instalador e sem admin** em `C:\Users\Iberika\tools\`: Temurin JDK 21.0.12.1+1 e Ghidra 12.1.4 (SHA-256 dos zips conferidos com os publicados). Nenhuma variável de ambiente global foi alterada.
   - Como o MCP da REA não conecta no Claude Code e a CLI reanalisaria o binário a cada comando, foi usado um *bridge* próprio (script Python no scratchpad) que mantém **uma** sessão `rea mcp` viva com `GHIDRA_INSTALL_DIR`, `JAVA_HOME`, `GHIDRA_HEADLESS_MAXMEM=2G` e `REA_GHIDRA_STARTUP_TIMEOUT_MS` alto (o padrão de 330 s é curto para este binário). Análise: ~19 min, Java em ~0,7–1,1 GB, sem travar a máquina. Sessão fechada com `close_binary`; pacote de evidências exportado (fica fora do repositório).
   - Descompiladas: `entry`, rotina pós-criação (atualização e estatísticas), download WinINet, carga dos `functions-*.ini`, descoberta das DLLs e túnel SSH. Resultados no `CLAUDE.md` §1.5 (tabela "Descompilação") e §1.4.
   - MySQL Workbench 8.0.47: MSI baixado (assinatura Oracle válida) e **só extraído** com `msiexec /a` em `C:\Users\Iberika\tools\wb-extract` (1,2 GB; apagado depois, a pedido do usuário, junto com os downloads); não foi instalado nem executado. REA: `inspect_artifact` no `sakila_full.mwb` e `inspect_managed_artifact` em 3 assemblies. Resultados no `CLAUDE.md` §1.6, nos novos registries da §2.4, na **Fase 13** e na §9.
   - A documentação foi commitada pelo usuário em `3738555` e `e2c2346`.
9. **2026-10-14:** "atualize o handoff, verifique se as ferramentas da REA estão instaladas e, se tudo estiver ok, comece a Fase 0". Feito:
   - **REA:** depois de reiniciar o Claude Code, o servidor MCP `rea` conecta e expõe todas as ferramentas (`mcp__rea__*`). O `rea doctor` mostra o Ghidra 12.1.4 e o JDK 21 saudáveis; Hopper e IDA estão ausentes (opcionais).
   - **Toolchain encontrado:** VS 2026 Build Tools 18.10 com MSVC 14.51 (toolset v145), CMake, Ninja e vcpkg embutidos. Há também um VS Community 2026 18.4 sem MSVC útil e com data de suporte vencida. Faltavam o Qt e as ferramentas do LLVM.
   - **Instalado com autorização do usuário:** Qt 6.11.3 `qtbase` para MSVC 64 bits (binários oficiais, 275 MB, em `C:\Users\Iberika\tools\Qt`) e, via `pip install --user`, aqtinstall 3.3.0, clang-format 23.1.3 e clang-tidy 22.1.8. O aqtinstall **não funciona** com o layout novo do repositório do Qt no Windows, por isso foi escrito `tools/fetch_qt.py` (ADR-0006).
   - **Fase 0 implementada** (detalhes e pendências em §6 e no `CLAUDE.md` §5).

---

## 4. O que foi descoberto sobre o HeidiSQL

Os detalhes estão no `CLAUDE.md` §1. Resumo:

### 4.1 Código-fonte (v12.21, Free Pascal/Lazarus, ~64 mil linhas)
- Unidades centrais: `main.pas` (16k linhas, um "God object"), `dbconnection.pas` (11k; `TDBConnection` com subclasses por SGBD; `TDBQuery`), `dbstructures*.pas` (`TSqlProvider` + enum `TQueryId` com mais de 80 queries por SGBD), `apphelpers.pas` (configurações, `TSQLBatch` como divisor de SQL, `TQueryThread`), `table_editor`, `tabletools` (manutenção, busca, dump, edição em massa, gerador de dados), `exportgrid` (16 formatos), `usermanager`, `grideditlinks`, `loaddata`, `connections`.
- O inventário completo de funcionalidades está no `CLAUDE.md` §1.3.

### 4.2 Binário `/usr/share/heidisql/heidisql-qt6` (sem descompilação)
- ELF x86-64 *stripped*, SHA-256 `8c8ff0423a0843a9456ed97e21ea757f540a4459871ac963189f6bb9e7091b5f`, 13,1 MB. Linka só `libQt6Pas`, `libX11` e `libc`; os drivers de banco são carregados com `dlopen`. O pacote `-debug` não tem símbolos úteis.
- Funcionalidades vistas **só no binário**: histórico de queries com retenção em dias, formatador online (sqlformat.org), pesquisa na web do termo selecionado, histórico de localizar/substituir, esquemas de cores.
- SSH via `ssh`/`sshpass` externos com `StrictHostKeyChecking=no`.
- Configurações em `~/.config/heidisql/`.

### 4.3 Medições em execução (gerenciador de sessões aberto, sem conexão)
- RSS de **~296 MB**, memória anônima privada de **~226 MB**, 12 threads, 113 `.so` carregadas, incluindo a `libsqlite3` sem nenhuma conexão SQLite.
- Esses números são a linha de base do orçamento do Mydbsql (`CLAUDE.md` §2.1: ≤ 60 MB de RSS, ≤ 25 MB de memória anônima, ≤ 300 ms de inicialização).
- **Efeito colateral:** rodar o HeidiSQL criou `~/.config/heidisql/` (`tabs.ini` vazio + `Snippets/`) na máquina do usuário. É inofensivo; ele pode apagar a pasta se quiser.

---

## 5. ⚠️ Por que a sessão caiu duas vezes (leia antes de rodar o Ghidra)

**Causa confirmada no `journalctl`:** o **OOM killer** do kernel. A máquina tem 3,6 GB de RAM + 1,8 GB de swap. A análise automática do Ghidra (Java) num binário de 13 MB passou de **1,3 GB de RSS** (4,3 GB virtuais). Junto com o GNOME e o VS Code, a memória acabou:
- `20:43:28` — o OOM killer matou o **`gnome-shell`** (primeira queda da sessão).
- `20:48:53` — o OOM killer matou o **`java` do Ghidra** e processos do **VS Code** (segunda queda). A REA registrou `"code":"cancelled"`.

**Não rode o Ghidra de novo nesta máquina do jeito que foi feito.** Opções, da mais segura para a menos:
1. **Pular a descompilação.** O código-fonte completo já responde às perguntas de arquitetura. A descompilação só confirmaria detalhes de memória e carregamento (§6). Esta é a recomendação, a menos que o usuário insista.
2. **Rodar em outra máquina com ≥ 8 GB de RAM** e trazer o snapshot da REA.
3. **Nesta máquina, com limites:** fechar o VS Code e outros apps; limitar o heap do Ghidra (em `/opt/ghidra/support/launch.properties`, `MAXMEM=1G`, ou a variável que o `analyzeHeadless` aceitar; confira antes de editar, pois a pasta é do sistema e exige sudo e a permissão do usuário); rodar com `systemd-run --user --scope -p MemoryMax=1500M …` para que só a análise morra se estourar, e não a sessão gráfica.

**Pergunte ao usuário qual opção ele prefere antes de rodar qualquer coisa pesada.**

**Atualização 2026-10-14 (máquina Windows, 7,7 GB):** resolvido. Com autorização do usuário, JDK 21 e Ghidra 12.1.4 foram instalados em `C:\Users\Iberika\tools\` e a descompilação do build Windows foi feita (§3, item 8). O heap da JVM foi limitado com `GHIDRA_HEADLESS_MAXMEM=2G` (a REA lê essa variável); não foi preciso editar o `launch.properties`.

---

## 6. Pendências (em ordem)

1. **[Feito] Descompilação do build Windows** (`CLAUDE.md` §1.5, tabela "Descompilação"). Situação das perguntas originais:
   - *De onde vêm os ~226 MB de memória anônima do Linux?* **Em parte (inferência):** o build Windows usa ~24,6 MB; a hipótese dos `functions-*.ini` caiu (são lidos por conexão, depois de conectar). O grosso deve vir do port Lazarus/Qt6. Confirmar exigiria um profiler no Linux.
   - *Por que a `libsqlite3` sobe na inicialização no Linux?* **Em aberto para o Linux.** No Windows a descoberta de drivers só lista nomes de DLL e nada é carregado antes de conectar.
   - *Como são montados os nomes das libs?* **Respondida (pseudocódigo):** uma regex por família, varrendo `*.dll` na pasta do app, com cache por tipo de conexão; MSSQL via provedores OLE DB.
   - *Como é montado o comando SSH?* **Respondida (pseudocódigo):** ordem completa dos argumentos, porta local alternativa, respostas automáticas a prompts e máscara da senha só no log.
   - **Regra:** é uma reimplementação limpa. Use a descompilação só para entender comportamento; **nunca** copie lógica, código ou estruturas para o Mydbsql (`CLAUDE.md` §0). O pseudocódigo não vai para o repositório.
2. **[Decidir com o usuário] Metas de memória por plataforma** (`CLAUDE.md` §2.1, nota ⚠️). **Primeiro dado medido (2026-10-14, Windows, release, janela Qt 6.11.3 vazia, mediana de 3 execuções aquecidas):** **~67 ms** até a primeira exposição, **~32,6 MB** de working set, **~10,4 MB** de *private bytes* e 78 módulos (incluindo `spdlog.dll` e `fmt.dll`, porque o triplet `x64-windows` do vcpkg é dinâmico). A primeira execução a frio levou ~227 ms. Isso já cabe nas metas da Fase 0 (≤ 150 ms, ≤ 40 MB). Faltam os números de Linux e macOS, que virão da CI, para fixar metas por SO.
3. **[Feito] MCP da REA no Claude Code:** conecta depois de reiniciar o Claude Code e expõe todas as ferramentas.
4. **[Opcional] Medir o MySQL Workbench em execução** (memória e tempo de abertura) para comparar com o orçamento. Exige baixar e instalar o MSI de novo (a cópia extraída foi apagada); ainda não foi feito.
5. **[Fase 0, feito localmente no Windows]**
   - CMake ≥ 3.25 + Ninja; presets `debug`, `release` (LTO `/GL` e `/LTCG` verificados; `-O2` e símbolos ocultos), `asan` e `tsan` (só fora do Windows); alvos `mydb_core`, `mydb_platform`, `mydb_logging`, `mydb_ui`, `mydb_app_support`, `mydbsql`, `mydb_tests`, `mydb_bench` e o plugin de teste `mydb_test_plugin`.
   - vcpkg em modo manifesto (`vcpkg.json`: spdlog, gtest e benchmark) com baseline na tag `2026.07.29` (`vcpkg-configuration.json`).
   - `src/infra/platform/`: `IPlatform` (diretórios, executável, pasta de plugins, sufixo de biblioteca, memória, bibliotecas carregadas, `openUrl` restrito a http, https e mailto), `DynamicLibrary` (RAII) e as implementações Windows, Linux e macOS. A escolha do SO é feita no CMake, não por `#ifdef`.
   - `DbError` com `ConnectionError`, `QueryError` e `CancelledError`; spdlog assíncrono com arquivo rotativo em `<cache>/logs`.
   - Janela vazia (`ui/main/MainWindow`); `--measure-startup[=arquivo]` e `--settle-ms=N`; `EventLoopWatchdog` (> 16 ms) nos builds de debug.
   - Testes (GoogleTest): 24 unitários, a checagem de `#ifdef` (`lint.platform_ifdefs`) e a fumaça da janela (`ui.measure_startup_smoke`, plataforma offscreen). O teste do plugin prova que uma biblioteca não aparece em `loadedLibraries()` antes de ser aberta e some depois de fechada.
   - Benchmarks (`bench/`), orçamento em `bench/budget.json` e comparação em `tools/check_budget.py`.
   - `.clang-format`, `.clang-tidy` (convenções de nome da §6; cada exclusão tem um motivo escrito), `tools/run_clang_tidy.py`, `tools/run_clang_format.py` e `tools/requirements-dev.txt` com as versões fixas.
   - CI em `.github/workflows/ci.yml` com a action `.github/actions/setup-deps`: lint, matriz de 5 builds (Linux GCC e Clang, Linux ARM64, Windows MSVC e macOS), sanitizers, clang-tidy e orçamento nas 3 plataformas.
   - ADRs 0001–0005 e o **ADR-0006** (Qt pelos binários oficiais, mudança em relação à §3 da `CLAUDE.md`, já atualizada). `README.md` com as instruções de build; `CLAUDE.md` §2.6 (MSVC v145 aceito), §3, §5 e §7 atualizados.
6. **[Fase 0, falta]**
   - **Rodar a CI pela primeira vez.** Depende de commit e push, que o usuário não pediu ainda. As implementações Linux e macOS do `IPlatform` **nunca foram compiladas**; espere ajustes na primeira execução.
   - Gerar os baselines `bench/baseline/{linux,windows,macos}.json` **nos runners da CI** (artefato `startup-<plataforma>` + `tools/check_budget.py --write-baseline`) e commitá-los.
   - Pontos a vigiar na CI: os pacotes apt do Qt no Ubuntu 22.04; o `xvfb-run` no job de orçamento do Linux; os binários do Qt 6.11 no Linux exigem glibc 2.34 (ADR-0006: não atende à meta de glibc ≥ 2.31 da §2.6 para pacotes distribuídos); macOS com o Qt `clang_64` universal.
   - O `CMakeUserPresets.json` local (não versionado) tem os presets `win-debug` e `win-release` com os caminhos desta máquina.
7. **[Sugestão] Triplet estático no Windows** (`x64-windows-static-md`): eliminaria `spdlog.dll`, `fmt.dll`, `gtest*.dll` e `benchmark*.dll` e reduziria os módulos carregados na inicialização. Não foi feito para não divergir da CI sem medir.
8. **Commit da Fase 0**, quando o usuário pedir (Conventional Commits, `CLAUDE.md` §8).

---

## 7. Decisões já tomadas (não refazer sem motivo)

| Decisão | Onde está | Observação |
|---|---|---|
| Reimplementação limpa; nada de código, ícones ou `.ini` do HeidiSQL (GPL-2.0) | `CLAUDE.md` §0 | Inegociável |
| C++20, CMake + vcpkg, Qt 6 Widgets, QScintilla | `CLAUDE.md` §3 | Escolhas de projeto do assistente; o usuário não contestou. Podem ser revistas via ADR |
| Drivers nativos (libmariadb, libpq, sqlite3) sem Qt SQL, carregados como plugins sob demanda | §2.2, §3 | Por leveza |
| libssh2 embutido em vez de ssh/plink externos | §2.6, §3 | Por portabilidade e segurança |
| Arquitetura em camadas (`core`, `sdk`, `drivers`, `modules`, `infra`, `services`, `ui`, `app`) e registries de módulos | §2.4, §4 | Extensibilidade |
| Orçamento de desempenho (≤ 60 MB RSS, ≤ 300 ms de inicialização, …) | §2.1 | Metas escolhidas pelo assistente a partir das medições do HeidiSQL; ajustáveis |
| Linux, Windows e macOS obrigatórios; código de SO só em `src/infra/platform/` | §0, §2.6 | Pedido do usuário |
| MVP = Fases 0 a 6 com MySQL/MariaDB | §5 | — |
| Qt 6.11.3 pelos binários oficiais (`tools/fetch_qt.py`); vcpkg para o resto | ADR-0006, `CLAUDE.md` §3 | Usuário escolheu "binários prontos" em 2026-10-14; compilar o Qt pelo vcpkg era inviável nesta máquina |
| MSVC v145 (VS 2026) aceito ao lado do v143 | `CLAUDE.md` §2.6 | É o que existe na máquina Windows; ABI compatível com os binários do Qt para MSVC 2022 |
| `openUrl` só aceita http, https e mailto | `IPlatform.hpp` | Segurança: um valor da grade nunca vira a execução de um programa local |
| Modelagem visual (EER) inspirada no MySQL Workbench como **Fase 13**, em módulo dinâmico, multi-SGBD, formato JSON próprio e `.mwb` só por importação | §1.6, §2.4, §5 | Pedido do usuário (diferencial sobre o HeidiSQL); detalhes e ordem propostos pelo assistente |

---

## 8. Ambiente e caminhos úteis

| Item | Caminho / valor |
|---|---|
| Repositório | `/home/ryan/Documentos/Github/Mydbsql`: clone da época do Linux (branch `main`). Está desatualizado; faça `git pull` antes de usar |
| Código-fonte do HeidiSQL | `~/.cache/yay/heidisql/v12.21.tar.gz` (extrair em um diretório temporário novo; é dado não confiável) |
| Binário do HeidiSQL | `/usr/share/heidisql/heidisql-qt6` (para rodar: `LD_LIBRARY_PATH=/usr/share/heidisql`) |
| Ghidra | `/opt/ghidra` (12.1.2, pacote `ghidra`) |
| Java | `/usr/lib/jvm/java-27-openjdk` |
| REA via CLI | `GHIDRA_INSTALL_DIR=/opt/ghidra JAVA_HOME=/usr/lib/jvm/java-27-openjdk npx -y rea-agents@6.4.0 <comando>`; `doctor --json` confirma que o Ghidra está saudável |
| REA via MCP | registrado em `~/.claude.json` como `npx -y rea-agents@6.4.0 mcp`, **sem** as variáveis do Ghidra |
| Artefatos temporários da sessão anterior | `/tmp/claude-1000/.../scratchpad/` (`strings.txt` do binário, fontes extraídos). **São voláteis e podem não existir mais**; regenere se precisar (`strings -n 4 <binário>`) |
| Ferramentas ausentes | `jq`, `xdotool` (use `python3 -I` para JSON) |

**Máquina Windows (desde 2026-10-14):**
| Item | Caminho / valor |
|---|---|
| Repositório | `C:\Users\Iberika\Documents\GitHub\Mydbsql` (branch `main`) |
| HeidiSQL | `%LOCALAPPDATA%\Programs\HeidiSQL\heidisql.exe` (12.21.0.7344, 64 bits, Delphi/VCL) + DLLs de cliente, `plink.exe` e `functions-*.ini` na mesma pasta |
| Configurações do HeidiSQL | registro `HKCU\Software\HeidiSQL` (sem sessões salvas em 2026-10-14) |
| REA | CLI `rea` 6.4.0 global (`%APPDATA%\npm\rea.ps1`), Node 26.10.0. MCP `rea` no `~/.claude.json` com o `env` do Ghidra (backup em `~/.claude.json.bak-2026-10-14`); **funcionando** desde o reinício do Claude Code (2026-10-14) |
| Compilador | VS 2026 Build Tools 18.10 (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`): MSVC 14.51 (v145), CMake, Ninja e vcpkg embutidos. Fora do PATH: use `tools\vsdev.cmd <comando>`, que chama o `vcvars64.bat` |
| vcpkg | o embutido no VS (`…\BuildTools\VC\vcpkg`), em modo manifesto. Pacotes em `build/vcpkg_installed`; cache binário padrão do usuário |
| Qt | `C:\Users\Iberika\tools\Qt\6.11.3\msvc2022_64` (só `qtbase`, binários oficiais via `tools/fetch_qt.py`). Para rodar o `mydbsql.exe` fora do CTest, ponha `…\msvc2022_64\bin` no PATH |
| clang-format / clang-tidy | 23.1.3 / 22.1.8 via `pip --user`, em `%APPDATA%\Python\Python314\Scripts` (fora do PATH) |
| Build local | `tools\vsdev.cmd cmake --preset win-debug`, `… cmake --build --preset win-debug`, `… ctest --preset win-debug` (presets do `CMakeUserPresets.json` local, com 3 jobs por causa da RAM) |
| Java | `C:\Users\Iberika\tools\jdk-21.0.12.1+1` (Temurin, zip; não está no PATH global) |
| Ghidra | `C:\Users\Iberika\tools\ghidra_12.1.4_PUBLIC` (zip oficial; `rea doctor` o reconhece com `GHIDRA_INSTALL_DIR` e `JAVA_HOME` definidos) |
| MySQL Workbench 8.0.47 | **apagado** a pedido do usuário em 2026-10-14 (a cópia extraída e os downloads). Para reanalisar: baixar `https://cdn.mysql.com//Downloads/MySQLGUITools/mysql-workbench-community-8.0.47-winx64.msi` (SHA-256 `f7a0f3c811bcec93456184bb252540cbae4ea32f9af2cc910760fccbce6767bd`) e extrair com `msiexec /a <msi> /qn TARGETDIR=<pasta nova>`. Os arquivos úteis ficam em `PFiles64\MySQL\MySQL Workbench 8.0 CE\` (`structs/`, `data/`, `modules/`, `extras/sakila_full.mwb`) |
| Tamanho do Workbench instalado | ~1,2 GB, dos quais ~1 GB é o `swb` (MySQL Shell ~764 MB + MySQL Router ~244 MB); o Workbench em si tem ~200 MB |
| Análise do HeidiSQL no Ghidra | ~19 min com `GHIDRA_HEADLESS_MAXMEM=2G`; a análise é efêmera (apagada no `close_binary`), então repetir custa o mesmo tempo |
| Python | `C:\Python314\python.exe` (use `python -I`; para imprimir Unicode, `sys.stdout.reconfigure(encoding='utf-8')`) |
| Shells | PowerShell 5.1 e Git Bash |
| Artefatos da sessão de 2026-10-14 | scratchpad temporário da sessão (voláteis): JSON das evidências REA, pacote de evidências exportado (`heidisql_evidence_bundle.json`), `strings.txt`, `pe_imports.py`, `pe_exports.py`, `measure_heidi.ps1`, `rea_bridge.py`/`rea_call.py` (sessão MCP persistente da REA via HTTP local) e as amostras `run1..3`. Regenere se precisar |

---

## 9. Como retomar (checklist para o próximo agente)

1. Ler este arquivo e o `CLAUDE.md`.
2. Rodar `git status`: a Fase 0 pode ainda não estar commitada (§6, item 8).
3. Perguntar ao usuário se pode commitar e fazer push para rodar a CI (§6, item 6), e se as metas de memória viram metas por plataforma (§6, item 2).
4. **Cuidado com a memória:** a máquina Windows tem 7,7 GB, mas costuma ter menos de 1 GB livre. Builds com no máximo 3 jobs (os presets `win-*` já fazem isso); nada de Ghidra junto com builds. No notebook Linux (3,6 GB), use `-j2`.
5. Ao terminar cada etapa, atualizar este `handoff.md` (§3 linha do tempo, §6 pendências) e marcar os itens no `CLAUDE.md` §5.
