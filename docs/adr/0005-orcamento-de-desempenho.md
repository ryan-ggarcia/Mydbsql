# ADR-0005: Orçamento de desempenho verificado na CI

- **Status:** aceito
- **Data:** 2026-10-14

## Contexto

"Leve" e "rápido" são os objetivos 1 e 2 do produto. Sem medição contínua, eles se degradam aos poucos. O HeidiSQL serve de referência medida (§1.5): ~296 MB de RSS no Linux/Qt6 e ~47,6 MB de working set no Windows/VCL, com o gerenciador de sessões aberto.

## Decisão

- O executável aceita `--measure-startup[=<arquivo>]`. Ele mede de `main()` até a primeira exposição (`QEvent::Expose`) da janela, espera `--settle-ms` (padrão de 500 ms) para a memória se acomodar, emite um JSON com `startup_ms`, `rss_kb`, `anon_kb` e `libs_loaded` e encerra. As leituras vêm de `IPlatform::processMemory()` e `IPlatform::loadedLibraries()`.
- O job `startup-budget` da CI compila em release, roda a medição 3 vezes (vale a mediana) e os benchmarks do Google Benchmark. Depois `tools/check_budget.py` confere:
  - os limites absolutos de `bench/budget.json` (na Fase 0: ≤ 150 ms, ≤ 40 MB de RSS e ≤ 25 MB de memória privada);
  - a regressão contra `bench/baseline/<plataforma>.json`, que falha acima de 10%.
- Os baselines são gerados **nos runners da CI** (artefato `startup-<plataforma>` + `--write-baseline`), nunca numa máquina de desenvolvimento, porque o hardware muda os números.
- Em builds de debug, o `EventLoopWatchdog` registra no log qualquer evento que ocupe a thread da UI por mais de 16 ms (§2.3, regra 1).

## Consequências

- Os runners compartilhados da CI têm ruído. A tolerância de 10% pode precisar de repetições extras ou de um ajuste por métrica. Isso será revisto com dados reais.
- Enquanto um baseline não existe para uma plataforma, só os limites absolutos são conferidos, e o script imprime o baseline sugerido.
