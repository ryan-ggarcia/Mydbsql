# ADR-0011: Linux e Windows primeiro; macOS numa etapa final de portabilidade

- **Status:** aceito (decisão do usuário em 2026-10-16)
- **Data:** 2026-10-16

## Contexto

O CLAUDE.md (§0, §2.6) exige que todo PR compile e passe nos testes nas três plataformas. Até a Fase 3, o macOS foi o SO que mais custou: metas de memória próprias (§2.1), tempo de início que varia 2× entre execuções na CI, benchmarks só informativos e um cofre de senhas (Keychain) que só roda na CI, sem um Mac para testar de perto.

## Decisão

- Linux e Windows continuam obrigatórios em todo PR.
- O macOS continua na CI, mas os jobs dele (`build-test` e `startup-budget`) não bloqueiam (`continue-on-error` pela entrada `optional` da matriz).
- O código do macOS que já existe fica. Nas fases seguintes não se gasta esforço com correções exclusivas do macOS; elas vão para a lista de pendências do `handoff.md`.
- Depois da última fase do plano, uma etapa de portabilidade fecha essa lista e o macOS volta a bloquear a CI.

## Consequências

- As fases andam sem esperar o runner do macOS nem investigar falhas que só acontecem nele.
- O macOS pode regredir em silêncio; a lista de pendências e o resultado (não bloqueante) da CI servem de registro.
- A regra de código de SO só em `src/infra/platform/` (§2.6, regra 1) continua valendo, o que mantém o custo da etapa final baixo.
