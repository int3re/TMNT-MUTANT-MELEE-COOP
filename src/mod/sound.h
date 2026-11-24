// Sound of ztmnt.exe (DirectSound). The heroes' lines are told by the game's clock (see sound.cpp):
// the game's rules ask whether a line is being said, and the answer must be the same on every PC.
// The lab's switch: a test copy's sound is muted in Windows' mixer.
#pragma once
#include "mod/config.h"

namespace sound {

void install(const Config& cfg);

// At an epoch (sim.h): no line is being said, the choice of lines starts anew from `seed`.
void on_epoch(uint32_t seed);

}  // namespace sound
