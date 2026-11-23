import pytest

from tests.conftest import ROOT
from tests.runtime.test_cli import run

DATA = ROOT / "orig/GPIP01/sys/paths.txt"


def boot(tmp_path):
    p = tmp_path / "script.txt"
    p.write_text("300 START\n303 -\n400 A\n403 -\n")  # title -> Start: the game checks the card
    r = run(["--headless", "--fast", "--frames", "1100", "--input-script", str(p)], tmp_path,
            env_extra={"XDG_DATA_HOME": str(tmp_path / "data")})
    assert r.returncode == 0, r.stderr[-2000:]
    return r.stderr


def test_save_file_survives_a_restart(tmp_path):
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    first = boot(tmp_path)
    assert "create '~Pikmin dataFile'" in first
    second = boot(tmp_path)
    assert "create '" not in second, "the save made by the first run was not recognised"
