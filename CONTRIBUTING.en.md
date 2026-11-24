# Contributing

[Русский](CONTRIBUTING.md) · **English**

Thank you for wanting to help. What the mod needs most now is real games and the logs after them:
everything in it has been checked in the lab only — on copies of the game on one computer. The real Steam,
the game's real window in front of a player, gamepads and long network games have not been checked.

## Reporting a problem

Open an [issue](https://github.com/int3re/TMNT-MUTANT-MELEE-COOP/issues) and say:

- the mod's version — the launcher shows it, and it is also in the bottom line of the game's main menu and
  in the first line of `meleenet.log` — and the launcher's version (it is in the properties of the file
  `meleenet-launcher.exe`);
- what you did and what happened; for a network game, how many players there were, who was the host and
  how you connected (Steam or an address);
- attach, from the game's folder, `meleenet.log` — **from every PC**. Without the other players' logs a
  network error can rarely be found. If the game closed and started again for the network game (the
  fallback), attach `meleenet-menu.log` too. The launcher opens both logs: its settings → ЛАУНЧЕР
  (launcher) → «Открыть журнал» (open the log) and «Журнал до сессии» (the log before the session).

Look the logs through before posting: they hold the game's folder and the players' names, after a game by
address the addresses, after a game over Steam your Steam name and Steam IDs. Those lines can be removed.

What is known and what has not been checked: the [README](README.en.md), the page
[If something is wrong](docs/en/TROUBLESHOOTING.md) and the end of the page
[How it works](docs/en/ARCHITECTURE.md#what-has-not-been-checked).

## Proposing a change

1. Build the mod: [docs/en/BUILDING.md](docs/en/BUILDING.md).
2. Read how it works: [docs/en/ARCHITECTURE.md](docs/en/ARCHITECTURE.md); how the game itself is made is
   in [re/notes.md](re/notes.md) (in Russian). The one rule of the network game is that all copies of the
   game must compute the same. Whatever changes how the game plays has to be the same on every PC or
   travel over the network as controls; whatever changes the picture only must touch neither the game's
   state nor its random number generator.
3. Check the change in the lab (`tools/lab`): one copy (`run_one.py`), two copies with one script
   (`run_twice.py`), a network game (`run_net.py`), a meeting through the page (`run_page.py`). The
   copies' logs have to match frame for frame. If the protocol stays the same, play the new build against
   the earlier one too: `run_net.py … "p2:asi=<an earlier meleenet.asi>"`.
4. If the change changes the packets — their layout, the channels or the meaning of a field — raise the
   network protocol (`src/common/protocol.h`) and add the new version to the table `kProtocols`
   (`src/launcher/core.cpp`) and to `dist/release.json`. When games meet they compare the protocol's
   number, and the mod's version is only written into the log, so the protocol is worth raising also when
   a new version would compute the game differently from the earlier one.

A few habits of this code:

- the game's addresses are used only after the SHA-256 of `ztmnt.exe` has matched; before a function of
  the game is hooked, its first bytes are compared with what they should be (as `sim.cpp`, `input.cpp`,
  `menu.cpp` and `sound.cpp` do): if they differ, the feature is switched off and the log says so — the
  game must not break on a file the mod does not know;
- the game's addresses and structures are gathered in one file, `src/mod/game.h`, each with a note;
- the mod's menus and messages are made by the game's own means: its interface scripts, its buttons and
  its font. The mod has no windows of its own over the game;
- a new item of a menu or of the launcher goes where a player would look for it, not to the end of a
  list: СЕТЕВАЯ ИГРА is the third button of the main menu, and the row «Меню» (menus) stands on the tab
  ГРАФИКА (graphics) right after «Формат кадра» (the frame's shape);
- a lab run never comes onto the desktop: the game's window is off the screen and does not take the focus,
  a full screen is forbidden;
- comments in the code are in English; texts for the player are in Russian;
- every page of the documentation has two copies: the Russian one (`docs/X.md`) and the English one
  (`docs/en/X.md`). A change goes into both.

## What is not in the repository and will not be

Files of the game — including what the tools for taking it apart get out of it: a copy of `ztmnt.exe`,
its decompiled code, files from the archive — and the Steamworks SDK. Please do not send them in issues or
pull requests. The notes on taking the game apart (`re/notes.md`) and the tools themselves (`tools/re`)
are in the repository: they read your own copy of the game.
