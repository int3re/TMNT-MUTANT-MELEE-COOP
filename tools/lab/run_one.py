"""One copy of the game, alone, with a key script.

  python run_one.py <frames> "<script lines separated by ;>" [key=value ...] [--copy test-mm] [--args "<game args>"]
                    [--shots 300,600] [--keep-save] [--sheet out.png] [--tail N] [--grep regex]

key=value pairs go into the ini's [debug] section ("video.mode=window" goes into [video]).
"""
import os, sys, time
import lab


def main(argv):
    frames = int(argv[1])
    script = [s.strip() for s in argv[2].split(";") if s.strip()] if len(argv) > 2 else []
    copy = "test-mm"
    args = ""
    shots = ""
    out_sheet = None
    keep = False
    tail = 60
    grep = None
    extra = {}
    i = 3
    while i < len(argv):
        a = argv[i]
        if a == "--copy":
            copy = argv[i + 1]; i += 2
        elif a == "--args":
            args = argv[i + 1]; i += 2
        elif a == "--shots":
            shots = argv[i + 1]; i += 2
        elif a == "--sheet":
            out_sheet = argv[i + 1]; i += 2
        elif a == "--tail":
            tail = int(argv[i + 1]); i += 2
        elif a == "--grep":
            grep = argv[i + 1]; i += 2
        elif a == "--keep-save":
            keep = True; i += 1
        else:
            k, v = a.split("=", 1)
            sec, key = k.split(".", 1) if "." in k else ("debug", k)
            extra.setdefault(sec, []).append("%s = %s" % (key, v))
            i += 1
    d = lab.ensure_copy(copy)
    video = {"mode": "window", "window_width": "1280", "window_height": "720", "antialiasing": "0"}
    for line in extra.pop("video", []):
        k, v = line.split(" = ", 1)
        video[k] = v
    ini = "[video]\n" + "".join("%s = %s\n" % kv for kv in video.items())
    ini += lab.GUARD + "autoquit_frame = %d\nscript = lab-script.txt\nsim_log = 1\n" % frames
    if shots:
        ini += "shots = %s\n" % shots
    ini += "\n".join(extra.pop("debug", [])) + "\n"
    for sec, lines in extra.items():
        ini += "[%s]\n%s\n" % (sec, "\n".join(lines))
    lab.install(d, ini, script, keep_save=keep)
    g = lab.Game(d, args)
    print(g.wait(frames / 60.0 * 3 + 90))
    lab.show_log(d, tail, grep)
    pics = lab.shots(d)
    if pics and out_sheet:
        print("sheet:", lab.sheet(pics, out_sheet, cols=3, k=2))


if __name__ == "__main__":
    main(sys.argv)
