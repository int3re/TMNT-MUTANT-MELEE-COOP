"""The digit 4 for the game's menu font, as a C++ header (src/mod/glyph4.h).

The Russian release drew Ч over the font's 4 and Б over its 6. The mod gives the font a real 6 (the font's
own 9 turned half a turn) and a real 4 - which no glyph of the font gives by turning, so it is drawn here
in the font's manner: a white body with a black edge and a soft rim, 25 x 45 texels, the body on the rows
the font's other digits take (10..38).

  python tools/glyph_four.py            writes src/mod/glyph4.h
  python tools/glyph_four.py out.png    also saves the glyph as a picture

Needs Pillow and numpy.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H, SS = 25, 45, 8


def paint():
    big = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(big)

    def pts(*p):
        return [(x * SS, y * SS) for x, y in p]

    top, bot = 10.0, 38.0      # the rows of the font's digits
    sx0, sx1 = 13.0, 19.5      # the upright
    by0, by1 = 26.0, 32.0      # the bar
    d.polygon(pts((sx0, top), (sx1, top), (sx1, bot), (sx0, bot)), fill=255)
    d.polygon(pts((2.5, by0), (22.5, by0), (22.5, by1), (2.5, by1)), fill=255)
    # the slanted stroke from the bar's left end up to the upright's top
    d.polygon(pts((2.5, by0), (sx0 - 3.0, top), (sx0 + 3.0, top), (8.5, by0 + 0.5), (8.5, by1 - 3), (2.5, by1 - 3)), fill=255)
    body = np.array(big.resize((W, H), Image.LANCZOS)).astype(float) / 255
    grow = np.array(big.filter(ImageFilter.MaxFilter(2 * int(1.6 * SS) + 1)).resize((W, H), Image.LANCZOS)).astype(float) / 255
    rim = np.array(big.filter(ImageFilter.MaxFilter(2 * int(2.5 * SS) + 1)).resize((W, H), Image.LANCZOS)).astype(float) / 255
    alpha = np.clip(np.maximum(grow, rim * 0.35), 0, 1)
    return (body * 255).astype(np.uint8), (alpha * 255).astype(np.uint8)


def rows(a):
    return ",\n".join("    " + ", ".join("%3d" % v for v in row) for row in a)


def main(argv):
    body, alpha = paint()
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = ("// The digit 4 of the menu font, drawn by tools/glyph_four.py: white level and opacity per texel.\n"
            "#pragma once\n#include <cstdint>\n\nnamespace glyph4 {\n\n"
            "constexpr int kWidth = %d, kHeight = %d;\n\n" % (W, H) +
            "inline const uint8_t kBody[kWidth * kHeight] = {\n" + rows(body) + "};\n\n"
            "inline const uint8_t kAlpha[kWidth * kHeight] = {\n" + rows(alpha) + "};\n\n"
            "}  // namespace glyph4\n")
    with open(os.path.join(repo, "src", "mod", "glyph4.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if len(argv) > 1:
        rgba = np.zeros((H, W, 4), dtype=np.uint8)
        rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = body
        rgba[..., 3] = alpha
        Image.fromarray(rgba, "RGBA").save(argv[1])
    print("src/mod/glyph4.h written (%dx%d)" % (W, H))


if __name__ == "__main__":
    main(sys.argv)
