#!/bin/sh

# Step 1: Run CMake configuration
cmake -B build
if [ $? -ne 0 ]; then
    echo "❌ CMake configuration failed!"
    exit 1
fi

# Step 2: Build the project
cmake --build build
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
python src/pyhton/main.py

echo "\n===========================================C++============================================\n"

./build/app/algo_trader_app