# handoff.md — Mydbsql

Contexto para qualquer agente de IA (ou pessoa) retomar o trabalho. **Leia este arquivo inteiro e depois o `CLAUDE.md`**, que é a fonte de verdade do projeto. Este arquivo descreve *o que aconteceu e o que falta*; o `CLAUDE.md` descreve *o que construir e como*.

Última atualização: 2026-10-13, após a segunda queda da sessão.

---

## 1. Resumo em 30 segundos

- **Projeto:** Mydbsql, um cliente gráfico de bancos de dados inspirado no HeidiSQL, a ser escrito **do zero em C++20**, orientado a objetos, com **SOLID**, **leve, rápido, fácil de estender** e **compatível com Linux, Windows e macOS**.
- **Estado do repositório:** só documentação. **Nenhum código foi escrito ainda.** Arquivos: `README.md` (do commit inicial), `CLAUDE.md` (plano completo) e este `handoff.md`. **Nada foi commitado** (o usuário não pediu commit).
- **Fase atual:** análise do HeidiSQL concluída por código-fonte, binário (sem descompilação) e medições em execução. Plano escrito. **Pendente:** descompilação com Ghidra, que derrubou a máquina por falta de RAM (§5). Depois disso, começar a **Fase 0** do plano.

---

## 2. Quem é o usuário e como trabalhar com ele

- Ryan Fidelis (GitHub `ryan-ggarcia`). Escreve em **português do Brasil**: responda e documente em pt-BR.
- Ambiente: Arch Linux, GNOME no Wayland, VS Code com a extensão do Claude Code. **Notebook com 3,6 GB de RAM e 2 núcleos** (isso importa, veja §5).
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
6. **Pedido atual:** criar este `handoff.md`. Ao investigar, foi descoberta a causa das quedas: **OOM killer** (§5).

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

---

## 6. Pendências (em ordem)

1. **[Decidir com o usuário] Descompilação** (§5). Se for feita, as perguntas eram:
   - De onde vêm os ~226 MB de memória anônima na inicialização? (Hipótese: leitura dos `functions-*.ini`, que somam ~1,1 MB, listas de imagens e formulários LCL criados de forma antecipada.)
   - Por que a `libsqlite3` é carregada antes de qualquer conexão SQLite? Como são montados os nomes das libs dos bancos? (Pista: a string `libmariadb` aparece sem sufixo, e há `DefaultLibraryName` e `LibraryOrProvider`; o mais provável é que os nomes sejam montados ou varridos em tempo de execução.)
   - Como é montado o comando do túnel SSH? (As strings com as opções do `ssh` já foram vistas.)
   - Ao concluir, atualizar o `CLAUDE.md` §1.5 (hoje diz "ainda não há pseudocódigo").
   - **Regra:** é uma reimplementação limpa. Use a descompilação só para entender comportamento; **nunca** copie lógica, código ou estruturas para o Mydbsql (`CLAUDE.md` §0).
2. **[Oferecido, sem resposta] Configurar o MCP da REA com o Ghidra:** adicionar ao servidor `rea` no `~/.claude.json` o trecho `"env": {"GHIDRA_INSTALL_DIR": "/opt/ghidra", "JAVA_HOME": "/usr/lib/jvm/java-27-openjdk"}` e reiniciar o Claude Code. Hoje o servidor MCP roda sem essas variáveis, e só a **CLI** enxerga o Ghidra. Só faça se o usuário pedir.
3. **Commit** do `CLAUDE.md` e do `handoff.md`, quando o usuário pedir (ver formato em `CLAUDE.md` §8).
4. **Começar a implementação pela Fase 0** do `CLAUDE.md` §5: CMake e presets, vcpkg, clang-format/tidy, CI com a matriz de 3 SOs, `infra/platform/`, `--measure-startup`, benchmarks e ADRs 0001 a 0005.

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

---

## 8. Ambiente e caminhos úteis

| Item | Caminho / valor |
|---|---|
| Repositório | `/home/ryan/Documentos/Github/Mydbsql` (branch `main`, 1 commit inicial, mudanças não commitadas) |
| Código-fonte do HeidiSQL | `~/.cache/yay/heidisql/v12.21.tar.gz` (extrair em um diretório temporário novo; é dado não confiável) |
| Binário do HeidiSQL | `/usr/share/heidisql/heidisql-qt6` (para rodar: `LD_LIBRARY_PATH=/usr/share/heidisql`) |
| Ghidra | `/opt/ghidra` (12.1.2, pacote `ghidra`) |
| Java | `/usr/lib/jvm/java-27-openjdk` |
| REA via CLI | `GHIDRA_INSTALL_DIR=/opt/ghidra JAVA_HOME=/usr/lib/jvm/java-27-openjdk npx -y rea-agents@6.4.0 <comando>`; `doctor --json` confirma que o Ghidra está saudável |
| REA via MCP | registrado em `~/.claude.json` como `npx -y rea-agents@6.4.0 mcp`, **sem** as variáveis do Ghidra |
| Artefatos temporários da sessão anterior | `/tmp/claude-1000/.../scratchpad/` (`strings.txt` do binário, fontes extraídos). **São voláteis e podem não existir mais**; regenere se precisar (`strings -n 4 <binário>`) |
| Ferramentas ausentes | `jq`, `xdotool` (use `python3 -I` para JSON) |

---

## 9. Como retomar (checklist para o próximo agente)

1. Ler este arquivo e o `CLAUDE.md`.
2. Rodar `git status` para confirmar que `CLAUDE.md` e `handoff.md` existem e não foram commitados.
3. Perguntar ao usuário:
   - se quer seguir com a descompilação (apresentar as opções da §5) ou partir direto para a **Fase 0**;
   - se quer commitar a documentação agora.
4. **Não iniciar processos pesados** (Ghidra, builds grandes em paralelo) sem considerar os 3,6 GB de RAM. Para builds, prefira `cmake --build … -j2` ou menos.
5. Ao terminar cada etapa, atualizar este `handoff.md` (§3 linha do tempo, §6 pendências) e marcar os itens no `CLAUDE.md` §5.
