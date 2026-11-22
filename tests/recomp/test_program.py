import re
from pathlib import Path

import pytest

from tests.conftest import OVERRIDDEN_LIBS, ROOT

OVERRIDES = ROOT / "recomp/overrides.toml"


@pytest.fixture(scope="module")
def generated(game_image, tmp_path_factory):
    from recomp.program import generate

    out = tmp_path_factory.mktemp("gen")
    report = generate(game_image, OVERRIDES, out, shards=16)
    return report, out


def _all_gen(out: Path) -> str:
    return "".join(p.read_text() for p in sorted(out.glob("gen_*.c")))


def _addr_of(img, name: str) -> int:
    return next(f.addr for f in img.functions if f.name == name and f.parent is None)


def test_overrides_toml_matches_test_list():
    import tomllib

    libs = set(tomllib.loads(OVERRIDES.read_text())["libraries"]["override"])
    assert libs == OVERRIDDEN_LIBS


def test_report_clean(generated):
    report, _ = generated
    assert report.errors == []
    assert report.unimpl == []
    assert report.functions > 7000


def test_overrides_applied(generated):
    _, out = generated
    assert not re.search(r"^void f_801f4230\(", _all_gen(out), re.M)  # OSInit (os.a)
    assert "void hle_OSInit(ppc_ctx *c)" in (out / "hle_stubs.c").read_text()
    assert "{0x801f4230u, hle_OSInit}" in (out / "functable.c").read_text()


def test_entry_points_emitted(generated):
    _, out = generated
    assert re.search(r"^void f_80216bc4\(ppc_ctx \*c\)", _all_gen(out), re.M)  # _savefpr_20


def test_main_calls(generated, game_image):
    _, out = generated
    src = _all_gen(out)
    body = src[src.index("void f_800055c0("):]
    body = body[: body.index("\n}\n")]
    assert f"f_{_addr_of(game_image, 'Initialise__6SystemFv'):08x}(c);" in body
    assert f"f_{_addr_of(game_image, 'alloc__6SystemFUl'):08x}(c);" in body


def test_shard_count_and_unchanged_files_untouched(game_image, generated, tmp_path):
    from recomp.program import generate

    _, out = generated
    assert len(list(out.glob("gen_*.c"))) == 16
    before = {p.name: p.stat().st_mtime_ns for p in out.iterdir()}
    generate(game_image, OVERRIDES, out, shards=16)
    after = {p.name: p.stat().st_mtime_ns for p in out.iterdir() if p.name != "stamp"}
    assert all(after[n] == before[n] for n in after)
