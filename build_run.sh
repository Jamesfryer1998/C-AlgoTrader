#!/bin/sh

cmake -B build
cmake --build build

echo "========================================================================================\n"

./build/app/algo_trader_app