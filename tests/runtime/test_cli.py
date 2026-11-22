import subprocess

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"


def run(args, tmp_path, **kw):
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    env = {"XDG_CONFIG_HOME": str(tmp_path / "cfg"), "HOME": str(tmp_path), "PATH": "/usr/bin:/bin"}
    return subprocess.run([PIKMIN, *args], capture_output=True, text=True, env=env, timeout=60, **kw)


def test_no_disc_explains_both_options(tmp_path):
    r = run(["--headless"], tmp_path)
    assert r.returncode == 2
    assert "--disc" in r.stderr and "config.toml" in r.stderr
    assert len(r.stderr.strip().splitlines()) == 1


def test_non_gamecube_file_is_one_line_error(tmp_path):
    junk = tmp_path / "junk.iso"
    junk.write_bytes(b"\0" * 4096)
    r = run(["--headless", "--disc", str(junk)], tmp_path)
    assert r.returncode == 2
    assert "not a GameCube disc" in r.stderr
    assert len(r.stderr.strip().splitlines()) == 1


def test_strong_hle_definitions_linked():
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    out = subprocess.run(["nm", PIKMIN], capture_output=True, text=True, check=True).stdout
    kinds = {line.split()[-1]: line.split()[-2] for line in out.splitlines() if len(line.split()) >= 2}
    assert kinds.get("hle_OSReport") == "T"
    assert kinds.get("hle_GXInit") == "T"
    assert kinds.get("hle_OSAllocFromHeap") == "W"  # still an unimplemented weak stub
