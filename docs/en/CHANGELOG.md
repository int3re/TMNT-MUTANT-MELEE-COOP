# History of versions

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../CHANGELOG.md)

The newest version is at the top. **Protocol** is the network protocol: players meet in a network game
only on versions with the same one. Version 0.1.0 was not published; the first public release is 0.1.1.

| Version | Launcher | Protocol | Date | In a line |
|---|---|---|---|---|
| [0.1.1](#011) | 1.1.0 | 1 | 24 Nov 2026 | menus fill a wide frame; the launcher has a look of its own and updates the mod and itself |
| [0.1.0](#010) | 1.0.0 | 1 | 23 Nov 2026 | the first network game, a window and a wide frame, the first launcher |

Who plays with whom over the network:

| Versions | Protocol | Together |
|---|---|---|
| 0.1.0 – 0.1.1 | 1 | play with each other |

## 0.1.1

Launcher 1.1.0. The first public release. It plays with 0.1.0 over the network: both speak protocol 1.
This was checked in the lab: a network game with 0.1.1 as the host and 0.1.0 as the guest stayed the same
on both copies for 2500 frames, a fight included.

- **Menus fill a wide frame.** Backgrounds, pipes and brick walls go on to the edges of the screen; the
  black bars at the sides are gone. In 0.1.0 the menus stayed a 4:3 block with bars, and only a fight was
  wide. Texts, buttons and portraits stand where they stood; what reaches an edge of the screen is what
  gets wider.
- **The frame around the map** of the story mode, ИСТОРИЯ (Story), goes to the edges too: without that the
  bare map would be seen beside it.
- **On 21:9 and 16:10 screens the texts stand on their plates.** The game kept numbers of its own for
  text, left at the consoles' 16:9: on 21:9 the texts stood beside their plates.
- The library, БИБЛИОТЕКА (Library), is still a 4:3 page on black.
- The status shown in Steam and the refusal «В этой игре уже все четыре игрока» (this game already has all
  four players) speak of this game and of four players: they were another mod's words.

Launcher 1.1.0:

- **A look of its own.** The background is the wall of the game's own menus: a honeycomb that brightens
  towards the right edge, black pipes, a wall of blue bricks with a ramp. Windows and buttons are black, in
  a steel rim. The colour of the honeycomb and of the accents is the bandana of one of the four turtles: it
  is chosen on the tab ЛАУНЧЕР (launcher), and by default it is Leonardo's blue. A mask in that colour is
  tied beside the title.
- **Updates.** The launcher learns of new versions on GitHub by itself and updates the mod and itself: the
  repository's address is now written into it.
- On the tab ГРАФИКА (graphics), a row «Меню» (menus): «На весь кадр» (the whole frame) or «4:3 с
  полосами» (4:3 with bars).
- On the tab УПРАВЛЕНИЕ (controls) an action can be given the middle button of the mouse; it is labelled
  «Колесо» (wheel).
- The button «Журнал до сессии» (the log before the session) opens `meleenet-menu.log`. It took the place
  of a button that looked for files this mod never writes.
- Removing the mod also removes what the mod wrote itself: `meleenet.log`, `meleenet-menu.log`,
  `meleenet-sim.log` and `tmnt-net.sv`.
- Installing turns the line `menus = bars` of a `meleenet.ini` left from 0.1.0 into `menus = wide`, once:
  nobody chose `bars` there, it was that version's default. From then on the line is the player's choice.

Wide menus were pictured in the lab on 16:9, 16:10 and 21:9 frames: the main menu, the network pages, the
choice of a hero, of an arena and of a fight's options, loading, the option screens, the story mode (hero,
back story, map, rules), the pause. Not pictured: a fight's results, the story's victory, defeat and unlock
screens, the credits. On 21:9 the brick ramp on the right is mirrored whole and looks like a hill.

## 0.1.0

Protocol 1. Launcher 1.0.0. The version was not published.

- **A network game for 2–4 players.** Each computer plays the whole game and only the controls go over the
  network; the game's clock is the same on all of them, 60 frames a second. The main menu has a new
  button, СЕТЕВАЯ ИГРА (network game): open a game, friends' games on Steam, join by a typed address. The
  menu and the network pages are made of the game's own buttons, the words are in its font. When the host
  starts, everybody's game closes and opens again — in the network game.
- A network game is played on the host's save; the guest's own save is not touched. The host refuses a
  guest whose `ztmnt.exe` or `archive.arc` differs.
- **A window instead of the 640×480 full screen:** without a frame over the whole monitor, or an ordinary
  window; any size of the frame.
- **Wide screen in fights:** the frame has the shape of the monitor and more is seen at the sides.
- **The picture:** smoothed edges (MSAA up to 8x) and anisotropic filtering (sharp textures at an angle)
  up to 16x.
- **Buttons of the mouse** are bound to actions like keys. They are the game's own bindings, in its file
  `tmntwire.hks`.
- **A rare crash on the loading screen is fixed.** The game drew the loading screen and loaded the level
  on two threads in one Direct3D device, unprotected; the mod makes the device multithread-safe.
- **Launcher 1.0.0:** one file with the mod inside; finds the game, installs and removes the mod, starts
  the game; its tabs ЛАУНЧЕР, ГРАФИКА, СЕТЬ (network) and УПРАВЛЕНИЕ edit `meleenet.ini` and the keys in
  `tmntwire.hks`.
