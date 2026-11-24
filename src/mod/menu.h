// The mod's place in the game's own menus.
//
// The main menu is a script of the game's interface (ui/scripts/UI_ModeSel_PC.txt in the archive:
// five buttons that slide in and out) driven by one function of the main menu's mode. The mod gives
// the game a script of six buttons made from the game's own (the same picture, sounds and movement)
// and drives them itself: СЕТЕВАЯ ИГРА is the third button, and the network game's pages (open a game,
// friends' games, an address, the lobby) are the same six buttons with other words - nothing is drawn
// that the game does not draw itself.
//
// Also the mod's notices over the picture, written by the game's own text drawing.
#pragma once
#include <windows.h>

#include "mod/config.h"

namespace menu {

void install(const Config& cfg);

// The window's keys and letters while an address is being typed on the network page. True: taken.
bool on_key(unsigned vk);
bool on_char(wchar_t ch);

}  // namespace menu
