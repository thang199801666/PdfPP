#!/usr/bin/env python3
"""Compare Pdf++.Benchmarks CSV output with an accepted JSON baseline."""
import argparse, csv, json, sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('baseline', type=Path)
parser.add_argument('current', type=Path)
parser.add_argument('--max-regression-percent', type=float, default=10.0)
args = parser.parse_args()
base = json.loads(args.baseline.read_text(encoding='utf-8'))
with args.current.open(newline='', encoding='utf-8') as stream:
    rows = {row['workload']: float(row['median_ms']) for row in csv.DictReader(stream)}
failures = []
for name, expected in base.items():
    if name not in rows:
        failures.append(f'{name}: missing')
        continue
    limit = float(expected) * (1.0 + args.max_regression_percent / 100.0)
    if rows[name] > limit:
        failures.append(f'{name}: {rows[name]:.3f} ms > {limit:.3f} ms')
if failures:
    print('Benchmark regression gate failed:', file=sys.stderr)
    for item in failures: print(f'  - {item}', file=sys.stderr)
    raise SystemExit(1)
print(f'Benchmark regression gate passed for {len(base)} workloads.')
