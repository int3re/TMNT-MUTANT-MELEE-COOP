# Playing over the network

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · **Network play** ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../NETPLAY.md)

The game has a mode for four, **РУКОПАШНЫЙ БОЙ** (Melee), on one computer. meleenet plays that same mode
over the network: each player has a copy of the game and a screen of their own, and each computer is one of
the four players. Player 1 is the one who opens the game (the host); the others join (the guests). Two,
three or four can play.

<p align="center">
  <img src="../images/game-session.png" alt="A network game: a fight of four players" width="820"><br>
  <sub>A fight of four players in a network game.</sub>
</p>

## Before you start

- All players have **the same game** — the same `ztmnt.exe` and the same `archive.arc` — and versions of
  the mod with **the same network protocol** (the launcher shows it under each version). The host does not
  let in a guest whose files differ, and the guest sees why at the bottom of the screen.
- **Over Steam:** Steam is running at all players and they are friends in Steam. The game does not have to
  be in Steam: the mod comes to Steam as Valve's test application Spacewar (AppID 480). This has not been
  checked on real Steam accounts yet — only with a stand-in for Steam in the lab.
- **Without Steam:** the guests need a direct way to the host over UDP, port `47830`: one local network, a
  port forwarded at the host's router, or a common virtual network (Radmin VPN, Hamachi).
- Set the keys and the mouse's buttons beforehand, in the [launcher](LAUNCHER.md).

## Starting a game

<p align="center">
  <img src="../images/game-main-menu.png" alt="The main menu with the button СЕТЕВАЯ ИГРА" width="410">
  <img src="../images/game-network-page.png" alt="A page of the network game" width="410">
</p>

1. **Everybody** starts the game as usual and chooses **СЕТЕВАЯ ИГРА** (Network game) in the main menu —
   the third button, under РУКОПАШНЫЙ БОЙ.
2. **The host** chooses **СОЗДАТЬ ИГРУ** (Open a game). The page of the game opens: four places, **I.** –
   **IV.**; the first holds the host's name, the others say СВОБОДНО (free).
3. **The guests** join:
   - *over Steam* — **ИГРЫ ДРУЗЕЙ В STEAM** (Friends' games on Steam), then the row with the host's name;
   - *by address* — put the highlight on the row **АДРЕС** (Address) and press Enter; type the host's
     address (digits and dots; Backspace deletes, Ctrl+V pastes an address from the clipboard) and press
     Enter again; then choose **ВОЙТИ ПО АДРЕСУ** (Join by address). The host sees their own address at
     the bottom of the screen: «АДРЕС ДЛЯ ДРУЗЕЙ: …» (address for friends).
4. When everybody is in the list, **the host** chooses **НАЧАТЬ ИГРУ** (Start the game; the highlight goes
   to this row by itself as soon as a second player is in). Everybody's game closes and opens again a few
   seconds later — in the common game, straight in the main menu.
5. From there the game goes on as usual. Any player leads the menus; in РУКОПАШНЫЙ БОЙ each player picks
   their own hero.

<p align="center">
  <img src="../images/game-hero-select.png" alt="Choosing heroes in a network game" width="820">
</p>

### The rows of the pages

The pages are walked with the keys of the game's own menus: the arrows, Enter, Esc (back).

| Page | Row | What it does |
|---|---|---|
| the first | **СОЗДАТЬ ИГРУ** | opens your game for the others: you are the host |
| | **ИГРЫ ДРУЗЕЙ В STEAM** | the list of games found over Steam. Pale until Steam is up |
| | **ВОЙТИ ПО АДРЕСУ** | join the host at the address of the row below |
| | **АДРЕС: …** | Enter starts the typing, Enter again (or Esc) ends it. A port can be added to the address: `192.168.0.10:47830` |
| | **STEAM ПОДКЛЮЧЕН** (Steam is up) / **STEAM НЕ ЗАПУЩЕН** (Steam is not running) | only tells; it cannot be chosen |
| | **НАЗАД** (Back) | to the main menu |
| friends' games | the host's name | join that game. If it cannot be joined, the reason follows the name: another version of meleenet, another `ztmnt.exe`, «уже играет» (already playing) |
| | **ИДЕТ ПОИСК...** (Searching) | only tells: the list refreshes by itself |
| an open game | **I.** – **IV.** | who is here already |
| | **НАЧАТЬ ИГРУ** | the host's, once there are two players or more; before that **ЖДЕМ ИГРОКОВ** (Waiting for players). A guest has **ЖДЕМ ХОСТА** (Waiting for the host) in this place |
| | **ЗАКРЫТЬ ИГРУ** (Close the game) / **ВЫЙТИ** (Leave) | the host closes the game they opened; a guest leaves it |

All the pages are made of the six buttons of the main menu: the same pictures, the same font, the same
highlight. In the game's Russian font the digits 4 and 6 are drawn as the letters «ч» and «б» — that is how
they look in an address; the players' places are numbered in Roman numerals for that reason.

Who sees a game opened over Steam — the host's friends, or everybody who has the mod — is chosen
beforehand, in the launcher: СЕТЬ (Network) → «Видят в Steam» (Seen in Steam).

### A network game begins with a restart

After **НАЧАТЬ ИГРУ** each game closes and starts again — for the common game. The logos and the "press
Enter" screen pass by themselves, fast, and a few seconds later everybody is in the common main menu. It is
done this way because a game in which something has already been played and a game that has just started
do not behave alike; after the restart all the games are the same from the first frame.

The games that started again wait for each other for up to 45 seconds. If a guest has not found the host in
that time, or nobody has come to the host, the top of the screen says «ИГРОКИ НЕ ПОДКЛЮЧИЛИСЬ. СЕТЕВАЯ ИГРА
НЕ НАЧАЛАСЬ» (the players did not connect; the network game did not start): close the game and begin again.

## In a network game

- **Each plays with their own keys** on their own computer — whichever of the four players they are.
- **РУКОПАШНЫЙ БОЙ is for everybody**, up to four players. **ИСТОРИЯ** (Story) is made for one player in
  the game itself: over the network one player plays it and the others watch.
- **The menus are common.** Any player leads them: what one has chosen is chosen for all.
- **There is no СЕТЕВАЯ ИГРА item in the menu**: its place says «В СЕТИ: ИГРОКОВ …» (online: … players). To
  open another network game, start the game again.
- **The save is the host's.** The heroes and arenas that are unlocked are theirs; for the network game the
  guests get a copy of the host's save (`tmnt-net.sv`). A guest's own save, `tmnt.sv`, is not touched.
- **The pause.** Esc in a fight is the game's own pause. By the code this press goes into the common game
  like any other, so the pause should come up for everybody; it has not been checked yet, neither by real
  play nor in the lab.
- **Movies are not shown**: no two computers would leave a movie in the same frame.
- **The picture may differ.** Resolution, the shape of the frame and the look of the menus are each
  player's own: they do not change how the game plays. Particles are at 100 % for everybody in a network
  game, whatever was chosen in the launcher: their number is a part of the game itself.
- **The mod's messages** are at the top of the frame, in white letters of the game's own font: that a
  player has left, that the connection is lost, that the games have parted.

## When something goes wrong on the way

- **A long level load** does not break the connection: the others' games wait meanwhile. The same is done
  for a window being dragged with the mouse, but that has not been checked with a real window.
- **While somebody's game is busy or falls behind, the others stand and wait**: all the games make every
  frame together. For the same reason a slow computer slows the game for everybody.
- **A guest has closed the game, or nothing is heard from them for 15 seconds** — the others see «ИГРОК …
  ВЫШЕЛ ИЗ ИГРЫ» (player … has left the game) and play on; the hero of the one who left gets no more
  controls. This is what the code says; a guest leaving in the middle of a fight has not been checked yet.
- **The host has closed the game, or the connection to them is lost for 15 seconds** — the network game is
  over. The guests see «СВЯЗЬ С ХОСТОМ ПОТЕРЯНА. ДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН» (the connection to the host is
  lost; you play on alone), and each goes on by themselves. A host whom all the guests have left sees
  «СЕТЕВАЯ ИГРА ОКОНЧЕНА» (the network game is over).
- **«ИГРЫ РАЗОШЛИСЬ. НАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО»** (the games have parted; start the network game again) —
  the games on different computers no longer match. Start the network game again and keep `meleenet.log` of
  every player ([If something is wrong](TROUBLESHOOTING.md)).
- **There is no joining a game that is already going, and no coming back into it**: a network game begins
  for everybody at once, with a restart.

## How it stays the same on all computers

Nothing of the picture is sent. Each computer plays the whole game; only the players' presses go over the
network. A frame is not played until the presses of all the players for it have come, so all the games do
the same thing frame for frame. Once a second the games compare a fingerprint of their state; if it does
not match, the screen says «ИГРЫ РАЗОШЛИСЬ». The delay between pressing a key and the frame that uses it is
the **input delay** (1 to 12 frames, 2 by default; the host's value is the one in force): more of it is
smoother on a poor connection, less is more responsive.

## Versions that play together

| Versions of the mod | Network protocol | Play together |
|---|---|---|
| 0.1.0 – 0.1.1 | 1 | yes |

Version 0.1.1 with version 0.1.0 was checked in the lab: a network game with 0.1.1 as the host and 0.1.0 as
the guest stayed the same on both for 2500 frames, a fight included.

## What has been checked and what has not

- Checked in the lab — on several copies of the game on one computer, their windows off the screen: network
  games of two, three and four copies by address and through a stand-in for Steam, with packet loss; copies
  with frames of different shapes; version 0.1.1 against 0.1.0.
- **Not checked by real play:** real Steam accounts; the game's real window in front — the keyboard, the
  mouse's buttons, typing an address; gamepads; long network games; the pause in a network game; a guest
  leaving in the middle of a fight.
