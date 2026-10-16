# ADR-0012: Ícones SVG próprios com o QtSvg carregado sob demanda

- **Status:** aceito (decisão do usuário em 2026-10-16)
- **Data:** 2026-10-16

## Contexto

A Fase 4 trouxe a árvore de objetos e a barra de ferramentas, que precisam de ícones. A §2.2 (regra 9) pede ícones em SVG monocromático num `.qrc` comprimido, desenhados e guardados em cache no tamanho exato pedido. O Qt só lê SVG com o módulo QtSvg, e a regra 3 da mesma seção exige um ADR com o custo medido para cada dependência nova. As alternativas eram desenhar os ícones em código com `QPainter` (sem dependência) ou usar os ícones genéricos do `QStyle` (sem ícones de tabela, view ou trigger).

## Decisão

- Os ícones são SVGs próprios (desenho original, sem nada do HeidiSQL nem dos Icons8) em `resources/icons/`, com `stroke="currentColor"`, embutidos no `mydb_ui` por `qt_add_resources` com zlib.
- `ui::appIcon()` cria um `QIconEngine` que troca `currentColor` pela cor do texto da paleta (o ícone acompanha o tema claro ou escuro), desenha no tamanho físico pedido (HiDPI) com o `QImageReader` e guarda o resultado no `QPixmapCache`.
- O código não linka o `Qt6::Svg`: o SVG é lido pelo plugin de imagens `qsvg`, que o Qt carrega na primeira leitura. O gerenciador de sessões (a primeira janela) não usa esses ícones, então o QtSvg só entra na memória depois de conectar.
- O `tools/fetch_qt.py` e a CI baixam `qtbase,qtsvg`.

## Custo medido (Windows, release, 2026-10-16)

| Item | Valor |
|---|---|
| Bibliotecas novas | `Qt6Svg.dll` (595 KB) e o plugin `imageformats/qsvg.dll` (39 KB), só depois do primeiro ícone |
| Memória ao desenhar o primeiro ícone | +164 kB privada, +1,3 MB residente (teste `MainWindowTest::svgIconsLoadQtSvgOnlyWhenDrawn`) |
| Início (`--measure-startup`, gerenciador de sessões) | inalterado: ~81 ms, ~39,4 MB, 81 módulos (baseline: 75,9 ms, 40,3 MB, 81) |
| 15 ícones no executável | ~3 KB de SVG antes da compressão |

## Consequências

- Ícones nítidos em qualquer escala e no tema do sistema, sem custo na inicialização.
- O teste de UI confirma que o plugin do QtSvg não está carregado antes de o primeiro ícone ser desenhado.
- O empacotamento (Fase 12) precisa levar `Qt6Svg` e o plugin `imageformats/qsvg`.
- Exportar o diagrama em SVG (Fase 13) usa o `QSvgGenerator`, que exige linkar o `Qt6::Svg`: isso continua dependendo de outro ADR.
