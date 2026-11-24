"""A network session of 2..4 copies of the game on this PC (ENet on localhost, or the lab's stand-in
for Steam): the host is test-mm, the guests test-mmb, test-mmc, test-mmd. Each copy plays its own key
script as its own player; the per-frame logs of all copies must be the same.

  python run_net.py <frames> [--p1 "<script>"] [--p2 "<script>"] [--p3 ...] [--p4 ...] [--players N]
                    [--via enet|mock] [--shots a,b] [--sheet out.png] [key=value ...] [pN:key=value ...] [pN:asi=<another meleenet.asi>]

Script frames are game frames of the session (it starts with the game). key=value goes into [debug]
of every copy ("net.delay=3" into [net]); "p2:net_loss=20" into copy 2 only.
"""
import os, shutil, sys, time
import lab

COPIES = ["test-mm", "test-mmb", "test-mmc", "test-mmd"]


def sim_lines(copy):
    p = os.path.join(copy, "meleenet-sim.log")
    return open(p, encoding="latin1").read().splitlines() if os.path.exists(p) else []


def compare(copies, show=4):
    logs = [sim_lines(c) for c in copies]
    n = min(len(l) for l in logs)
    bad = 0
    for k in range(1, len(logs)):
        for i in range(n):
            if logs[0][i] != logs[k][i]:
                print("copy %d DIFFERS from the host at line %d:" % (k + 1, i + 1))
                for j in range(max(0, i - 1), min(n, i + show)):
                    print("  host " + logs[0][j])
                    print("  p%d   " % (k + 1) + logs[k][j])
                bad += 1
                break
    print("%s: %d frames compared (%s)" % ("DIFFERENT" if bad else "identical", n, ", ".join(str(len(l)) for l in logs)))
    return bad


def main(argv):
    frames = int(argv[1])
    scripts = {1: [], 2: [], 3: [], 4: []}
    players, via, shots, out_sheet = 2, "enet", "", None
    extra, per = {}, {1: {}, 2: {}, 3: {}, 4: {}}
    i = 2
    while i < len(argv):
        a = argv[i]
        if a in ("--p1", "--p2", "--p3", "--p4"):
            scripts[int(a[3])] = [s.strip() for s in argv[i + 1].split(";") if s.strip()]; i += 2
        elif a == "--players":
            players = int(argv[i + 1]); i += 2
        elif a == "--via":
            via = argv[i + 1]; i += 2
        elif a == "--shots":
            shots = argv[i + 1]; i += 2
        elif a == "--sheet":
            out_sheet = argv[i + 1]; i += 2
        else:
            target = extra
            if a[0] == "p" and a[2] == ":":
                target = per[int(a[1])]
                a = a[3:]
            k, v = a.split("=", 1)
            sec, key = k.split(".", 1) if "." in k else ("debug", k)
            target.setdefault(sec, []).append("%s = %s" % (key, v))
            i += 1
    copies = [lab.ensure_copy(c) for c in COPIES[:players]]
    mock_dir = os.path.join(lab.WORK, "test-mm", "mock")
    games = []
    sizes = [(1280, 720), (960, 720), (1024, 576), (800, 600)]
    for n, d in enumerate(copies):
        ini = "[video]\nmode = window\nwindow_width = %d\nwindow_height = %d\nantialiasing = 0\n" % sizes[n]
        ini += "[net]\nmode = %s\nconnect = 127.0.0.1\nname = %s\ntransport = %s\nwait = 60\n" % (
            "host" if n == 0 else "join", ["Leo", "Raph", "Don", "Mike"][n], via)
        merged = {}
        for src in (extra, per[n + 1]):
            for sec, lines in src.items():
                merged.setdefault(sec, []).extend(lines)
        asi = None
        for line in list(merged.get("debug", [])):
            if line.startswith("asi = "):
                asi = line[6:]
                merged["debug"].remove(line)
        ini += "\n".join(merged.pop("net", [])) + "\n"
        ini += lab.GUARD + "autoquit_frame = %d\nscript = lab-script.txt\nsim_log = 1\nnet_players = %d\n" % (frames, players)
        if via == "mock":
            os.makedirs(mock_dir, exist_ok=True)
            ini += "mock_slot = %d\nmock_dir = %s\n" % (n + 1, mock_dir.replace("\\", "/"))
        if shots:
            ini += "shots = %s\n" % shots
        ini += "\n".join(merged.pop("debug", [])) + "\n"
        for sec, lines in merged.items():
            ini += "[%s]\n%s\n" % (sec, "\n".join(lines))
        lab.install(d, ini, scripts[n + 1], keep_save=(n == 0))
        if asi:  # this copy plays another build of the mod (an older version against the new one)
            shutil.copyfile(asi, os.path.join(d, "meleenet.asi"))
            print("copy %d plays %s" % (n + 1, asi))
        games.append(lab.Game(d))
        time.sleep(0.3 if n else 1.0)
    for r in lab.wait_all(games, frames / 60.0 * 3 + 150):
        print(r)
    for n, d in enumerate(copies):
        print("--- copy %d" % (n + 1))
        lab.show_log(d, 12, "net:|exception|stack|ERROR|WARN|notice|enters TtGameStart|enters TtMeleeGameMode")
    bad = compare(copies)
    if out_sheet:
        pics = []
        for d in copies:
            pics += lab.shots(d)
        if pics:
            print("sheet:", lab.sheet(pics, out_sheet, cols=max(1, len(pics) // len(copies)), k=2))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
