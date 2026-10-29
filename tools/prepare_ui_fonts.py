"""Prepare the private static UI font family from the pinned upstream OTF files.

Authoring only: fonttools==4.60.1. The application has no Python dependency.
Usage: python tools/prepare_ui_fonts.py SOURCE_DIRECTORY
"""
from pathlib import Path
import hashlib
import sys
from fontTools.ttLib import TTFont

SOURCE = Path(sys.argv[1])
DESTINATION = Path(__file__).resolve().parents[1] / "resources" / "fonts"
DESTINATION.mkdir(exist_ok=True)
EXPECTED_HASHES = {
    "Regular": "69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68",
    "Medium": "b46988ef13e8bac08f3933af686eaf770972994f9b6d335be0184d60169b5431",
    "Bold": "5a6ceb287ed2fc6cfc6213144ebea68cbd94b20fc9eb873d8486493bf02d9bda",
}

for style in ("Regular", "Medium", "Bold"):
    source = SOURCE / f"NotoSansKR-{style}.otf"
    if hashlib.sha256(source.read_bytes()).hexdigest() != EXPECTED_HASHES[style]:
        raise ValueError(f"Unexpected source font: {source}")
    font = TTFont(source, recalcTimestamp=False)
    assert "fvar" not in font, "Only static upstream fonts are accepted"
    for record in font["name"].names:
        if record.nameID in (1, 3, 4, 6, 16, 18, 21):
            value = record.toUnicode().replace("Noto Sans KR", "NookBit UI Sans").replace("NotoSansKR", "NookBitUISans")
            record.string = value.encode(record.getEncoding())
    cff = font["CFF "].cff
    cff.fontNames = [name.replace("NotoSansKR", "NookBitUISans") for name in cff.fontNames]
    for top in cff.topDictIndex:
        for key in ("FullName", "FamilyName"):
            value = getattr(top, key, None)
            if value:
                setattr(top, key, value.replace("Noto Sans KR", "NookBit UI Sans"))
    font.save(DESTINATION / f"NookBitUISans-{style}.otf")
    print(f"{source.name} SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()}")

# Keep the upstream license and copyright notice available in the distribution.
font = TTFont(SOURCE / "NotoSansKR-Regular.otf")
copyright_notice = next(record.toUnicode() for record in font["name"].names if record.nameID == 0)
(DESTINATION / "OFL.txt").write_text(copyright_notice + "\n\n" + (SOURCE / "OFL.txt").read_text(), encoding="utf-8")
