"""meleenet-latest.json of a release: what a launcher reads to know the newest version.

    python tools/release_manifest.py <meleenet-launcher.exe> <out.json>

A release is one file for players, meleenet-launcher.exe, with the mod inside. dist/release.json holds what
a person writes for each release (the version, the protocol, the date, a title and up to four short notes
for the launcher's news, the protocol of earlier versions); this adds the launcher's size and SHA-256.
The json goes into the GitHub release as an asset beside the launcher: a launcher asks for
.../releases/latest/download/meleenet-latest.json, downloads the file it names, checks it and installs the
mod from inside it."""
import hashlib, io, json, os, re, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(launcher, out):
    rel = json.load(io.open(os.path.join(HERE, "dist", "release.json"), encoding="utf-8"))
    version = re.search(r'MELEENET_VERSION "([^"]+)"', io.open(os.path.join(HERE, "src/common/version.h"), encoding="utf-8").read()).group(1)
    protocol = int(re.search(r"kVersion = (\d+)", io.open(os.path.join(HERE, "src/common/protocol.h"), encoding="utf-8").read()).group(1))
    lver = re.search(r'kLauncherVersion\[\] = L"([^"]+)"', io.open(os.path.join(HERE, "src/launcher/launcher.h"), encoding="utf-8").read()).group(1)
    assert rel["version"] == version, "dist/release.json is for %s, the source is %s" % (rel["version"], version)
    assert rel["protocol"] == protocol, "dist/release.json says protocol %s, the source %s" % (rel["protocol"], protocol)
    assert 1 <= len(rel["notes"]) <= 4 and all(len(n) <= 110 for n in rel["notes"]), "one to four notes of at most 110 characters"
    assert len(rel["title"]) <= 40, "a title of at most 40 characters"
    data = open(launcher, "rb").read()
    assert ("version %s\n" % version).encode() in data and ("launcher %s\n" % lver).encode() in data, \
        "this launcher was not built from this source (its package index says otherwise): rebuild"
    file = {"name": os.path.basename(launcher), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    doc = {"schema": 2, "version": version, "protocol": protocol, "date": rel["date"], "title": rel["title"], "notes": rel["notes"],
           "launcher": dict(file, version=lver), "setup": file, "protocols": rel["protocols"]}
    io.open(out, "w", encoding="utf-8", newline="\n").write(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    print("written:", out)
    print("  launcher %s with the mod %s (protocol %d) inside: %d bytes, sha256 %s" % (lver, version, protocol, file["size"], file["sha256"]))


if __name__ == "__main__":
    main(*sys.argv[1:3])
