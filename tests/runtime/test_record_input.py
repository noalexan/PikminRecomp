import pytest

from tests.conftest import ROOT
from tests.runtime.test_cli import run

DATA = ROOT / "orig/GPIP01/sys/paths.txt"
SCRIPT = """300 START
303 -
400 A
403 -
660 - 0 127
670 - 0 0
700 A+B 10 -20 30 -40
703 SWARM
710 -
"""


def states(text, last):
    """Per-retrace (buttons, sticks) from an input script, as the runtime applies it."""
    entries = []
    for line in text.splitlines():
        line = line.split("#")[0].split()
        if not line:
            continue
        frame, buttons, *sticks = line
        names = frozenset() if buttons == "-" else frozenset(buttons.split("+"))
        sticks = tuple(int(v) for v in sticks) + (0,) * (4 - len(sticks))
        entries.append((int(frame), names, sticks))
    out, cur, i = [], None, 0
    for f in range(last + 1):
        while i < len(entries) and entries[i][0] <= f:
            cur = entries[i][1:]
            i += 1
        out.append(cur or (frozenset(), (0, 0, 0, 0)))
    return out


def test_record_roundtrip(tmp_path):
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    script = tmp_path / "in.txt"
    script.write_text(SCRIPT)
    rec, rec2 = tmp_path / "rec.txt", tmp_path / "rec2.txt"
    r = run(["--headless", "--fast", "--frames", "800", "--input-script", str(script), "--record-input", str(rec)],
            tmp_path)
    assert r.returncode == 0, r.stderr[-2000:]
    text = rec.read_text()
    assert text.startswith("# pikmin ") and "clock=fast" in text.splitlines()[0]
    # PADRead starts after boot: compare from the first recorded frame on.
    first = int(text.splitlines()[1].split()[0])
    assert first < 300
    assert states(text, 790)[first:] == states(SCRIPT, 790)[first:]

    r = run(["--headless", "--fast", "--frames", "800", "--input-script", str(rec), "--record-input", str(rec2)],
            tmp_path)
    assert r.returncode == 0, r.stderr[-2000:]
    assert rec2.read_text().splitlines()[1:] == text.splitlines()[1:]


def test_record_input_named_in_log_header(tmp_path):
    if not DATA.exists():
        pytest.skip("extracted game data not available")
    rec = tmp_path / "rec.txt"
    r = run(["--headless", "--fast", "--frames", "30", "--record-input", str(rec)], tmp_path,
            {"XDG_STATE_HOME": str(tmp_path / "state")})
    assert r.returncode == 0, r.stderr
    log = next((tmp_path / "state/pikminrecomp/logs").glob("*.log")).read_text()
    assert f"input recording: {rec}" in log
