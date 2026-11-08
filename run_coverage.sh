#!/bin/bash
# Unit test coverage: which lines of src/ the tests run.
#
#   ./run_coverage.sh          builds build-coverage/, runs the tests, prints a summary per file and writes
#                              an HTML report to build-coverage/coverage/index.html
#
# Needs gcovr (pip install gcovr, or brew install gcovr).

set -e

BUILD_DIR="build-coverage"
REPORT_DIR="$BUILD_DIR/coverage"

if ! command -v gcovr > /dev/null; then
    echo "gcovr is needed: pip install gcovr (or brew install gcovr)"
    exit 1
fi

# Apple's compiler is Clang, whose coverage files are read with llvm-cov's gcov mode
GCOV="gcov"
if [[ "$(uname)" == "Darwin" ]]; then
    GCOV="xcrun llvm-cov gcov"
fi

cmake -S . -B "$BUILD_DIR" -DENABLE_COVERAGE=ON
cmake --build "$BUILD_DIR" -j

# Counts add up across runs, so start from zero
find "$BUILD_DIR" -name "*.gcda" -delete
(cd "$BUILD_DIR" && ctest -j4 --output-on-failure)

mkdir -p "$REPORT_DIR"
gcovr --root . --gcov-executable "$GCOV" --filter src/ \
      --exclude-throw-branches --exclude-unreachable-branches \
      --html-details "$REPORT_DIR/index.html" --print-summary --sort uncovered-number \
      "$BUILD_DIR"

echo "HTML report: $REPORT_DIR/index.html"
