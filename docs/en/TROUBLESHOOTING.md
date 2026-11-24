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

**The game is slow or jerky.**
The mod counts the game's time in frames: 60 frames are a second of the game. When the computer cannot draw
60 pictures a second, in a fight the mod plays frames without their pictures — up to three in a row: the
picture gets jerky, but the game keeps its real speed. This is the setting **Пропуск кадров** (frame
skipping; launcher: ГРАФИКА → КАРТИНКА; in `meleenet.ini`, `[video] frame_skip`), on by default. Outside a
fight — in the menus and in a fight's opening — no frames are skipped, and a computer that cannot draw them
slows the game.

How many pictures were shown and how many frames were played without one is written in `meleenet.log` in
the game's folder every half a minute (the numbers in the example are made up):

```
video: 3600 pictures shown, 60.0 a second lately, 0 frames played without a picture; render targets set 14400 times, refused 0
```

- `a second lately` under 60, or a growing number before `frames played without a picture`, means the
  computer cannot draw in time. Try closing programs that use the video card, or lowering **ГРАФИКА →
  Сглаживание** (Picture → Smoothing) in the launcher (8x → 4x → Выкл).
- The mod does not skip more than three frames in a row: a computer that cannot draw even every fourth
  picture in time still slows the fight.
- The game is slow in a fight itself and the log shows no frames without a picture — check that **Пропуск
  кадров** is **Вкл** (on) and that the mod is not older than 0.2.0.
- **In a network game** all the games make every frame together. In a fight one slow computer no longer
  slows the others — if frame skipping is on at that computer itself; outside a fight it still slows
  everybody. In the lab one copy of two needed 30 ms more for every picture: with the setting on both played
  the fight at 60 frames a second, with it off both at 29.

**Black bars at the sides.**
In the menus: ГРАФИКА → **Меню** (Menus): **На весь кадр** (the whole frame). In fights: ГРАФИКА →
**Формат кадра** (Shape of the frame): **Под монитор** (the monitor's). The page БИБЛИОТЕКА (Library) is
left as it is: 4:3 on black.

**Numbers show the letters Ч and Б instead of 4 and 6.**
The Russian release's font has letters painted in the places of these two digits. Since version 0.2.0 the
mod gives the menu font a real 4 and 6: in the address, in the players' numbers, in a fight's clock and
scores, in the options. If the letters still show, the mod is older than 0.2.0 or it could not add to the
font: `meleenet.log` then has a line with the word `text:` that says why. The address you typed is right
all the same.

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

**Something is written after the host's name in the list.**
- «ДРУГАЯ ВЕРСИЯ MELEENET» (another version of meleenet) — you and the host have different network
  protocols, and such a game cannot be chosen. Version 0.2.0 (protocol 2) does not play with versions 0.1.x
  (protocol 1): both update.
- «ДРУГОЙ ZTMNT.EXE» (another ztmnt.exe) — different editions of the game; such a game cannot be chosen.
- «ИДЕТ ИГРА» (a game is going on) — that game is being played, but it can be chosen: you come into it
  between two fights ([Network play](NETPLAY.md#coming-into-a-game-that-is-being-played)).

**The page of the game says ВОЙДЕТЕ ПОСЛЕ БОЯ** (you come in after the fight).
The host's game is already being played. Wait on this page: as soon as the session is between two fights,
every game — yours too — closes and starts again, now with you in. A fight is not interrupted for it, so
the wait lasts until the fight ends.

**«НОВЫЙ ИГРОК ЖДЕТ: ОН ВОЙДЕТ ПОСЛЕ БОЯ»** (a new player waits: they come in after the fight).
The host sees this when a new player knocked during a fight. Nothing has to be done: after the fight every
game starts again, with the new player in.

**In the middle of a network game every game closed and opened again.**
A new player came in, or one whose link was lost came back: between two fights the session begins anew for
everybody. After that start everybody is in the main menu; a story goes on from what the host's save holds.

**A game that is being played cannot be joined.**
- The host of a running game waits for newcomers both on Steam and by address. If one way does not work,
  try the other: Steam may not be running at the host's, or the port `47830` may be taken or closed.
- A session has four players at most.
- Coming into a running game has not been checked on real Steam yet — only with the stand-in for Steam in
  the lab, and by address.

**«ХОСТ НЕ ПУСТИЛ: …»** (the host did not let you in).
The reason follows the colon: the host has another version of the mod, another version of the game
(`ztmnt.exe`), other files of the game (`archive.arc`), the game already has four players, or it is already
starting — in the last case the games are starting again right then, so try a little later. All players
need the same game and versions of the mod with the same protocol (the launcher shows it under each
version).

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

**«ИГРОК … ВЫШЕЛ ИЗ ИГРЫ»** (player … has left the game).
That player has closed the game, or nothing was heard from them for 15 seconds. The others play on. The
player can come back: for them the session is over, СЕТЕВАЯ ИГРА (Network game) is in their main menu again
— they join the host as a new player does (ИГРЫ ДРУЗЕЙ В STEAM or ВОЙТИ ПО АДРЕСУ), and between two fights
every game starts again, with them in. If the only guest has left, the host sees «СЕТЕВАЯ ИГРА ОКОНЧЕНА»
(the network game is over): for the guest to come back, the host chooses СЕТЕВАЯ ИГРА → СОЗДАТЬ ИГРУ (Open
a game) again.

**«СВЯЗЬ С ХОСТОМ ПОТЕРЯНА. ДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН»** (the connection to the host is lost; you play on
alone).
The host has closed the game, or the connection to them was lost for 15 seconds. For you the network game
is over, and СЕТЕВАЯ ИГРА is in your main menu again — the game need not be closed. If the host goes on
playing with the others, join them as a new player: you are in the game between two fights. If the host has
closed the game, begin the network game anew.

**In the story my fighter is not in the match.**
Some matches of the story stay for the hero alone, and the partners watch them: the training matches with
the mecha turtle, the bomb match and the other mini-games, the four matches where an enemy comes back as
another one, and matches that already use all eight places
([Network play](NETPLAY.md#the-story-together)). A partner cannot choose a fighter: it is a turtle by the
player's number.

**In a network game my Enter does not end the pause.**
While the second row of the pause menu — leave the fight — is highlighted, only the keys of the player who
leads the menus act. On the first row any player's Esc or Enter goes on with the fight.

**«ИГРЫ РАЗОШЛИСЬ. НАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО»** (the games have parted; start the network game again).
The games on different computers no longer match. Before you start the game again, keep `meleenet.log` of
every player: the log is written anew at every start of the game. Then begin the network game again and,
if you can, report the problem.

**My game stands and waits.**
Some player's game is busy (a long load) or slow outside a fight, or the connection is poor. Every half a
minute `meleenet.log` says how long the game has waited for the others (the numbers in the example are made
up):

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
