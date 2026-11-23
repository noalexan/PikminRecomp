import re

import pytest

from tests.conftest import ROOT
from tests.runtime.test_cli import run

DATA = ROOT / "orig/GPIP01/sys/paths.txt"


def _hold(stderr):
    m = re.search(r"\[loading\] boot logo held from retrace (\d+) to (\d+)", stderr)
    return m


def test_boot_logo_held_in_real_time(tmp_path):
    # The red Nintendo logo stays up about as long as the boot loads took from a real disc.
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    r = run(["--headless", "--frames", "200"], tmp_path)
    assert r.returncode == 0, r.stderr
    m = _hold(r.stderr)
    assert m, r.stderr[-2000:]
    start, end = int(m[1]), int(m[2])
    assert end - start >= 150
    titles = r.stderr.find("[scene] TitlesSection::init")
    assert titles > r.stderr.find("boot logo held"), "title must start after the hold"


def test_boot_logo_not_held_in_fast_mode(tmp_path):
    # --fast keeps the original timeline so input scripts and screenshots stay frame-exact.
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    r = run(["--headless", "--fast", "--frames", "60"], tmp_path)
    assert r.returncode == 0, r.stderr
    assert "boot logo held" not in r.stderr
    assert "[scene] TitlesSection::init" in r.stderr
