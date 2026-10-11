#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

if [ ! -d "build" ]; then
    echo "Error: build directory not found. Run cmake -S . -B build first."
    exit 1
fi

ctest --test-dir build --output-on-failure
