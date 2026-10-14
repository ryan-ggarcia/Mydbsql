# ADR-0001: Arquitetura em camadas

- **Status:** aceito
- **Data:** 2026-10-14

## Contexto

O HeidiSQL concentra UI, estado e lógica de banco em poucos arquivos enormes: `main.pas` tem cerca de 16 mil linhas e `TDBConnection` acumula dezenas de responsabilidades (CLAUDE.md §1.2 e §1.4). Isso dificulta testar, portar e estender o programa. O Mydbsql precisa ser fácil de estender (objetivo 3) e testável sem banco nem interface gráfica.

## Decisão

O código é dividido nas camadas `core`, `sdk`, `drivers`, `infra`, `services`, `ui`, `modules` e `app`, com as regras da CLAUDE.md §4.1:

- As dependências só apontam para baixo. O `core` não depende de nada do projeto, nem do Qt.
- `drivers/` e `infra/` dependem só do `core`; `ui/` depende de `services/` e do `core`.
- Só `app/` (a raiz de composição) conhece as implementações concretas. Os services recebem interfaces no construtor (DIP).
- Cada camada vira um alvo CMake próprio (`mydb_core`, `mydb_platform`, `mydb_logging`, `mydb_ui`…). O grafo de `target_link_libraries` faz valer as regras: uma inclusão proibida não compila.

## Consequências

- O core e os services são testados com GoogleTest e mocks, sem Qt nem banco.
- Mais alvos e mais arquivos do que num projeto monolítico. Esse custo é aceito em troca do isolamento.
- Código específico de SO fica restrito a `src/infra/platform/` (§2.6, regra 1). A checagem automática está em `tools/check_platform_ifdefs.py` e roda como teste do CTest e na CI.
