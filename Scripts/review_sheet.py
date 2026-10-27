"""Project Rain - art review sheet: our shot next to fixed Monument Valley references, same layout every round.
    python3 Scripts/review_sheet.py Saved/Shots/<shot>.png SourceArt/Review/<name>.jpg
The references are in the gitignored SourceArt/Reference (not for the repository or the report)."""
import sys

from PIL import Image, ImageDraw

REF = "/Users/guocheng/ProjectRain/SourceArt/Reference/"
REFS = [("REF A: Monument Valley 2 (mv_2_09)", REF + "MonumentValley2/mv_2_09.jpg"),
        ("REF B: Monument Valley 2 (mv_2_06)", REF + "MonumentValley2/mv_2_06.jpg"),
        ("REF C: Monument Valley 2 (mv_2_03)", REF + "MonumentValley2/mv_2_03.jpg")]


def main(shot, out):
    W, H = 960, 540
    sheet = Image.new("RGB", (2 * W, 2 * H), (255, 255, 255))
    tiles = [("OURS (UE 5.7 render)", shot)] + REFS
    for i, (label, path) in enumerate(tiles):
        im = Image.open(path).convert("RGB").resize((W, H), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 8 * len(label) + 16, 22], fill=(0, 0, 0))
        d.text((8, 5), label, fill=(255, 255, 255))
        sheet.paste(im, ((i % 2) * W, (i // 2) * H))
    sheet.save(out, quality=92)
    print(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
