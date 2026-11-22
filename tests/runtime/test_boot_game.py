"""Milestone 2 gate: the real game boots headless and runs 600 frames."""
import os
import re
import subprocess
from pathlib import Path

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"
DISC = Path(os.environ.get("PIKMIN_DISC", Path.home() / "Downloads/GPIP01/game.iso"))


def test_headless_boot_600_frames(tmp_path):
    if not PIKMIN.exists() or not DISC.exists():
        pytest.skip("pikmin binary or disc image not available")
    env = {"XDG_CONFIG_HOME": str(tmp_path), "HOME": str(tmp_path), "PATH": "/usr/bin:/bin"}
    r = subprocess.run([PIKMIN, "--disc", DISC, "--headless", "--fast", "--frames", "600"],
                       capture_output=True, text=True, env=env, timeout=300)
    (ROOT / "build/boot_stdout.txt").write_text(r.stdout)
    (ROOT / "build/boot_stderr.txt").write_text(r.stderr)
    bad = [l for l in r.stderr.splitlines() if re.search(r"ppc_hle_missing|ppc_unimpl|watchdog|ppc_bad_access", l)]
    assert not bad, bad[:5]
    assert r.returncode == 0, r.stderr[-2000:]
    # The retail build prints nothing via OSReport in this window (verified with breakpoints on
    # hle_OSReport/WriteUARTN/HIOWrite), so progress is shown by the title-screen assets loading.
    assert re.search(r"^\[dvd\] open dataDir/cinemas/titles/", r.stderr, re.M), "title assets not loaded"
    assert "[vi] frame limit 600 reached" in r.stderr
    assert len(re.findall(r"^\[dvd\] open ", r.stderr, re.M)) >= 5
    assert len(re.findall(r"^\[os\] OSCreateThread ", r.stderr, re.M)) >= 2
