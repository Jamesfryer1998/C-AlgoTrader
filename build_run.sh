#!/bin/sh

cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure

echo "========================================================================================\n"

./build/app/algo_trader_app