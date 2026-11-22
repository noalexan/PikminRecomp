import struct
import subprocess

import pytest

from tests.conftest import ROOT

PIKMIN = ROOT / "build/cmake/pikmin"


def synthetic_iso(path):
    """Minimal valid GPIP01 image: header, one-entry FST, and an empty DOL header."""
    d = bytearray(0x3000)
    d[0:6] = b"GPIP01"
    struct.pack_into(">I", d, 0x1C, 0xC2339F3D)
    struct.pack_into(">I", d, 0x420, 0x2000)  # DOL offset
    struct.pack_into(">II", d, 0x424, 0x1000, 12)  # FST offset/size
    struct.pack_into(">III", d, 0x1000, 0x01000000, 0, 1)  # root only
    path.write_bytes(bytes(d))
    return path


def test_no_gpu_says_try_headless(tmp_path):
    if not PIKMIN.exists():
        pytest.skip("pikmin executable not built")
    iso = synthetic_iso(tmp_path / "mini.iso")
    env = {"HOME": str(tmp_path), "XDG_CONFIG_HOME": str(tmp_path / "cfg"), "PATH": "/usr/bin:/bin",
           "VK_ICD_FILENAMES": "/nonexistent.json", "VK_DRIVER_FILES": "/nonexistent.json",
           "SDL_VIDEO_DRIVER": "offscreen"}
    r = subprocess.run([PIKMIN, "--disc", iso], capture_output=True, text=True, env=env, timeout=60)
    assert r.returncode == 2, r.stderr[-2000:]
    lines = [l for l in r.stderr.strip().splitlines() if l.startswith("pikmin:")]
    assert len(lines) == 1 and "try --headless" in lines[0], r.stderr[-2000:]
