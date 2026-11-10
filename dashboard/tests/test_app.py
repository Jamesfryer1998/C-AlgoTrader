import pytest

dash = pytest.importorskip("dash")   # installed from requirements.txt (CI, your env); skipped without it

from dashboard.app import SentParts  # noqa: E402


def test_only_changed_parts_are_sent_again():
    sent = SentParts(None)
    assert sent.update("tape", ["10:01 KO buy"], len) == 1          # the browser has nothing yet
    assert sent.update("tape", ["10:01 KO buy"], len) is dash.no_update

    again = SentParts(sent.fingerprints)                            # the next refresh, from the page's store
    assert again.update("tape", ["10:01 KO buy"], len) is dash.no_update
    assert again.update("tape", ["10:01 KO buy", "10:05 PEP sell"], len) == 2
    assert again.update("events", [], len) == 0                     # a part seen for the first time
