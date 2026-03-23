#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
SIM_BIN="${BUILD_DIR}/lob_simulator"
FRONTEND_RESULTS_DIR="${ROOT_DIR}/frontend/results"

mkdir -p "${BUILD_DIR}"

if [[ ! -x "${SIM_BIN}" ]]; then
  echo "building ${SIM_BIN}"
  c++ -std=c++20 -O3 -pthread -I"${ROOT_DIR}/include" \
    "${ROOT_DIR}/src/engines/baseline_order_book.cpp" \
    "${ROOT_DIR}/src/engines/optimized_order_book.cpp" \
    "${ROOT_DIR}/src/workload.cpp" \
    "${ROOT_DIR}/src/dataset.cpp" \
    "${ROOT_DIR}/src/replay.cpp" \
    "${ROOT_DIR}/src/benchmark.cpp" \
    "${ROOT_DIR}/src/main.cpp" \
    -o "${SIM_BIN}"
fi

mkdir -p "${FRONTEND_RESULTS_DIR}"

"${SIM_BIN}" --mode export-dashboard --profile balanced --orders 200000 --seed 42 --output "${ROOT_DIR}/results/balanced.csv"
"${SIM_BIN}" --mode export-dashboard --profile cancel_heavy --orders 200000 --seed 42 --output "${ROOT_DIR}/results/cancel_heavy.csv"
"${SIM_BIN}" --mode export-dashboard --profile bursty --orders 200000 --seed 42 --output "${ROOT_DIR}/results/bursty.csv"

if [[ -f "${ROOT_DIR}/data/aapl_lobster_normalized.csv" ]]; then
  "${SIM_BIN}" --mode export-dashboard --dataset "${ROOT_DIR}/data/aapl_lobster_normalized.csv" --output "${ROOT_DIR}/results/aapl_lobster.csv"
fi

cp "${ROOT_DIR}"/results/*.csv "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true
cp "${ROOT_DIR}"/results/replay_*.json "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true
cp "${ROOT_DIR}"/results/*_comparison.md "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true
cp "${ROOT_DIR}"/results/*_flamegraph.svg "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true
cp "${ROOT_DIR}"/results/*_perf.txt "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true
cp "${ROOT_DIR}"/results/*_sample.txt "${FRONTEND_RESULTS_DIR}"/ 2>/dev/null || true

echo "frontend bundle prepared at ${FRONTEND_RESULTS_DIR}"
