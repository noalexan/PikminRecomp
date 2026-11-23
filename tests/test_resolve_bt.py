import subprocess
import sys

import pytest

from tests.conftest import ROOT

LOG = """[boot] something
=== pikmin crash: SIGSEGV ===
lr 0x80044760 r1 0x80400000 r3 0x00000000 r4 0x00000000 r5 0x00000000
backtrace:
./pikmin(f_80044758+0x10) [0x1]
=== end ===
[boot] second run
=== pikmin crash: ppc_bad_access ===
guest address 0x7ffffff0
last HLE GXBegin lr 0x8002a560
lr 0x8002a560 r1 0x80400000 r3 0x00000000 r4 0x00000000 r5 0x00000000
backtrace:
./pikmin(hle_GXBegin+0x10) [0x1]
./pikmin(f_8004281c_orig+0x10) [0x2]
=== end ===
"""


def test_resolves_crash_report(tmp_path):
    if not (ROOT / "orig/GPIP01/files/dataDir/build.map").exists():
        pytest.skip("game data not available")
    log = tmp_path / "crash.log"
    log.write_text(LOG)
    out = subprocess.run([sys.executable, ROOT / "tools/resolve_bt.py", log], capture_output=True, text=True,
                         check=True).stdout
    assert "update__11CullFrustumFffff" in out
    assert ".cpp" in out.split("update__11CullFrustumFffff")[1].splitlines()[0]  # owning object
    assert "update__11CamDataInfoFfR8Matrix4f" in out  # lr 0x8002a560 resolved
    assert "hle_GXBegin" in out
    assert "f_80044758" not in out  # first report is ignored
