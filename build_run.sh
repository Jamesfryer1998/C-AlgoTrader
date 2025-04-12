#!/bin/sh

# Detect the number of CPU cores
NUM_CORES=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null)

# Step 1: Run CMake configuration
cmake -B build
if [ $? -ne 0 ]; then
    echo "❌ CMake configuration failed!"
    exit 1
fi

# Step 2: Build the project using all available cores
cmake --build build -- -j${NUM_CORES}
if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi

# Step 3: Run tests only if the build succeeded
ctest --test-dir build --output-on-failure
if [ $? -ne 0 ]; then
    echo "❌ Tests failed!"
    exit 1
fi

echo "✅ All builds and tests successful!"

echo "\n==========================================Python==========================================\n"

# Maybe run this from within C++ code every minute (time interval)?
# python src/python/main.py
# if [ $? -ne 0 ]; then
#     echo "❌ No Data found"
#     exit 1
# fi

echo "\n===========================================C++============================================\n"

./build/app/algo_trader_app
