// Screen for ztmnt.exe. The game knows only an exclusive full screen: at every start it takes
// 640x480 in 16 bits, minimizes itself when another window comes in front, and starts itself again
// to change the resolution. The mod makes the game's Direct3D 8 device a windowed one of any size and
// places the game's window itself: without a frame over the whole monitor (borderless) or as an
// ordinary window. RenderWare still believes in a full-screen mode of that size.
//
// The picture: any resolution, the monitor's own shape (the game's console wide-screen switch, taught
// every shape), smoothed edges, sharp textures at an angle.
//
// Also the lab's eyes: frames saved from the back buffer, never from the desktop.
#pragma once
#include <windows.h>

#include "mod/config.h"

namespace video {

void install(const Config& cfg);

HWND window();
// The game is asked to close (as by its window's cross); if it has not gone in a few seconds, it is ended.
void request_quit();
// Pictures shown so far.
unsigned frames();
// The size of the frame the game draws.
int frame_w();
int frame_h();
// Once a game frame (from the frame gate): the wide-screen switch is held, a due screenshot is asked for.
void on_frame(unsigned frame);
// True while this game's window is the one in front.
bool in_front();
// Wide menus: how much wider than the 640 units of a screen's layout the picture is, on each side, in
// those units (0: the picture is 4:3, or the menus keep the game's bars at their sides).
float menu_extra();

}  // namespace video
