# ADR-0013: Editor SQL próprio sobre QPlainTextEdit

- **Status:** aceito (decisão do usuário em 2026-10-21)
- **Data:** 2026-10-21

## Contexto

O plano (§3) indicava o QScintilla para o editor da Fase 5. Dois problemas apareceram ao preparar a fase:

- O QScintilla é GPL-3.0 (ou licença comercial). Usá-lo obrigaria o Mydbsql a ser GPL-3, e o projeto ainda não escolheu licença.
- O pacote `qscintilla` do vcpkg depende do `qtbase` do vcpkg, ou seja, compila o Qt inteiro, o que o ADR-0006 evitou. Usá-lo com os binários oficiais exigiria compilá-lo do fonte nas três plataformas.

## Decisão

- O editor é um `QPlainTextEdit` nosso (`ui/editors/SqlEditor`), com realce por `QSyntaxHighlighter` sobre o `core::sql::SqlTokenizer` já existente (o estado de comentários e strings abertos passa de um bloco para o outro), números de linha, dobra, localizar/substituir, comentar e mover linhas.
- O autocomplete usa um índice em memória nos services (`services/query/CompletionIndex`) e um popup Qt.
- Nenhuma dependência nova.

## Consequências

- Mais código nosso (dobra e números de linha não vêm prontos), mas sem restrição de licença e sem custo de build ou de memória extra.
- O realce reaproveita o mesmo analisador léxico do `SqlSplitter`: o que o editor colore é o que o app executa.
- Junto com esta decisão, quatro itens da Fase 5 foram adiados para a Fase 12: esquemas de cores do editor, ajuda SQL contextual, pesquisa na web e formatador online.
