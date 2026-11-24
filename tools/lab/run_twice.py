"""Determinism: two copies of the game play the same key script at the same time; their per-frame
logs (meleenet-sim.log: random draws, a fingerprint of the state, the pads) must be the same.

Both copies run with [debug] epoch = 1: when the main menu is up the mod enters it anew with the
clock and the random numbers reset; the script's frames and the compared lines count from there.
`--pre-b "<script>"` gives copy B another history before that (it wanders through the menus first).

  python run_twice.py <frames> "<script lines separated by ;>" [key=value ...] [--shots a,b] [--sheet out.png]
                      [--pre-b "<script>"]
"""
import os, sys
import lab

# From the start-up to the main menu.
BOOT = ["200 enter", "400 enter", "600 enter", "800 enter", "1000 enter", "1200 enter"]


def epoch_lines(copy):
    lines = open(os.path.join(copy, "meleenet-sim.log"), encoding="latin1").read().splitlines()
    marks = [i for i, l in enumerate(lines) if l.startswith("EPOCH")]
    return lines[marks[-1] + 1:] if marks else lines


def compare(a, b, show=6):
    la, lb = epoch_lines(a), epoch_lines(b)
    n = min(len(la), len(lb))
    for i in range(n):
        if la[i] != lb[i]:
            print("DIFFERENT from line %d of %d / %d:" % (i + 1, len(la), len(lb)))
            for j in range(max(0, i - 2), min(n, i + show)):
                mark = "  " if la[j] == lb[j] else "! "
                print(mark + "A " + la[j])
                if la[j] != lb[j]:
                    print(mark + "B " + lb[j])
            return i + 1
    print("identical: %d frames (A has %d, B has %d)" % (n, len(la), len(lb)))
    return 0 if n else 1


def main(argv):
    frames = int(argv[1])
    script = [s.strip() for s in argv[2].split(";") if s.strip()]
    shots, out_sheet, pre_b, epoch = "", None, None, True
    extra = []
    i = 3
    while i < len(argv):
        if argv[i] == "--shots":
            shots = argv[i + 1]; i += 2
        elif argv[i] == "--sheet":
            out_sheet = argv[i + 1]; i += 2
        elif argv[i] == "--no-epoch":
            epoch = False; i += 1
        elif argv[i] == "--pre-b":
            pre_b = [s.strip() for s in argv[i + 1].split(";") if s.strip()]; i += 2
        else:
            k, v = argv[i].split("=", 1)
            extra.append("%s = %s" % (k, v))
            i += 1
    copies = [lab.ensure_copy("test-mm"), lab.ensure_copy("test-mmb")]
    games = []
    for n, d in enumerate(copies):
        ini = "[video]\nmode = window\nwindow_width = %d\nwindow_height = %d\nantialiasing = 0\n" % ((1280, 720) if n == 0 else (960, 720))
        ini += lab.GUARD + "autoquit_frame = %d\nscript = lab-script.txt\nsim_log = 1\nepoch = %d\n" % (frames, 1 if epoch else 0)
        if shots:
            ini += "shots = %s\n" % shots
        if n == 1 and pre_b:
            ini += "epoch_at = %d\n" % (max(int(l.split()[0].split("-")[-1]) for l in pre_b) + 200)
        ini += "\n".join(extra) + "\n"
        # before the epoch: "pre." lines (absolute frames); after it: the script
        pre = BOOT if (n == 0 or not pre_b) else pre_b
        lab.install(d, ini, (["pre " + l for l in pre] if epoch else []) + script)
        games.append(lab.Game(d))
    for r in lab.wait_all(games, frames / 60.0 * 3 + 180):
        print(r)
    for d in copies:
        lab.show_log(d, 5, "exception|stack|ERROR|WARN|epoch")
    bad = compare(*copies)
    if out_sheet:
        pics = lab.shots(copies[0]) + lab.shots(copies[1])
        if pics:
            print("sheet:", lab.sheet(pics, out_sheet, cols=max(1, len(pics) // 2), k=2))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
