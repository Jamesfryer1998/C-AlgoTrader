"""Where everything is: the project root, its config files and the trader's records folders."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

CONFIG_FOLDER = "config"
GENERAL_CONFIG = "general.json"
STRATEGY_CONFIG = "strategy.json"
RISK_CONFIG = "risk.json"
DEFAULT_DATA_FOLDER = "data/"
LIVE_FOLDER = "live"
DRY_RUN_FOLDER = "live_dry_run"
BACKTEST_FOLDER = "backtest"
TRADER_APP = "build/app/algo_trader_app"
PAPER_TRADING_URL = "https://paper-api.alpaca.markets"
TEST_FEED = "test"


@dataclass(frozen=True)
class Project:
    """The C++ project the dashboard watches (the folder holding config/, data/ and build/)."""

    root: Path

    @staticmethod
    def find(start: Path | None = None) -> "Project":
        """The project root: the first folder up from `start` (this file by default) that has config/."""
        here = (start or Path(__file__)).resolve()
        for folder in [here, *here.parents]:
            if (folder / CONFIG_FOLDER / GENERAL_CONFIG).exists():
                return Project(folder)
        raise FileNotFoundError(f"No {CONFIG_FOLDER}/{GENERAL_CONFIG} above {here}")

    def config(self, name: str) -> dict:
        path = self.root / CONFIG_FOLDER / name
        return json.loads(path.read_text()) if path.exists() else {}

    @property
    def general(self) -> dict:
        return self.config(GENERAL_CONFIG)

    @property
    def risk(self) -> dict:
        return self.config(RISK_CONFIG)

    @property
    def strategy(self) -> dict:
        return self.config(STRATEGY_CONFIG)

    @property
    def data_root(self) -> Path:
        return self.root / self.general.get("marketDataBasePath", DEFAULT_DATA_FOLDER)

    @property
    def trader_app(self) -> Path:
        return self.root / TRADER_APP

    @property
    def is_dry_run(self) -> bool:
        alpaca = self.general.get("alpaca", {})
        return bool(alpaca.get("dry_run", False)) or alpaca.get("data_feed") == TEST_FEED

    @property
    def is_real_money(self) -> bool:
        """Live trading on a real account: the dashboard asks before starting it."""
        url = self.general.get("alpaca", {}).get("trading_url", PAPER_TRADING_URL)
        return url != PAPER_TRADING_URL and not self.is_dry_run

    @property
    def live_records(self) -> Path:
        """Where the trader keeps this account's records: data/live, or data/live_dry_run for a dry run."""
        return self.data_root / (DRY_RUN_FOLDER if self.is_dry_run else LIVE_FOLDER)

    def backtest_records(self, day: str) -> Path:
        return self.data_root / BACKTEST_FOLDER / day

    def describe_mode(self) -> str:
        if self.is_dry_run:
            return "Dry run (orders printed, not sent)"
        return "REAL MONEY" if self.is_real_money else "Paper account"
