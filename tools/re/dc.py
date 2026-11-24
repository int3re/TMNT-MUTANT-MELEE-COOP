"""Reads re/dump/decomp.c (Ghidra's C for every function of ztmnt.exe).

  python dc.py <addr> [<addr> ...]     the function at (or holding) each address
  python dc.py -g <regex> [-n N]       functions whose text matches: address, size, first matching lines
  python dc.py -c <addr>               functions that call / mention FUN_<addr> or DAT_<addr>
"""
import bisect, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(os.path.dirname(os.path.dirname(HERE)), "re", "dump", "decomp.c")
text = open(PATH, encoding="utf-8", errors="replace").read()
heads = [(m.start(), int(m.group(1), 16), int(m.group(3))) for m in re.finditer(r"^// ===== ([0-9a-f]{8}) (\S+) size (\d+)", text, re.M)]
starts = [h[1] for h in heads]
order = sorted(range(len(heads)), key=lambda i: heads[i][1])
sorted_addrs = [heads[i][1] for i in order]


def body(i):
    end = heads[i + 1][0] if i + 1 < len(heads) else len(text)
    return text[heads[i][0]:end]


def find(addr):
    k = bisect.bisect_right(sorted_addrs, addr) - 1
    if k < 0:
        return None
    return order[k]


def main(argv):
    if argv[1] == "-g":
        rx = re.compile(argv[2])
        n = int(argv[argv.index("-n") + 1]) if "-n" in argv else 3
        for i in range(len(heads)):
            b = body(i)
            hits = [l.strip() for l in b.splitlines()[1:] if rx.search(l)]
            if hits:
                print("%08X size %-5d %s" % (heads[i][1], heads[i][2], " | ".join(h[:110] for h in hits[:n])))
    elif argv[1] == "-c":
        a = int(argv[2], 16)
        rx = re.compile(r"(FUN|DAT|PTR|_DAT|LAB|thunk_FUN|PTR_FUN|PTR_DAT)_%08x\b" % a)
        for i in range(len(heads)):
            if heads[i][1] == a:
                continue
            b = body(i)
            hits = [l.strip() for l in b.splitlines()[1:] if rx.search(l)]
            if hits:
                print("%08X size %-5d %s" % (heads[i][1], heads[i][2], " | ".join(h[:120] for h in hits[:3])))
    else:
        for arg in argv[1:]:
            i = find(int(arg, 16))
            print(body(i) if i is not None else "// no function at %s" % arg)


if __name__ == "__main__":
    main(sys.argv)
