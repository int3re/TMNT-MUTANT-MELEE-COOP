"""Guarded lab runner for test copies of TMNT Mutant Melee (ztmnt.exe).

A test copy is a folder next to the repo (test-mm, test-mmb, ...) whose big files are hard links to
the player's game; the game's own settings and save are the copy's own. The game is started with the
mod's [debug] guards on (window off the screen, never activated, never full screen, muted) and is
killed when the time is up or when the mod did not come up at once.
"""
import ctypes, os, re, shutil, struct, sys, time, zlib
from ctypes import wintypes as wt

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.dirname(REPO)
GAME = os.path.join(WORK, "TMNT Mutant Melee")
DIST = os.path.join(REPO, "build", "dist", "Release")
OWN = ("tmnt.ini", "tmntwire.hks", "tmnt.sv")  # per copy, never linked
SKIP = ("unins000.dat", "unins000.exe", "meleenet.log", "meleenet.ini", "meleenet.asi", "dinput8.dll", "steam_api.dll")
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

GUARD = """[debug]
guard = 1
offscreen = 1
no_activate = 1
mute = 1
movies = 0
"""


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


k32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD,
                               ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                               ctypes.POINTER(PROCESS_INFORMATION)]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
k32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]


def copy_dir(name):
    return os.path.join(WORK, name)


def ensure_copy(name):
    """Makes (or completes) a test copy: hard links to the game's files, the settings as copies."""
    dst = copy_dir(name)
    for root, dirs, files in os.walk(GAME):
        rel = os.path.relpath(root, GAME)
        if rel.split(os.sep)[0] == "screenshots":
            continue
        out = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(out, exist_ok=True)
        for f in files:
            if f.lower() in SKIP or (rel == "." and f.lower() in OWN):
                continue
            t = os.path.join(out, f)
            if not os.path.exists(t):
                os.link(os.path.join(root, f), t)
    hks = os.path.join(dst, "tmntwire.hks")
    if not os.path.exists(hks) and os.path.exists(os.path.join(GAME, "tmntwire.hks")):
        shutil.copyfile(os.path.join(GAME, "tmntwire.hks"), hks)
    return dst


def install(copy, ini_text, script=None, keep_save=False):
    """Puts the built mod, an ini and a key script into a test copy; removes old logs, shots and the save."""
    for f in ("meleenet.asi", "dinput8.dll", "steam_api.dll"):
        if os.path.exists(os.path.join(DIST, f)):
            shutil.copyfile(os.path.join(DIST, f), os.path.join(copy, f))
    with open(os.path.join(copy, "meleenet.ini"), "w", encoding="utf-8") as fh:
        fh.write(ini_text)
    if script is not None:
        with open(os.path.join(copy, "lab-script.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(script) + "\n")
    for f in ("meleenet.log", "meleenet-sim.log", "meleenet-rng.log") + (() if keep_save else ("tmnt.sv",)):
        p = os.path.join(copy, f)
        if os.path.exists(p):
            os.remove(p)
    pics = os.path.join(copy, "screenshots")
    if os.path.isdir(pics):
        for f in os.listdir(pics):
            os.remove(os.path.join(pics, f))


class Game:
    def __init__(self, copy, args=""):
        self.copy = copy
        exe = os.path.join(copy, "ztmnt.exe")
        si = STARTUPINFOW()
        si.cb = ctypes.sizeof(si)
        si.dwFlags = 0x1  # STARTF_USESHOWWINDOW
        si.wShowWindow = 4  # SW_SHOWNOACTIVATE
        self.pi = PROCESS_INFORMATION()
        cmd = ctypes.create_unicode_buffer('"%s" %s' % (exe, args))
        if not k32.CreateProcessW(exe, cmd, None, None, False, 0, None, copy, ctypes.byref(si), ctypes.byref(self.pi)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.t0 = time.time()

    def alive(self):
        return k32.WaitForSingleObject(self.pi.hProcess, 0) != 0

    def kill(self):
        if self.alive():
            k32.TerminateProcess(self.pi.hProcess, 99)
            k32.WaitForSingleObject(self.pi.hProcess, 3000)

    def exit_code(self):
        code = wt.DWORD()
        k32.GetExitCodeProcess(self.pi.hProcess, ctypes.byref(code))
        return code.value

    def wait(self, timeout, need_log_s=3.0):
        """Waits for the game to end by itself; kills it at the timeout, or early when the mod's log
        did not appear (the mod is not in: nothing guards the run)."""
        return wait_all([self], timeout, need_log_s)[0]


def wait_all(games, timeout, need_log_s=3.0):
    t0 = time.time()
    res = [None] * len(games)
    while any(r is None for r in res):
        for i, g in enumerate(games):
            if res[i] is not None:
                continue
            if not g.alive():
                res[i] = "ended by itself after %.1f s, exit code %d" % (time.time() - g.t0, g.exit_code())
            elif time.time() - g.t0 > need_log_s and not os.path.exists(os.path.join(g.copy, "meleenet.log")):
                g.kill()
                res[i] = "killed: no meleenet.log (the mod did not load)"
            elif time.time() - t0 > timeout:
                g.kill()
                res[i] = "killed: timeout %.0f s" % timeout
        time.sleep(0.05)
    return res


def show_log(copy, tail=200, grep=None):
    p = os.path.join(copy, "meleenet.log")
    if not os.path.exists(p):
        print("(no meleenet.log)")
        return
    with open(p, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    if grep:
        rx = re.compile(grep)
        lines = [l for l in lines if rx.search(l)]
    for line in lines[-tail:]:
        print(line)


# --- pictures -------------------------------------------------------------------------------------------
def read_bmp(path):
    """(width, height, rows of RGB bytes, top first) of a 32-bit top-down BMP the mod wrote."""
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    h = abs(h)
    rows = []
    for y in range(h):
        row = d[off + y * w * 4: off + (y + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows.append(bytes(rgb))
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(bytes(1) + r for r in rows)
    png = bytes([0x89]) + b"PNG" + bytes([13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def shrink(w, h, rows, k):
    """Every k-th pixel (enough to look at a frame)."""
    if k <= 1:
        return w, h, rows
    out = []
    for y in range(0, h - h % k, k):
        r = rows[y]
        line = bytearray()
        for x in range(0, w - w % k, k):
            line += r[x * 3:x * 3 + 3]
        out.append(bytes(line))
    return (w - w % k) // k, (h - h % k) // k, out


def shots(copy):
    d = os.path.join(copy, "screenshots")
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".bmp"))


def sheet(paths, out, cols=3, k=2):
    """A contact sheet (PNG) of saved frames."""
    pics = [shrink(*read_bmp(p), k) for p in paths]
    if not pics:
        return None
    cols = min(cols, len(pics))
    w = max(p[0] for p in pics)
    h = max(p[1] for p in pics)
    n_rows = (len(pics) + cols - 1) // cols
    rows = []
    for r in range(n_rows):
        for y in range(h):
            line = bytearray()
            for c in range(cols):
                i = r * cols + c
                if i < len(pics) and y < pics[i][1]:
                    line += pics[i][2][y] + bytes((w - pics[i][0]) * 3)
                else:
                    line += bytes([32]) * (w * 3)
            rows.append(bytes(line))
    write_png(out, w * cols, h * n_rows, rows)
    return out

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", wt.WCHAR * 260)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]


def games_in(copy):
    """Process ids of the games running from a test copy (a game that starts itself again is a new one)."""
    out = []
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    e = PROCESSENTRY32W()
    e.dwSize = ctypes.sizeof(e)
    ok = k32.Process32FirstW(snap, ctypes.byref(e))
    while ok:
        if e.szExeFile.lower() == "ztmnt.exe":
            h = k32.OpenProcess(0x1000 | 0x1, False, e.th32ProcessID)  # QUERY_LIMITED_INFORMATION | TERMINATE
            if h:
                buf = ctypes.create_unicode_buffer(1024)
                n = wt.DWORD(1024)
                if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) and \
                        os.path.normcase(os.path.dirname(buf.value)) == os.path.normcase(copy):
                    out.append(e.th32ProcessID)
                k32.CloseHandle(h)
        ok = k32.Process32NextW(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return out


def wait_copies(copies, timeout):
    """Waits until no game runs from any of the copies (games that start themselves again included);
    at the timeout whatever is left is killed. Returns what happened."""
    t0 = time.time()
    quiet = 0
    while True:
        left = [p for c in copies for p in games_in(c)]
        if not left:
            quiet += 1
            if quiet >= 12:  # (a game starting itself again is gone for a moment)
                return "all ended by themselves after %.1f s" % (time.time() - t0 - 3)
        else:
            quiet = 0
        if time.time() - t0 > timeout:
            for p in left:
                h = k32.OpenProcess(0x1, False, p)
                if h:
                    k32.TerminateProcess(h, 99)
                    k32.CloseHandle(h)
            return "killed %d game(s): timeout %.0f s" % (len(left), timeout)
        time.sleep(0.25)

