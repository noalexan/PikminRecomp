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
    assert "[menu] patched 20 menu screens, added 20 settings screens" in err


def test_title_quit(tmp_path):
    # Quit Game is the third title item; its confirmation opens on No: stick up + A = Yes.
    err = play(tmp_path, OPEN_MENU + down(360, 2) + "420 A\n423 -\n500 - 0 127\n505 - 0 0\n540 A\n543 -\n", 700)
    assert "[settings] quit" in err and "frame limit" not in err


def test_display_setting_saved(tmp_path):
    # Options > Display (5th item), stick right on Screen = Window, B saves.
    script = (OPEN_MENU + down(360, 1) + "400 A\n403 -\n" + down(460, 4) + "560 A\n563 -\n"
              "620 - 127 0\n625 - 0 0\n660 B\n663 -\n")
    err = play(tmp_path, script, 720)
    assert "[settings] fullscreen = false" in err
    assert "fullscreen = false" in (tmp_path / "cfg/pikminrecomp/config.toml").read_text()


def test_native_items_untouched(tmp_path):
    # Down once + A is still Options; nothing of ours fires.
    err = play(tmp_path, OPEN_MENU + down(360, 1) + "400 A\n403 -\n", 700)
    assert "added item" not in err
