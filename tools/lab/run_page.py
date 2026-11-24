"""The whole way through the network page: 2..4 copies start as ordinary games, their scripts walk the
main menu to СЕТЕВАЯ ИГРА, one opens a game, the others join (by address on localhost, or over the lab's
stand-in for Steam), the host starts - every game starts itself again into the session and plays its
"game" lines there. The per-frame logs of the session games must be the same.

  python run_page.py <session frames> [--players N] [--via addr|mock] [--p1 "<game script>"] [--p2 ...]
                     [--shots a,b] [--sheet out.png] [key=value ...]

The menu part is fixed (frames of the first game): the main menu is up at ~1050.
"""
import os, sys, time
import lab, run_net

MENU_HOST = ["menu 200 enter", "menu 400 enter", "menu 600 enter", "menu 800 enter", "menu 1000 enter",
             "menu 1150 down", "menu 1200 down", "menu 1260 enter",   # СЕТЕВАЯ ИГРА
             "menu 1450 enter"]                                        # СОЗДАТЬ ИГРУ
MENU_GUEST_ADDR = ["menu 200 enter", "menu 400 enter", "menu 600 enter", "menu 800 enter", "menu 1000 enter",
                   "menu 1150 down", "menu 1200 down", "menu 1260 enter",
                   "menu 1700 down", "menu 1800 enter"]  # ВОЙТИ ПО АДРЕСУ (the friends' row is off without Steam)
MENU_GUEST_MOCK = ["menu 200 enter", "menu 400 enter", "menu 600 enter", "menu 800 enter", "menu 1000 enter",
                   "menu 1150 down", "menu 1200 down", "menu 1260 enter",
                   "menu 1700 down", "menu 1750 enter",                    # ИГРЫ ДРУЗЕЙ
                   "menu 2000 enter"]                                      # the first game
START = ["menu 2400 down", "menu 2450 down", "menu 2500 down", "menu 2600 enter"]  # (the cursor is on НАЧАТЬ ИГРУ already; a press is enough)


def main(argv):
    frames = int(argv[1])
    scripts = {1: [], 2: [], 3: [], 4: []}
    players, via, shots, out_sheet = 2, "addr", "", None
    extra = []
    i = 2
    while i < len(argv):
        a = argv[i]
        if a in ("--p1", "--p2", "--p3", "--p4"):
            scripts[int(a[3])] = ["game " + s.strip() for s in argv[i + 1].split(";") if s.strip()]; i += 2
        elif a == "--players":
            players = int(argv[i + 1]); i += 2
        elif a == "--via":
            via = argv[i + 1]; i += 2
        elif a == "--shots":
            shots = argv[i + 1]; i += 2
        elif a == "--sheet":
            out_sheet = argv[i + 1]; i += 2
        else:
            k, v = a.split("=", 1)
            extra.append("%s = %s" % (k, v))
            i += 1
    copies = [lab.ensure_copy(c) for c in run_net.COPIES[:players]]
    mock_dir = os.path.join(lab.WORK, "test-mm", "mock")
    sizes = [(1280, 720), (960, 720), (1024, 576), (800, 600)]
    for n, d in enumerate(copies):
        ini = "[video]\nmode = window\nwindow_width = %d\nwindow_height = %d\nantialiasing = 0\n" % sizes[n]
        ini += "[net]\nconnect = 127.0.0.1\nname = %s\ntransport = %s\n" % (
            ["Leo", "Raph", "Don", "Mike"][n], "mock" if via == "mock" else "enet")
        ini += lab.GUARD + "autoquit_frame = %d\nscript = lab-script.txt\nsim_log = 1\npage = 1\naddress = 1\n" % frames
        if via == "mock":
            os.makedirs(mock_dir, exist_ok=True)
            ini += "mock_slot = %d\nmock_dir = %s\n" % (n + 1, mock_dir.replace("\\", "/"))
        if shots:
            ini += "shots = %s\n" % shots
        ini += "\n".join(extra) + "\n"
        if n == 0:
            menu = MENU_HOST + ["menu 2600 enter"]   # НАЧАТЬ ИГРУ
        else:
            menu = MENU_GUEST_MOCK if via == "mock" else MENU_GUEST_ADDR
        lab.install(d, ini, menu + scripts[n + 1], keep_save=(n == 0))
        for f in ("meleenet-menu.log",):
            p = os.path.join(d, f)
            if os.path.exists(p):
                os.remove(p)
        lab.Game(d)
        time.sleep(0.5)
    print(lab.wait_copies(copies, frames / 60.0 * 3 + 240))
    for n, d in enumerate(copies):
        print("--- copy %d" % (n + 1))
        p = os.path.join(d, "meleenet-menu.log")
        if os.path.exists(p):
            for line in open(p, encoding="utf-8", errors="replace").read().splitlines():
                if "lobby:" in line or "ERROR" in line or "exception" in line:
                    print("  (menu) " + line[:170])
        lab.show_log(d, 10, "net:|exception|stack|ERROR|notice|started again|enters TtMeleeGameMode")
    bad = run_net.compare(copies)
    if out_sheet:
        pics = []
        for d in copies:
            pics += lab.shots(d)
        if pics:
            print("sheet:", lab.sheet(pics, out_sheet, cols=max(1, len(pics) // len(copies)), k=2))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
