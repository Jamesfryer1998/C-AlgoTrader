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


def component_ids(component, found=None) -> set:
    """Every id in a Dash layout."""
    found = set() if found is None else found
    if isinstance(component, (list, tuple)):
        for child in component:
            component_ids(child, found)
    elif hasattr(component, "to_plotly_json"):
        if getattr(component, "id", None):
            found.add(component.id)
        component_ids(getattr(component, "children", None), found)
    return found


def test_every_callback_uses_parts_of_the_page(project):
    """A callback whose Input or State isn't on the page never runs (the browser only logs an error), as the
    History tab did when its day picker was dropped from the layout."""
    from dashboard.app import create_app
    app = create_app(project)
    on_page = component_ids(app.layout())
    for callback in app.callback_map.values():
        for dependency in callback["inputs"] + callback["state"]:
            assert dependency["id"] in on_page, f"{dependency['id']} isn't in the layout"
