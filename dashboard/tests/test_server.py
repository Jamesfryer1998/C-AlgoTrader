"""The System tab's server facts."""

import json

from dashboard import server, views


def test_folder_sizes_biggest_first_and_cached(project):
    data = project.data_root
    (data / "morning" / "daily").mkdir(parents=True)
    (data / "morning" / "daily" / "AAPL.csv").write_bytes(b"x" * 3000)
    (data / "picks").mkdir()
    (data / "picks" / "a.csv").write_bytes(b"x" * 100)
    (data / "portfolio.json").write_bytes(b"x" * 10)
    sizes = server.folder_sizes(data, now=1000)
    assert [(s.name, s.bytes, s.files) for s in sizes] == [("morning", 3000, 1), ("picks", 100, 1),
                                                          (server.LOOSE_FILES, 10, 1), ("live", 0, 0)]
    (data / "picks" / "b.csv").write_bytes(b"x" * 5000)
    assert server.folder_sizes(data, now=1001)[0].name == "morning"   # remembered
    assert server.folder_sizes(data, now=1000 + server.SIZES_FOR_SECONDS + 1)[0].name == "picks"


def test_data_table_against_the_limit(project):
    (project.root / "config" / "general.json").write_text(json.dumps({"data_limit_gb": 1}))
    limit = server.data_limit_bytes(project)
    assert limit == server.GIGABYTE
    table = views.data_table([server.FolderSize("morning", int(0.9 * limit), 12000)], limit)
    assert table.rows[0] == [("morning", ""), ("922 MB", ""), ("12,000", ""), ("90.0%", "")]
    total = table.rows[-1]
    assert total[0] == ("All of data/", views.WARNING) and total[3][0] == "90.0% of 1.0 GB"


def test_server_table(project):
    assert views.size(512) == "512 B" and views.size(3 * 1024 ** 2) == "3 MB" and views.size(2.5 * server.GIGABYTE) == "2.5 GB"
    table = views.server_table(server.Disk(100 * server.GIGABYTE, 90 * server.GIGABYTE, 10 * server.GIGABYTE),
                               server.Memory(8 * server.GIGABYTE, 2 * server.GIGABYTE), (0.5, 0.4, 0.3), 90000,
                               {"commit": "abc1234", "branch": "main", "subject": "A change",
                                "deployed_utc": "2026-11-14T19:00:00Z"}, 4)
    rows = {row[0][0]: row[1] for row in table.rows}
    assert rows["Disk"] == ("10.0 GB free of 100.0 GB (90% used)", views.WARNING)
    assert rows["Memory"][0] == "2.0 GB free of 8.0 GB"
    assert rows["Deployed"][0] == "abc1234 on main, 2026-11-14 19:00 UTC"
    assert server.deployed(project) is None
    (project.root / server.DEPLOYED_FILE).write_text(json.dumps({"commit": "abc1234"}))
    assert server.deployed(project)["commit"] == "abc1234"
