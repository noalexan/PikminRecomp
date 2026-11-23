import pytest

from tests.conftest import ROOT
from tests.runtime.test_cli import run

DATA = ROOT / "orig/GPIP01/sys/paths.txt"
# Title: PRESS START opens the Start/Options menu (cursor on Start).
OPEN_MENU = "300 START\n303 -\n"


def down(frame, times):
    """Stick down `times` times starting at `frame` (menus follow the analog stick)."""
    lines = []
    for i in range(times):
        f = frame + 20 * i
        lines += [f"{f} - 0 -127", f"{f + 5} - 0 0"]
    return "\n".join(lines) + "\n"


def play(tmp_path, script, frames):
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    p = tmp_path / "script.txt"
    p.write_text(script)
    r = run(["--headless", "--fast", "--frames", str(frames), "--input-script", str(p)], tmp_path)
    assert r.returncode == 0, r.stderr[-2000:]
    return r.stderr


def test_menu_screens_patched(tmp_path):
    err = play(tmp_path, "", 30)
    assert "[menu] added Settings and Quit Game to 15 menu screens" in err


def test_menu_extra_indices(tmp_path):
    err = play(tmp_path, OPEN_MENU + down(360, 2) + "420 A\n423 -\n", 700)
    assert "[menu] settings requested" in err
    assert "quit requested" not in err


def test_menu_quit_entry(tmp_path):
    err = play(tmp_path, OPEN_MENU + down(360, 3) + "440 A\n443 -\n", 700)
    assert "[menu] quit requested" in err


def test_native_items_untouched(tmp_path):
    # Down once + A is still Options; nothing of ours fires.
    err = play(tmp_path, OPEN_MENU + down(360, 1) + "400 A\n403 -\n", 700)
    assert "requested" not in err
