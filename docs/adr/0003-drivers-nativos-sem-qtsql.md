# ADR-0003: Drivers nativos, sem Qt SQL

- **Status:** aceito
- **Data:** 2026-10-14

## Contexto

Um cliente de banco precisa de recursos que as camadas genéricas escondem: múltiplos resultados por execução, `affected_rows` e `warning_count`, cancelamento (`KILL QUERY`, `PQcancel`, `sqlite3_interrupt`), leitura em streaming, metadados específicos e o thread id da conexão. O Qt SQL entrega uma API mínima comum, converte cada célula para `QVariant` e prende o core ao Qt, o que viola a §4.1.

## Decisão

- Cada SGBD tem um driver próprio em `src/drivers/<sgbd>/` sobre a API C oficial (libmariadb, libpq, sqlite3; depois FreeTDS e fbclient). Ele implementa as interfaces do core (`IConnection`, `IResultSet`, `ISqlDialect`, `IMetadataProvider` e as interfaces opcionais).
- Os handles C ficam em wrappers RAII com deleter próprio (§0).
- Cada driver é um **plugin dinâmico** carregado na primeira conexão daquele tipo (§2.2, regra 2), usando `infra::platform::DynamicLibrary`. O teste `DynamicLibraryTest.LoadsResolvesAndUnloadsOnDemand` já prova o mecanismo com um plugin de teste: a biblioteca não aparece em `IPlatform::loadedLibraries()` antes de ser aberta.

## Consequências

- Mais código por SGBD, mas sob controle total. A suíte de contrato (§4.3, L) garante comportamento igual entre os drivers.
- Nenhuma biblioteca de banco é carregada na inicialização. O HeidiSQL no Linux carrega a `libsqlite3` sem nenhuma conexão aberta (§1.5).
