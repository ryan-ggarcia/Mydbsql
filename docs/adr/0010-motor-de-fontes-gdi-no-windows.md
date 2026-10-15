# ADR-0010: Motor de fontes GDI por padrão no Windows

- **Status:** aceito (decisão do usuário em 2026-10-15)
- **Data:** 2026-10-15

## Contexto

Na Fase 3, a primeira janela passou a ser o gerenciador de sessões, com texto de verdade. No Windows, a inicialização foi de ~67 ms (janela vazia da Fase 0) para ~175 ms e ~42,9 MB, acima do orçamento da Fase 0 (≤ 150 ms, ≤ 40 MB). A medição por etapas mostrou que quase todo o custo vem de criar o primeiro widget com texto: o Qt carrega o banco de fontes pelo DirectWrite, que enumera todas as fontes instaladas (386 na máquina de desenvolvimento).

| Motor de fontes (release, mediana) | Início | Working set |
|---|---|---|
| DirectWrite (padrão do Qt 6) | ~175 ms | ~42,9 MB |
| GDI (`windows:fontengine=gdi`) | ~68 ms | ~40,5 MB |

## Decisão

- No Windows, o app define `QT_QPA_PLATFORM=windows:fontengine=gdi` antes de criar o `QApplication`, só quando o usuário não definiu `QT_QPA_PLATFORM` e não passou `-platform`.
- O valor vem do CMake (`MYDB_DEFAULT_QPA_PLATFORM`), sem `#ifdef` de SO no código (§2.6, regra 1). Nos outros SOs o valor é vazio e nada muda.

## Consequências

- O Windows volta para dentro do orçamento.
- O GDI não desenha emoji colorido nem fontes variáveis. Para um cliente de banco de dados isso não faz diferença, e o texto continua com ClearType.
- Quem preferir o DirectWrite pode iniciar com `QT_QPA_PLATFORM=windows` ou `-platform windows`.
- Os baselines de `bench/baseline/` foram medidos com a janela vazia; com o gerenciador de sessões, a memória privada sobe ~18%. Eles serão regenerados a partir da CI.
