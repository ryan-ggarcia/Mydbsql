# ADR-0007: Drivers identificados por texto, não por enum

- **Status:** aceito
- **Data:** 2026-10-15

## Contexto

O esboço da CLAUDE.md §4.2 identificava o SGBD por um `enum class DriverKind { MySql, MariaDb, PostgreSql, … }`, usado em `ConnectionParams::driver` e em `IDriver::kind()`. Um enum fechado contraria a §2.4 ("adicionar uma funcionalidade = criar arquivos novos + uma linha de registro") e o OCP: um SGBD novo exigiria editar o enum no `core` e recompilar tudo que depende dele, inclusive os módulos dinâmicos já distribuídos.

## Decisão

- `IDriver::id()` devolve um texto estável (`"mysql"`, `"postgresql"`, `"sqlite"`), que também é a chave no `DriverRegistry`.
- `ConnectionParams::driverId` guarda esse texto. As sessões salvas (Fase 3) persistem o texto.
- Diferenças entre variantes do mesmo protocolo (MySQL × MariaDB, PostgreSQL × Redshift) ficam no `ServerInfo` e no `ISqlDialect::supports(Feature)`, e não no id do driver.

## Consequências

- Um driver novo entra como módulo, sem tocar no `core`.
- Um id digitado errado só é detectado em tempo de execução (`RegistryError`). As sessões salvas validam o id ao carregar.
