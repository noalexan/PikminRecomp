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


def _bad_overrides(tmp_path, text):
    p = tmp_path / "overrides.toml"
    p.write_text(text)
    return p


def test_malformed_overrides_is_recomp_error(game_image, tmp_path):
    from recomp.emit import RecompError
    from recomp.program import load_overrides

    p = _bad_overrides(tmp_path, "[libraries\n")
    with pytest.raises(RecompError, match="overrides.toml"):
        load_overrides(game_image, p)


def test_override_must_be_list(game_image, tmp_path):
    from recomp.emit import RecompError
    from recomp.program import load_overrides

    p = _bad_overrides(tmp_path, '[libraries]\noverride = "gx.a"\n')
    with pytest.raises(RecompError, match="list"):
        load_overrides(game_image, p)


def test_unknown_library_rejected(game_image, tmp_path):
    from recomp.emit import RecompError
    from recomp.program import load_overrides

    p = _bad_overrides(tmp_path, '[libraries]\noverride = ["gx.A"]\n')
    with pytest.raises(RecompError, match="gx.A"):
        load_overrides(game_image, p)


def test_cli_reports_bad_overrides_without_traceback(game_image, tmp_path):
    import subprocess
    import sys

    from tests.conftest import DOL, MAP

    p = _bad_overrides(tmp_path, "[libraries\n")
    r = subprocess.run([sys.executable, "-m", "recomp", "--dol", DOL, "--map", MAP, "--overrides", p,
                        "--out", tmp_path / "out"], capture_output=True, text=True, cwd=ROOT)
    assert r.returncode == 2
    assert "Traceback" not in r.stderr and "overrides.toml" in r.stderr


def test_hle_stubs_are_weak(generated):
    _, out = generated
    lines = [l for l in (out / "hle_stubs.c").read_text().splitlines() if "void hle_" in l]
    assert lines and all(l.startswith("__attribute__((weak)) void hle_") for l in lines)


def test_jaudio_entry_points_overridden(generated, game_image):
    _, out = generated
    src = _all_gen(out)
    for name in ("Jac_Init", "StartAudioThread"):
        addr = _addr_of(game_image, name)
        assert not re.search(rf"^void f_{addr:08x}\(", src, re.M), name
