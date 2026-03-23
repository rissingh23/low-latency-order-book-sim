#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
SIM_BIN="${BUILD_DIR}/lob_simulator"

INPUT_MODE="${1:-balanced}"
ORDERS="${2:-2000000}"
RESULT_PREFIX="${3:-profile_${INPUT_MODE}}"

if [[ ! -x "${SIM_BIN}" ]]; then
  echo "missing ${SIM_BIN}; build the project first" >&2
  exit 1
fi

mkdir -p "${ROOT_DIR}/results"

run_target=("${SIM_BIN}" --mode benchmark)
if [[ -f "${ROOT_DIR}/data/${INPUT_MODE}.csv" ]]; then
  run_target+=(--dataset "${ROOT_DIR}/data/${INPUT_MODE}.csv")
else
  run_target+=(--profile "${INPUT_MODE}" --orders "${ORDERS}" --seed 42)
fi
run_target+=(--output "${ROOT_DIR}/results/${RESULT_PREFIX}.csv")

if command -v perf >/dev/null 2>&1; then
  perf stat "${run_target[@]}" 2> "${ROOT_DIR}/results/${RESULT_PREFIX}_perf.txt"
  perf record -F 199 -g -- "${run_target[@]}" >/dev/null 2>&1
  perf script > "${ROOT_DIR}/results/${RESULT_PREFIX}.folded"
  echo "perf artifacts written under results/${RESULT_PREFIX}_*"
  exit 0
fi

if command -v sample >/dev/null 2>&1; then
  "${run_target[@]}" >/tmp/"${RESULT_PREFIX}".stdout &
  target_pid=$!
  sleep 1
  sample "${target_pid}" 2 -file "${ROOT_DIR}/results/${RESULT_PREFIX}_sample.txt" >/dev/null 2>&1 || true
  wait "${target_pid}"
  python3 "${ROOT_DIR}/scripts/sample_to_flamegraph.py" \
    --input "${ROOT_DIR}/results/${RESULT_PREFIX}_sample.txt" \
    --output "${ROOT_DIR}/results/${RESULT_PREFIX}_flamegraph.svg"
  {
    echo "perf unavailable on this host; generated macOS sample profile instead."
    echo
    cat /tmp/"${RESULT_PREFIX}".stdout
  } > "${ROOT_DIR}/results/${RESULT_PREFIX}_perf.txt"
  echo "sample/flamegraph artifacts written under results/${RESULT_PREFIX}_*"
  exit 0
fi

echo "neither perf nor sample is available on this machine" >&2
exit 1
