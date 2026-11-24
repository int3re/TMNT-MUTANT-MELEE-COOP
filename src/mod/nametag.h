// Players' names over their fighters.
//
// The game marks a human's fighter with a small tag over its head: an arrow in the pad's colour and
// "1P" .. "4P" (0x419FF0, drawn for every fighter a human plays). In a network session the tag's
// words are the player's name instead - the same tag, the same place, the game's own text drawing.
#pragma once
#include <string>

#include "mod/config.h"

namespace nametag {

void install(const Config& cfg);

// From the hook of the game's text drawing: the text about to be drawn is a fighter's tag and a name
// goes in its place. `name` gets the words (ready for the game's font), `wide` the rectangle to draw them in.
bool substitute(const wchar_t* text, const float* rect, std::wstring& name, float wide[4]);

}  // namespace nametag
