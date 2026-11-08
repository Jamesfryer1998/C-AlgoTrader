#!/bin/bash
# The trading dashboard on http://127.0.0.1:8050 (this computer only).
# The trader itself still runs without it: ./build_run.sh, ./build/app/algo_trader_app --backtest, ...
#
# First time:
#   conda create -n algotrader python=3.13 -y
#   conda activate algotrader
#   python -m pip install -r dashboard/requirements.txt

cd "$(dirname "$0")"

# The active conda / virtual environment's Python (python3 on the PATH can be another one outside it)
if [[ -n "$CONDA_PREFIX" && -x "$CONDA_PREFIX/bin/python" ]]; then
    PYTHON="$CONDA_PREFIX/bin/python"
elif [[ -n "$VIRTUAL_ENV" && -x "$VIRTUAL_ENV/bin/python" ]]; then
    PYTHON="$VIRTUAL_ENV/bin/python"
else
    PYTHON="python3"
fi

exec "$PYTHON" -m dashboard "$@"
