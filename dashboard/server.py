"""What the server has, for the System tab: the data folder part by part against its limit, the disk under it,
memory, load, uptime, and which commit is deployed (deploy/deploy.sh writes DEPLOYED.json)."""

from __future__ import annotations

import json
import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path

from .project import Project

DATA_LIMIT_KEY = "data_limit_gb"      # config/general.json
DEFAULT_LIMIT_GB = 5.0                # the trader's own default (SaveData::DEFAULT_LIMIT_GB)
DEPLOYED_FILE = "DEPLOYED.json"
SIZES_FOR_SECONDS = 300               # adding up tens of thousands of files: at most every 5 minutes
LOOSE_FILES = "(files in data/)"
GIGABYTE = 1024 ** 3
KILOBYTE = 1024
MEMINFO = Path("/proc/meminfo")
UPTIME = Path("/proc/uptime")

_sizes_cache: dict[Path, tuple[float, list["FolderSize"]]] = {}


@dataclass
class FolderSize:
    name: str
    bytes: int
    files: int


@dataclass
class Disk:
    total: int
    used: int
    free: int


@dataclass
class Memory:
    total: int
    available: int


def data_limit_bytes(project: Project) -> int:
    return int(float(project.general.get(DATA_LIMIT_KEY, DEFAULT_LIMIT_GB)) * GIGABYTE)


def _size_of(path: Path) -> tuple[int, int]:
    """Bytes and files under a folder (links not followed)."""
    total = files = 0
    for folder, _, names in os.walk(path):
        for name in names:
            try:
                total += (Path(folder) / name).lstat().st_size
                files += 1
            except OSError:
                pass
    return total, files


def folder_sizes(data_root: Path, now: float | None = None) -> list[FolderSize]:
    """Each folder in data/ (and the loose files together), biggest first. Remembered for a few minutes."""
    now = now or time.time()
    cached = _sizes_cache.get(data_root)
    if cached and now - cached[0] < SIZES_FOR_SECONDS:
        return cached[1]
    sizes, loose = [], FolderSize(LOOSE_FILES, 0, 0)
    try:
        entries = list(data_root.iterdir())
    except OSError:
        entries = []
    for entry in entries:
        try:
            if entry.is_dir() and not entry.is_symlink():
                total, files = _size_of(entry)
                sizes.append(FolderSize(entry.name, total, files))
            else:
                loose.bytes += entry.lstat().st_size
                loose.files += 1
        except OSError:
            pass
    if loose.files:
        sizes.append(loose)
    sizes.sort(key=lambda size: size.bytes, reverse=True)
    _sizes_cache[data_root] = (now, sizes)
    return sizes


def disk(path: Path) -> Disk | None:
    try:
        usage = shutil.disk_usage(path)
    except OSError:
        return None
    return Disk(usage.total, usage.used, usage.free)


def memory() -> Memory | None:
    """From /proc/meminfo (Linux only)."""
    try:
        fields = dict(line.split(":", 1) for line in MEMINFO.read_text().splitlines() if ":" in line)
        kilobytes = lambda key: int(fields[key].split()[0]) * KILOBYTE
        return Memory(kilobytes("MemTotal"), kilobytes("MemAvailable"))
    except (OSError, KeyError, ValueError, IndexError):
        return None


def load() -> tuple[float, float, float] | None:
    """The 1, 5 and 15 minute load averages."""
    try:
        return os.getloadavg()
    except OSError:
        return None


def uptime_seconds() -> float | None:
    try:
        return float(UPTIME.read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return None


def deployed(project: Project) -> dict | None:
    """{"commit", "branch", "subject", "deployed_utc"}, or None when not deployed by deploy.sh (e.g. on a laptop)."""
    try:
        return json.loads((project.root / DEPLOYED_FILE).read_text())
    except (OSError, ValueError):
        return None
