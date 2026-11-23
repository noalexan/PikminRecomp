import re
import subprocess

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"


def run(args, tmp_path, env_extra=None, timeout=60, **kw):
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    env = {"XDG_CONFIG_HOME": str(tmp_path / "cfg"), "HOME": str(tmp_path), "PATH": "/usr/bin:/bin"}
    env.update(env_extra or {})
    return subprocess.run([PIKMIN, *args], capture_output=True, text=True, env=env, timeout=timeout, **kw)


def test_runs_without_a_disc_image(tmp_path):
    # No --disc and no config: the extracted tree (installed, or orig/ in the checkout) is used,
    # and main.dol comes from the binary itself.
    if not (ROOT / "orig/GPIP01/sys/paths.txt").exists():
        pytest.skip("extracted game data not available")
    r = run(["--headless", "--fast", "--frames", "60"], tmp_path)
    assert r.returncode == 0, r.stderr
    assert "orig/GPIP01" in r.stderr


def test_run_writes_log_file(tmp_path):
    if not (ROOT / "orig/GPIP01/sys/paths.txt").exists():
        pytest.skip("extracted game data not available")
    r = run(["--headless", "--fast", "--frames", "60"], tmp_path, {"XDG_STATE_HOME": str(tmp_path / "state")})
    assert r.returncode == 0, r.stderr
    logs = list((tmp_path / "state/pikminrecomp/logs").glob("pikmin-*.log"))
    assert len(logs) == 1
    text = logs[0].read_text()
    assert "[run] pikmin " in text
    assert "(Debug)" in text or "(Release)" in text
    assert "clock: fast" in text
    assert "[boot]" in text


def test_log_dir_unwritable_still_runs(tmp_path):
    if not (ROOT / "orig/GPIP01/sys/paths.txt").exists():
        pytest.skip("extracted game data not available")
    blocker = tmp_path / "state"
    blocker.write_text("")  # a file where the directory should be
    r = run(["--headless", "--fast", "--frames", "30"], tmp_path, {"XDG_STATE_HOME": str(blocker)})
    assert r.returncode == 0, r.stderr
    assert "cannot create log directory" in r.stderr
    assert "[boot]" in r.stderr


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


def test_every_referenced_gx_function_has_a_shim():
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    gen = ROOT / "build/cmake/gen"
    referenced = set()
    for f in gen.glob("gen_*.c"):
        referenced |= set(re.findall(r"\b(hle_GX\w+)\(c\)", f.read_text()))
    out = subprocess.run(["nm", PIKMIN], capture_output=True, text=True, check=True).stdout
    kinds = {line.split()[-1]: line.split()[-2] for line in out.splitlines() if len(line.split()) >= 2}
    weak = sorted(n for n in referenced if kinds.get(n) != "T")
    assert len(referenced) == 77 and not weak, weak
