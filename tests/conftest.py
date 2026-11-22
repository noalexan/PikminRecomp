from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DOL = ROOT / "orig/GPIP01/sys/main.dol"
MAP = ROOT / "orig/GPIP01/files/dataDir/build.map"


@pytest.fixture(scope="session")
def game_image():
    if not DOL.exists() or not MAP.exists():
        pytest.skip("disc data not extracted (orig/GPIP01 missing)")
    from recomp.image import load_image

    return load_image(DOL, MAP)

# Libraries replaced by host code (must match recomp/overrides.toml).
OVERRIDDEN_LIBS = {
    "gx.a", "vi.a", "pad.a", "si.a", "dvd.a", "card.a", "os.a", "ar.a", "exi.a", "ai.a",
    "dsp.a", "db.a", "hio.a", "base.a", "amcExi2.a", "amcnotstub.a", "OdemuExi2.a",
    "odenotstub.a", "TRK_MINNOW_DOLPHIN.a",
}
