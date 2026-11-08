"""A throwaway project for each test: config/ and data/, with records copied from a real backtest."""

import json
import shutil
from pathlib import Path

import pytest

from dashboard.project import Project

FIXTURES = Path(__file__).parent / "fixtures"
BACKTEST_DAY = "2026-10-29"


@pytest.fixture
def project(tmp_path: Path) -> Project:
    (tmp_path / "config").mkdir()
    (tmp_path / "config" / "general.json").write_text(json.dumps({
        "marketDataBasePath": "data/", "alpaca": {"trading_url": "https://paper-api.alpaca.markets"}}))
    (tmp_path / "config" / "risk.json").write_text(json.dumps({
        "max_position_size": 10, "risk": {"name": "KELLY", "max_stock_percent": 20}}))
    (tmp_path / "data" / "live").mkdir(parents=True)
    return Project(tmp_path)


@pytest.fixture
def backtest_folder(project: Project) -> Path:
    """The records a real `algo_trader_app --backtest 2026-10-29` wrote (fixtures/backtest_day)."""
    folder = project.backtest_records(BACKTEST_DAY)
    shutil.copytree(FIXTURES / "backtest_day", folder)
    return folder
