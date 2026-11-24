// The game begins anew without a new process.
//
// Two games play the same frames only when they begin from the same state, and a game that has already
// played keeps things of it in pools and globals that no reset reaches. A process that has just started
// has none. So, at the moment the game enters its very first mode, the mod keeps a copy of everything the
// game's logic lives in - the exe's own variables and the game's heap - and, when a session begins, puts
// that copy back: the game is a just started process again, in the same window, with the same device.
// What the kept memory points at outside itself (textures, sound buffers, files open at that moment) is
// held so that it cannot go away; what was made after it is let go.
#pragma once
#include "mod/config.h"

namespace anew {

void install(const Config& cfg);

// The game's Direct3D device has just been made: what it makes from now on is watched.
void on_device(void* device);

// The device is about to be reset (it was lost): nothing can be held through that. The game can no longer
// begin anew in place; a session then starts a new process, as it did before.
void before_reset();

// At the game's first mode, before it is entered: the game's memory is kept.
void capture();

// The kept memory is put back (the caller is at the same place of the game's code: about to enter the
// first mode). False: there is nothing kept - the game goes on as it is.
bool restore();

// There is a kept memory to go back to.
bool ready();

}  // namespace anew
