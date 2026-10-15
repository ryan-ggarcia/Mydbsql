# ADR-0008: Execução em streaming (IExecution)

- **Status:** aceito (decisão do usuário em 2026-10-15)
- **Data:** 2026-10-15

## Contexto

A CLAUDE.md tinha um conflito. A §4.2 definia `IConnection::execute()` devolvendo um `ExecutionResult` com todos os resultados do lote de uma vez (`std::vector<std::unique_ptr<IResultSet>>`). A §2.2, regra 4, exige leitura linha a linha. No MySQL, o streaming real (`mysql_use_result`) só deixa um resultado aberto por vez: o próximo só existe depois que o atual foi lido ou descartado. Juntar tudo num vetor obrigaria a usar `mysql_store_result` e guardar cada resultado inteiro na memória do cliente.

## Decisão

- `IConnection::execute(sql)` devolve `std::unique_ptr<IExecution>`, posicionada antes do primeiro resultado.
- `IExecution::nextResult()` avança para o resultado da próxima sentença; `resultSet()` dá as linhas (ou `nullptr` para INSERT, UPDATE, DDL), e `rowsAffected()`, `warnings()` e `timing()` dão os dados daquela sentença.
- Nada é lido do servidor antes de ser pedido. Destruir a execução antes do fim descarta o resto e deixa a conexão pronta para a próxima; uma execução nova descarta a anterior.
- Um erro numa sentença do meio do lote é lançado por `nextResult()`, e as seguintes não são executadas.

## Consequências

- Um SELECT de milhões de linhas ocupa memória constante no cliente. A grade da Fase 6 lê só o que precisa mostrar.
- Quem usa a API precisa consumir os resultados em ordem. A suíte de contrato cobre lotes, abandono no meio e erros no meio do lote.
- A §4.2 da CLAUDE.md foi atualizada.
