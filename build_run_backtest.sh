#!/bin/sh

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect the number of CPU cores
NUM_CORES=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null)

echo -e "${BLUE}Building AlgoTrader Backtester...${NC}"

# Step 1: Run CMake configuration
cmake -B build
if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake configuration failed!${NC}"
    exit 1
fi

# Step 2: Build the project using all available cores
echo -e "${BLUE}Building with ${NUM_CORES} cores...${NC}"
cmake --build build -- -j${NUM_CORES}
if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Build failed!${NC}"
    exit 1
fi

# Step 3: Run tests only if the build succeeded
echo -e "${BLUE}Running tests...${NC}"
ctest --test-dir build --output-on-failure
if [ $? -ne 0 ]; then
    echo -e "${RED}⚠️ Some tests failed, but continuing with execution${NC}"
fi

echo -e "${GREEN}✅ Build successful!${NC}"

echo -e "\n${BLUE}==================== Running Backtester ====================\n${NC}"

# Run the backtester with any command line arguments passed to this script
./build/app/backtest_app $@

echo -e "\n${GREEN}✅ Backtester execution complete!${NC}"