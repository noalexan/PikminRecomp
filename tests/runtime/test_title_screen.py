"""Milestones 3+4 gate: the title screen renders on the GPU and Start opens the menu."""
import os
import re
import subprocess
from pathlib import Path

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"
DISC = Path(os.environ.get("PIKMIN_DISC", Path.home() / "Downloads/GPIP01/game.iso"))
FATAL = re.compile(r"ppc_hle_missing|ppc_unimpl|ppc_bad_access|watchdog|\[aurora\] ERROR")


def ppm_pixels(path):
    data = path.read_bytes()
    _, dims, _, px = data.split(b"\n", 3)
    return px


def run(tmp_path, shot, script=None):
    args = [PIKMIN, "--disc", DISC, "--offscreen", "--fast", "--frames", "750", "--screenshot", f"700:{shot}"]
    if script:
        args += ["--input-script", script]
    env = {"HOME": str(tmp_path), "XDG_CONFIG_HOME": str(tmp_path / "cfg"), "XDG_DATA_HOME": str(tmp_path / "data"),
           "PATH": "/usr/bin:/bin"}
    r = subprocess.run(args, capture_output=True, text=True, env=env, timeout=300)
    assert r.returncode == 0 and not FATAL.search(r.stderr), r.stderr[-2000:]
    return ppm_pixels(shot)


def test_title_renders_and_start_opens_menu(tmp_path):
    if not PIKMIN.exists() or not DISC.exists():
        pytest.skip("pikmin binary or disc image not available")
    title = run(tmp_path, tmp_path / "title.ppm")
    assert len(set(title[i:i + 3] for i in range(0, len(title), 3 * 97))) > 32, "title screen looks blank"
    script = tmp_path / "start.txt"
    script.write_text("300 START\n303 -\n")
    menu = run(tmp_path, tmp_path / "menu.ppm", script)
    differing = sum(1 for i in range(0, len(title), 3 * 97) if title[i:i + 3] != menu[i:i + 3])
    assert differing > len(title) // (3 * 97) // 20, "pressing Start changed nothing"
