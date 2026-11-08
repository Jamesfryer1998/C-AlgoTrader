"""Starting, stopping and flattening a real process: a stand-in algo_trader_app that writes status.json with
its process id (as the C++ trader does) and says which signal it got."""

import json
import stat
import subprocess
import time
from pathlib import Path

import pytest

from dashboard import records
from dashboard.trader import TraderControl, is_trader_process

FAKE_TRADER = """#!/usr/bin/env python3
import json, signal, sys, time
from pathlib import Path

records = Path("data/live")
records.mkdir(parents=True, exist_ok=True)
args = sys.argv[1:]

def write(state, note=""):
    status = {"state": state, "pid": __import__("os").getpid(), "note": note,
              "updated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    (records / "status.json").write_text(json.dumps(status))

if args[:1] == ["--backtest"]:
    folder = Path("data/backtest") / args[1]
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "equity.csv").write_text("time,equity\\n" + args[1] + " 09:30:00,100000\\n")
    print("backtested", args[1])
    sys.exit(0)

def stop(signum, frame):
    write("stopped", "flattened" if signum == signal.SIGUSR1 else "stopped")
    sys.exit(0)

signal.signal(signal.SIGINT, stop)
signal.signal(signal.SIGUSR1, stop)
write("running")
print("trading", flush=True)
while True:
    time.sleep(0.05)
"""

WAIT_SECONDS = 10


def wait_for(check):
    deadline = time.time() + WAIT_SECONDS
    while time.time() < deadline:
        if check():
            return True
        time.sleep(0.05)
    return False


@pytest.fixture
def control(project):
    app = project.trader_app
    app.parent.mkdir(parents=True)
    app.write_text(FAKE_TRADER)
    app.chmod(app.stat().st_mode | stat.S_IEXEC)
    control = TraderControl(project)
    yield control
    pid = control.running_pid()
    if pid:
        import os, signal
        os.kill(pid, signal.SIGKILL)


def live_status(control):
    return records.status(control.project.live_records)


def what_happened(control) -> str:
    """For a failed check: what the stand-in trader printed and last wrote, and what ps says about it."""
    pid = int(live_status(control).get("pid") or 0)
    ps = subprocess.run(["ps", "-ww", "-p", str(pid), "-o", "args="], capture_output=True, text=True).stdout if pid else ""
    return (f"status.json: {live_status(control)}\nps: {ps!r}\n"
            f"trader.log: {records.log_tail(control.project.live_records, 20)}")


def test_start_then_stop(control):
    assert control.running_pid() is None
    assert control.start() == "Starting"
    assert wait_for(lambda: control.running_pid() is not None), what_happened(control)
    assert control.start() == "Already running"
    assert "trading" in records.log_tail(control.project.live_records, 5)

    assert control.stop() == "Stopping"
    assert wait_for(lambda: live_status(control).get("state") == "stopped")
    assert live_status(control)["note"] == "stopped"      # it got SIGINT
    assert wait_for(lambda: control.running_pid() is None)


def test_flatten_sends_its_own_signal(control):
    control.start()
    assert wait_for(lambda: control.running_pid() is not None), what_happened(control)
    assert control.flatten() == "Closing every position, then stopping"
    assert wait_for(lambda: live_status(control).get("state") == "stopped")
    assert live_status(control)["note"] == "flattened"    # it got SIGUSR1


def test_nothing_to_stop_when_not_running(control):
    assert control.stop() == "Not running"
    assert control.flatten() == "Not running"


def test_a_stale_pid_is_not_mistaken_for_the_trader(control):
    # status.json from a run long gone, its pid now some other program's (here: this test's own)
    import os
    folder = control.project.live_records
    (folder / "status.json").write_text(json.dumps({"state": "running", "pid": os.getpid()}))
    assert not is_trader_process(os.getpid())
    assert control.running_pid() is None
    assert control.stop() == "Not running"


def test_missing_app(project):
    assert "Not built yet" in TraderControl(project).start()


def test_a_backtest_runs_in_the_background(control):
    job = control.run_backtest("2026-10-29")
    assert wait_for(lambda: job.state != "running")
    assert job.state == "done"
    assert "backtested 2026-10-29" in job.output
    assert (control.project.backtest_records("2026-10-29") / "equity.csv").exists()
