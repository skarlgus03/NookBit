# Bundled UI fonts

NookBit UI Sans is Noto Sans KR 2.004 with a private family/PostScript name.
Only naming metadata was changed; outlines, coverage and static weights are kept.
The private name prevents collisions with installed Noto Sans KR variable fonts.
The application embeds Regular (400), Medium (500) and Bold (700). Requests for
600 use the closest available face selected by Qt; no variable weight axis is used.

Source: https://github.com/notofonts/noto-cjk/tree/Sans2.004/Sans/SubsetOTF/KR
License: SIL Open Font License 1.1; see [OFL.txt](OFL.txt).
The original copyright and license metadata are retained in each font, and the
distribution includes `licenses/NotoSansKR-OFL.txt`.

To reproduce, download `NotoSansKR-Regular.otf`, `NotoSansKR-Medium.otf` and
`NotoSansKR-Bold.otf` from that tag, plus its root `LICENSE` as `OFL.txt`.
Install `fonttools==4.60.1` in an authoring environment and run:

```text
python tools/prepare_ui_fonts.py SOURCE_DIRECTORY
```

The script checks upstream SHA-256 hashes before changing the names. Neither
Python nor fonttools is required to build or run the application from this repo.
