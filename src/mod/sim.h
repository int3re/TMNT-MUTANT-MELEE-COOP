// The game's time, made the same on every PC.
//
// The game steps its world by the real time the last frame took (times its "speed scale", 0.85, and
// smoothed). For a network game every PC must step the same world by the same amount, so the mod gives
// the game a clock of its own: exactly 0.85 / 60 s per game frame, and the mod itself keeps the frames
// at 60 a second. A game frame = one pass of the game's "system update" (pads, messages, clock).
//
// The frame gate is where the game has just read its devices: the pads of the frame are decided there
// (a network session's, a lab script's, or this PC's own).
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace sim {

void install(const Config& cfg);

// Game frames so far.
uint32_t frame();

// An epoch: the main menu's mode is entered anew, and right before it the clock and the random numbers
// are reset - the moment from which the games of a session are the same. `then` is called at that moment.
bool request_epoch(uint32_t seed, void (*then)() = nullptr);
bool epoch_pending();
uint32_t epochs();
// Game frames since the latest epoch (since the start when there was none).
uint32_t since_epoch();

// Before the process is ended by the lab's autoquit: the debug logs are written out.
void flush();

// The class name of the game's current mode ("TtMeleeGameMode"), "" when none.
const char* mode_name();
// The same for any object of the game with a virtual table.
const char* class_name(const void* object);

// A fingerprint of the game's state that must be the same on every PC of a session.
uint32_t state_hash();

// The game's random numbers start anew from `seed` (a session's start).
void reseed(uint32_t seed);

}  // namespace sim
