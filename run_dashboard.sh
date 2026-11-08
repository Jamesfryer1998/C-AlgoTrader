#!/bin/bash
# The trading dashboard on http://127.0.0.1:8050 (this computer only).
# The trader itself still runs without it: ./build_run.sh, ./build/app/algo_trader_app --backtest, ...
#
# First time: python3 -m pip install -r dashboard/requirements.txt

cd "$(dirname "$0")"
exec python3 -m dashboard "$@"
