"""The launcher's icon (src/launcher/assets/launcher.ico): a cell of the game's honeycomb in a steel rim,
and across it a turtle's mask - Leonardo's blue bandana with its two eye holes, the knot and the two loose
ends, as the launcher draws it beside its title. Drawn large and scaled down. Needs Pillow.

    python tools/launcher_icon.py src/launcher/assets/launcher.ico [preview.png]"""
import math
import sys

from PIL import Image, ImageDraw

S = 1024
C = S / 2
STEEL, BLACK, PAPER = (143, 163, 184), (0, 0, 0), (243, 248, 246)
BASE, BLUE = (4, 11, 13), (46, 134, 232)


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def hexagon(r):
    return [(C + r * math.cos(math.radians(60 * i)), C + r * math.sin(math.radians(60 * i))) for i in range(6)]


def quad(p0, p1, p2, n=24):
    """Points of a quadratic curve from p0 to p2 with the control point p1 (without p0)."""
    out = []
    for i in range(1, n + 1):
        t = i / n
        out.append(((1 - t) ** 2 * p0[0] + 2 * t * (1 - t) * p1[0] + t * t * p2[0],
                    (1 - t) ** 2 * p0[1] + 2 * t * (1 - t) * p1[1] + t * t * p2[1]))
    return out


# The mask as the launcher draws it (view.cpp, kMask*), in its 272 x 90 box.
BAND = [(4, 30)] + quad((4, 30), (100, 10), (196, 30)) + [(196, 62)] + quad((196, 62), (100, 44), (4, 62))
EYE_L = [(50, 34)] + quad((50, 34), (70, 27), (90, 38)) + quad((90, 38), (72, 50), (54, 46))
EYE_R = [(150, 34)] + quad((150, 34), (130, 27), (110, 38)) + quad((110, 38), (128, 50), (146, 46))
KNOT = [(190, 34), (208, 30), (212, 58), (194, 62)]
END_HIGH = [(206, 36), (262, 12), (254, 28), (270, 36), (210, 50)]
END_LOW = [(206, 48), (256, 60), (246, 70), (258, 84), (204, 60)]
SHEEN = [(18, 30)] + quad((18, 30), (100, 14), (182, 30))


def main(out, preview=None):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.polygon(hexagon(500), fill=BLACK)
    d.polygon(hexagon(482), fill=STEEL)
    d.polygon(hexagon(446), fill=BLACK)
    d.polygon(hexagon(428), fill=mix(BASE, BLUE, 0.42))
    d.polygon(hexagon(330), fill=mix(BASE, BLUE, 0.28))

    # (taller than the launcher draws it: at 16 pixels the band must still be a band)
    kx, ky, ox, oy = 3.05, 4.5, 150.0, 318.0

    def place(points):
        return [(ox + kx * x, oy + ky * y) for x, y in points]

    def part(points, fill, line=22):
        p = place(points)
        d.line(p + [p[0], p[1]], fill=BLACK, width=line, joint="curve")
        d.polygon(p, fill=fill)

    part(END_LOW, mix(BLUE, BLACK, 0.24))
    part(END_HIGH, BLUE)
    part(BAND, BLUE)
    d.line(place(SHEEN), fill=mix(BLUE, (255, 255, 255), 0.38), width=10, joint="curve")
    part(EYE_L, PAPER, 16)
    part(EYE_R, PAPER, 16)
    part(KNOT, BLUE)

    if preview:
        im.resize((256, 256), Image.LANCZOS).save(preview)
    im.resize((256, 256), Image.LANCZOS).save(out, format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
    print("written:", out)


if __name__ == "__main__":
    main(*sys.argv[1:3])
