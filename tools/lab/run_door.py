"""A player comes into a session that is being played: copies 1 and 2 go through the network page into a
session and start a fight; copy 3 is started then, walks its own menu to the network page and dials the
host. It waits at the door until the fight is left, then every game starts itself again into a session of
three, which plays the "game" lines (the per-frame logs of that last session must be the same).

  python run_door.py <frames of the last session> [--via addr|mock] [--shots a,b] [--sheet out.png] [key=value ...]
"""
import os, sys, time
import lab, run_net, run_page

# The session's game: the main menu is up at frame 150.
P1 = ["game 200 down", "game 260 enter", "game 500 enter", "game 900 enter", "game 1100 enter", "game 1300 enter",
      "game 1500 enter", "game 1700 enter", "game 1900 enter", "game 2100-2300 right+attack",
      "game 3000 esc", "game 3100 down", "game 3200 enter"]   # (the first session's fight is left here)
P2 = ["game 450 enter", "game 600 right", "game 650 enter", "game 850 enter", "game 2100-2300 left+attack"]
P3 = ["game 460 enter", "game 620 right", "game 640 right", "game 700 enter", "game 860 enter", "game 2100-2300 up+attack"]


def main(argv):
    frames = int(argv[1])
    via, shots, out_sheet = "addr", "", None
    extra = []
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == "--via":
            via = argv[i + 1]; i += 2
        elif a == "--shots":
            shots = argv[i + 1]; i += 2
        elif a == "--sheet":
            out_sheet = argv[i + 1]; i += 2
        else:
            k, v = a.split("=", 1)
            extra.append("%s = %s" % (k, v))
            i += 1
    copies = [lab.ensure_copy(c) for c in run_net.COPIES[:3]]
    mock_dir = os.path.join(lab.WORK, "test-mm", "mock")
    sizes = [(1280, 720), (960, 720), (1024, 576)]
    guest_menu = run_page.MENU_GUEST_MOCK if via == "mock" else run_page.MENU_GUEST_ADDR
    menus = [run_page.MENU_HOST + ["menu 2600 enter"], guest_menu, guest_menu]
    scripts = [P1, P2, P3]

    def start(n):
        d = copies[n]
        ini = "[video]\nmode = window\nwindow_width = %d\nwindow_height = %d\nantialiasing = 0\n" % sizes[n]
        ini += "[net]\nconnect = 127.0.0.1\nname = %s\ntransport = %s\n" % (
            ["Leo", "Raph", "Don"][n], "mock" if via == "mock" else "enet")
        ini += lab.GUARD + "autoquit_frame = %d\nautoquit_players = 3\nscript = lab-script.txt\nsim_log = 1\npage = 1\naddress = 1\n" % frames
        if via == "mock":
            os.makedirs(mock_dir, exist_ok=True)
            ini += "mock_slot = %d\nmock_dir = %s\n" % (n + 1, mock_dir.replace("\\", "/"))
        if shots:
            ini += "shots = %s\n" % shots
        ini += "\n".join(extra) + "\n"
        lab.install(d, ini, menus[n] + scripts[n], keep_save=(n == 0))
        p = os.path.join(d, "meleenet-menu.log")
        if os.path.exists(p):
            os.remove(p)
        lab.Game(d)

    start(0)
    time.sleep(0.5)
    start(1)
    # copy 3 comes when the first session is being played
    log = os.path.join(copies[0], "meleenet.log")
    t0 = time.time()
    while time.time() - t0 < 180:
        if os.path.exists(log) and "net: playing from frame" in open(log, encoding="utf-8", errors="replace").read():
            break
        time.sleep(0.5)
    else:
        print("the first session did not begin")
    print("the first session is being played after %.0f s; copy 3 starts" % (time.time() - t0))
    start(2)
    print(lab.wait_copies(copies, frames / 60.0 * 3 + 400))
    for n, d in enumerate(copies):
        print("--- copy %d" % (n + 1))
        p = os.path.join(d, "meleenet-menu.log")
        if os.path.exists(p):
            for line in open(p, encoding="utf-8", errors="replace").read().splitlines():
                if "lobby:" in line or "ERROR" in line or "exception" in line or "door" in line or "starts again" in line:
                    print("  (before) " + line[:170])
        lab.show_log(d, 12, "net:|exception|stack|ERROR|started again|enters TtMeleeGameMode")
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
