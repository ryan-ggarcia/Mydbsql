#!/usr/bin/env python3
"""Confere o orçamento de desempenho (CLAUDE.md §2.1 e §2.5).

Entradas:
  --startup run1.json run2.json run3.json   saídas de `mydbsql --measure-startup=<arquivo>` (usa a mediana)
  --benchmarks bench.json [ui.json …]       saídas de `mydb_bench`/`mydb_ui_bench --benchmark_out=<arquivo>
                                            --benchmark_out_format=json`
  --budget bench/budget.json                limites absolutos da fase atual ("platforms" sobrepõe por SO;
                                            "benchmarks_ms" limita benchmarks pelo nome)
  --baseline bench/baseline/<plataforma>.json   valores de referência; regressão > tolerância falha

Sem baseline (primeira execução numa plataforma), só os limites absolutos valem e o script imprime
o baseline sugerido. Use --write-baseline para gravá-lo.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
from pathlib import Path

# Métricas de inicialização em que "menor é melhor".
STARTUP_METRICS = ("startup_ms", "rss_kb", "anon_kb", "libs_loaded")
TIME_UNIT_TO_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def median_startup(paths: list[Path]) -> dict:
    runs = [load(p) for p in paths]
    platforms = {run["platform"] for run in runs}
    if len(platforms) != 1:
        sys.exit(f"Execuções de plataformas diferentes: {platforms}")
    if any(run.get("debug_build") for run in runs):
        print("aviso: medição de um build de debug; o orçamento vale para release", file=sys.stderr)
    result = {"platform": platforms.pop(), "runs": len(runs)}
    for metric in STARTUP_METRICS:
        result[metric] = statistics.median(run[metric] for run in runs)
    return result


def benchmark_times(path: Path) -> dict[str, float]:
    data = load(path)
    times: dict[str, float] = {}
    for bench in data.get("benchmarks", []):
        if bench.get("run_type", "iteration") != "iteration":
            continue  # ignora agregados (mean/median/stddev) quando há repetições
        factor = TIME_UNIT_TO_NS[bench.get("time_unit", "ns")]
        times[bench["name"]] = bench["real_time"] * factor
    return times


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # acentos legíveis também no console do Windows
        stream.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--startup", nargs="+", type=Path, required=True)
    parser.add_argument("--benchmarks", nargs="+", type=Path, default=[])
    parser.add_argument("--budget", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--write-baseline", action="store_true")
    args = parser.parse_args()

    startup = median_startup(args.startup)
    benchmarks: dict[str, float] = {}
    for path in args.benchmarks:
        benchmarks.update(benchmark_times(path))
    budget = load(args.budget)
    failures: list[str] = []

    print(f"Inicialização ({startup['platform']}, mediana de {startup['runs']}):")
    limits = {**budget.get("startup", {}), **budget.get("platforms", {}).get(startup["platform"], {})}
    for metric in STARTUP_METRICS:
        limit = limits.get(metric)
        value = startup[metric]
        status = "ok"
        if limit is not None and value > limit:
            status = f"ESTOUROU o orçamento ({limit})"
            failures.append(f"{metric}={value} > orçamento {limit}")
        print(f"  {metric:12} {value:>10} {status}")

    # Limites absolutos de benchmarks (ex.: árvore com 10 mil tabelas, §2.1), em milissegundos.
    for name, limit_ms in budget.get("benchmarks_ms", {}).items():
        value = benchmarks.get(name)
        if value is None:
            failures.append(f"{name}: benchmark com limite no orçamento não foi executado")
            continue
        value_ms = value / 1e6
        status = "ok" if value_ms <= limit_ms else f"ESTOUROU o orçamento ({limit_ms} ms)"
        if value_ms > limit_ms:
            failures.append(f"{name}={value_ms:.1f} ms > orçamento {limit_ms} ms")
        print(f"  bench {name:28} {value_ms:>10.2f} ms {status}")

    if args.baseline.exists():
        baseline = load(args.baseline)
        default_tolerance = float(baseline.get("tolerance", 0.10))
        overrides = baseline.get("tolerances", {})
        for metric in STARTUP_METRICS:
            reference = baseline.get("startup", {}).get(metric)
            tolerance = float(overrides.get(metric, default_tolerance))
            if reference and startup[metric] > reference * (1 + tolerance):
                failures.append(f"{metric}: {startup[metric]} > baseline {reference} +{tolerance:.0%}")
        tolerance = float(overrides.get("benchmarks", default_tolerance))
        # Em runners com muito ruído (macOS), os benchmarks só são publicados, sem falhar a CI.
        informative = bool(baseline.get("benchmarksInformative", False))
        for name, reference in baseline.get("benchmarks", {}).items():
            value = benchmarks.get(name)
            if value is None:
                continue
            ratio = value / reference
            print(f"  bench {name:28} {value:>12.1f} ns  ({ratio:.2f}× baseline)")
            if ratio > 1 + tolerance and not informative:
                failures.append(f"{name}: {value:.1f} ns > baseline {reference:.1f} ns +{tolerance:.0%}")
    else:
        print(f"Sem baseline em {args.baseline}: só o orçamento absoluto foi conferido.")

    suggested = {
        "platform": startup["platform"],
        "tolerance": 0.10,
        "tolerances": {"startup_ms": 1.0, "benchmarks": 1.0},
        "startup": {metric: startup[metric] for metric in STARTUP_METRICS},
        "benchmarks": {name: round(value, 1) for name, value in sorted(benchmarks.items())},
    }
    if args.write_baseline:
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_text(json.dumps(suggested, indent=2) + "\n", encoding="utf-8")
        print(f"Baseline gravado em {args.baseline}")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # Anotações são públicas; os artefatos e os logs exigem login.
        print(f"::notice title=Medição ({startup['platform']})::{json.dumps(suggested)}")
    if not args.write_baseline and not args.baseline.exists():
        print("Baseline sugerido:\n" + json.dumps(suggested, indent=2))

    if failures:
        print("\nFALHOU:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print("\nOrçamento respeitado.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
