// The players' controls. Once a frame the game reads the keyboard and the joysticks and turns them,
// by the bindings of tmntwire.hks, into four "pads": 19 numbers each (a stick and buttons). Everything
// in the game - menus and fights - reads those pads. The mod sits right after that reading: what the
// game is told for a player can be replaced (a lab script; a network session's frame).
//
// The mouse: its buttons are five more keys of the keyboard (codes no keyboard has), so the game's own
// bindings can name them.
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace input {

// A player's pad for one frame, as it goes over the network.
struct Pad {
  int8_t x = 0, y = 0;   // the stick, -127..127 (x: right, y: up)
  uint16_t buttons = 0;  // bit n = the pad's value n + 2 (game::PadValue: special .. F11)
  bool operator==(const Pad& o) const { return x == o.x && y == o.y && buttons == o.buttons; }
  bool operator!=(const Pad& o) const { return !(*this == o); }
};

constexpr uint16_t bit(int value) { return static_cast<uint16_t>(1u << (value - 2)); }

// Mouse buttons as key codes of the bindings.
constexpr int kMouseLeft = 0xF0, kMouseRight = 0xF1, kMouseMiddle = 0xF2, kMouseSide1 = 0xF3, kMouseSide2 = 0xF4;

void install(const Config& cfg);

// After the game read its devices for game frame `frame`: this PC's own player now (pad 1 of this
// PC's bindings), with the lab script's presses.
Pad read_local(unsigned frame);

// What the game is told for `player` (0..3) this frame.
void write(int player, const Pad& pad);
// A pad the game counts as plugged in (menus listen to plugged-in pads only).
void set_connected(int player, bool on);
bool connected(int player);

// Lab: the script's pads are written (no network session). `pre`: the lines marked "pre" (the run
// before the lab's epoch, frames from the game's start); otherwise the other lines.
void apply_script(unsigned frame, bool pre = false);

}  // namespace input
