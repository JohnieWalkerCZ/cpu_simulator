#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

mkdir -p build/validation-results
./build/cpu_validate configs/8bit.json validation/programs/8bit_arithmetic.asm \
  --expect R0=4 --expect FLAGS=2 \
  --result build/validation-results/8bit-von-neumann.json \
  --trace build/validation-results/8bit-von-neumann-trace.json
./build/cpu_validate configs/8bit_harvard.json validation/programs/8bit_arithmetic.asm \
  --expect R0=4 --expect FLAGS=2 \
  --result build/validation-results/8bit-harvard.json
./build/cpu_validate configs/tiny4.json validation/programs/tiny4_add.asm \
  --expect A=4 --result build/validation-results/tiny4.json
./build/cpu_validate configs/16bit.json validation/programs/16bit_multiply.asm \
  --expect R0=42 --result build/validation-results/16bit.json
./build/cpu_validate configs/8bit.json validation/programs/call_branch.asm \
  --expect R0=0 --expect R2=42 --expect SP=65535 \
  --result build/validation-results/call-branch-stack.json
./build/cpu_validate configs/8bit.json validation/programs/mmio_multiply.asm \
  --expect R4=65 --result build/validation-results/declarative-mmio.json

echo "Validation results: build/validation-results/"
