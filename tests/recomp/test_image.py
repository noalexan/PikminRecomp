import struct
from pathlib import Path

import pytest

from recomp.image import Function, ImageError, load_image


def make_dol(path: Path, text_addr: int, text: bytes) -> Path:
    """Minimal DOL: one text section at offset 0x100."""
    hdr = bytearray(0x100)
    struct.pack_into(">I", hdr, 0x00, 0x100)  # T0 offset
    struct.pack_into(">I", hdr, 0x48, text_addr)  # T0 address
    struct.pack_into(">I", hdr, 0x90, len(text))  # T0 size
    struct.pack_into(">I", hdr, 0xE0, text_addr)  # entry
    path.write_bytes(bytes(hdr) + text)
    return path


def make_map(path: Path, text_lines: list[str], text_addr: int, text_size: int) -> Path:
    lines = [".text section layout", "  Starting        Virtual", "  address  Size   address", "  -----------------------"]
    lines += text_lines
    lines += ["", "", "Memory map:", "                   Starting Size     File",
              "                   address           Offset",
              f"            .text  {text_addr:08x} {text_size:08x} 00000100", ""]
    path.write_bytes("\r\n".join(lines).encode("latin-1"))
    return path


TEXT_ADDR = 0x80003100


@pytest.fixture
def files(tmp_path):
    def build(text_lines, text_addr=TEXT_ADDR, map_addr=TEXT_ADDR, size=0x200):
        dol = make_dol(tmp_path / "t.dol", text_addr, bytes(size))
        mp = make_map(tmp_path / "t.map", text_lines, map_addr, size)
        return dol, mp
    return build


def test_parses_layout_line(files):
    img = load_image(*files(["  00000040 000114 80003140  4 __start \tos.a __start.c"]))
    assert img.functions == [Function("__start", 0x80003140, 0x114, "os.a", "__start.c", None)]


def test_object_without_library(files):
    img = load_image(*files(["  00000000 000008 80003100  4 main \tsysBootup.o "]))
    f = img.functions[0]
    assert (f.name, f.library, f.obj) == ("main", None, "sysBootup.o")


def test_entry_of_line(files):
    img = load_image(*files([
        "  00000000 00004c 80003100  4 __save_fpr \tRuntime.PPCEABI.H.a runtime.c",
        "  00000018 000000 80003118 _savefpr_20 (entry of __save_fpr) \tRuntime.PPCEABI.H.a runtime.c",
    ]))
    e = img.function_at(0x80003118)
    assert e.parent == "__save_fpr"
    assert e.size == 0x8000314c - 0x80003118


def test_skips_section_markers_and_ellipsis(files):
    img = load_image(*files([
        "  00000000 000100 80003100  1 .text \tfoo.a foo.c",
        "  00000000 000010 80003100  4 ...text.0 \tfoo.a foo.c",
        "  00000000 000010 80003100  4 real \tfoo.a foo.c",
    ]))
    assert [f.name for f in img.functions] == ["real"]


def test_missing_dol_message(files):
    _, mp = files(["  00000000 000010 80003100  4 real \tfoo.a foo.c"])
    with pytest.raises(ImageError) as e:
        load_image(Path("nope.dol"), mp)
    assert "nope.dol" in str(e.value) and "not found" in str(e.value)


def test_map_dol_mismatch(files):
    with pytest.raises(ImageError, match="does not match"):
        load_image(*files(["  00000000 000010 80003100  4 real \tfoo.a foo.c"], map_addr=0x80004000))


def test_real_image(game_image):
    img = game_image
    assert img.function_at(0x80003140).name == "__start"
    assert img.function_at(0x8021b194).name == "strlen"
    assert img.function_at(0x80216bc4).parent == "__save_fpr"
    assert len([f for f in img.functions if f.parent]) == 24
    assert img.read_u32(0x800055c0) == 0x7C0802A6
