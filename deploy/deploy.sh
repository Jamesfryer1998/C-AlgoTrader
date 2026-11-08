#!/usr/bin/env bash
# Deploy a checked-out commit to this server: build the trader, update the dashboard's Python env,
# restart the dashboard. Called by the deploy job running on the server:  deploy/deploy.sh <checkout-dir>
#
# Layout on the server:
#   /opt/algo-trader/app          code (rsynced from the checkout) and its build/ (kept, so builds are incremental)
#   /opt/algo-trader/app/data     symlink to /var/lib/algo-trader/data: bars and trading records (never touched)
#   /opt/algo-trader/env          symlink to the algotrader conda env
#   /etc/algo-trader.env          the Alpaca keys (never in the code folder)
#
# A running trader is left alone: it keeps trading on its old build until it's stopped and started again.
set -euo pipefail

SRC="${1:?usage: deploy.sh <checkout-dir>}"
APP_ROOT="${APP_ROOT:-/opt/algo-trader}"
APP_DIR="$APP_ROOT/app"
DATA_DIR="${DATA_DIR:-/var/lib/algo-trader/data}"
ENV_NAME="${ENV_NAME:-algotrader}"
PYTHON_VERSION="3.13"
CONDA_BASE="${CONDA_BASE:-$HOME/miniforge3}"
SERVICE="algo-trader-dashboard.service"
TRADER_APPS=(algo_trader_app optimise_app quant_app screen_app risk_app)   # the tests are CI's job
REQUIREMENTS="$APP_DIR/dashboard/requirements.txt"

SETUP_GUIDE="deploy/README.md"
SECRETS_FILE="/etc/algo-trader.env"
SUDOERS_FILE="/etc/sudoers.d/algo-trader"
DEPLOY_USER="$(id -un)"

# ── the one-off server setup, checked first so a missing step fails in seconds with its name ──
missing=()
[[ -f "$CONDA_BASE/etc/profile.d/conda.sh" ]] || missing+=("step 1: miniforge for $DEPLOY_USER in $CONDA_BASE")
sudo -n -l /usr/bin/systemctl daemon-reload > /dev/null 2>&1 || missing+=("step 2: $SUDOERS_FILE")
[[ -r "$SECRETS_FILE" ]] || missing+=("step 3: $SECRETS_FILE with the Alpaca keys, readable by $DEPLOY_USER")
if (( ${#missing[@]} )); then
  echo "The server isn't set up yet (see $SETUP_GUIDE):"
  printf '  - %s\n' "${missing[@]}"
  exit 1
fi

echo "==> Deploying $(git -C "$SRC" rev-parse --short HEAD) to $APP_DIR"
mkdir -p "$APP_DIR" "$DATA_DIR"
rsync -a --delete \
  --exclude '.git' --exclude '/build/' --exclude '/data' --exclude '.env' \
  --exclude '__pycache__' --exclude '.pytest_cache' \
  "$SRC"/ "$APP_DIR"/
[[ -L "$APP_DIR/data" ]] || ln -s "$DATA_DIR" "$APP_DIR/data"

# ── the C++ apps ──
echo "==> Building the trader"
cmake -S "$APP_DIR" -B "$APP_DIR/build" > /dev/null
cmake --build "$APP_DIR/build" --target "${TRADER_APPS[@]}" -j "$(nproc)"
"$APP_DIR/build/app/algo_trader_app" --help > /dev/null

# ── conda env: create on first deploy, update only when the dashboard's requirements change ──
# shellcheck disable=SC1091
source "$CONDA_BASE/etc/profile.d/conda.sh"
DEPS_HASH=$(sha256sum "$REQUIREMENTS" | cut -d' ' -f1)
HASH_FILE="$APP_ROOT/.deps_hash"

env_exists() { conda env list | grep -qE "^$ENV_NAME\s"; }

if ! env_exists || [[ ! -f "$HASH_FILE" ]]; then
  # No hash file means the env was never finished (e.g. an earlier deploy failed part-way): rebuild it.
  echo "==> Creating conda env $ENV_NAME"
  env_exists && conda env remove -y -n "$ENV_NAME"
  conda create -y -n "$ENV_NAME" "python=$PYTHON_VERSION"
  conda activate "$ENV_NAME"
  pip install --no-cache-dir -r "$REQUIREMENTS"
  conda deactivate
  echo "$DEPS_HASH" > "$HASH_FILE"
elif [[ "$(cat "$HASH_FILE")" != "$DEPS_HASH" ]]; then
  echo "==> Dashboard requirements changed; updating env"
  conda activate "$ENV_NAME"
  pip install --no-cache-dir -r "$REQUIREMENTS"
  conda deactivate
  echo "$DEPS_HASH" > "$HASH_FILE"
fi
ln -sfn "$(conda env list | awk -v n="$ENV_NAME" '$1==n {print $NF}')" "$APP_ROOT/env"

# ── smoke test before touching the running dashboard ──
conda activate "$ENV_NAME"
(cd "$APP_DIR" && python -c "from dashboard import wsgi")
TRADER_PID=$(cd "$APP_DIR" && python -c \
  "from dashboard.project import Project; from dashboard.trader import TraderControl; \
print(TraderControl(Project.find()).running_pid() or '')")
conda deactivate

# ── systemd unit (reload only if it changed) ──
UNITS_CHANGED=0
for unit in "$APP_DIR"/deploy/systemd/*; do
  dest="/etc/systemd/system/$(basename "$unit")"
  if ! cmp -s "$unit" "$dest"; then
    sudo install -m 0644 "$unit" "$dest"
    UNITS_CHANGED=1
  fi
done
if [[ $UNITS_CHANGED -eq 1 ]]; then
  echo "==> systemd unit changed; reloading"
  sudo systemctl daemon-reload
  sudo systemctl enable "$SERVICE"
fi

echo "==> Restarting the dashboard"
sudo systemctl restart "$SERVICE"
sudo systemctl --no-pager --lines=0 status "$SERVICE"
if [[ -n "$TRADER_PID" ]]; then
  echo "==> The trader is running (pid $TRADER_PID) and was left alone: it runs the new build after Stop and Start"
fi
echo "==> Deploy complete"
