import re

import pytest

from tests.conftest import MAP, ROOT


def test_ctors_bounds_match_map():
    if not MAP.exists():
        pytest.skip("disc data not extracted")
    src = (ROOT / "runtime/rt/boot.cpp").read_text()
    start = int(re.search(r"kCtorsStart\s*=\s*(0x[0-9a-fA-F]+)", src)[1], 16)
    end = int(re.search(r"kCtorsEnd\s*=\s*(0x[0-9a-fA-F]+)", src)[1], 16)
    text = MAP.read_bytes().decode("latin-1").replace("\r", "")
    assert start == int(re.search(r"^\s+_ctors\s+([0-9a-f]{8})$", text, re.M)[1], 16)
    m = re.search(r"^\s+\.ctors\s+([0-9a-f]{8}) ([0-9a-f]{8}) [0-9a-f]{8}$", text, re.M)
    assert end == int(m[1], 16) + int(m[2], 16)
