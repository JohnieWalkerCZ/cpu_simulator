#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build
cmake --build build --target cpu_validate
mkdir -p build/validation-results
./build/cpu_validate configs/16bit.json validation/programs/16bit_multiply.asm \
  --expect R0=42 --result build/validation-results/demo.json \
  --trace build/validation-results/demo-trace.json

echo "Demo result: build/validation-results/demo.json"
