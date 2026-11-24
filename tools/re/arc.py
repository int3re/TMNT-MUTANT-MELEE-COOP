"""The game's archive (archive.arc = index, archive.dat = data).

  python arc.py ls [regex]            files (path, size, offset)
  python arc.py cat <path>            a file's bytes to stdout (text files as text)
  python arc.py get <path> <out>      a file to disk
  python arc.py dump <regex> <dir>    every matching file under <dir>

Index: name[0x64], size, dirs, files, (dat handle), strings offset, files offset; then dirs of
{name, parent, first child, next sibling, first file} (links are indices + 1... see below) and files of
{name, next file of the dir, offset in .dat, size, flags}; names are offsets into the strings.
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "TMNT Mutant Melee")


class Arc:
    def __init__(self, folder=GAME, name="archive"):
        self.folder = folder
        self.name = name
        d = open(os.path.join(folder, name + ".arc"), "rb").read()
        self.d = d
        self.ndirs, self.nfiles = struct.unpack_from("<II", d, 0x68)
        self.strs, self.ftab = struct.unpack_from("<II", d, 0x74)
        self.dirs = [struct.unpack_from("<5i", d, 0x7C + 20 * i) for i in range(self.ndirs)]
        self.files = [struct.unpack_from("<IiIII", d, self.ftab + 20 * i) for i in range(self.nfiles)]
        self.paths = {}
        self._walk()

    def s(self, off):
        e = self.d.index(b"\0", self.strs + off)
        return self.d[self.strs + off:e].decode("latin1")

    def _walk(self):
        # A dir's fields: name, three links to dirs (index, 0 = none, -1 = dir 0), first file (index, 0 = none, -1 = file 0).
        def link(v):
            return None if v == 0 else (0 if v == -1 else v)

        seen = set()

        def visit(i, prefix):
            if i in seen:
                return
            seen.add(i)
            name, a, b, c, f = self.dirs[i]
            path = prefix + self.s(name)
            self.dirpath[i] = path
            fi = link(f)
            guard = 0
            while fi is not None and guard < 100000:
                fn, nxt, off, size, flags = self.files[fi]
                self.paths[(path + "/" + self.s(fn)).lstrip("/")] = (off, size, fi)
                fi = link(nxt)
                guard += 1
            return path

        self.dirpath = {}
        # find the tree: try each link as child / sibling by a trial walk from dir 0
        def tree(i, prefix, child_field, sib_field):
            while i is not None and i not in seen:
                path = visit(i, prefix)
                ch = link(self.dirs[i][child_field])
                if ch is not None:
                    tree(ch, path + "/", child_field, sib_field)
                i = link(self.dirs[i][sib_field])

        tree(0, "", 2, 3)
        if len(seen) < self.ndirs:  # the other order of links
            seen.clear()
            self.paths.clear()
            tree(0, "", 3, 2)
        self.seen_dirs = len(seen)

    def read(self, path):
        key = path.replace("\\", "/").lstrip("/")
        hit = self.paths.get(key)
        if hit is None:
            low = {k.lower(): v for k, v in self.paths.items()}
            hit = low.get(key.lower())
        if hit is None:
            raise KeyError(path)
        off, size, _ = hit
        with open(os.path.join(self.folder, self.name + ".dat"), "rb") as f:
            f.seek(off)
            return f.read(size)


def main(argv):
    a = Arc()
    cmd = argv[1]
    if cmd == "ls":
        rx = re.compile(argv[2], re.I) if len(argv) > 2 else None
        n = 0
        for p in sorted(a.paths):
            if rx is None or rx.search(p):
                off, size, _ = a.paths[p]
                print("%9d  %08X  %s" % (size, off, p))
                n += 1
        print("%d files (%d of %d dirs walked, %d files in the index)" % (n, a.seen_dirs, a.ndirs, a.nfiles), file=sys.stderr)
    elif cmd == "cat":
        data = a.read(argv[2])
        try:
            sys.stdout.write(data.decode("cp1251"))
        except Exception:
            sys.stdout.buffer.write(data)
    elif cmd == "get":
        open(argv[3], "wb").write(a.read(argv[2]))
    elif cmd == "dump":
        rx = re.compile(argv[2], re.I)
        for p in sorted(a.paths):
            if rx.search(p):
                out = os.path.join(argv[3], p.replace("/", os.sep))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                open(out, "wb").write(a.read(p))
                print(p)


if __name__ == "__main__":
    main(sys.argv)
