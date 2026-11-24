"""Textures of the game as PNG pictures (RenderWare native textures for Direct3D 8 inside archive.dat).

  python tex.py <name> [<name> ...] [--out dir] [--game "<game folder>"]

Finds a texture by its name in archive.dat, decodes its first level (DXT1 / DXT3 / DXT5, or plain 16 / 32
bit) and writes <name>.png (transparent texels are drawn over a magenta and grey check so they are seen).
"""
import mmap, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "TMNT Mutant Melee")


def c565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def dxt(data, w, h, kind):
    px = [[(0, 0, 0, 255)] * w for _ in range(h)]
    pos = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = None
            if kind == 3:
                bits = struct.unpack_from("<Q", data, pos)[0]
                alpha = [((bits >> (4 * i)) & 15) * 17 for i in range(16)]
                pos += 8
            elif kind == 5:
                a0, a1 = data[pos], data[pos + 1]
                bits = int.from_bytes(data[pos + 2:pos + 8], "little")
                if a0 > a1:
                    t = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    t = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [t[(bits >> (3 * i)) & 7] for i in range(16)]
                pos += 8
            c0, c1, idx = struct.unpack_from("<HHI", data, pos)
            pos += 8
            a, b = c565(c0), c565(c1)
            if c0 > c1 or kind != 1:
                cols = [a + (255,), b + (255,),
                        tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                        tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
            else:
                cols = [a + (255,), b + (255,), tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
            for i in range(16):
                x, y = bx + (i & 3), by + (i >> 2)
                if x < w and y < h:
                    c = cols[(idx >> (2 * i)) & 3]
                    px[y][x] = c[:3] + (alpha[i],) if alpha else c
    return px


def plain(data, w, h, depth, fmt):
    px = []
    for y in range(h):
        row = []
        for x in range(w):
            if depth == 32:
                b, g, r, a = data[(y * w + x) * 4:(y * w + x) * 4 + 4]
                row.append((r, g, b, a if fmt & 0x0F00 != 0x0600 else 255))
            else:
                v = struct.unpack_from("<H", data, (y * w + x) * 2)[0]
                kind = fmt & 0x0F00
                if kind == 0x0100:    # 1555
                    row.append((((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31,
                                255 if v & 0x8000 else 0))
                elif kind == 0x0300:  # 4444
                    row.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, (v >> 12) * 17))
                else:                 # 565
                    row.append(c565(v) + (255,))
        px.append(row)
    return px


def write_png(path, px):
    h, w = len(px), len(px[0])
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = px[y][x]
            check = (255, 0, 255) if ((x >> 3) + (y >> 3)) & 1 else (128, 128, 128)
            raw += bytes((c * a + k * (255 - a)) // 255 for c, k in zip((r, g, b), check))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def find(m, name):
    key = name.encode() + b"\0"
    i = 0
    while True:
        i = m.find(key, i)
        if i < 0:
            return None
        if m[i - 8:i - 4] == b"\x08\0\0\0":
            return i
        i += 1


def info(m, i):
    fmt, alpha, w, h, depth, mips, typ, comp = struct.unpack_from("<IIHHBBBB", m, i + 64)
    return dict(fmt=fmt, alpha=alpha, w=w, h=h, depth=depth, mips=mips, comp=comp)


def decode(m, i):
    t = info(m, i)
    pos = i + 80
    if t["fmt"] & 0x2000 or t["fmt"] & 0x4000:  # a palette comes first
        raise ValueError("palette textures are not read")
    size = struct.unpack_from("<I", m, pos)[0]
    data = m[pos + 4:pos + 4 + size]
    if t["comp"]:
        return t, dxt(data, t["w"], t["h"], t["comp"])
    return t, plain(data, t["w"], t["h"], t["depth"], t["fmt"])


def main(argv):
    names, out, game = [], ".", GAME
    i = 1
    while i < len(argv):
        if argv[i] == "--out":
            out = argv[i + 1]; i += 2
        elif argv[i] == "--game":
            game = argv[i + 1]; i += 2
        else:
            names.append(argv[i]); i += 1
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(game, "archive.dat"), "rb") as f:
        m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        for n in names:
            at = find(m, n)
            if at is None:
                print(n, "not found")
                continue
            try:
                t, px = decode(m, at)
            except Exception as e:  # noqa
                print(n, "cannot be read:", e)
                continue
            p = os.path.join(out, n + ".png")
            write_png(p, px)
            print("%s %dx%d alpha %d comp %d -> %s" % (n, t["w"], t["h"], t["alpha"], t["comp"], p))


if __name__ == "__main__":
    main(sys.argv)
