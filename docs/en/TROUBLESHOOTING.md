# If something is wrong

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · **Troubleshooting**

**English** · [Русский](../TROUBLESHOOTING.md)

## The launcher

**Windows says "Windows protected your PC".**
The launcher is not signed with a certificate. Press **More info → Run anyway**. The file's SHA-256 is in
`meleenet-latest.json` of the [release](https://github.com/int3re/TMNT-MUTANT-MELEE-COOP/releases/latest).

**The launcher does not find the game.**
Press **ВЫБРАТЬ ПАПКУ ИГРЫ** (Choose the game's folder) or **Изменить** (Change) at the bottom of the
window, and choose the folder that holds `ztmnt.exe`.

**«ztmnt.exe в этой папке не тот, для которого сделан мод: с ним мод не включится»** (the ztmnt.exe in this
folder is not the one the mod is made for).
Your `ztmnt.exe` is not the one the mod is made for. The mod will change nothing in such a game. The
supported file is the Russian release's, SHA-256 `585a5809…f9bbdce2` ([Install](INSTALL.md)).

**GitHub does not answer / the update does not download.**
Settings → **ЛАУНЧЕР** (Launcher) → **Прокси для GitHub** (a proxy for GitHub): try **Авто** (Auto), or
**Свой** (Own) with the address of your proxy. Without GitHub the launcher still installs the mod it
carries and starts the game.

**The game is running and the launcher will not install.**
While the game is open, the mod's files in its folder cannot be replaced. Close the game, or let the
launcher ask it to close (**ЗАКРЫТЬ ИГРУ И ОБНОВИТЬ** — Close the game and update): the install begins by
itself.

**«Нужно разрешение Windows»** (Windows' permission is needed).
The game is in a folder an ordinary program may not write to (Program Files, for example). The launcher
asks Windows for administrator rights; without them the mod cannot be put into such a folder or taken out
of it. Press **ПОВТОРИТЬ** (Again) and allow it.

**«Папка игры закрыта для записи: запустите лаунчер от имени администратора»** (the game's folder is closed
for writing: run the launcher as administrator).
The launcher could not write a setting into the game's folder. Run it as administrator, or move the game
into a folder that can be written to.

**The rows of the tabs ГРАФИКА, СЕТЬ and УПРАВЛЕНИЕ are pale.**
The mod is not installed: these settings are kept in `meleenet.ini`, which comes with the mod.

## The game

**There is no СЕТЕВАЯ ИГРА (Network game) button in the main menu.**
Look into the game's folder. If there is no `meleenet.log` there, the mod has most likely not loaded: in
the launcher press the settings → **ЛАУНЧЕР** → **Переустановить мод** (Reinstall the mod). If the file is
there, it says why — for example, that `ztmnt.exe` is not the one the mod is made for.

**The game is slow.**
The mod counts the game's time in frames: 60 frames are a second of the game. If the computer shows fewer
than 60 frames a second, the game runs slower than it should. How many frames were shown is written in
`meleenet.log` in the game's folder every half a minute (the numbers in the example are made up):

```
video: 3600 pictures shown, 60.0 a second lately; render targets set 14400 times, refused 0
```

- Fewer than 60 a second means the computer cannot draw that many. Try closing programs that use the video
  card, or lowering **ГРАФИКА → Сглаживание** (Picture → Smoothing) in the launcher (8x → 4x → Выкл).
- **In a network game a slow computer slows the game for everybody**: all the games make every frame
  together.

**Black bars at the sides.**
In the menus: ГРАФИКА → **Меню** (Menus): **На весь кадр** (the whole frame). In fights: ГРАФИКА →
**Формат кадра** (Shape of the frame): **Под монитор** (the monitor's). The page БИБЛИОТЕКА (Library) is
left as it is: 4:3 on black.

**An address shows «ч» and «б» instead of 4 and 6.**
In the game's Russian font these two digits are drawn that way. The address is typed correctly all the
same; it is the same with a player's number in the message «ИГРОК … ВЫШЕЛ ИЗ ИГРЫ» (player … has left the
game).

**Smoothing does not work.**
It comes on only when the frame fills the whole window: **Разрешение: Как монитор** (Resolution: as the
monitor) and **Формат кадра: Под монитор** (or **Окно игры: В окне** — the game's window: in a window).
Otherwise `meleenet.log` has a line like

```
video: antialiasing off: the frame 1280x720 does not fill the window (1920x1080)
```

**The mouse's buttons do nothing.**
- They act in a fight, not in the menus.
- A change made in the launcher takes effect at the next start of the game.
- In the launcher: СЕТЬ → **Кнопки мыши в игре** (the mouse's buttons in the game) — **Работают** (work).
- The game's window has to be the one in front, and the pointer over the game's picture.
- The buttons have not been checked with a real mouse in the game's real window yet: if they do not work
  with all of the above in place, report it.

**The launcher will not bind a key.**
Esc, Enter, F1 and F11 are kept by the game for itself; the launcher does not bind Caps Lock, Num Lock,
Scroll Lock, Pause or the Windows keys either. Choose another key.

## A network game

**The row ИГРЫ ДРУЗЕЙ В STEAM (Friends' games on Steam) is pale and the page says «STEAM НЕ ЗАПУЩЕН»**
(Steam is not running).
Start Steam: the page tries to reach it again by itself a few seconds later. Why Steam did not come up is
written in `meleenet.log`. With `[net] transport = enet` in `meleenet.ini` the mod does not turn to Steam
at all. The mod has not been checked with real Steam accounts yet: if it does not work out, play by address
and report the problem.

**The friend's game is not in the list (Steam).**
Steam must be running at all players; the host must choose **СОЗДАТЬ ИГРУ** (Open a game) and stay on that
page. A game opened with **Видят в Steam: Друзья** (Seen in Steam: friends) is seen only by the host's
Steam friends. The list refreshes by itself every few seconds.

**Something is written after the host's name in the list, and the game cannot be chosen.**
That is the reason: «ДРУГАЯ ВЕРСИЯ MELEENET» (another version of meleenet) — you and the host have
different network protocols, both update; «ДРУГОЙ ZTMNT.EXE» (another ztmnt.exe) — different editions of
the game; «УЖЕ ИГРАЕТ» (already playing) — that game has already begun.

**«ХОСТ НЕ ПУСТИЛ: …»** (the host did not let you in).
The reason follows the colon: the host has another version of the mod, another version of the game
(`ztmnt.exe`), other files of the game (`archive.arc`), the game already has four players, or it is already
starting. All players need the same game and versions of the mod with the same protocol (the launcher shows
it under each version).

**Cannot join by address: «ХОСТ НЕ ОТВЕЧАЕТ»** (the host does not answer).
A guest needs a way to the host over UDP, port `47830`. Things worth checking:

- the address. At the bottom of the screen the host sees the address of their computer in its own network —
  it will do for a game in one local network. A game over the internet needs the host's outer address and
  the port forwarded at their router; a game over a virtual network (Radmin VPN, Hamachi) needs the host's
  address in that network;
- the host's firewall: the game must be allowed to accept connections.

**«НЕ УДАЛОСЬ ОТКРЫТЬ ИГРУ: STEAM НЕДОСТУПЕН, ПОРТ ЗАНЯТ»** (could not open the game: Steam is not
available, the port is taken).
The game could be opened neither over Steam nor by address: Steam is not available, and UDP port `47830` on
this computer is taken by another program — a second copy of the game, for example. Close it, or set
another port: `[net] port` in `meleenet.ini`.

**All the games closed after НАЧАТЬ ИГРУ (Start the game).**
That is how a network game begins: the games close and open again a few seconds later, already in the
common game.

**«ИГРОКИ НЕ ПОДКЛЮЧИЛИСЬ. СЕТЕВАЯ ИГРА НЕ НАЧАЛАСЬ»** (the players did not connect; the network game did
not start).
After the restart the games did not find each other in 45 seconds. Close the game and begin the network
game again.

**«ИГРОК … ВЫШЕЛ ИЗ ИГРЫ».**
That player has closed the game, or nothing was heard from them for 15 seconds. The others play on. There
is no coming back into a game that is going.

**«СВЯЗЬ С ХОСТОМ ПОТЕРЯНА. ДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН»** (the connection to the host is lost; you play on
alone).
The host has closed the game, or the connection to them was lost for 15 seconds. The network game is over;
to play together again, begin it anew.

**«ИГРЫ РАЗОШЛИСЬ. НАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО»** (the games have parted; start the network game again).
The games on different computers no longer match. Before you start the game again, keep `meleenet.log` of
every player: the log is written anew at every start of the game. Then begin the network game again and,
if you can, report the problem.

**My game stands and waits.**
Some player's game is busy (a long load) or slow, or the connection is poor. Every half a minute
`meleenet.log` says how long the game has waited for the others (the numbers in the example are made up):

```
net: frame 3600, ping 24 ms, waited 310 ms in 18 waits so far, the keeper stood in 0 times
```

On a poor connection the host may try a longer input delay: launcher → СЕТЬ → **Задержка ввода** (Input
delay).

## Reporting a problem

Open an [issue](https://github.com/int3re/TMNT-MUTANT-MELEE-COOP/issues) and attach, from the game's folder
of **every** computer of the network game:

- `meleenet.log` — the log of the last run;
- `meleenet-menu.log` — the log of the game the network game was started from.

The launcher opens both: the settings → **ЛАУНЧЕР** → **Открыть журнал** (Open the log) and **Журнал до
сессии** (The log before the session). If the launcher says there is no log, and the game is in a folder an
ordinary program may not write to (Program Files, for example), look for the logs in
`%LOCALAPPDATA%\VirtualStore`, in a folder with the same path as the game's: Windows may put them there.
Say what version the launcher shows, what you did and what happened.

> The logs hold your Steam name, your Steam ID, network addresses and the folder the game is in. Look them
> through before posting them in public, or remove those lines.

## Removing the mod

Launcher → the settings → **ЛАУНЧЕР** → **Удалить мод** (Remove the mod). Or delete from the game's folder
the files listed in `meleenet-install.txt`: the game is then as it was.
