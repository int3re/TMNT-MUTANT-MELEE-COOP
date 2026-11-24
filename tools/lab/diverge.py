"""After run_twice.py: where the two copies part, and (when the run had rng_trace) the first random draw
that differs, with its callers.

  python diverge.py
"""
import os, sys
import lab, run_twice


def main():
    a, b = lab.copy_dir("test-mm"), lab.copy_dir("test-mmb")
    la, lb = run_twice.epoch_lines(a), run_twice.epoch_lines(b)
    first = None
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            first = i
            break
    if first is None:
        print("no difference in %d frames" % min(len(la), len(lb)))
        return
    for j in range(max(0, first - 2), min(len(la), len(lb), first + 3)):
        print(("  " if la[j] == lb[j] else "! ") + "A " + la[j])
        if la[j] != lb[j]:
            print("! B " + lb[j])
    for i, x in enumerate(la):
        if "TtMeleeGameMode" in x or "TtMiniGameMode" in x:
            print("the fight starts at", x.split()[0])
            break
    ra, rb = os.path.join(a, "meleenet-rng.log"), os.path.join(b, "meleenet-rng.log")
    if not (os.path.exists(ra) and os.path.exists(rb)):
        return
    ta = open(ra).read().splitlines()
    tb = open(rb).read().splitlines()

    def key(l):
        p = l.split()
        return (p[0], p[1], p[2]) + tuple(p[4:])

    print("draws traced: A %d, B %d" % (len(ta), len(tb)))
    for i, (x, y) in enumerate(zip(ta, tb)):
        if key(x) != key(y):
            for j in range(max(0, i - 3), min(len(ta), len(tb), i + 5)):
                print(("  " if key(ta[j]) == key(tb[j]) else "! ") + "A " + ta[j])
                if key(ta[j]) != key(tb[j]):
                    print("! B " + tb[j])
            return
    print("the traced draws are the same as far as both go")


if __name__ == "__main__":
    main()
