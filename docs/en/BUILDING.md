# Building from source

[Documentation](README.md) · [How it works](ARCHITECTURE.md) · [History](CHANGELOG.md)

**English** · [Русский](../BUILDING.md)

## What you need

- **Windows** and **Visual Studio** with the C++ tools (x86).
- **CMake 3.20** or newer (the one that comes with Visual Studio will do).
- **Python 3** — only for the tools. The release's manifest and the lab need nothing but the standard
  library; `tools/re/mmre.py` needs the package `capstone`, `tools/launcher_fonts.py` needs `fontTools` and
  `brotli`, `tools/launcher_icon.py` needs `Pillow`.

Everything is built for **Win32**: `ztmnt.exe` is a 32-bit process, and so is whatever goes into it.
CMake stops with an error if it is configured for another platform. After the build
`tools/check_x86.cmake` looks at the header of `meleenet.asi` through `dumpbin`: if the file is not x86,
the build fails.

## What is taken from outside

| What | Where the build looks for it | Without it |
|---|---|---|
| **ENet** (UDP) | `../tmnt2003-netplay/third_party/enet` — the tree of [TMNT-2003-COOP](https://github.com/int3re/TMNT-2003-COOP), cloned with `--recursive` next to this one, into a folder named `tmnt2003-netplay`. Another place: `-DTMNT1_DIR=<path>` | the build does not start |
| **Ultimate ASI Loader**, 32-bit | `../tmnt2003-netplay/third_party/ultimate-asi-loader/bin/Win32/Release/dinput8.dll` (built there by `tools/build_asi_loader.ps1`). Any 32-bit build of the [loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): `-DUAL_DLL=<file>` | there is no `dinput8.dll` in the result and the launcher is not built |
| **Steamworks SDK** | `../steamworks_sdk_165/sdk`. Another place: `-DSTEAMWORKS_SDK_DIR=<path to sdk>` | the mod is built without Steam: games by address and the lab's stand-in for Steam |

The loader's path does not follow `TMNT1_DIR`: if the TMNT-2003-COOP tree is somewhere else, give
`-DUAL_DLL` as well.

MinHook is in the repository (`third_party/minhook`). The Steamworks SDK is Valve's and is never a part
of the repository.

The launcher's fonts and icon are in the repository too (`src/launcher/assets`; the fonts' licences and a
description lie beside them). The fonts are made by `tools/launcher_fonts.py`, the icon is drawn by
`tools/launcher_icon.py`. Without the file of the headings' font, `DelaGothicOne-Regular.ttf`, the build
still goes through and the launcher draws its headings in a system font.

## Build

This repository and the TMNT-2003-COOP tree go into folders side by side:

```bash
git clone https://github.com/int3re/TMNT-MUTANT-MELEE-COOP
```

```bash
git clone --recursive https://github.com/int3re/TMNT-2003-COOP tmnt2003-netplay
```

The loader (once):

```bash
powershell -ExecutionPolicy Bypass -File tmnt2003-netplay/tools/build_asi_loader.ps1
```

Configure and build from the folder `TMNT-MUTANT-MELEE-COOP`:

```bash
cmake -S . -B build -A Win32
```

```bash
cmake --build build --config Release
```

While it configures, CMake says whether the Steamworks SDK was found (`Steam: ON` or `Steam: OFF`) and
which versions of the launcher and of the mod go into the build.

The result is in `build/dist/Release`:

| File | What |
|---|---|
| `meleenet-launcher.exe` | the launcher with the mod inside: the file that goes into a release |
| `meleenet.asi`, `meleenet.ini`, `meleenet-README.md` | the mod, its settings, its short description |
| `dinput8.dll` | Ultimate ASI Loader |
| `steam_api.dll` | Steam's library, if the SDK was found |

Only the mod:

```bash
cmake --build build --config Release --target meleenet
```

Only the launcher (the mod is built too: it lies inside the launcher):

```bash
cmake --build build --config Release --target meleenet_launcher
```

The versions and `dist/release.json` are dependencies of the configuration: after a change to them CMake
configures again by itself at the next build, and the package inside the launcher gets the new numbers.

The C runtime is linked statically: a player needs no Visual C++ redistributable.

`tools/build.ps1` does the same in one command. It takes CMake from `PATH` or, if it is not there, the
one that comes with the newest Visual Studio installed; configures the build if it is not configured yet
or `-Configure` is given; builds the target `-Target` (without it, `meleenet`); and prints only the
errors, the warnings of the mod's own code and the list of the finished files:

```bash
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configure -Target meleenet_launcher
```

## The tree

| Folder | What is in it |
|---|---|
| `src/mod` | the mod, `meleenet.asi`: see [How it works](ARCHITECTURE.md) |
| `src/common` | what the mod's parts share: the log, the network transports (ENet, Steam, the lab's stand-in for Steam), Steam's lobbies, the protocol, SHA-256 |
| `src/launcher` | `meleenet-launcher.exe`; its fonts and icon in `assets` |
| `dist` | what goes to a player as it is: `meleenet.ini`, the short description, `release.json` |
| `docs` | this documentation and its pictures |
| `re` | `notes.md` — the notes on taking `ztmnt.exe` apart (in Russian): the main loop, time and random numbers, input, the screen, the network, the game's interface |
| `tools` | the one-command build, the release's manifest, the launcher's fonts and icon, the lab (`lab`), the tools for taking the game apart (`re`) |
| `third_party/minhook` | MinHook |

## Versions

| What | Where |
|---|---|
| the mod's version | `src/common/version.h`, `MELEENET_VERSION` and `MELEENET_VERSION_W` |
| the network protocol | `src/common/protocol.h`, `kVersion` — changed on any change to the packets' layout, to the use of the channels or to the meaning of a field; its history is in the comment there |
| the launcher's version | `src/launcher/launcher.h`, `kLauncherVersion` — changed when the launcher itself changes |
| which version of the mod speaks which protocol | `src/launcher/core.cpp`, the table `kProtocols` — for an installed version that a release's description does not name |
| what a release says of itself | `dist/release.json`: version, protocol, date, a title (40 characters at most), one to four notes (110 characters each at most), the protocols of the earlier versions |

## A release

A release is two files under the tag `v<version>`: `meleenet-launcher.exe` and `meleenet-latest.json`.

1. Fill `dist/release.json` and the versions **before** the build: `release.json` is built into the
   launcher, which takes from it the notes on the version of the mod it carries inside. If `release.json`
   is written for another version than the one in `version.h`, the configuration warns of it.
2. Build.
3. Make the manifest:

   ```bash
   python tools/release_manifest.py build/dist/Release/meleenet-launcher.exe build/dist/Release/meleenet-latest.json
   ```

   The manifest holds what is in `release.json` plus the launcher file's name, size and SHA-256 and the
   launcher's version. The script stops if `release.json` and the source disagree on the version or on the
   protocol, if the title or a note is longer than allowed, and if the launcher was not built from this
   source.
4. Publish both files as the assets of the GitHub release `v<version>`. A launcher asks for
   `<repository>/releases/latest/download/meleenet-latest.json` and takes the launcher file from
   `<repository>/releases/download/v<version>/`.

The repository's address is in `src/launcher/launcher.h` (`kRepoUrl`): a fork that wants its own updates
changes it there. With an empty `kRepoUrl` the launcher does not go to the network.

## The lab

`tools/lab` runs copies of the game with the mod without ever showing them: the window is off the screen
and never takes the focus, a full screen is forbidden, the copy's sound is muted, movies are not played,
and the run ends by itself at a given game frame.

It needs your own game in a folder named `TMNT Mutant Melee` next to the repository (the game is not a
part of the repository) and the built mod in `build/dist/Release`. The test copies — the folders `test-mm`,
`test-mmb`, `test-mmc`, `test-mmd` next to the repository — are made by the lab itself (`lab.py`): the
game's files in them are hard links, and a copy has of its own only the settings, the bindings and the save
(`tmnt.ini`, `tmntwire.hks`, `tmnt.sv`).

| Script | What it does |
|---|---|
| `run_one.py <frames> "<script>" [key=value …]` | one copy with a script of presses; `--shots 300,600` saves pictures of those frames, `--sheet <file.png>` puts them on one sheet |
| `run_twice.py <frames> "<script>"` | two copies play one and the same script at the same time: are their frame logs the same? The script's frames count from the main menu: the mod enters it anew with the clock and the random numbers reset. `--pre-b "<script>"` gives the second copy another history before that moment |
| `diverge.py` | after `run_twice.py`: where the copies parted and, if the run had `rng_trace`, the first random number that differs, with the addresses of those who drew it |
| `run_net.py <frames> [--players N] [--p1 "<script>"] [--p2 …] [--via enet\|mock]` | a network game of two to four copies on one computer, by address or through the stand-in for Steam; each copy plays its own script as its own player; the logs of all copies are compared frame by frame |
| `run_page.py <frames> [--players N] [--via addr\|mock]` | the whole way through the page СЕТЕВАЯ ИГРА (network game): the copies start as ordinary games, walk the menu, one opens a game, the others join, the host starts — and each game starts itself again, now in the network game |

For example, one copy: 1500 frames, five presses of Enter, the picture of frame 1300 on a sheet:

```bash
python tools/lab/run_one.py 1500 "200 enter;400 enter;600 enter;800 enter;1000 enter" --shots 1300 --sheet menu.png
```

Three copies in one network game, 1200 frames:

```bash
python tools/lab/run_net.py 1200 --players 3
```

A script is lines of `<frame>[-<frame>] [pN.]<control>[+<control>…]`, separated by `;` on the command
line: `420 enter`, `600-640 right+attack`, `700 p2.jump`. The controls: `up`, `down`, `left`, `right`,
`attack`, `alt`, `jump`, `action`, `special`, `block`, `esc`, `enter`, `back`, `start`, `space`, `f11`. A
press without a range lasts four frames. The frames are game frames, not pictures on the screen.

The frame log is `meleenet-sim.log` in the copy's folder: a line for every game frame with its number, the
game's mode, how many random numbers were drawn, a fingerprint of the state, a fingerprint of the pads and
the game's time.

The lab's switches are the `[debug]` section of `meleenet.ini`; each is described in `src/mod/config.h` — a
lossy and broken network, a stalled frame, a trace of the random numbers, pictures of frames, and more.
`key=value` on the command line goes there too. In `run_one.py` and `run_net.py` a key with a section's
name goes into that section: `net.delay=3` into `[net]`, `video.mode=window` into `[video]`; in
`run_net.py`, `p2:net_loss=20` goes to the second copy only.

**An earlier version against the new one.** In `run_net.py`, `pN:asi=<file>` gives copy N another build of
`meleenet.asi`:

```bash
python tools/lab/run_net.py 2500 --p1 "<script>" --p2 "<script>" "p2:asi=<an earlier meleenet.asi>"
```

This is how 0.1.1 was checked against 0.1.0: 0.1.1 as the host, 0.1.0 as the guest, 2500 frames with a
fight, and the logs were the same.

What the lab cannot check is the game's real window in front of a player (the keyboard, a real mouse,
typing an address, Alt+Tab) and the real Steam. Those are checked by playing.

## Tools for taking the game apart

`tools/re` holds the tools the [notes](../../re/notes.md) were written with. They read your own copy of the
game; the folder `re/dump/`, where the copy of the exe and its decompiled code lie, never gets into the
repository.

| Tool | What it does |
|---|---|
| `mmre.py` | reads `ztmnt.exe`: a disassembler, the function around an address, references to an address, strings, bytes, classes with their tables (RTTI), imports. It reads `re/dump/ztmnt.exe` — a copy of the exe that you put there yourself; your own names of addresses go into `re/names.txt` |
| `ghidra/ExportDecomp.java` | a Ghidra post-script: the decompiled C of every function into one file (its argument is the path of that file) |
| `dc.py` | reads that file, `re/dump/decomp.c`: the function at an address, a search of the text (`-g`), who mentions an address (`-c`) |
| `arc.py` | the game's archive (`archive.arc` and `archive.dat`): the list of files, a file to the screen or to the disk. It looks for the game in the folder `TMNT Mutant Melee` next to the repository |
| `tex.py` | the game's textures as PNG; `--game "<game folder>"` for another folder of the game |

## The launcher without its window

| Command | What it does |
|---|---|
| `meleenet-launcher.exe --shot <screen> <file.png>` | a screen of the launcher as a picture: `ready`, `checking`, `update`, `downloading`, `selfupdate`, `install`, `nogame`, `offline`, `blocked`, `running`, `problem`, `settings`, `graphics`, `sound` (this is the tab СЕТЬ (network): the code keeps a name left from the launchers' shared code), `controls`, `controls2`, `capture`. `--scale N` is the picture's scale, `--accent 0..3` the bandana's colour |
| `meleenet-launcher.exe /install "<game folder>" /quiet` | installs the mod into the game's folder without a window; the exit code is 0 or 2. `/package <launcher file>` installs the mod from another launcher file |
| `meleenet-launcher.exe /uninstall "<game folder>" /quiet` | removes the mod |
| `meleenet-launcher.exe --drive "<steps>" --pick <game folder> --settings <ini> --cache <folder> --out <log>` | the launcher run by a script, never shown. The steps, separated by `;`: `wait:<ms>`, `until:<screen>:<ms>`, `click:<name>`, `type:<text>`, `keydown:<vk>:<scan>:<ext>`, `rdown`, `xdown:<n>`, `state`, `shot:<png>`, `quit`. `--repo <address>` is the repository's page served from elsewhere |
| `meleenet-launcher.exe --check --out <log>` | asks GitHub for the newest release and writes what came |
| `meleenet-launcher.exe --where --out <log>` | writes where the launcher would find the game and what is installed in it |

## What is not in the repository

- Files of the game, and the Steamworks SDK.
- What is taken out of the game when it is taken apart: the folder `re/dump/` (the copy of the exe, the
  decompiled code, the Ghidra project). The notes themselves (`re/notes.md`) and the tools (`tools/re`)
  are in the repository.
- The lab's pictures (`re/shots/`) and earlier builds of the mod (`re/old/`).
