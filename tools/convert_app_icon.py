"""Convert the approved PNG artwork to the app's multi-resolution Windows icon.

Requires Pillow. No crop, background removal or redesign is performed.
"""

import argparse
from pathlib import Path

from PIL import Image


# 원본 PNG와 투명도를 유지하고 여러 해상도의 Windows 아이콘을 생성하는 함수.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    destination = Path(__file__).resolve().parents[1] / "resources" / "icons"
    sizes = [(size, size) for size in (16, 24, 32, 48, 64, 128, 256)]

    with Image.open(args.source) as source:
        if source.format != "PNG" or source.width != source.height:
            parser.error("Source must be a square PNG.")
        artwork = source.convert("RGBA")

    destination.mkdir(parents=True, exist_ok=True)
    # 승인된 PNG는 재인코딩하지 않고 원본 바이트 그대로 보관한다.
    png = destination / "nookbit.png"
    if args.source.resolve() != png.resolve():
        png.write_bytes(args.source.read_bytes())
    icon = destination / "nookbit.ico"
    artwork.save(icon, format="ICO", sizes=sizes)

    with Image.open(icon) as result:
        if result.ico.sizes() != set(sizes):
            raise RuntimeError("Missing icon resolutions.")
        for size in sizes:
            frame = result.ico.getimage(size)
            if frame.size != size or "A" not in frame.getbands():
                raise RuntimeError("Invalid icon frame or missing alpha channel.")
            if artwork.getpixel((0, 0))[3] == 0 and frame.getpixel((0, 0))[3] != 0:
                raise RuntimeError("Transparent background was not preserved.")
    print(f"App icon converted and verified: {icon}")


if __name__ == "__main__":
    main()
