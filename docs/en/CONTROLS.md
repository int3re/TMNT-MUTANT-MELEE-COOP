# Controls

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
**Controls** · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../CONTROLS.md)

The keys are the game's own: they are written in its file of bindings, `tmntwire.hks`. The mod does not
replace them: it adds the mouse's buttons, which can be bound like keys, and lets the
[launcher](LAUNCHER.md) set everything before the game starts.

## The keys

The launcher has two ready layouts: «Как в игре» (as in the game) — the keys the game has by itself — and
«WASD + мышь» (WASD + mouse).

| Action | As the launcher calls it | As in the game | WASD + mouse |
|---|---|---|---|
| Up / down / left / right | Вверх / Вниз / Влево / Вправо | W / S / A / D | W / S / A / D |
| Jump | Прыжок | J | Space |
| Attack | Атака | N | left mouse button |
| Second attack | Вторая атака | M | right mouse button |
| Action | Действие | H | E |
| Special move | Спецприём | Space | Q |
| Block | Блок | B | Left Shift |

In the game's menus the arrows, Enter and Esc always work, whatever keys are bound. In a fight Esc is the
pause.

**In a network game each player uses these keys** on their own computer — whichever of the four players
they are.

## Changing the keys

<p align="center">
  <img src="../images/launcher-controls.png" alt="The launcher: keys and layouts" width="820">
</p>

In the launcher: the settings → **УПРАВЛЕНИЕ** (Controls).

**Раскладка** (Layout; the arrows at the top right) switches the ready sets: «Как в игре» and «WASD +
мышь». Once you have changed keys yourself there is a third, «Своя» (your own): the launcher remembers your
keys and brings them back when you choose «Своя» after a ready layout.

One key:

1. Press the cell of the action. It lights up and says «нажмите…» (press…).
2. Press the new key. Esc cancels.

- A key that another action has changes places with the key this action had.
- Not every key can be bound. Esc, Enter, F1 and F11 are kept by the game for itself; the launcher does not
  take Caps Lock, Num Lock, Scroll Lock, Pause or the Windows keys either. If a key will not do, the
  launcher says so in its bottom line and the cell goes on waiting.
- **По умолчанию** (Defaults) returns the game's own keys.

The keys are written at once, into `tmntwire.hks` in the game's folder, and take effect at the next start
of the game. Before the first change in a run the launcher puts a copy of the file as it was into
`%LOCALAPPDATA%\meleenet\cache` (`tmntwire.hks.before-launcher`).

## The mouse's buttons

An action can have a button of the mouse **in place of** its key: the left one, the right one, the middle
one (the wheel pressed) and the two side ones.

<p align="center">
  <img src="../images/launcher-controls-wait.png" alt="A cell waits for a key or a button of the mouse" width="820">
</p>

1. Press the cell of the action.
2. Press the right, the middle or a side button of the mouse. To give the action the left button, press
   **that same cell** with it once more.

While a cell waits, the hint under the table says: «Нажмите клавишу или кнопку мыши: правую, среднюю,
боковую — или левую по этой же ячейке. Esc — отмена.» (Press a key or a button of the mouse: the right, the
middle, a side one — or the left one on this same cell. Esc cancels.)

| On the cell | The button |
|---|---|
| ЛКМ | left |
| ПКМ | right |
| Колесо | middle (the wheel pressed) |
| Мышь 4 | the first side button |
| Мышь 5 | the second side button |

- A button, like a key, belongs to one action: if another action had it, the two change places.
- **По умолчанию** returns the game's own keys, with no buttons of the mouse.

How they work in the game — this is what the mod's code says; it has not been checked with a real mouse in
the game's real window yet:

- **In a fight** a button does what the key would do. This holds in a network game too: the button goes
  into your controls before they are sent to the others.
- The buttons count as pressed only while the game's window is in front and the pointer is over the game's
  picture: a click on another window is not a punch.
- **In the menus** the mouse's buttons do nothing: there the game reads only the arrows, Enter and Esc from
  the keyboard, not the keys that are bound.
- **The mouse's buttons need the mod**: it is the mod that hands them to the game as keys. Without the mod,
  or with the buttons turned off (launcher → СЕТЬ (Network) → «Кнопки мыши в игре» (the mouse's buttons in
  the game): «Выкл» (off)), an action that has a button is left without a key — give it an ordinary one.

## Gamepads

Gamepads are bound by the game itself: its file of bindings, `tmntwire.hks`, holds both the keyboard and
the joysticks. The launcher changes only the keyboard's record in it, and the mod adds nothing for
gamepads. Gamepads have not been checked with the mod.

## What has not been checked

Nobody has yet checked the keyboard, the mouse's buttons or the typing of an address in the game's real
window in front: in the lab the windows of the test copies stand off the screen and are never active, and
the presses are fed by a script. Gamepads have not been checked at all.
