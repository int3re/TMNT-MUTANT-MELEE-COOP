// Words of the mod on the game's screen: who left, that the games have parted, what a waiting game
// waits for. Shown over the picture for a few seconds.
#pragma once
#include "mod/config.h"

namespace notice {

void install(const Config& cfg);

// `text` (lines parted by a line feed) is shown for `seconds`.
void say(const wchar_t* text, int seconds);

// The words to show now ("" when none), and whether there are any.
const wchar_t* current();

}  // namespace notice
