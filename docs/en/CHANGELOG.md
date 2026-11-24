# History of versions

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../CHANGELOG.md)

The newest version is at the top. **Protocol** is the network protocol: players meet in a network game
only on versions with the same one. Version 0.1.0 was not published; the first public release is 0.1.1.

| Version | Launcher | Protocol | Date | In a line |
|---|---|---|---|---|
| [0.3.0](#030) | 1.3.0 | 3 | 24 Nov 2026 | a network game begins inside the running game, no windows closing; players' names over the fighters; the story together to its end |
| [0.2.0](#020) | 1.2.0 | 2 | 24 Nov 2026 | the story together; coming into a running game; anybody's pause; frame skipping; a real 4 and 6 |
| [0.1.1](#011) | 1.1.0 | 1 | 24 Nov 2026 | menus fill a wide frame; the launcher has a look of its own and updates the mod and itself |
| [0.1.0](#010) | 1.0.0 | 1 | 23 Nov 2026 | the first network game, a window and a wide frame, the first launcher |

Who plays with whom over the network:

| Versions | Protocol | Together |
|---|---|---|
| 0.3.0 | 3 | with 0.3.0 only; does not play with 0.2.0 or 0.1.x |
| 0.2.0 | 2 | with 0.2.0 only; does not play with 0.1.x |
| 0.1.0 – 0.1.1 | 1 | play with each other |

## 0.3.0

Launcher 1.3.0. Protocol 3: it does not play with 0.2.0 or 0.1.x over the network — every player has to
update.

Everything listed here was checked in the lab only: two to four copies of the game on one computer, with no
window on the screen, by address and through the stand-in for Steam. Real Steam accounts, a real focused
window and gamepads are still not checked by a real game.

- **A network game begins inside the running game: no restart.** Before 0.3.0 a network game began by closing
  every game and starting it again, as a new process; a player who came into a running game came in by such a
  restart of every game too. Now nothing is closed: after НАЧАТЬ ИГРУ (Start the game) the picture goes
  through the game's start-up again in a couple of seconds — eight times as fast as usual, with no logos to
  sit through — and everybody is in the main menu of the common game. The window, the video device and Steam
  stay as they are.
  - Why it is done this way. A game that has already played keeps things of it (the order of its particle
    systems, the AI's queued messages and more), so two games with different pasts part. A process that has
    just started has no past. At the moment the game enters its very first mode the mod keeps a copy of the
    game's whole memory, and to begin a session it puts that copy back (15–30 ms): every PC gets the state of
    a just started process without a new process. More in
    [How it works](ARCHITECTURE.md#the-game-begins-anew-without-a-new-process).
  - A player who comes into a running game, or comes back after a lost link, comes in the same way: between
    two fights everybody's game begins anew in place — no window is closed — now with that player in.
  - When a session is over for a guest (the host left, the link was lost) and the guest's game is back at its
    main menu, the game begins anew once more by itself — a couple of seconds — now on the player's own save:
    in a session a guest plays on the host's save, afterwards the game is the player's own again.
  - The fallback. If the memory cannot be kept or put back — with `[video] mode = game` (the game's own
    exclusive full screen), or when the video device was lost and reset after the game had started (the
    screen was locked, for example) — the mod does what 0.2.0 did: the game starts again as a new process. A
    game that takes this path and games that begin in place play together.
  - If the games did not find each other («ИГРОКИ НЕ ПОДКЛЮЧИЛИСЬ»), the game goes on to its main menu by
    itself and stays its player's own game, on the player's own save: before, it stayed at the "press Enter"
    screen as a session's game with no players.
- **Players' names over their fighters.** In a network game the tag over a human's fighter — the game's own
  arrow in the player's colour, which says 1P – 4P in the original — shows the player's name, in РУКОПАШНЫЙ
  БОЙ (Melee) and in ИСТОРИЯ (Story) alike. The name is the player's Steam name. Without Steam (a game by
  address with Steam not running) it is `[net] name` of `meleenet.ini`, if the player wrote a name there by
  hand (Cyrillic works), else «ИГРОК N» (player N) by the player's number. The launcher has no row for the
  name. Names with digits show a real 4 and 6.
- **The story together, further.** The partners now fight in the training matches with the mecha turtle (in
  0.2.0 these were for the hero alone) and in the matches where an enemy comes back as another one — the last
  stage of Leonardo, Raphael, Donatello and Casey (Shredder, then the golden Shredder). A story can be played
  together to its end. Still for the hero alone: the bomb match and the other mini-games (they run in a mode
  of their own, made for one fighter) and matches that already use all eight places.
- The game no longer resets its video device in its very first frame: RenderWare did that to switch "wait for
  the monitor" on, and a window's device has no such setting.
- `[net] name` in `meleenet.ini` is now a fallback name, for when Steam is not running: in UTF-8 or in
  Windows' own code page, 31 bytes at most ([Settings](SETTINGS.md#net)).

Launcher 1.3.0:

- It carries the mod 0.3.0. The note under the rows of the tab СЕТЬ (network) tells of the name: «Над вашим
  бойцом — ваш ник из Steam» (over your fighter is your Steam name). Nothing else changed in the launcher.
- The button «Журнал до сессии» (the log before the session) still opens `meleenet-menu.log`. That file now
  appears only when a session had to start a new process (the fallback): when a session began inside the
  running game, everything is in `meleenet.log`.

What exactly was checked in the lab:

- Beginning inside the running game: sessions of two, three and four copies through the network page (by
  address and through the stand-in for Steam), with antialiasing on and off. A session begun inside the running
  game was compared line by line with a session of freshly started processes: the same frames.
- The game's past: a copy that had played fights and the story and visited the library before the session
  against a copy that had not — identical 4500 frames. Five returns to the start in a row after played fights:
  the process's memory stays about 280 MB and does not grow. A 15000-frame session with two fights played out.
- Coming in and coming back: a third copy comes into a running game; a guest loses its link, gets its own game
  back and joins again — five times in a row.
- The story: the last stage with three copies (the golden Shredder appears, the logs were identical), the
  training with two.
- Not checked: a real game with sound audible (the lab runs muted) and a lost video device.

## 0.2.0

Launcher 1.2.0. Protocol 2: it does not play with 0.1.0 or 0.1.1 over the network — every player has to
update.

Everything listed here was checked in the lab only: two to four copies of the game on one computer, with no
window on the screen, by address and through the stand-in for Steam. Real Steam accounts, a real focused
window, gamepads and long sessions are still not checked.

- **The story is played together.** In a network game ИСТОРИЯ (Story) is for everybody. The player who chose
  it in the main menu leads: chooses the hero and moves on the map, as before. In every match the other
  players fight beside the hero as partners. A partner plays a turtle by the player's number: 1 Leonardo, 2
  Raphael, 3 Donatello, 4 Michelangelo; a partner whose turtle is the story's hero takes the first turtle
  nobody has. A partner cannot choose a fighter yet.
  - Partners are on the hero's side by the game's own team rules: team-mates cannot hurt each other, enemies
    attack them too, each has a panel in the HUD (when all four panels are the enemies', a partner takes one
    of theirs), and each starts with the same health as the hero.
  - "Last man standing" matches are played by the game's team rules when partners are in: a side wins when
    the other side is down, and the hero's fall alone does not end the match — a partner can finish it. In
    the other match types the story's own rules stay as written: where the hero's knock-out ends a match, it
    still does, partners or not.
  - These stay for the hero alone (the partners watch, their fighters are not in): the training matches with
    the mecha turtle, the bomb match and the other mini-games, the four matches where an enemy comes back as
    another one (the last stages of Leonardo, Raphael, Donatello and Casey: Shredder), and matches that
    already use all eight places.
  - Progress and unlocks are the host's save, as before.
- **A player can come into a game that is being played, and come back after a lost link.** The host's game
  stays open while it is played. In ИГРЫ ДРУЗЕЙ В STEAM (Friends' games on Steam) it is marked «ИДЕТ ИГРА»
  (a game is going on) and can be chosen (before, a game that was being played could not be); by address it
  is joined the same way as before.
  - The newcomer waits on the network page: the row says ВОЙДЕТЕ ПОСЛЕ БОЯ (you come in after the fight), the
    small line «У ХОСТА ИДЕТ ИГРА: МЕЖДУ БОЯМИ ОНА НАЧНЕТСЯ ЗАНОВО, УЖЕ С ВАМИ» (the host is playing: between
    two fights the game starts again with you in). When a fight is going on, the host sees the notice «НОВЫЙ
    ИГРОК ЖДЕТ: ОН ВОЙДЕТ ПОСЛЕ БОЯ» (a new player waits: they come in after the fight).
  - When the session is between two fights (the main menu, the choice of a hero, of an arena and of a fight's
    options, the story's choice of a hero, the story's map), every game — the newcomer's too — closes and
    starts again into a session of all of them, exactly as at the first start. A fight is never interrupted
    for it. After that start the games are at the main menu; a story goes on from what the host's save holds.
  - A player whose link was lost, or who closed the game, comes back the same way: for them the session is
    over, СЕТЕВАЯ ИГРА (Network game) is in their main menu again, and they join the host as a newcomer.
  - When a session is over for a game (everybody left, the link to the host is lost), the game frees its port
    and its place on Steam: a new network game can be opened or joined from the same running game.
  - Still four players at most. Refusals a newcomer may read: «в игре уже четыре игрока» (the game already
    has four players), «игра уже начинается» (the game is already starting), another version of the mod,
    another version of the game or other files of the game.
  - The host of a game that is being played waits for newcomers both ways - on Steam and by address -
    whichever way its first guests came (Steam must be running at the host's, the port must be free). In the
    lab the newcomer came the same way as the first two.
- **Anybody pauses.** Esc of any player pauses the fight for everybody; any player's Esc or Enter goes on
  with it. The pause menu's second row — leave the fight — stays with the player who leads the menus (the one
  who chose the fight in the main menu). Before 0.2.0 only that player could pause at all.
- **A slow PC no longer slows a fight.** A new setting, «Пропуск кадров» (frame skipping), on by default
  (`[video] frame_skip = 1`): when the PC cannot draw 60 pictures a second, a fight's frames are played
  without their pictures, up to three in a row. The game keeps its real speed, and in a network game one slow
  PC no longer slows everybody. It works in fights only: menus and a fight's opening are still slowed by a PC
  that cannot draw them.
- **A real 4 and 6.** The Russian release's font has Ч in the place of 4 and Б in the place of 6. The mod
  gives the menu font a real 4 and 6: in the address on the network page, in the players' numbers, in a
  fight's clock and scores, in the options. Words keep their Ч and Б. The rows of an open game's page are
  numbered 1. 2. 3. 4. — they used to say I. II. III. IV.
- Fixed: «ИГРЫ РАЗОШЛИСЬ» (the games have parted) was not shown when a guest's game was a frame or two ahead
  of the host's (its fingerprint came before the host had its own). Now the divergence is noticed at the next
  check.
- Drawing and particles no longer take random numbers from the game's rules: what a PC draws, how wide its
  frame is, or whether it draws a frame at all cannot part the games.
- Wide menus: the story's results screen fills a wide frame — its black borders stand at the picture's edges.
- The key `vsync` of the section `[video]` is gone: the mod read it and did nothing with it.
- The log's line about the picture now tells of the frames without a picture too:

  ```
  video: 3600 pictures shown, 60.0 a second lately, 0 frames played without a picture; render targets set 14400 times, refused 0
  ```

Launcher 1.2.0:

- On the tab ГРАФИКА (graphics), in the box КАРТИНКА (picture), a row «Пропуск кадров» (frame skipping): «Вкл»
  (on) or «Выкл» (off). Nothing else changed in the launcher.

What exactly was checked in the lab:

- The story: two and three copies; a duel stage (won; the hero down and the partner wins; everybody down —
  lost, then the rematch) and a team stage with a friend from the game's script (three players); the logs
  were identical frame by frame. Not checked: every stage of every hero, a real game.
- Coming into a running game: a third copy knocks during a fight of two (by address and through the stand-in
  for Steam) and is in after the fight; a guest returns after its link was lost for good; the logs of the new
  session were identical. Not checked on real Steam.
- The pause: a guest pauses and goes on with the fight; the player who leads the menus leaves the fight.
- Frame skipping: one copy of two needed 30 ms more for every picture. With the setting on both copies played
  the fight at 60 frames a second, with it off both at 29.

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
