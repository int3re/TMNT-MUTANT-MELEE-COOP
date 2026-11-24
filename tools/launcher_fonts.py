"""The launcher's fonts (src/launcher/assets/fonts), all under the SIL Open Font License (OFL-*.txt beside
them; none declares a reserved font name). Needs fontTools and brotli.

    python tools/launcher_fonts.py <folder with the .woff2 pieces> src/launcher/assets/fonts
        Onest and JetBrains Mono: static TTF files made from the Google Fonts pieces (variable, one file per
        range of characters) that an exported design page carries. For each family and weight the Latin and
        the Cyrillic piece are pinned at that weight and merged into one file.

    python tools/launcher_fonts.py --display <DelaGothicOne-Regular.ttf> src/launcher/assets/fonts
        The headings' font, Dela Gothic One (github.com/google/fonts, ofl/delagothicone): the Latin and the
        Cyrillic part of it. The whole file is 2.5 MB, most of it Japanese the launcher never writes."""
import glob, os, sys
from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

WANT = {"Onest": [(400, "Regular"), (500, "Medium"), (600, "SemiBold"), (700, "Bold")],
        "JetBrains Mono": [(500, "Medium"), (700, "Bold")]}


def set_names(font, family, style, weight):
    name = font["name"]
    full = family + " " + style
    ps = family.replace(" ", "") + "-" + style
    for rec in list(name.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 25):
            name.removeNames(nameID=rec.nameID)
    for pid, eid, lang in ((3, 1, 0x409), (1, 0, 0)):
        bold = style == "Bold"
        name.setName(family if style in ("Regular", "Bold") else full, 1, pid, eid, lang)
        name.setName("Bold" if bold else "Regular", 2, pid, eid, lang)
        name.setName(ps + ";tmnt2net", 3, pid, eid, lang)
        name.setName(full, 4, pid, eid, lang)
        name.setName(ps, 6, pid, eid, lang)
        name.setName(family, 16, pid, eid, lang)
        name.setName(style, 17, pid, eid, lang)
    font["OS/2"].usWeightClass = weight
    fs = font["OS/2"].fsSelection & ~0x61  # italic, bold, regular
    font["OS/2"].fsSelection = fs | (0x20 if style == "Bold" else 0x40)
    font["head"].macStyle = 1 if style == "Bold" else 0


def main(src, dst):
    os.makedirs(dst, exist_ok=True)
    pieces = {}
    for p in glob.glob(os.path.join(src, "*.woff2")):
        f = TTFont(p)
        pieces.setdefault(f["name"].getDebugName(1), []).append(p)
    for family, weights in WANT.items():
        files = pieces.get(family)
        if not files:
            print("no pieces of", family)
            continue
        for weight, style in weights:
            parts = []
            for i, p in enumerate(sorted(files, key=lambda q: -len(TTFont(q).getBestCmap()))):
                f = TTFont(p)
                f.flavor = None
                f = instantiateVariableFont(f, {"wght": weight})
                for tag in ("STAT", "avar", "MVAR", "HVAR", "DSIG", "GDEF", "GSUB", "GPOS"):
                    if tag in f:  # (the pieces' layout tables do not merge: plain glyphs are enough here)
                        del f[tag]
                tmp = os.path.join(dst, "_part%d.ttf" % i)
                f.save(tmp)
                parts.append(tmp)
            merged = Merger().merge(parts)
            set_names(merged, family, style, weight)
            out = os.path.join(dst, family.replace(" ", "") + "-" + style + ".ttf")
            merged.save(out)
            for t in parts:
                os.remove(t)
            cmap = TTFont(out).getBestCmap()
            print("%-28s %6d bytes, %d characters, Cyrillic %s, Latin %s" % (
                os.path.basename(out), os.path.getsize(out), len(cmap), 0x416 in cmap, ord("g") in cmap))


def display(src, dst):
    """Latin, Cyrillic, digits and punctuation of the headings' font; its names and metrics stay."""
    os.makedirs(dst, exist_ok=True)
    opt = subset.Options()
    opt.layout_features = ["kern", "liga", "ccmp", "locl", "case", "mark", "mkmk"]
    opt.name_IDs = ["*"]
    opt.name_languages = ["*"]
    opt.notdef_outline = True
    opt.hinting = False
    opt.drop_tables += ["DSIG", "vhea", "vmtx"]
    font = TTFont(src)
    ranges = [(0x20, 0x7E), (0xA0, 0xFF), (0x400, 0x45F), (0x490, 0x491), (0x2010, 0x2027), (0x2030, 0x203A), (0x20AC, 0x20AC),
              (0x2116, 0x2116), (0x2122, 0x2122), (0x2190, 0x2193), (0x2212, 0x2212)]
    sub = subset.Subsetter(opt)
    sub.populate(unicodes=[c for a, b in ranges for c in range(a, b + 1)])
    sub.subset(font)
    out = os.path.join(dst, "DelaGothicOne-Regular.ttf")
    font.save(out)
    made = TTFont(out)
    cmap = made.getBestCmap()
    print("%-28s %6d bytes, %d characters, Cyrillic %s, Latin %s, family %r" % (
        os.path.basename(out), os.path.getsize(out), len(cmap), 0x416 in cmap, ord("g") in cmap, made["name"].getDebugName(1)))


if __name__ == "__main__":
    if sys.argv[1] == "--display":
        display(sys.argv[2], sys.argv[3])
    else:
        main(sys.argv[1], sys.argv[2])
