// A real 4 and 6 for the Russian release's font.
//
// The Russian release painted Cyrillic letters over the font's small Latin letters and over two digits:
// Ч stands in the place of 4, Б in the place of 6. Its numbers read "1:чб" where the clock says 1:46.
// The mod gives the menu font two more glyphs under codes no text uses - a 6 that is the font's own 9
// turned half a turn, and a 4 painted into a free cell of the font's picture - and, on the way to the
// font, rewrites the 4s and 6s of every text that are digits and not letters.
#pragma once
#include <string>

#include "mod/config.h"

namespace digits {

void install(const Config& cfg);

// After the game has loaded its fonts and texts: the glyphs are added.
void on_fonts_loaded();

// The font has the two glyphs.
bool ready();

}  // namespace digits
