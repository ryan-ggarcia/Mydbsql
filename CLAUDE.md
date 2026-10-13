# CLAUDE.md — Mydbsql

Mydbsql é um cliente gráfico de bancos de dados (MySQL/MariaDB, PostgreSQL, SQLite e, depois, SQL Server e Firebird) inspirado no **HeidiSQL**, escrito do zero em **C++20**, **orientado a objetos** e guiado pelos princípios **SOLID**.

**Objetivos de produto:**

0. **Multiplataforma (pré-requisito):** roda nativamente em **Linux** (plataforma principal de desenvolvimento), **Windows** e **macOS**, com as mesmas funcionalidades. Nenhuma funcionalidade é exclusiva de um sistema, salvo limitação real do SO, documentada.
1. **Leve:** pouca memória, binário pequeno, poucas dependências.
2. **Rápido:** abre quase instantaneamente, nunca trava a interface e lida com resultados grandes sem engasgar.
3. **Fácil de estender:** uma funcionalidade nova entra como um módulo novo, sem mexer no que já existe.

A §2 detalha esses objetivos com metas numéricas e regras de portabilidade (§2.6). Toda decisão técnica deve ser avaliada contra eles.

Este arquivo é a fonte de verdade para quem trabalhar no repositório (humanos ou Claude). Ele reúne:

1. a análise do HeidiSQL que serviu de referência (código-fonte e engenharia reversa do binário);
2. os requisitos de leveza, velocidade e extensibilidade;
3. a arquitetura-alvo do Mydbsql;
4. o plano de implementação completo, por fases;
5. as convenções de código e de processo.

---

## 0. Regras inegociáveis

- **Reimplementação limpa (clean-room).** O HeidiSQL é GPL-2.0. Usamos o HeidiSQL só como referência de *comportamento e funcionalidades*. **Nunca** copie nem traduza código Pascal, arquivos `.lfm`, ícones (os ícones Icons8 do HeidiSQL têm licença exclusiva) ou textos de tradução. Os arquivos `functions-*.ini` do HeidiSQL também não devem ser copiados: o catálogo de funções SQL deve ser gerado a partir da documentação oficial de cada SGBD.
- **C++20**, sem extensões de compilador. O código compila sem warnings com `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) e `/W4 /WX` (MSVC).
- **Compatível com Linux, Windows e macOS desde o primeiro commit.** Todo PR precisa compilar e passar nos testes nas três plataformas (CI). Código específico de SO (`#ifdef _WIN32`, `__linux__`, `__APPLE__`) só pode existir dentro de `src/infra/platform/` (§2.6).
- **Nada de lógica de banco na camada de UI.** A UI só conversa com *services* e *interfaces* (veja §4).
- **RAII em tudo.** Nada de `new`/`delete` soltos. Use `std::unique_ptr` por padrão e `std::shared_ptr` só quando a posse for realmente compartilhada. Handles de C (MYSQL*, PGconn*, sqlite3*) ficam encapsulados em wrappers com deleter próprio.
- **Toda funcionalidade nova vem com testes** (unitários no core; de integração nos drivers, contra containers).
- **Senhas nunca vão em texto puro** para disco nem para o log. Use o keychain do SO (libsecret, Keychain ou Windows Credential Manager) via `ICredentialStore`.

---

## 1. Análise do HeidiSQL (referência)

### 1.1 Origem da análise

- Versão analisada: **HeidiSQL 12.21**, branch Lazarus/FreePascal (port multiplataforma; a partir da v13 o build Windows também sai dela).
- Material, em duas frentes:
  1. **Código-fonte completo** (`v12.21.tar.gz`, `source/*.pas`, cerca de 64 mil linhas): arquitetura e funcionalidades (§1.2–1.3).
  2. **Engenharia reversa do binário instalado** `/usr/share/heidisql/heidisql-qt6` (pacote AUR `heidisql-qt6` 12.21-2), com a skill REA, binutils e medições em execução (§1.5).
- UI: LCL (Lazarus), com **VirtualTreeView** nas grades e árvores e **SynEdit** no editor SQL.
- Drivers: carrega as bibliotecas nativas do cliente em tempo de execução (`TDbLib`, via dlopen/LoadLibrary): libmysqlclient/libmariadb, libpq, sqlite3 (inclusive SQLCipher), FreeTDS (MSSQL) e fbclient (Interbase/Firebird).

### 1.2 Arquitetura observada (unidades principais)

| Unidade HeidiSQL | Linhas | Responsabilidade |
|---|---|---|
| `main.pas` | ~16k | Formulário principal: árvore de objetos, abas, ações, log. Um "God object" que mistura UI, estado e lógica. |
| `dbconnection.pas` | ~11k | `TConnectionParameters`, `TDBConnection` (base abstrata) com as subclasses `TMySQLConnection`, `TPgConnection`, `TSQLiteConnection`, `TSqlSrvConnection` e `TInterbaseConnection`; `TDBQuery` com as subclasses correspondentes; `TDBObject`; `TTableColumn`/`TTableKey`/`TForeignKey`/`TCheckConstraint`; túnel SSH (`TSecureShellCmd`, que usa plink/ssh como processo externo). |
| `dbstructures*.pas` | ~6k | `TNetType` (18 tipos de conexão), `TNetTypeGroup`, `TSqlProvider` (SQL específico por SGBD, indexado por `TQueryId` com mais de 80 ids), catálogo de tipos de dados (`TDBDatatype` + categorias) e bindings das libs C (`TDbLib`). |
| `apphelpers.pas` | ~4.5k | Configurações (`TAppSettings`, mais de 200 chaves `TAppSettingIndex`), divisor de SQL (`TSQLBatch`/`TSQLSentence`), `TQueryThread` (execução assíncrona de lotes), utilitários. |
| `jsonregistry.pas` | — | Persistência das configurações em JSON (no lugar do registro do Windows). |
| `table_editor.pas` | ~3.3k | Designer de tabela: colunas, índices, FKs, CHECKs, opções e preview do DDL (ALTER/CREATE). |
| `tabletools.pas` | ~2.5k | Ferramentas por modo `TToolMode`: manutenção (CHECK/ANALYZE/OPTIMIZE/REPAIR), busca de texto no servidor, exportação SQL (dump), edição em massa e geração de dados. |
| `usermanager.pas` / `rolemanagerpg.pas` | ~3.2k | Gestão de usuários e privilégios (MySQL) e de roles (PostgreSQL). |
| `grideditlinks.pas` | ~1.9k | Editores in-place da grade: Hex, DateTime, Enum, Set, texto, default de coluna e tipo de dado. |
| `exportgrid.pas` | ~1.3k | Exportação de resultados em 16 formatos (§1.3). |
| `loaddata.pas` / `csv_detector.pas` | ~1.2k | Importação de CSV, via `LOAD DATA [LOCAL] INFILE` no servidor ou por parse no cliente, com detecção automática de layout. |
| `connections.pas` | ~1.8k | Gerenciador de sessões (árvore com pastas, todas as opções de conexão, SSH e SSL). |
| `routine_editor`, `trigger_editor`, `event_editor`, `view` | — | Editores de procedures/functions, triggers, events e views. |
| `copytable`, `insertfiles`, `reformatter`, `searchreplace`, `sqlhelp`, `bineditor`, `texteditor`, `data_sorting`, `column_selection`, `createdatabase`, `editvar`, `preferences`, `updatecheck`, `customize_highlighter` | — | Diálogos auxiliares. |

### 1.3 Inventário de funcionalidades (o que vamos replicar)

**Sessões e conexão**
- Gerenciador de sessões em árvore, com pastas, cor por sessão, comentário, contador de conexões e data da última conexão. Exporta e importa as configurações.
- Tipos de conexão: MySQL/MariaDB (TCP, socket ou named pipe, túnel SSH, AWS RDS, admin do ProxySQL), PostgreSQL (TCP, túnel SSH; também Redshift), SQLite (incluindo arquivo criptografado), MSSQL e Interbase/Firebird.
- Opções: SSL (chave, certificado, CA, cipher, nível de verificação), compressão, timeout de query, keep-alive (ping periódico), fuso local, script de inicialização, filtro de bancos (lista ou regex de ignorar), prompt de login, plugin cleartext, log de DDL/DML em arquivo.
- Túnel SSH: host, porta, usuário, senha ou chave privada, porta local e timeout.
- Várias conexões abertas ao mesmo tempo na mesma árvore.

**Navegação (árvore de objetos)**
- Hierarquia servidor → bancos → tabelas, views, procedures, functions, triggers e events (e colunas, opcionalmente).
- Filtros de texto para bancos e objetos, favoritos, agrupamento por tipo, tamanho dos objetos, cache de metadados com refresh parcial ou completo.

**Abas de contexto**
- *Host*: bancos (com tamanhos), variáveis (editáveis, globais/sessão), status, processlist (com kill) e estatísticas de comandos.
- *Database*: lista de objetos com linhas, tamanho, engine, collation e datas.
- *Table/Editor*: designer de tabela, view, rotina, trigger ou event.
- *Data*: grade paginada (mostrar as próximas N linhas ou todas), filtro WHERE com autocomplete, filtro rápido (valor focado, clipboard, NULL/NOT NULL), ordenação multi-coluna, seleção de colunas, edição in-place, inserir, duplicar e excluir linha, salvar ou cancelar alterações, seguir FK, edição de BLOB/hex, exibir timestamps UNIX como data.
- *Query*: várias abas de query, editor com realce de sintaxe, autocomplete (palavras-chave, objetos, colunas, funções), dobra de código, localizar/substituir, executar tudo, a seleção ou a query atual, EXPLAIN, vários resultados por execução, delimitador configurável, parar em erro, lote "de uma vez" ou query a query, cancelar, snippets, reformatar SQL, ajuda SQL contextual, salvar/carregar `.sql`.

**Ferramentas**
- Manutenção de tabelas (CHECK/ANALYZE/CHECKSUM/OPTIMIZE/REPAIR).
- Busca de texto em todo o servidor.
- Exportação SQL (dump): estrutura e/ou dados, DROP/CREATE, para arquivo, ZIP, clipboard, diretório (um arquivo por objeto), outro banco ou outro servidor.
- Edição em massa de tabelas (engine, collation, mover de banco, etc.).
- Gerador de dados de teste.
- Copiar tabela; importar CSV; inserir arquivos em colunas BLOB; rodar arquivo SQL; esvaziar ou dropar objetos.
- Exportar grade: Excel, CSV, HTML, XML, SQL INSERT/INSERT IGNORE/REPLACE/DELETE+INSERT/UPDATE, LaTeX, Textile, Jira, array PHP, Markdown, JSON e JSON Lines.
- Gerenciador de usuários e privilégios (MySQL) e de roles (PostgreSQL); trocar senha.
- FLUSH (hosts, logs, privileges, tables, status) e attach/detach de banco (SQLite).

**Encontrado só no binário (engenharia reversa, §1.5)**
- **Histórico de queries** persistente, com retenção configurável em dias (`asQueryHistoryEnabled`, `asQueryHistoryKeepDays`, `TQueryHistory`) e ação "Clear query history".
- **Formatador SQL online** opcional (`https://sqlformat.org/api/v1/format`), além do formatador local.
- **Pesquisa na web** do termo selecionado (Google, Bing, DuckDuckGo, Ecosia, Baidu, usando o modelo `%query`).
- Histórico de busca e substituição no editor (`asFindDialogSearchHistory`/`ReplaceHistory`).
- Esquemas de cores do editor (`TAppColorScheme(s)`).

**Infraestrutura**
- Painel de log com categorias (info, SQL, SQL do usuário, erro, debug, script).
- Preferências: fontes, cores do realce, limites de linhas, formatos e atalhos.
- Internacionalização (gettext), tema escuro e verificação de atualização.

### 1.4 Lições de design (o que fazer melhor que o HeidiSQL)

| Problema no HeidiSQL | Solução no Mydbsql (princípio) |
|---|---|
| `main.pas` com 16k linhas concentra tudo | Views finas + ViewModels/Presenters + Services (**SRP**) |
| `TDBConnection` tem dezenas de responsabilidades (query, cache, escape, metadados, SSH, log, keep-alive) | Separar em `IConnection`, `ISqlDialect`, `IMetadataProvider`, `IMetadataCache`, `ITunnel`, `IQueryLogger` (**SRP**, **ISP**) |
| `case NetType of …` espalhado pelo código | Polimorfismo + registro de drivers (`DriverRegistry`) (**OCP**) |
| `TSqlProvider` indexado por um enum gigante (`TQueryId`) | Interfaces de dialeto coesas e específicas (`IDdlGenerator`, `IAdminQueries`, …) (**ISP**) |
| UI depende de classes concretas de conexão | UI depende só de interfaces injetadas (**DIP**) |
| Configurações globais acessadas em qualquer lugar (`AppSettings`) | `ISettingsStore` injetado; configurações tipadas por módulo |
| Exportadores implementados como `case` dentro de um formulário | Uma classe `IResultExporter` por formato, registradas numa fábrica (**OCP**, **LSP**) |
| ~226 MB de memória anônima privada só para exibir o gerenciador de sessões (§1.5) | Inicialização preguiçosa e orçamento de memória (§2) |
| Bibliotecas carregadas antes de serem necessárias (a `libsqlite3` sobe na inicialização) | Driver só é carregado na primeira conexão daquele tipo (§2.2) |
| Túnel SSH via processo externo (`ssh`/`plink` + `sshpass`) com `StrictHostKeyChecking=no` | libssh2 embutido, com verificação de host key |
| Um executável monolítico de ~13 MB onde tudo é compilado junto | Núcleo pequeno + módulos registráveis (§2.4) |

### 1.5 Engenharia reversa do binário (evidências)

Alvo: `/usr/share/heidisql/heidisql-qt6`, ELF x86-64, *stripped*, SHA-256 `8c8ff0423a0843a9456ed97e21ea757f540a4459871ac963189f6bb9e7091b5f`. O pacote `heidisql-qt6-debug` também vem *stripped*, sem tabela de símbolos útil.

**Limites desta análise:** ainda **não há pseudocódigo**. A análise estática usou `readelf`/`size` e as strings de RTTI do Free Pascal; a dinâmica usou `/proc/<pid>`. Depois o Ghidra 12.1.2 (`/opt/ghidra`) e o JDK 27 foram instalados e a REA os reconhece pela CLI, mas a análise completa do binário foi **morta duas vezes pelo OOM killer**: a máquina de desenvolvimento tem 3,6 GB de RAM, e o Java do Ghidra passou de 1,3 GB, derrubando também o `gnome-shell` e o VS Code. Veja `handoff.md` para retomar com segurança (limitar o heap do Ghidra ou usar uma máquina com ≥ 8 GB).

**Observado (estático):**
| Item | Valor |
|---|---|
| Tamanho | 13,1 MB no total: `.text` 7,2 MB, `.data` 2,3 MB, `.rodata` 0,76 MB |
| Dependências linkadas (`NEEDED`) | só `libQt6Pas.so.6`, `libX11.so.6` e `libc.so.6` |
| Drivers de banco | carregados em tempo de execução com `dlopen`/`dlsym` (strings `fbclient.so`, `libsybdb.so`, regex `^(gds32\|ibclient\|fbclient).*\.so`) |
| SSH | processo externo com `-o StrictHostKeyChecking=no -o ExitOnForwardFailure=yes -o ServerAliveInterval=60 …` e `sshpass` para senha |
| Configurações | `~/.config/heidisql/` (`settings.json`, `tabs.ini`, `Snippets/`) |
| Classes RTTI | cerca de 2.360 nomes de tipos `T*`. Confirmam a arquitetura do código-fonte (`TDBConnection`, `TMySQLConnection`, `TPgConnection`, `TSQLiteConnection`, `TSqlSrvConnection`, `TQueryThread`, `TSQLSentence`, `TJsonRegistry`) e mostram o uso do SQLDB do FPC (`TSQLConnection`, `TSQLDBLibraryLoader`) no caminho do MSSQL |
| Serviços remotos | `heidisql.com/savestats.php` (estatísticas de uso), verificação de atualização, `sqlformat.org` |

**Observado (execução, Arch Linux, Qt 6.11.2, Wayland, gerenciador de sessões aberto, sem conexão):**
| Métrica | Valor |
|---|---|
| RSS estável (a partir de ~2 s) | **~296 MB** (302.764 kB) |
| PSS | 270 MB |
| Memória anônima privada | **~226 MB** (231.316 kB) |
| Threads | 12 |
| Bibliotecas `.so` mapeadas | 113 (inclui `libsqlite3` sem nenhuma conexão SQLite aberta) |

**Inferência:** a maior parte da memória privada vem de estruturas criadas na inicialização (listas de ícones, catálogos `functions-*.ini` com ~1,1 MB de texto parseado, formulários LCL instanciados de forma antecipada). Isso não foi confirmado por descompilação. Mesmo assim, define a linha de base que o Mydbsql precisa superar com folga (§2.1).

**Efeito colateral das medições:** executar o binário criou `~/.config/heidisql/` (com `tabs.ini` vazio e `Snippets/`).

---

## 2. Leve, rápido e fácil de estender (requisitos de produto)

Estes requisitos **valem tanto quanto as funcionalidades**. Uma funcionalidade que estoura o orçamento abaixo não está pronta. Se um item de outra seção entrar em conflito com esta, esta seção vence, até que um ADR diga o contrário.

### 2.1 Orçamento (metas mensuráveis)

Condições de medição: build *release*, SSD, sem conexão aberta, salvo indicação. As metas valem **nas três plataformas**; a referência do HeidiSQL foi medida no Linux x86-64. As leituras de memória usam `IPlatform::processMemory()` (§2.6): `/proc/self/status` e `smaps_rollup` no Linux, `GetProcessMemoryInfo` no Windows e `task_info` no macOS.

| Métrica | HeidiSQL 12.21 (§1.5) | **Meta Mydbsql** | Como medir |
|---|---|---|---|
| Memória (RSS) ociosa com o gerenciador de sessões aberto | ~296 MB | **≤ 60 MB** | `--measure-startup` lê `/proc/self/status` |
| Memória anônima privada ociosa | ~226 MB | **≤ 25 MB** | `/proc/self/smaps_rollup` |
| Tempo até a primeira janela utilizável (*cold start*) | não medido | **≤ 300 ms** | `--measure-startup`: de `main()` ao primeiro *expose* da janela |
| Tempo até a janela principal após conectar (servidor local) | não medido | **≤ 200 ms** além da latência do servidor | log de tempo do `SessionService` |
| Bibliotecas carregadas na inicialização | 113 | **só Qt Core/Gui/Widgets + libc/libstdc++ + plugins Qt da plataforma**; nenhum driver de banco | `/proc/<pid>/maps` |
| Executável principal (stripped, sem as libs do Qt) | 13,1 MB | **≤ 6 MB** | `size` em CI |
| Trabalho bloqueante na thread da UI | — | **nenhuma tarefa > 16 ms**; zero I/O de rede ou disco | watchdog de debug (§2.3) |
| Rolagem da grade com 1 milhão de linhas carregadas | — | 60 fps, memória ≤ 1,5× o tamanho bruto dos dados | benchmark de UI |
| Árvore com 10 mil tabelas | — | carregar ≤ 500 ms; filtrar ≤ 50 ms | benchmark de integração |
| Executar um arquivo `.sql` de 1 GB | — | memória constante (streaming), ≤ 80 MB | teste de integração |

As metas são revisadas a cada fase (§5). Uma regressão maior que **10%** em relação a `bench/baseline.json` falha a CI.

### 2.2 Regras de leveza

1. **Tudo é preguiçoso (*lazy*).** Drivers, diálogos, editores de objeto, catálogos de funções SQL, ícones e traduções só são carregados quando usados pela primeira vez. Diálogos são destruídos ao fechar, não ficam escondidos.
2. **Drivers de banco são plugins carregados sob demanda** (`mydb_driver_mysql.so`, …). A libmariadb, a libpq e a sqlite3 só entram na memória na primeira conexão daquele tipo (o HeidiSQL já carrega a `libsqlite3` na inicialização).
3. **Dependências pesadas são proibidas:** QtWebEngine, QML/Qt Quick, Qt SQL, Boost (inteiro), Python embutido, frameworks de UI adicionais. **Toda dependência nova precisa de um ADR** com o custo medido: tamanho, tempo de inicialização e memória.
4. **Resultados em streaming:** `IResultSet` lê linha a linha. A grade usa `canFetchMore`/`fetchMore` e guarda as células num **buffer compacto por bloco** (uma arena contígua de bytes + vetor de offsets + bitmap de NULL). Nada de um `std::string` ou `QVariant` por célula.
5. **BLOB/TEXT grandes:** a grade guarda só um prefixo (padrão de 256 bytes). O valor completo é buscado sob demanda ao abrir o editor.
6. **Limites padrão:** a aba Data busca 1.000 linhas por página; a aba Query avisa antes de materializar mais de 100 mil linhas na grade (a exportação continua em streaming).
7. **Sem rede na inicialização.** Verificação de atualização e estatísticas são *opt-in*, adiadas e nunca bloqueiam.
8. **Liberar memória ao fechar** abas de query, resultados e conexões. Caches de metadados têm limite (LRU por banco).
9. **Recursos enxutos:** ícones em SVG monocromático dentro de um `.qrc` comprimido, renderizados e cacheados no tamanho exato pedido.

### 2.3 Regras de velocidade

1. **A thread da UI só desenha.** Toda query, introspecção, leitura de arquivo, exportação e importação roda num `ConnectionWorker` ou num pool de tarefas. Em builds de debug, um **watchdog** registra no log qualquer bloqueio do *event loop* maior que 16 ms.
2. **Tudo é cancelável** (`std::stop_token` + `IConnection::cancelCurrent()`).
3. **Introspecção em lote:** uma consulta por banco (`information_schema`/`pg_catalog`) traz colunas, índices e FKs de todas as tabelas; nunca uma consulta por tabela. O `CREATE` dos objetos é buscado em lote quando vários forem precisos (o HeidiSQL faz algo parecido com `PrefetchCreateCode`).
4. **Cache de metadados com invalidação seletiva:** um DDL executado invalida só os objetos afetados.
5. **Autocomplete** usa um índice em memória construído em segundo plano (vetor ordenado + busca por prefixo) e responde em ≤ 10 ms.
6. **Realce de sintaxe incremental** (lexer do QScintilla) e parse do SQL só na região visível ou alterada.
7. **Zero cópias desnecessárias:** `std::string_view`/`std::span` nas leituras, semântica de movimento, `reserve()` em laços conhecidos. A conversão para `QString` acontece só na hora de desenhar a célula.
8. **Build release:** `-O2`, LTO e `-fvisibility=hidden`; PGO opcional no pacote final.
9. **Medir antes de otimizar:** benchmarks em `bench/` (Google Benchmark) para o splitter, o buffer de resultados, os exportadores e o parse de metadados.

### 2.4 Extensibilidade: arquitetura de módulos

Objetivo: **adicionar uma funcionalidade = criar arquivos novos + uma linha de registro.** Sem editar `switch` central, `MainWindow` ou outros módulos.

**Pontos de extensão (registries)**, todos no `src/sdk/` (headers públicos e estáveis):

| Registry | O que registra | Exemplo |
|---|---|---|
| `DriverRegistry` | `IDriver` (SGBD) | MySQL, PostgreSQL, SQLite, MSSQL |
| `ExporterRegistry` | `IResultExporter` | CSV, JSON, Markdown, XLSX |
| `ImporterRegistry` | `IImporter` | CSV, JSON Lines, SQL |
| `ToolRegistry` | `ITool` (service + fábrica de view) | Manutenção, busca no servidor, gerador de dados |
| `ObjectEditorRegistry` | editor por tipo de objeto × driver | Table designer, editor de trigger |
| `CommandRegistry` | `ICommand` (ação + atalho + onde aparece no menu e na toolbar) | "Copiar como INSERT" |
| `SettingsPageRegistry` | página de preferências do módulo | Preferências do exportador XLSX |
| `FormatterRegistry` | `ISqlFormatter` | formatador local; formatador online opcional |

**Contrato de um módulo:**

```cpp
namespace mydb::sdk {

class ModuleContext {                  // acesso somente aos registries e serviços públicos
public:
    virtual DriverRegistry&   drivers()   = 0;
    virtual ExporterRegistry& exporters() = 0;
    virtual ToolRegistry&     tools()     = 0;
    virtual CommandRegistry&  commands()  = 0;
    // …
protected:
    ~ModuleContext() = default;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual std::string_view id() const = 0;          // "export.markdown"
    virtual Version apiVersion() const = 0;           // checado no carregamento
    virtual void registerInto(ModuleContext&) = 0;    // só registra fábricas; não cria nada pesado
};

} // namespace mydb::sdk

// Plugin dinâmico (drivers e módulos opcionais): um único símbolo exportado
extern "C" MYDB_EXPORT mydb::sdk::IModule* mydb_module_entry();
```

**Regras de módulo:**
- Um módulo depende só de `core/` e `sdk/`. **Nunca de outro módulo.** A comunicação entre módulos acontece por eventos (`EventBus`/sinais) ou por serviços expostos no `ModuleContext`.
- `registerInto` só registra **fábricas**. Objetos são criados na primeira utilização, o que mantém a leveza da §2.2.
- Duas formas de módulo:
  - **Estático:** compilado no executável e registrado em `app/BuiltinModules.cpp`. É o padrão para funcionalidades pequenas.
  - **Dinâmico:** `.so`/`.dll`/`.dylib` em `<prefix>/lib/mydbsql/modules/`, carregado sob demanda. É o padrão para **drivers** e funcionalidades grandes ou opcionais.
- Plugins dinâmicos exigem o mesmo compilador e a mesma versão do Qt do executável. O carregador verifica `apiVersion()` e recusa versões incompatíveis com uma mensagem clara.
- Cada módulo pode ser desligado em Preferências → Módulos. Um módulo desligado não é carregado e não custa memória.

**Receitas (checklists):**
- *Novo formato de exportação:* `src/modules/export_<fmt>/` com uma classe `IResultExporter` + registro + testes (golden files). Nenhum outro arquivo muda.
- *Novo SGBD:* `src/drivers/<sgbd>/` implementando `IDriver`, `IConnection`, `IResultSet`, `ISqlDialect` e `IMetadataProvider` (as interfaces opcionais, só se o SGBD suportar). Passar na suíte de contrato. Adicionar o serviço ao `tests/docker-compose.yml`.
- *Nova ferramenta:* `src/modules/tool_<nome>/` com `ITool` (service sem Qt + view Qt), comandos registrados no `CommandRegistry` e página de preferências opcional.
- *Nova ação de menu ou atalho:* uma classe `ICommand` + registro. A `MainWindow` monta menus e toolbars a partir do `CommandRegistry`, sem nenhum item fixo no código da janela.

### 2.5 Como garantir (processo)

- `mydbsql --measure-startup` imprime JSON com `startup_ms`, `rss_kb`, `anon_kb` e `libs_loaded` e encerra. É rodado em CI a cada PR (3 execuções, mediana).
- Benchmarks (`bench/`) rodam em CI e comparam com `bench/baseline.json`; uma regressão maior que 10% falha.
- Teste automático que confirma que **nenhum driver é carregado** antes da primeira conexão, em cada SO (`IPlatform::loadedLibraries()`: `/proc/self/maps` no Linux, `EnumProcessModules` no Windows, `_dyld_image_name` no macOS).
- Teste que confirma que nenhum módulo inclui headers de outro módulo (checagem de dependências no CMake: cada módulo só linka `mydb_core` e `mydb_sdk`).
- Em todo PR que adiciona dependência ou funcionalidade grande: anexar o antes e depois de `--measure-startup`.

### 2.6 Compatibilidade multiplataforma

**Matriz de suporte (obrigatória):**

| SO | Versões mínimas | Arquiteturas | Compilador | Sessão gráfica |
|---|---|---|---|---|
| **Linux** (plataforma principal) | glibc ≥ 2.31 (Ubuntu 20.04+, Debian 11+, Fedora atual, Arch) | x86-64, ARM64 | GCC ≥ 12 ou Clang ≥ 15 | **Wayland e X11** |
| **Windows** | 10 (1809+) e 11 | x86-64, ARM64 | MSVC 2022 (v143); Clang-cl opcional | — |
| **macOS** | 12 Monterey+ | Apple Silicon (arm64) e Intel (x86-64), binário universal | Apple Clang (Xcode 15+) | — |

Outros sistemas (FreeBSD, por exemplo) são "melhor esforço": devem compilar se as dependências existirem, mas não bloqueiam a CI.

**Regras de portabilidade:**
1. **Camada de plataforma única:** todo código dependente de SO fica em `src/infra/platform/`, atrás da interface `IPlatform`. Ela cobre caminhos de configuração, cache e dados (XDG no Linux, `%APPDATA%` no Windows, `~/Library/Application Support` no macOS), carregamento de bibliotecas dinâmicas (`dlopen`/`LoadLibraryW`), medição de memória, bibliotecas carregadas, abrir URL ou arquivo no app padrão, e diretório de plugins. Fora dessa pasta, `#ifdef` de SO é proibido (o clang-tidy e um grep na CI verificam).
2. **Caminhos:** sempre `std::filesystem::path`. Nunca concatene caminhos com `/` ou `\\` em string. No Windows, converta para UTF-16 só na fronteira com a API (`path::native()`).
3. **Texto:** UTF-8 em todo o core. Arquivos lidos ou gravados pelo app (SQL, CSV, exportações) têm encoding e fim de linha (LF/CRLF) configuráveis, com detecção de BOM na importação.
4. **Nomes de bibliotecas e plugins:** sufixo resolvido pelo `IPlatform` (`.so`, `.dll`, `.dylib`). Os drivers procuram a lib do cliente primeiro no diretório da aplicação e depois nos caminhos do sistema.
5. **Credenciais:** QtKeychain (libsecret/KWallet no Linux, Credential Manager no Windows, Keychain no macOS). Se o serviço não existir (ex.: Linux sem *secret service*), o app avisa e pergunta a senha a cada conexão; **nunca** grava em texto puro.
6. **SSH:** libssh2 embutido. Nenhuma dependência de `ssh`, `plink` ou `sshpass` externos, que o HeidiSQL usa (§1.5) e se comportam diferente em cada SO.
7. **UI nativa e HiDPI:** só widgets Qt padrão + estilo nativo de cada SO (Fusion como alternativa). Ícones SVG, escala fracionária (1,25×, 1,5×, 2×), modo escuro seguindo o sistema. Atalhos via `QKeySequence::StandardKey`, para que `Ctrl` vire `Cmd` no macOS. Menus seguem a convenção de cada SO (menu "Preferências" no app menu do macOS).
8. **Sistema de arquivos:** não presuma diferença ou igualdade entre maiúsculas e minúsculas em nomes de arquivo. Use `QSaveFile` ou a escrita atômica do `IPlatform` (no Windows, `MoveFileExW` com `REPLACE_EXISTING`).
9. **Processos e sinais:** nada de `fork`, `signal` ou `/proc` fora de `infra/platform/`. O cancelamento usa as APIs dos drivers (§4.5), não sinais.
10. **Build:** CMake + vcpkg com *triplets* por plataforma (`x64-linux`, `arm64-linux`, `x64-windows`, `arm64-windows`, `arm64-osx`, `x64-osx`). Nenhum script de build exclusivo de um SO, exceto o empacotamento.

**CI (GitHub Actions), em todo PR:**
- `ubuntu-22.04` (GCC e Clang, x86-64) + `ubuntu-24.04-arm` (ARM64); testes de UI com `QT_QPA_PLATFORM=offscreen`.
- `windows-latest` (MSVC, x86-64).
- `macos-latest` (arm64) + build universal no job de release.
- Testes de integração com bancos (docker-compose) rodam no Linux. No Windows e no macOS rodam os testes unitários, os de contrato com SQLite e os de UI.
- `--measure-startup` e os benchmarks rodam nas três plataformas, cada uma com sua `baseline.json`.

**Empacotamento por SO** (Fase 12): AppImage e Flatpak (universais), `.deb`, `.rpm` e PKGBUILD (AUR) no Linux; MSI (WiX) e versão portátil `.zip` no Windows; `.dmg` assinado e notarizado no macOS (universal). Todos incluem as libs de cliente dos bancos para funcionar sem instalação extra.

---

## 3. Stack técnica

| Área | Escolha | Motivo |
|---|---|---|
| Linguagem | C++20 | Conceitos, `std::span`, `std::format` (ou `fmt` como fallback), ranges, `std::jthread`/`stop_token` |
| Build | CMake ≥ 3.25 + Ninja, presets em `CMakePresets.json` | Padrão multiplataforma |
| Dependências | vcpkg (manifest `vcpkg.json`) | Reprodutível em Linux, Windows e macOS |
| UI | **Qt 6 Widgets** (`QTreeView`/`QTableView` + models próprios) | Maduro, multiplataforma; equivale à variante Qt6 do HeidiSQL |
| Editor SQL | **QScintilla** (ou KSyntaxHighlighting + QPlainTextEdit) | Realce, dobra, autocomplete |
| MySQL/MariaDB | libmariadb (Connector/C) | API C estável, múltiplos resultados, `mysql_kill`/thread id |
| PostgreSQL | libpq | Oficial; `PQcancel` para cancelar |
| SQLite | sqlite3 (SQLCipher opcional) | — |
| MSSQL / Firebird | FreeTDS (db-lib) / fbclient | Fase posterior |
| SSH | libssh2 (túnel embutido) | Elimina a dependência de plink/ssh externos |
| Segredos | QtKeychain | libsecret, macOS Keychain e Windows Credential Manager |
| Configuração | JSON (`nlohmann/json`) no diretório de config de cada SO (resolvido pelo `IPlatform`, §2.6) | Legível, versionável, portátil |
| Log | spdlog (modo assíncrono, nível `info` em release) | Rápido; não bloqueia a UI |
| Plugins dinâmicos | `DynamicLibrary` RAII próprio (dlopen/LoadLibrary) ou `QPluginLoader` só em `app/` | Drivers e módulos opcionais carregados sob demanda (§2.2) |
| XLSX | Escritor próprio mínimo (zlib/minizip + XML em streaming), como módulo dinâmico | Evita dependências grandes para um único formato |
| Benchmarks | Google Benchmark + `--measure-startup` | Garante o orçamento da §2.1 |
| Testes | GoogleTest + GoogleMock; Testcontainers ou docker-compose para integração | Mocks das interfaces (DIP facilita) |
| Qualidade | clang-format, clang-tidy, sanitizers (ASan/UBSan/TSan) em CI, cppcheck | — |
| CI | GitHub Actions: Linux (x86-64 + ARM64), Windows (MSVC), macOS (arm64/universal) | Compatibilidade garantida a cada PR (§2.6) |
| i18n | Qt Linguist (`.ts`/`.qm`) | — |

> As escolhas de UI (Qt 6) e de editor (QScintilla) são decisões de projeto, não observações do HeidiSQL. Para mudá-las, altere esta seção primeiro. Qualquer dependência nova passa pelo crivo da §2.2 (regra 3: ADR com custo medido).

---

## 4. Arquitetura do Mydbsql

### 4.1 Camadas

```
┌──────────────────────────────────────────────────────────────┐
│  app/        main(), composição (injeção de dependências)    │
├──────────────────────────────────────────────────────────────┤
│  ui/         Views Qt (widgets, diálogos) + Qt models         │
│              ↕ só via ViewModels / Presenters                │
├──────────────────────────────────────────────────────────────┤
│  services/   Casos de uso: SessionService, QueryService,     │
│              DataEditService, ExportService, DumpService,    │
│              ImportService, UserAdminService, ...            │
├──────────────────────────────────────────────────────────────┤
│  core/       Domínio e interfaces puras (sem Qt nem libs C): │
│              IConnection, IResultSet, ISqlDialect,           │
│              IMetadataProvider, modelos de schema, SqlSplitter│
├──────────────────────────────────────────────────────────────┤
│  drivers/    mysql/, postgres/, sqlite/, mssql/, firebird/   │
│              (implementam as interfaces do core)             │
│  infra/      settings, credenciais, ssh, log, filesystem     │
└──────────────────────────────────────────────────────────────┘
```

Regra de dependência: **as setas só apontam para baixo, até o `core`**. O `core` não depende de nada do projeto, nem de Qt. `drivers/` e `infra/` dependem só do `core`. `ui/` depende de `services/` e do `core`. Só `app/` conhece as implementações concretas.

### 4.2 Interfaces centrais (`core/`)

```cpp
namespace mydb::core {

enum class DriverKind { MySql, MariaDb, PostgreSql, Sqlite, MsSql, Firebird };

struct ConnectionParams {               // valor imutável; sem senha (fica em ICredentialStore)
    std::string sessionId, host, user, defaultDatabase;
    uint16_t port{};
    DriverKind driver{};
    std::optional<SslOptions> ssl;
    std::optional<SshTunnelOptions> ssh;
    std::chrono::seconds queryTimeout{0}, keepAlive{0};
    std::string startupScript, ignoreDatabasePattern;
    bool compressed{}, localTimeZone{};
};

class IResultSet {                      // leitura linha a linha (streaming)
public:
    virtual ~IResultSet() = default;
    virtual std::span<const ColumnInfo> columns() const = 0;
    virtual bool next() = 0;
    virtual bool isNull(std::size_t col) const = 0;
    virtual std::string_view text(std::size_t col) const = 0;
    virtual std::span<const std::byte> bytes(std::size_t col) const = 0;
};

struct ExecutionResult {
    std::vector<std::unique_ptr<IResultSet>> resultSets;   // múltiplos resultados
    int64_t rowsAffected{-1};
    uint32_t warnings{};
    std::chrono::microseconds serverTime{}, networkTime{};
};

class IConnection {
public:
    virtual ~IConnection() = default;
    virtual void open() = 0;
    virtual void close() noexcept = 0;
    virtual bool ping(bool reconnect) = 0;
    virtual ExecutionResult execute(std::string_view sql) = 0;
    virtual void cancelCurrent() = 0;                       // thread-safe
    virtual void useDatabase(std::string_view db) = 0;
    virtual ServerInfo serverInfo() const = 0;
    virtual const ISqlDialect& dialect() const = 0;
    virtual IMetadataProvider& metadata() = 0;
};

class ISqlDialect {                     // regras textuais do SGBD (sem I/O)
public:
    virtual ~ISqlDialect() = default;
    virtual std::string quoteIdentifier(std::string_view) const = 0;
    virtual std::string quoteString(std::string_view) const = 0;
    virtual std::string quoteBinary(std::span<const std::byte>) const = 0;
    virtual std::string applyLimit(std::string_view select, int64_t limit, int64_t offset) const = 0;
    virtual std::span<const DataType> dataTypes() const = 0;
    virtual bool supports(Feature) const = 0;               // equivale ao TFeatureOrRequirement
};

class IMetadataProvider {               // introspecção (information_schema, pg_catalog, sqlite_master…)
public:
    virtual ~IMetadataProvider() = default;
    virtual std::vector<std::string> databases() = 0;
    virtual std::vector<DbObject> objects(std::string_view db) = 0;
    virtual TableDefinition table(const ObjectRef&) = 0;    // colunas, índices, FKs, CHECKs
    virtual std::string createStatement(const ObjectRef&) = 0;
};

// Interfaces segregadas e opcionais. O driver só implementa o que o SGBD suporta (ISP).
class IDdlGenerator      { /* create/alter/drop table, view, rotina, trigger, event */ };
class IServerAdmin       { /* variables, status, processlist, kill, flush */ };
class IUserAdmin         { /* usuários, roles, grants */ };
class ITableMaintenance  { /* check/analyze/optimize/repair */ };
class IBulkLoader        { /* LOAD DATA / COPY FROM / insert em lote */ };

class IDriver {                         // fábrica por SGBD, registrada no DriverRegistry (OCP)
public:
    virtual ~IDriver() = default;
    virtual DriverKind kind() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::unique_ptr<IConnection> createConnection(const ConnectionParams&, ICredentialStore&) const = 0;
};

} // namespace mydb::core
```

Para descobrir capacidades opcionais, use um `capabilities()` que devolve ponteiros para as interfaces opcionais (ou `nullptr`). Não use `dynamic_cast` espalhado pela UI.

### 4.3 Como cada princípio SOLID se aplica

- **S (responsabilidade única).** Cada classe tem um motivo para mudar: `SqlSplitter` só divide scripts; `MySqlDialect` só formata SQL; `MySqlConnection` só faz I/O; `MetadataCache` só cuida de cache e invalidação; `CsvExporter` só escreve CSV.
- **O (aberto/fechado).** Para adicionar um SGBD, crie `drivers/<novo>/` como módulo que registra um `IDriver` (§2.4). Para adicionar um formato de exportação, crie uma nova classe `IResultExporter` e registre-a. Nenhum `switch` central é editado.
- **L (substituição de Liskov).** Qualquer `IConnection` precisa passar na mesma suíte de testes de contrato (`tests/contract/connection_contract_test.cpp`), parametrizada por driver. Se um SGBD não suporta algo, a capacidade fica ausente em vez de lançar `NotImplemented`.
- **I (segregação de interfaces).** O antigo `TSqlProvider` monolítico vira `ISqlDialect`, `IDdlGenerator`, `IServerAdmin`, `IUserAdmin`, `ITableMaintenance` e `IBulkLoader`. A UI de manutenção depende só de `ITableMaintenance`.
- **D (inversão de dependência).** Services recebem interfaces no construtor. A composição concreta fica em `app/CompositionRoot.cpp`. Nada de singletons globais (exceção tolerada: o logger).

### 4.4 Padrões de projeto usados

| Padrão | Onde |
|---|---|
| Abstract Factory / Registry | `IDriver` + `DriverRegistry`; `ExporterRegistry` |
| Strategy | Exportadores, estratégias de importação (servidor × cliente), geração de dados |
| Command | Ações da UI (`QAction` → `ICommand` no ViewModel); edições de grade com undo/redo |
| Unit of Work | `DataEditSession`: acumula alterações de linhas e gera um único INSERT/UPDATE/DELETE por linha usando a chave primária ou única (equivale a `TDBQuery.SaveModifications`/`GetWhereClause`) |
| Observer | Sinais de `SessionService` (conectou, mudou de banco, objetos alterados), equivalentes aos `OnConnected`/`OnDatabaseChanged` do HeidiSQL |
| Builder | `CreateTableBuilder`, `AlterTableBuilder` (diff entre `TableDefinition` antiga e nova → DDL) |
| Decorator | `LoggingConnection`, `TimingConnection` envolvendo uma `IConnection` |
| Proxy / Cache | `CachedMetadataProvider` envolvendo `IMetadataProvider` |
| Template Method (com moderação) | Base de exportador de texto tabular (cabeçalho, linhas, rodapé) |
| MVVM / MVP | UI Qt |

### 4.5 Concorrência

- Cada sessão aberta tem **um worker thread** (`ConnectionWorker`) que serializa o uso da `IConnection`. As libs C não são thread-safe por handle.
- A UI nunca bloqueia: `QueryService::executeAsync(batch) → Task` emite progresso por sinal (query i/n, linhas, tempo).
- Cancelamento: `std::stop_token` + `IConnection::cancelCurrent()`, que abre uma conexão auxiliar com `KILL QUERY` no MySQL, usa `PQcancel` no PostgreSQL e `sqlite3_interrupt` no SQLite.
- O keep-alive é um timer que agenda `ping()` no worker, nunca na thread da UI.

### 4.6 Estrutura de diretórios

```
Mydbsql/
├── CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  .clang-tidy
├── CLAUDE.md  README.md  LICENSE
├── cmake/                         # módulos auxiliares (warnings, sanitizers)
├── src/
│   ├── core/                      # mydb_core (biblioteca estática, sem Qt)
│   │   ├── model/                 # DbObject, TableDefinition, Column, Index, ForeignKey…
│   │   ├── sql/                   # SqlSplitter, SqlTokenizer, SqlFormatter
│   │   ├── interfaces/            # IConnection, IResultSet, ISqlDialect, …
│   │   └── errors/                # DbError (código, SQLSTATE, mensagem, dica)
│   ├── sdk/                       # mydb_sdk: IModule, ModuleContext, registries (API pública estável, §2.4)
│   ├── drivers/                   # cada um vira um plugin dinâmico mydb_driver_<x> (carregado sob demanda)
│   │   ├── mysql/  postgres/  sqlite/  mssql/  firebird/
│   ├── modules/                   # funcionalidades plugáveis: export_csv/, export_xlsx/, tool_datagen/, …
│   ├── infra/
│   │   ├── platform/  settings/  credentials/  ssh/  logging/  files/   # platform/ = único lugar com código por SO
│   ├── services/
│   │   ├── session/  query/  dataedit/  export/  dump/  import/  admin/  search/  datagen/
│   ├── ui/
│   │   ├── main/                  # MainWindow, árvore de objetos, abas
│   │   ├── models/                # QAbstractItemModel: ResultSetModel, ObjectTreeModel
│   │   ├── editors/               # SqlEditor, TableDesigner, RoutineEditor, …
│   │   ├── dialogs/               # SessionManager, Preferences, Export, Import, …
│   │   └── delegates/             # editores in-place (Hex, DateTime, Enum, Set)
│   └── app/                       # main.cpp, CompositionRoot, BuiltinModules, ModuleLoader
├── resources/                     # ícones SVG próprios/livres, .qrc, traduções, snippets
├── bench/                         # Google Benchmark + baseline.json (§2.5)
├── tests/
│   ├── unit/  contract/  integration/  ui/
│   └── docker-compose.yml         # mysql, mariadb, postgres para testes de integração
└── docs/                          # ADRs (docs/adr/NNNN-titulo.md), diagramas
```

---

## 5. Plano de implementação

Cada fase termina com **build verde em CI nas 3 plataformas, testes passando, orçamento da §2.1 respeitado e um incremento utilizável**. Marque os itens concluídos (`[x]`) neste arquivo.

A partir da Fase 1, toda funcionalidade entra como **módulo** (§2.4). Se uma tarefa exigir editar `MainWindow` ou outro módulo para funcionar, falta um ponto de extensão: crie-o primeiro.

### Fase 0 — Fundação (infra do projeto)
- [ ] CMake + presets (`debug`, `release`, `asan`, `tsan`); vcpkg manifest; alvo `mydb_core` (lib), `mydbsql` (exe), `mydb_tests`.
- [ ] `.clang-format`, `.clang-tidy` (checks `modernize-*`, `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `readability-*`).
- [ ] GitHub Actions com a matriz da §2.6 (Linux x86-64/ARM64, Windows, macOS): build + testes + clang-tidy + sanitizers.
- [ ] `infra/platform/` com `IPlatform` e as três implementações (Linux, Windows, macOS): caminhos, libs dinâmicas, memória, bibliotecas carregadas, abrir URL; checagem de CI que proíbe `#ifdef` de SO fora dessa pasta.
- [ ] spdlog configurado; `DbError` com hierarquia de exceções (`ConnectionError`, `QueryError`, `CancelledError`).
- [ ] ADR-0001: arquitetura em camadas; ADR-0002: Qt 6 Widgets; ADR-0003: drivers nativos sem QtSql; ADR-0004: módulos e plugins (§2.4); ADR-0005: orçamento de desempenho (§2.1).
- [ ] Flag `--measure-startup` (JSON com `startup_ms`, `rss_kb`, `anon_kb`, `libs_loaded`) + job de CI que compara com `bench/baseline.json`.
- [ ] Google Benchmark configurado em `bench/`; watchdog do *event loop* (> 16 ms) em builds de debug.
- [ ] Release com LTO e `-fvisibility=hidden`.
- **Pronto quando:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` passa, a janela vazia abre e `--measure-startup` mostra ≤ 150 ms e ≤ 40 MB de RSS **nas três plataformas**.

### Fase 1 — Núcleo de domínio e SQL (`core/`)
- [ ] Modelos: `DbObject` (tipo, nome, banco, linhas, tamanho, datas), `Column`, `DataType` (+ categorias inteiro, real, texto, binário, temporal, espacial, outros), `Index`, `ForeignKey`, `CheckConstraint`, `TableDefinition`, `RoutineParam`.
- [ ] Interfaces da §4.2.
- [ ] `SqlTokenizer` + `SqlSplitter`: respeita strings, identificadores entre aspas, comentários `--`, `#`, `/* */`, `DELIMITER`, `$$`/`$tag$` do PostgreSQL e blocos `BEGIN…END`. Dá a posição de cada sentença (para "executar query atual").
- [ ] Testes unitários extensos do splitter (é a peça mais propensa a bugs). Benchmark: dividir 100 MB de SQL em streaming com memória constante.
- [ ] `sdk/`: `IModule`, `ModuleContext`, registries (§2.4); `ModuleLoader` (estático + dinâmico, checagem de `apiVersion`); `EventBus`.
- [ ] Checagem de dependências no CMake: módulos só linkam `mydb_core` + `mydb_sdk`.
- **Pronto quando:** cobertura ≥ 90% em `core/sql` e um módulo de exemplo (`modules/example_hello`) é carregado dinamicamente sem mudanças fora da pasta dele.

### Fase 2 — Primeiro driver: MySQL/MariaDB
- [ ] Wrapper RAII para `MYSQL*` e `MYSQL_RES*`.
- [ ] `MySqlConnection` (open, SSL, compressão, timeout, `CLIENT_MULTI_STATEMENTS`, múltiplos resultados, `affected_rows`, `warning_count`, thread id, cancel via `KILL QUERY`).
- [ ] `MySqlDialect` (crases, escape, `LIMIT`, tipos de dados MySQL e MariaDB, features por versão).
- [ ] `MySqlMetadataProvider` via `information_schema` + `SHOW CREATE …`.
- [ ] Suíte de **testes de contrato** de `IConnection` rodando contra MySQL 8 e MariaDB 11 (docker-compose).
- [ ] Empacotado como plugin dinâmico `mydb_driver_mysql`; teste confirma que a libmariadb **não** está mapeada antes da primeira conexão.
- **Pronto quando:** conecta, lista bancos e objetos, executa query e lê resultados nos testes de integração, com o driver carregado sob demanda.

### Fase 3 — Sessões, configurações e credenciais
- [ ] `ISettingsStore` (JSON) com migração por versão de esquema.
- [ ] `SessionRepository`: CRUD de sessões em árvore com pastas, cor, comentário, estatísticas; exportar e importar.
- [ ] `ICredentialStore` (QtKeychain) + fallback em memória para testes.
- [ ] UI: **Gerenciador de sessões** (árvore + abas Configurações, SSH, SSL, Avançado), testar conexão.
- **Pronto quando:** dá para criar uma sessão MySQL, salvar, reabrir o app e conectar sem digitar a senha de novo.

### Fase 4 — Janela principal e navegação
- [ ] `MainWindow`: árvore de objetos à esquerda, abas à direita, log embaixo (dock). Menus e toolbars são montados a partir do `CommandRegistry`, sem itens fixos no código da janela.
- [ ] `ObjectTreeModel` com carregamento lazy, várias conexões, filtros de banco e objeto, favoritos, agrupamento e tamanhos.
- [ ] `CachedMetadataProvider` com invalidação por eventos (DDL executado, refresh).
- [ ] `ConnectionWorker` + `SessionService` (sinais conectou, banco mudou, objetos mudaram).
- [ ] Aba **Host**: bancos, variáveis (editáveis), status, processlist (kill, auto-refresh), estatísticas de comandos (via `IServerAdmin`).
- [ ] Aba **Database**: lista de objetos com metadados.
- [ ] Painel de log com categorias e filtro (buffer circular com limite de linhas).
- **Pronto quando:** árvore com 10 mil tabelas carrega em ≤ 500 ms e filtra em ≤ 50 ms; a UI nunca congela durante a introspecção.

### Fase 5 — Editor de query
- [ ] `SqlEditor` (QScintilla): realce por dialeto, dobra de código, números de linha, localizar/substituir, comentar/descomentar, mover linhas, word wrap.
- [ ] Autocomplete: palavras-chave, funções (catálogo próprio por dialeto, em JSON em `resources/`), bancos, tabelas, colunas e aliases.
- [ ] Execução: tudo, seleção, query atual; lote de uma vez × query a query; parar em erro; cancelar; EXPLAIN.
- [ ] Múltiplas abas de query (renomear, fechar todas, restaurar ao reabrir); várias abas de resultado por execução.
- [ ] Snippets, salvar/carregar `.sql`, rodar arquivo SQL grande em streaming, reformatar SQL, ajuda SQL contextual.
- [ ] Métricas exibidas: linhas, tempo do servidor e tempo de rede, warnings.
- [ ] **Histórico de queries** persistente (SQLite local ou JSON Lines), com retenção em dias, busca e "limpar histórico" (encontrado no binário, §1.5).
- [ ] Histórico de localizar/substituir; esquemas de cores do editor.
- [ ] Módulos opcionais: pesquisa na web do termo selecionado (motor configurável) e formatador online (*opt-in*; o padrão é o formatador local).
- **Pronto quando:** autocomplete responde em ≤ 10 ms e digitar num arquivo de 50 mil linhas não engasga.

### Fase 6 — Grade de dados e edição
- [ ] `ResultBuffer` compacto (arena de bytes por bloco + offsets + bitmap de NULL; prefixo de BLOB/TEXT), com benchmarks.
- [ ] `ResultSetModel` (`QAbstractTableModel`) sobre o `ResultBuffer`, com `canFetchMore`/`fetchMore`, paginação ("próximas N", "todas") e busca incremental.
- [ ] Aba **Data**: filtro WHERE com autocomplete, filtros rápidos (valor, clipboard, NULL/NOT NULL), ordenação multi-coluna, seleção de colunas, preview de BLOB/imagem.
- [ ] Delegates: Hex/BLOB, DateTime, Enum, Set, texto longo (editor modal), valor de função (`NOW()` etc.), NULL explícito.
- [ ] `DataEditSession` (Unit of Work): inserir, duplicar (com ou sem chaves), excluir, salvar, cancelar; WHERE gerado pela chave primária/única. Se não houver chave, a grade fica somente leitura e mostra o aviso.
- [ ] Seguir FK, abrir URL, copiar formatado ou como nomes de colunas, salvar BLOB em arquivo, timestamp UNIX como data.
- **Pronto quando:** 1 milhão de linhas rolam a 60 fps, com memória ≤ 1,5× o tamanho bruto dos dados.

### Fase 7 — Designers de objetos (DDL)
- [ ] **Table designer**: colunas (tipo, tamanho, unsigned, nulo, default (texto, NULL, auto-inc, expressão), comentário, collation, virtual, invisível, SRID), índices, FKs, CHECKs, opções (engine, collation, auto-inc, comentário); preview do DDL.
- [ ] `AlterTableBuilder`: diff entre `TableDefinition`s → `ALTER TABLE` mínimo (reordenar, renomear, alterar).
- [ ] Editores de View, Procedure/Function (parâmetros IN/OUT/INOUT, rodar rotina), Trigger e Event.
- [ ] Criar e dropar banco; esvaziar e dropar objetos em massa (ordem segura de drop por dependência).

### Fase 8 — PostgreSQL e SQLite
- [ ] `PgConnection` (libpq), `PgDialect` (aspas duplas, `$$`, schemas, sequences, tipos enum nomeados), metadados via `pg_catalog`/`information_schema`, cancelamento via `PQcancel`.
- [ ] `SqliteConnection`, `SqliteDialect`, metadados via `sqlite_master`/`pragma_*`; attach/detach; SQLCipher opcional.
- [ ] Rodar a suíte de contrato nos três drivers. Corrija as abstrações que vazarem (sinal de violação de LSP/ISP).

### Fase 9 — Importação e exportação
- [ ] Cada formato é um **módulo** (`modules/export_<fmt>/`) que registra um `IResultExporter`: CSV, JSON, JSON Lines, Markdown, HTML, XML, SQL (INSERT, INSERT IGNORE, REPLACE, DELETE+INSERT, UPDATE), LaTeX, Textile, Jira, array PHP, Excel (XLSX com escritor próprio em streaming, módulo dinâmico). Escopo: seleção ou todas as linhas, coluna focada; destino arquivo (com abrir após criar) ou clipboard.
- [ ] **Dump SQL** (`DumpService`): estrutura e/ou dados, DROP/CREATE, INSERTs em lote respeitando `max_allowed_packet`; destino arquivo, ZIP, diretório (um arquivo por objeto), clipboard, outro banco ou outro servidor. Streaming, sem carregar tudo em memória.
- [ ] **Importar CSV**: detecção de layout (separador, aspas, cabeçalho, encoding), estratégia servidor (`LOAD DATA LOCAL`/`COPY`) ou cliente (INSERT em lote), mapeamento de colunas.
- [ ] Inserir arquivos em colunas BLOB; copiar tabela (estrutura ± dados ± filtro WHERE).

### Fase 10 — Ferramentas de manutenção e administração
- [ ] Manutenção de tabelas (`ITableMaintenance`).
- [ ] Busca de texto em todo o servidor ou bancos selecionados (tipos de coluna filtráveis).
- [ ] Edição em massa de tabelas.
- [ ] Gerador de dados de teste (Strategy por tipo de dado; respeita FKs e unicidade).
- [ ] Gerenciador de usuários MySQL (privilégios globais, de banco, tabela e coluna; limites de recursos; plugin de autenticação; roles) e de roles PostgreSQL.
- [ ] Trocar senha; FLUSH *.

### Fase 11 — Rede segura
- [ ] `ITunnel` + `Ssh2Tunnel` (libssh2): senha ou chave, porta local automática, timeout, verificação de host key (known_hosts próprio, com confirmação do usuário).
- [ ] SSL/TLS completo (modo de verificação, CA, cert, chave, cipher).
- [ ] Keep-alive e reconexão automática configuráveis.

### Fase 12 — Polimento e distribuição
- [ ] Preferências (fontes, cores, limites, atalhos, formatos de data e número); tema claro/escuro.
- [ ] i18n (pt_BR e en_US primeiro).
- [ ] Impressão de listas; relatório de crash; verificação de atualização (opcional, opt-in).
- [ ] MSSQL (FreeTDS) e Firebird (fbclient) como drivers extras.
- [ ] Empacotamento: AppImage/Flatpak, `.deb`, `.rpm`, PKGBUILD (AUR); MSI (WiX) + `.zip` portátil no Windows; `.dmg` universal assinado e notarizado no macOS (§2.6).
- [ ] Documentação de usuário.

### Ordem do MVP
Fases **0 → 6** com MySQL/MariaDB entregam um cliente útil: conectar, navegar, consultar e editar dados. As fases 7–12 vêm depois, e a ordem entre elas pode mudar conforme a demanda.

---

## 6. Convenções de código

- **Namespaces:** `mydb::core`, `mydb::drivers::mysql`, `mydb::services`, `mydb::ui`, `mydb::infra`.
- **Nomes:** classes e structs `PascalCase`; funções e variáveis `camelCase`; membros privados `m_` + camelCase; constantes `kPascalCase`; interfaces com prefixo `I` (`IConnection`); arquivos `PascalCase.hpp/.cpp`, um tipo público por arquivo.
- **Headers:** `#pragma once`; inclua o que usar; prefira forward declarations em headers.
- **Erros:** exceções para falhas (hierarquia `DbError`); `std::expected` (C++23) ou `tl::expected` só em fronteiras onde a falha é esperada (ex.: parse). Destrutores e `close()` são `noexcept`.
- **Strings:** UTF-8 em `std::string` no core; converta para `QString` só na camada `ui/`.
- **Const-correctness**, `[[nodiscard]]` em funções que retornam recursos ou resultados, `override` sempre, `final` em classes folha.
- **Sem herança de implementação profunda:** prefira composição; hierarquias de no máximo 2 níveis abaixo de uma interface.
- **SQL nunca é montado com concatenação de valores do usuário** sem passar por `ISqlDialect::quote*` (ou prepared statements quando o driver suportar).
- **Comentários:** explique o *porquê*, não o *o quê*. Doxygen (`///`) só em interfaces públicas do core.
- **Qt:** só em `ui/`, `app/` e, se necessário, em adaptadores de `infra/`. `core/` e `drivers/` não incluem headers Qt.

## 7. Comandos

```bash
# configurar e compilar
cmake --preset debug
cmake --build --preset debug

# testes unitários
ctest --preset debug -L unit

# testes de integração (sobe MySQL, MariaDB e PostgreSQL)
docker compose -f tests/docker-compose.yml up -d
ctest --preset debug -L integration

# análise estática e formatação
cmake --build --preset debug --target clang-tidy
cmake --build --preset debug --target format
```

> Esses comandos descrevem o alvo da Fase 0. Atualize-os se os presets ou os alvos mudarem.

## 8. Processo de trabalho

1. Pegue o próximo item não marcado da fase atual (§5).
2. Se a mudança afetar a arquitetura, escreva ou atualize um ADR em `docs/adr/`.
3. Escreva os testes primeiro para a lógica do core e dos services (TDD encorajado).
4. Implemente respeitando as camadas (§4.1) e SOLID (§4.3).
5. Rode build, testes, clang-tidy e clang-format localmente. Se a mudança afeta inicialização, memória ou um caminho quente, rode `mydbsql --measure-startup` e os benchmarks de `bench/` e anexe o antes e depois ao PR (§2.5).
6. Commits pequenos, em Conventional Commits (`feat(query): …`, `fix(mysql): …`, `refactor(core): …`, `test: …`, `docs: …`).
7. Marque o item como concluído aqui no CLAUDE.md, no mesmo PR.

## 9. Referência rápida HeidiSQL → Mydbsql

| HeidiSQL | Mydbsql |
|---|---|
| `TConnectionParameters` | `core::ConnectionParams` + `services::SessionRepository` |
| `TDBConnection` (+ subclasses) | `core::IConnection` + `drivers::*::XxxConnection` |
| `TDBQuery` (leitura + edição) | `core::IResultSet` (leitura) + `services::DataEditSession` (edição) |
| `TSqlProvider` / `TQueryId` | `ISqlDialect`, `IDdlGenerator`, `IServerAdmin`, `IUserAdmin`, … |
| `TDbLib` (carregamento dinâmico) | Linkagem via vcpkg; opcionalmente `DynamicLibrary` RAII para drivers plugáveis |
| `TDBObject` / `TTableColumn` / `TTableKey` / `TForeignKey` | `core::model::DbObject` / `Column` / `Index` / `ForeignKey` |
| `TDatabaseCache`, `TColumnCache`… | `CachedMetadataProvider` |
| `TSQLBatch` / `TSQLSentence` | `core::sql::SqlSplitter` / `SqlStatement` |
| `TQueryThread` | `services::ConnectionWorker` + `QueryTask` |
| `TSecureShellCmd` (plink/ssh externo) | `infra::ssh::Ssh2Tunnel` (libssh2) |
| `TAppSettings` / `jsonregistry` | `infra::settings::JsonSettingsStore` (`ISettingsStore`) |
| `TGridExportFormat` (enum + case) | `services::export::IResultExporter` + `ExporterRegistry` |
| `TToolMode` (tabletools) | Módulos separados (`ITool`): manutenção, busca no servidor, dump, edição em massa e gerador de dados |
| `grideditlinks` (`T*EditorLink`) | `ui::delegates::*Delegate` (`QStyledItemDelegate`) |
| `main.pas` | `MainWindow` (view) + `MainViewModel` + services; menus vindos do `CommandRegistry` |
| `TQueryHistory` (só visto no binário) | `services::query::QueryHistoryStore` |
| Tudo compilado num executável de 13 MB | Núcleo + `sdk/` + módulos e drivers plugáveis (§2.4) |
