#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel

for profile in balanced cancel_heavy bursty; do
  "${BUILD_DIR}/lob_simulator" \
    --mode export-dashboard \
    --profile "${profile}" \
    --orders 300000 \
    --seed 42 \
    --output "${ROOT_DIR}/results/${profile}.csv"
done

echo "Benchmark and replay artifacts written to ${ROOT_DIR}/results"
echo "Profile helper:"
echo "${ROOT_DIR}/scripts/profile_engine.sh balanced 2000000 balanced"
