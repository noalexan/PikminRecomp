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
