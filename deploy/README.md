# Home server setup (one-off)

Releases use **GitHub Actions**: `.github/workflows/ci.yml` builds and tests the C++ trader and the
dashboard on every push, and `.github/workflows/deploy.yml` deploys `main` once CI passes. The deploy
job runs **on the server** through a self-hosted GitHub Actions runner. The runner checks out the commit
and calls `deploy/deploy.sh`, which syncs the code, builds the C++ apps, updates the dashboard's conda
env if its requirements changed, installs the systemd unit and restarts the dashboard.

What runs where:

| | |
|---|---|
| Dashboard | `algo-trader-dashboard.service`, gunicorn on port 8050 |
| Trader | started and stopped from the dashboard (or by hand, see below). It keeps running if the dashboard restarts, and a deploy never stops it: it picks up a new build at its next Start |
| End-of-day backtests | the dashboard backtests each day the live account traded, about 20 minutes after the close (once Alpaca has all its bars), starting as the live account did. See them on the Backtest vs live tab the next morning |
| Code and build | `/opt/algo-trader/app` (`build/` is kept between deploys, so builds are incremental) |
| Bars and trading records | `/var/lib/algo-trader/data` (linked as `app/data`, never touched by deploys) |
| Alpaca keys | `/etc/algo-trader.env` |

Tested layout: an Ubuntu 24.04 LXC on the Proxmox box with about 2 cores, 4 GB RAM and 20 GB disk
(the build needs the memory more than the trader does). The runner only makes outbound connections
to GitHub, so nothing needs opening on your router.

## 1. Packages, user, folders, conda

```bash
# as root in a fresh container
apt update && apt install -y sudo curl git rsync ca-certificates build-essential cmake libssl-dev
adduser --system --group --shell /bin/bash --home /home/algo algo
mkdir -p /opt/algo-trader /var/lib/algo-trader/data
chown -R algo:algo /opt/algo-trader /var/lib/algo-trader

sudo -iu algo
curl -L -O https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh
bash Miniforge3-Linux-x86_64.sh -b -p ~/miniforge3
~/miniforge3/bin/conda init bash
```

## 2. Let the deploy restart the dashboard without a password

`sudo visudo -f /etc/sudoers.d/algo-trader`:

```
algo ALL=(root) NOPASSWD: /usr/bin/systemctl daemon-reload, \
  /usr/bin/systemctl enable algo-trader-dashboard.service, \
  /usr/bin/systemctl restart algo-trader-dashboard.service, \
  /usr/bin/systemctl --no-pager --lines=0 status algo-trader-dashboard.service, \
  /usr/bin/install -m 0644 /opt/algo-trader/app/deploy/systemd/* /etc/systemd/system/*, \
  /usr/bin/journalctl -u algo-trader-dashboard -n 50 --no-pager
```

## 3. The Alpaca keys

`/etc/algo-trader.env` (the dashboard's unit reads it and the trader inherits it). The code folder
is rsynced on every deploy, so don't keep a `.env` there.

```
APCA_API_KEY_ID=...
APCA_API_SECRET_KEY=...
```

`sudo chown root:algo /etc/algo-trader.env && sudo chmod 640 /etc/algo-trader.env`
(only root and the `algo` user can read it).

Paper or real money comes from `config/general.json` in the repo (`trading_url`), as on your Mac.

## 4. Self-hosted GitHub Actions runner

Repo → Settings → Actions → Runners → *New self-hosted runner* (Linux, x64). Run the install
commands it shows as the `algo` user, and when asked for labels add `algo-trader`.
Then install it as a service: `sudo ./svc.sh install algo && sudo ./svc.sh start`.

The deploy job uses a GitHub *environment* called `production` (created automatically on the
first run). Repo → Settings → Environments → production lets you require a manual approval
before each deploy, if you want one. You can also deploy any time from the Actions tab: Deploy
→ *Run workflow*.

GitHub only runs the Deploy workflow once it is on `main`: pushes to other branches run CI only.

## 5. First deploy

Push to `main` (or run the Deploy workflow by hand). The first one takes a few minutes longer: it
also compiles gtest, json and IXWebSocket, and creates the conda env.

Then open `http://<container's IP>:8050`.

**The dashboard has no login, and its Start button trades with your Alpaca keys.** Keep port 8050
on your LAN or Tailscale. Don't expose it to the internet.

## Useful commands

```bash
systemctl status algo-trader-dashboard
journalctl -u algo-trader-dashboard -f
tail -f /var/lib/algo-trader/data/live/trader.log      # the trader's output (live_dry_run/ for a dry run)
```

The command-line apps work on the server too, as `algo`:

```bash
cd /opt/algo-trader/app
set -a; . /etc/algo-trader.env; set +a
./build/app/algo_trader_app --backtest 2026-10-29 --quiet
```
