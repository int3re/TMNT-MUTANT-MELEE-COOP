"""Small reading kit for ztmnt.exe (TMNT Mutant Melee, PC). The exe is neither packed nor protected.

  python mmre.py dis  <addr> [instructions]     disassemble from an address
  python mmre.py func <addr>                    the function around / at an address
  python mmre.py xref <addr>                    code and data references to an address
  python mmre.py str  <regex> [-x]              strings that match (-x: with references)
  python mmre.py hex  <addr> [bytes]            bytes
  python mmre.py dw   <addr> [count]            dwords (pointers are named)
  python mmre.py calls <addr>                   calls made by the function at addr
  python mmre.py rtti [regex]                   classes with their vtables (MSVC RTTI)
  python mmre.py vt   <addr> [count]            a vtable's slots
  python mmre.py imp                            the import slots

The file is re/dump/ztmnt.exe (a copy of the game's exe), own names re/names.txt
("address name" per line).
"""
import os, re as _re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DUMP = os.path.join(REPO, "re", "dump")
BASE = 0x400000
TEXT = (0x401000, 0x55C000)      # .text + _rwcseg
RDATA = (0x55C000, 0x583000)
DATA = (0x583000, 0x68A000)      # .data + _rwdseg (the file holds only the first 0x27000 of .data)

raw = open(os.path.join(DUMP, "ztmnt.exe"), "rb").read()


def _image():
    """The exe laid out as in memory."""
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<H", raw, pe + 6)[0]
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    size = struct.unpack_from("<I", raw, pe + 24 + 56)[0]
    img = bytearray(size)
    hdr = struct.unpack_from("<I", raw, pe + 24 + 60)[0]
    img[:hdr] = raw[:hdr]
    so = pe + 24 + optsz
    for i in range(nsec):
        _, vs, va, rs, ro = struct.unpack_from("<8sIIII", raw, so + i * 40)
        n = min(rs, vs) if vs else rs
        img[va:va + n] = raw[ro:ro + n]
    return bytes(img)


img = _image()
names = {}


def _imports():
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    rva = struct.unpack_from("<I", img, pe + 24 + 96 + 8)[0]
    o = rva
    while True:
        oft, _, _, name, ft = struct.unpack_from("<IIIII", img, o)
        if not name:
            break
        dll = img[name:img.index(b"\0", name)].decode("latin1").split(".")[0]
        t, k = oft or ft, 0
        while True:
            v = struct.unpack_from("<I", img, t + 4 * k)[0]
            if not v:
                break
            if v & 0x80000000:
                n = "#%d" % (v & 0xFFFF)
            else:
                n = img[v + 2:img.index(b"\0", v + 2)].decode("latin1")
            names[BASE + ft + 4 * k] = "%s!%s" % (dll, n)
            k += 1
        o += 20


_imports()
_nm = os.path.join(REPO, "re", "names.txt")
if os.path.exists(_nm):
    for line in open(_nm, encoding="utf-8"):
        line = line.split("#")[0].strip()
        if line:
            a, n = line.split(None, 1)
            names[int(a, 16)] = n.strip()

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True


def rd(addr, n):
    return img[addr - BASE:addr - BASE + n]


def u32(addr):
    return struct.unpack_from("<I", img, addr - BASE)[0]


def f32(addr):
    return struct.unpack_from("<f", img, addr - BASE)[0]


def in_img(a):
    return BASE <= a < BASE + len(img)


def cstr(addr, limit=120):
    """ASCII string at addr or None."""
    if not in_img(addr):
        return None
    b = rd(addr, limit)
    n = b.find(b"\0")
    if n < 3:
        return None
    s = b[:n]
    if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return s.decode("latin1")
    return None


def note(v):
    """What an immediate / displacement points at."""
    if v in names:
        return names[v]
    if RDATA[0] <= v < DATA[1]:
        s = cstr(v)
        if s is not None:
            return '"%s"' % s.replace("\n", "\\n")
        if RDATA[0] <= v < RDATA[1]:
            f = f32(v)
            if f == f and 1e-6 < abs(f) < 1e7:
                return "float %g" % f
    return None


def dis(addr, count=40, stop_at_ret=False):
    out = []
    code = rd(addr, count * 15 + 16)
    n = 0
    for ins in md.disasm(code, addr):
        notes = []
        for op in ins.operands:
            v = None
            if op.type == X.X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
            elif op.type == X.X86_OP_MEM and op.mem.disp:
                v = op.mem.disp & 0xFFFFFFFF
            if v is not None and v >= BASE:
                t = note(v)
                if t and t not in notes:
                    notes.append(t)
        out.append("%08X  %-7s %s%s" % (ins.address, ins.mnemonic, ins.op_str, ("   ; " + " | ".join(notes)) if notes else ""))
        n += 1
        if n >= count:
            break
        if stop_at_ret and ins.mnemonic in ("ret", "retn"):
            break
    return out


_targets = None


def call_targets():
    """Every E8 rel32 target in .text plus every code pointer in the data (vtables): a good set of function starts."""
    global _targets
    if _targets is None:
        _targets = set()
        t0, t1 = TEXT[0] - BASE, TEXT[1] - BASE
        i = img.find(b"\xE8", t0, t1)
        while i != -1:
            rel = struct.unpack_from("<i", img, i + 1)[0]
            tgt = BASE + i + 5 + rel
            if TEXT[0] <= tgt < TEXT[1]:
                _targets.add(tgt)
            i = img.find(b"\xE8", i + 1, t1)
        for a in range(RDATA[0], DATA[1] - 4, 4):
            v = u32(a)
            if TEXT[0] <= v < TEXT[1]:
                _targets.add(v)
    return _targets


def func_start(addr):
    targets = call_targets()
    a = addr
    while a > TEXT[0]:
        if a in targets:
            return a
        a -= 1
        if addr - a > 0x4000:
            break
    return addr


def body(addr, limit=6000):
    """Addresses of the instructions of the function at addr, found by following the code."""
    seen = {}
    work = [addr]
    while work and len(seen) < limit:
        a = work.pop()
        while a not in seen and in_img(a):
            ins = next(md.disasm(rd(a, 16), a), None)
            if ins is None:
                break
            seen[a] = ins.size
            m = ins.mnemonic
            ops = ins.operands
            if m in ("ret", "retn", "int3", "retf"):
                break
            if m == "jmp":
                if ops and ops[0].type == X.X86_OP_IMM:
                    t = ops[0].imm & 0xFFFFFFFF
                    near = any(abs(t - s) < 0x800 for s in (a, addr)) or min(abs(t - s) for s in seen) < 0x400
                    if near and t not in call_targets():
                        work.append(t)
                elif ops and ops[0].type == X.X86_OP_MEM and ops[0].mem.base == 0 and ops[0].mem.scale == 4:
                    tbl = ops[0].mem.disp & 0xFFFFFFFF
                    for k in range(256):  # switch table
                        if not in_img(tbl + 4 * k):
                            break
                        t = u32(tbl + 4 * k)
                        if not (TEXT[0] <= t < TEXT[1]) or min(abs(t - s) for s in seen) > 0x2000:
                            break
                        work.append(t)
                break
            if m.startswith("j") or m.startswith("loop"):
                if ops and ops[0].type == X.X86_OP_IMM:
                    work.append(ops[0].imm & 0xFFFFFFFF)
            a += ins.size
    return seen


def func(addr, limit=6000):
    seen = body(addr, limit)
    out = []
    prev_end = None
    for a in sorted(seen):
        if prev_end is not None and a != prev_end:
            out.append("")
        out.extend(dis(a, 1))
        prev_end = a + seen[a]
    return out


def xrefs(addr):
    """(kind, where) for references to addr: calls/jumps rel32, imm32/disp32 in code, dwords in data."""
    res = []
    t0, t1 = TEXT[0] - BASE, TEXT[1] - BASE
    pat = struct.pack("<I", addr)
    i = img.find(pat, t0, t1)
    while i != -1:
        res.append(("code-imm", BASE + i))
        i = img.find(pat, i + 1, t1)
    for op, kind in ((b"\xE8", "call"), (b"\xE9", "jmp")):
        i = img.find(op, t0, t1)
        while i != -1:
            rel = struct.unpack_from("<i", img, i + 1)[0]
            if BASE + i + 5 + rel == addr:
                res.append((kind, BASE + i))
            i = img.find(op, i + 1, t1)
    d0, d1 = RDATA[0] - BASE, DATA[1] - BASE
    i = img.find(pat, d0, d1)
    while i != -1:
        if i % 4 == 0:
            res.append(("data", BASE + i))
        i = img.find(pat, i + 1, d1)
    return res


def strings(pattern, lo=RDATA[0], hi=DATA[1]):
    rx = _re.compile(pattern.encode("latin1"), _re.I)
    out = []
    for m in _re.finditer(rb"[\x20-\x7e\t]{4,}", img[lo - BASE:hi - BASE]):
        if rx.search(m.group()):
            out.append((lo + m.start(), m.group().decode("latin1")))
    return out


def rtti():
    """{vtable address: class name} from MSVC RTTI (the dword before a vtable points at the object locator)."""
    res = {}
    for a in range(RDATA[0], RDATA[1] - 8, 4):
        loc = u32(a)
        if not (RDATA[0] <= loc < RDATA[1] - 16):
            continue
        sig, off, cd, td = struct.unpack_from("<IIII", img, loc - BASE)
        if sig != 0 or not (DATA[0] <= td < DATA[1]):
            continue
        nm = rd(td + 8, 4)
        if nm[:2] != b".?":
            continue
        first = u32(a + 4)
        if not (TEXT[0] <= first < TEXT[1]):
            continue
        name = rd(td + 8, 128).split(b"\0")[0].decode("latin1")
        name = name[4:].rstrip("@")
        res[a + 4] = (name, off)
    return res


def vtable(addr, count=64):
    out = []
    known = rtti()
    for i in range(count):
        v = u32(addr + 4 * i)
        if not (TEXT[0] <= v < TEXT[1]):
            break
        if i and (addr + 4 * i) in known:
            break
        out.append((i, v))
    return out


def main(argv):
    cmd = argv[1]
    if cmd == "dis":
        print("\n".join(dis(int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 40)))
    elif cmd == "func":
        a = int(argv[2], 16)
        s = a if a in call_targets() else func_start(a)
        if s != a:
            print("; function starts at %08X" % s)
        print("\n".join(func(s)))
    elif cmd == "xref":
        a = int(argv[2], 16)
        for kind, where in xrefs(a):
            extra = ""
            if kind != "data":
                extra = "  in %08X" % func_start(where)
            print("%-8s %08X%s" % (kind, where, extra))
    elif cmd == "str":
        for a, s in strings(argv[2]):
            line = "%08X %s" % (a, s)
            if "-x" in argv:
                line += "   <- " + ", ".join("%08X(%08X)" % (w, func_start(w)) if k != "data" else "data %08X" % w for k, w in xrefs(a)[:8])
            print(line)
    elif cmd == "hex":
        a = int(argv[2], 16)
        n = int(argv[3]) if len(argv) > 3 else 64
        b = rd(a, n)
        for i in range(0, n, 16):
            row = b[i:i + 16]
            print("%08X  %-48s %s" % (a + i, " ".join("%02X" % c for c in row), "".join(chr(c) if 32 <= c < 127 else "." for c in row)))
    elif cmd == "dw":
        a = int(argv[2], 16)
        n = int(argv[3]) if len(argv) > 3 else 16
        for i in range(n):
            v = u32(a + 4 * i)
            t = note(v) or ""
            print("%08X  %08X  %g  %s" % (a + 4 * i, v, f32(a + 4 * i), t))
    elif cmd == "calls":
        a = int(argv[2], 16)
        for line in func(a):
            if " call " in line or "  call" in line:
                print(line)
    elif cmd == "rtti":
        rx = _re.compile(argv[2], _re.I) if len(argv) > 2 else None
        for vt, (name, off) in sorted(rtti().items(), key=lambda kv: (kv[1][0], kv[0])):
            if rx is None or rx.search(name):
                print("%08X  %-44s +%d  %d slots" % (vt, name, off, len(vtable(vt))))
    elif cmd == "vt":
        a = int(argv[2], 16)
        for i, v in vtable(a, int(argv[3]) if len(argv) > 3 else 64):
            print("%3d (+%03X)  %08X  %s" % (i, 4 * i, v, names.get(v, "")))
    elif cmd == "imp":
        for a in sorted(names):
            if "!" in names[a]:
                print("%08X  %s" % (a, names[a]))


if __name__ == "__main__":
    main(sys.argv)
