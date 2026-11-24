# Settings

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · **Settings** · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../SETTINGS.md)

There are three places where settings live:

| Where | What | Who changes it |
|---|---|---|
| `meleenet.ini` next to `ztmnt.exe` | the mod's own: the window, the picture, the network game, the mouse | the launcher's tabs ГРАФИКА (Picture) and СЕТЬ (Network), or a text editor |
| `tmntwire.hks` next to `ztmnt.exe` | the game's own file of bindings: the keys | the launcher's tab УПРАВЛЕНИЕ (Controls) |
| `%APPDATA%\meleenet\launcher.ini` | the launcher's own | the launcher's tab ЛАУНЧЕР (Launcher) |

Everything is read by the game **at its start**: a change made while the game runs takes effect the next
time.

## The launcher's rows and what they write

### ГРАФИКА

| Row | Values | `meleenet.ini`, `[video]` |
|---|---|---|
| **Окно игры** (the game's window) | На весь монитор (the whole monitor) · В окне (in a window) | `mode = borderless \| window` |
| **Разрешение** (resolution) | Как монитор (as the monitor) · the monitor's modes | `resolution = auto \| 1920x1080` |
| **Формат кадра** (shape of the frame) | Под монитор (the monitor's) · 4:3 с полосами (4:3 with bars) | `aspect = wide \| 4:3` |
| **Меню** (menus) | На весь кадр (the whole frame) · 4:3 с полосами | `menus = wide \| bars` |
| **Сглаживание** (smoothing) | Выкл (off) · 2x · 4x · 8x | `antialiasing = 0 \| 2 \| 4 \| 8` |
| **Фильтр текстур** (texture filter) | Выкл · 4x · 8x · 16x | `anisotropy = 0 \| 4 \| 8 \| 16` |
| **Контуры героев** (the heroes' outlines) | Рисованные (drawn) · Выкл | `toon_shading = 1 \| 0` |
| **Тени** (shadows) | Живые (live) · Простые (simple) | `shadows = 1 \| 0` |
| **Частицы** (particles) | Все (all) · Меньше (fewer) · Мало (few) | `particles = 100 \| 70 \| 40` |
| **Пропуск кадров** (frame skipping) | Вкл (on) · Выкл (off) | `frame_skip = 1 \| 0` |

The rows stand in two boxes: **ЭКРАН** (Screen) holds the first four, **КАРТИНКА** (Picture) the rest;
«Пропуск кадров» is the last row of КАРТИНКА. It came with launcher 1.2.0 and mod 0.2.0.

A row that has no effect now is shown pale: **Меню** — with the frame's shape «4:3 с полосами» (such a
frame has no sides for the menus to fill) and with an installed mod older than 0.1.1; **Пропуск кадров** —
with an installed mod older than 0.2.0.

**Пропуск кадров** (frame skipping). When the computer cannot draw 60 pictures a second, a fight's frames
are played without their pictures — up to three in a row. The game keeps its real speed, and in a network
game one slow computer does not slow the fight for the others. It works in fights only: menus and a fight's
opening are still slowed by a computer that cannot draw them. «Выкл» (off): every frame is drawn, and a slow
computer slows the game as it did before version 0.2.0. In the lab one copy of two needed 30 ms more for
every picture: with «Вкл» both played the fight at 60 frames a second, with «Выкл» both at 29.

### СЕТЬ

| Row | Values | `meleenet.ini` |
|---|---|---|
| **Задержка ввода** (input delay) | 1 – 6 frames | `[net] delay = 1 … 6` |
| **Видят в Steam** (seen in Steam) | Друзья (friends) · Все (everybody) | `[net] lobby = friends \| public` |
| **Кнопки мыши в игре** (the mouse's buttons in the game) | Работают (work) · Выкл (off) | `[input] mouse = 1 \| 0` |

### УПРАВЛЕНИЕ

The keys and the mouse's buttons: see [Controls](CONTROLS.md).

## `meleenet.ini`

A line that starts with `;` is a note (the notes in the file are in English). A key that is not in the
file has its default; a file that is not there means all the defaults. When the mod is updated the launcher
leaves your `meleenet.ini` as it is.

### `[video]`

| Key | Default | Meaning |
|---|---|---|
| `mode` | `borderless` | `borderless` — a window without a frame over the whole monitor: the desktop's resolution is not changed and Alt+Tab works at once. `window` — an ordinary window. `game` — as in the original: an exclusive 640×480 full screen; the mod then leaves the screen alone |
| `resolution` | `auto` | the size of the frame the game draws: `auto` — the monitor's (without a frame) or three quarters of the desktop's height (in a window); `1280x720` — a size of your own, 320x240 at least |
| `aspect` | `wide` | `wide` — the frame has the shape of the monitor (16:9, 16:10, 21:9), and in fights more is seen at the sides. `4:3` — the original picture with black bars at the sides |
| `menus` | `wide` | `wide` — the menus fill the whole wide frame: backgrounds, pipes and walls go on to the edges of the screen; `bars` — 4:3 menus with black bars at the sides |
| `window_width`, `window_height` | `0` | the size of the window for `mode = window`; `0` — the size of the frame |
| `antialiasing` | `4` | smoothed edges (MSAA): `0`, `2`, `4`, `8`. Works in `borderless` and `window` when the frame fills the whole window; a video card that cannot do that much gets less |
| `anisotropy` | `16` | anisotropic filtering (sharp textures at an angle): `0` — off, `2`, `4`, `8`, `16`; no more than the video card can do |
| `toon_shading` | `1` | the heroes' outlines: `1` — drawn, `0` — off |
| `shadows` | `1` | `1` — live shadows, `0` — simple ones |
| `particles` | `100` | how many of the particles are drawn, in percent: `0` … `100`. In a network game always `100` |
| `frame_skip` | `1` | `1` — when the computer cannot draw 60 pictures a second, a fight's frames are played without their pictures (up to three in a row) and the game keeps its speed — in a network game, everybody's; `0` — every frame is drawn. Read by the mod since version 0.2.0 |

`toon_shading`, `shadows` and `particles` are the game's own picture options; the game keeps them in its
`tmnt.ini` as well. The value in `meleenet.ini` wins; if `meleenet.ini` has no such key, `tmnt.ini` decides.

In version 0.1.0 the file said `menus = bars` by default: the wide menus were not finished then. On an
update to 0.1.1 the launcher changes that line to `menus = wide`, once; from then on it is yours.

An update leaves your `meleenet.ini` as it is, so a file left from 0.1.x has no `frame_skip` line: the
default, `1`, is in force until you change the row «Пропуск кадров» in the launcher.

### `[input]`

| Key | Default | Meaning |
|---|---|---|
| `mouse` | `1` | `1` — the mouse's buttons can be bound to actions like keys ([Controls](CONTROLS.md)); `0` — the mouse's buttons do not work in the game |
| `keys_own` | — | the launcher writes your own layout of keys here while a ready one is chosen; the mod does not read this key |

### `[net]`

| Key | Default | Meaning |
|---|---|---|
| `name` | `player` | your name on the СЕТЕВАЯ ИГРА (Network game) page, in Latin letters; while it is `player`, the name is taken from Steam if Steam is up |
| `port` | `47830` | the UDP port on which the host waits for guests who join by address. If the host's port is another one, the page shows the host's address with the port, and a guest types the address the same way: `192.168.0.10:47831` |
| `delay` | `2` | input delay in frames, `1` … `12`; the host's value is the one in force for everybody |
| `transport` | `auto` | how to connect from the СЕТЕВАЯ ИГРА page: `auto` — over Steam if it runs, and by address; `steam` — Steam only; `enet` — by address only |
| `lobby` | `friends` | who sees a game opened over Steam: `friends`, or `public` — everybody who has the mod |
| `connect` | — | the host's address that the row АДРЕС (Address) of the СЕТЕВАЯ ИГРА page starts with (without it the row starts as `192.168.0.`): so as not to type the address every time |

### `[log]`

| Key | Default | Meaning |
|---|---|---|
| `level` | `info` | how much goes into `meleenet.log`: `error`, `warn`, `info`, `debug` |

There is also a `[debug]` section for the test lab (windows off the screen, scripted presses, packet loss
and so on). It is described in the source, `src/mod/config.h`, and is never needed to play.

## When the game is in a protected folder

When the game is in a folder an ordinary program may not write to (Program Files, for example), Windows may
keep the files the game changes not there but in `%LOCALAPPDATA%\VirtualStore`, and show the game those
copies. The launcher takes this into account: it reads and changes the `meleenet.ini` and the
`tmntwire.hks` that the game sees. If a setting could not be written, it says so in the bottom line of its
window.

Before the launcher changes one of these files for the first time in a run, it puts a copy of what was
there into `%LOCALAPPDATA%\meleenet\cache` (`meleenet.ini.before-launcher`, `tmntwire.hks.before-launcher`).
