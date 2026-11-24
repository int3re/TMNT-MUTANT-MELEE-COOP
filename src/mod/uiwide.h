// Menus that fill a wide picture.
//
// The game's screens are scripts laid out for 4:3 (640 units wide). In a wide picture the game draws
// every 2D element towards the middle, so a screen is a 4:3 block with nothing at its sides (the game
// hides the sides with two black bars). The scripts pass through the mod on their way to the game's
// parser, and here they are made wide. An element that ends at an edge of the layout goes on to the
// picture's edge, in the way that suits what it shows:
//   - an element that repeats its texture (a background of hexagons, the pipe along the top) is made
//     wider, the texture repeated on;
//   - a few texels pulled wide (a black box, a line, a frame's middle) are pulled wider;
//   - the brick walls along the bottom and the loading screen's picture get a second piece beside
//     them with the texture turned around at the edge (they cannot be repeated: one wall ends in a
//     transparent piece, the other is a ramp);
//   - a heading's plate and the speed lines get a second piece that repeats their column at the edge.
// An element that waits beyond the layout's edge to slide in (or slides out beyond it) waits beyond the
// picture's edge. Everything else (buttons, texts, portraits) stays where the 4:3 layout put it.
#pragma once
#include <string>

namespace uiwide {

// `extra`: how much wider than the 640 units of the layout the picture is, on each side, in those units.
std::string widen(const std::string& script, float extra);

}  // namespace uiwide
