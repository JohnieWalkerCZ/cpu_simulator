#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Setup colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color
BOLD='\033[1m'

echo -e "${BOLD}Running CPU Simulator Test Suite...${NC}\n"

# Verify build directory exists
if [ ! -d "build" ]; then
    echo -e "${RED}Error: build directory not found. Please build the project first.${NC}"
    exit 1
fi

# List of tests to run
TESTS=(
    "test_config_parser"
    "test_alu_operations"
    "test_register_file"
    "test_memory"
    "test_decoder"
    "test_assembler"
    "test_cpu_integration"
)

PASSED=0
FAILED=0

for test in "${TESTS[@]}"; do
    test_path="./build/$test"
    if [ ! -f "$test_path" ]; then
        echo -e "${RED}Error: Test binary $test not found in build directory. Make sure you ran 'make'.${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    echo -e "${BOLD}Running $test...${NC}"
    # Run test and capture output
    if (cd build && "./$test"); then
        echo -e "${GREEN}✓ $test PASSED${NC}\n"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗ $test FAILED${NC}\n"
        FAILED=$((FAILED + 1))
    fi
done

echo -e "${BOLD}Test Summary:${NC}"
echo -e "${GREEN}Passed: $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi
