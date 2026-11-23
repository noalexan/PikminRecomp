import resource
import subprocess

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"
DATA = ROOT / "orig/GPIP01/sys/paths.txt"


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def crash(tmp_path, *args):
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    env = {"XDG_CONFIG_HOME": str(tmp_path / "cfg"), "XDG_STATE_HOME": str(tmp_path / "state"),
           "HOME": str(tmp_path), "PATH": "/usr/bin:/bin"}
    r = subprocess.run([PIKMIN, "--headless", "--fast", *args], capture_output=True, text=True, env=env,
                       timeout=60, preexec_fn=no_core)
    logs = list((tmp_path / "state/pikminrecomp/logs").glob("pikmin-*.log"))
    assert len(logs) == 1
    return r, logs[0].read_text(errors="replace")


def test_segv_report(tmp_path):
    r, log = crash(tmp_path, "--crash-test", "segv")
    assert r.returncode == -11, r.stderr[-3000:]
    for text in (r.stderr, log):
        assert "=== pikmin crash: SIGSEGV" in text
        assert "fault address 0x0000000000000000" in text
        assert "retrace 30" in text
        assert "guest thread 0x8" in text
        assert "lr 0x" in text and " r3 0x" in text
        assert "in IRQ callback: " in text
        assert "backtrace:" in text
        assert "recent events" in text
        assert " hle " in text
        assert "=== end ===" in text


def test_bad_access_one_report(tmp_path):
    r, log = crash(tmp_path, "--crash-test", "bad_access")
    assert r.returncode == -6, r.stderr[-3000:]
    assert r.stderr.count("=== pikmin crash:") == 1
    assert "=== pikmin crash: ppc_bad_access" in r.stderr
    assert "guest address 0x7ffffff0" in r.stderr
    assert "=== end ===" in log


def test_abort_report(tmp_path):
    r, _ = crash(tmp_path, "--crash-test", "abort")
    assert r.returncode == -6
    assert "=== pikmin crash: SIGABRT" in r.stderr


def test_double_fault_terminates(tmp_path):
    r, _ = crash(tmp_path, "--crash-test", "double_fault")
    assert r.returncode == -11
    assert "=== pikmin crash: SIGSEGV" in r.stderr


def test_watchdog_report(tmp_path):
    r, log = crash(tmp_path, "--watchdog", "1", "--crash-test", "hang")
    assert r.returncode == 3, r.stderr[-3000:]
    assert "=== pikmin crash: watchdog: no retrace for 1 s" in r.stderr
    assert "=== end ===" in log
