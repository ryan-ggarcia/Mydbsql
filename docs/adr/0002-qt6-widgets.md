# ADR-0002: Qt 6 Widgets como toolkit de UI

- **Status:** aceito
- **Data:** 2026-10-14

## Contexto

O Mydbsql precisa de UI nativa em Linux (Wayland e X11), Windows e macOS, com grades e árvores que aguentem milhões de linhas e 10 mil tabelas (§2.1). A engenharia reversa mostrou que o toolkit pesa tanto quanto o app: o HeidiSQL 12.21 no Linux (Lazarus sobre Qt6Pas) usa ~296 MB de RSS ociosos, e o mesmo programa no Windows (VCL) usa ~47,6 MB (§1.5).

## Decisão

- Usar **Qt 6 Widgets** (`QTreeView`/`QTableView` com models próprios), sem QML/Qt Quick, sem QtWebEngine e sem Qt SQL (§2.2, regra 3).
- O Qt só entra em `ui/`, `app/` e, se necessário, em adaptadores de `infra/` (§6).
- O custo de uma janela Qt 6 Widgets vazia é medido desde a Fase 0 pelo `--measure-startup` e conferido na CI contra `bench/budget.json`.

## Alternativas consideradas

- **wxWidgets:** nativo e leve, mas com modelo de dados de grade mais limitado e pouca integração com HiDPI e modo escuro no Linux.
- **Dear ImGui:** leve, mas não é UI nativa (acessibilidade, IME, menus do SO).
- **GTK:** experiência ruim fora do Linux.

## Consequências

- Uma dependência grande (Qt Core, Gui e Widgets). A instalação do Qt está no ADR-0006.
- Se a medição da Fase 0 mostrar que o Qt sozinho estoura o orçamento numa plataforma, as metas da §2.1 serão revistas por plataforma (pendência aberta no handoff).
