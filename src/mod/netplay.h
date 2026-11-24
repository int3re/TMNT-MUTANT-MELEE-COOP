// The network game: up to four PCs, each runs its own copy of the game, and all copies are fed the
// same pads frame by frame (lockstep). Player 1 is the host's; each plays with their own "player 1"
// controls. The host is the centre: guests send it their pads, it sends everybody every player's pads.
//
// A session starts with the process. The game is the same on every PC only when it has the same past,
// and a game that has already played a fight keeps things of it; a game started anew for the session
// has no past. So the players meet before the game's first frame (a guest takes the host's save), and
// all play the same frames from the very first one - a start-up without the logos, straight to the menu.
// The games get there from the network page of the running game (netlobby), which starts them again.
// A player who comes while the session is being played (or comes back after a lost link) waits at the
// host's door; between two fights the host gives the word and every game starts again, the newcomer in.
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace netplay {

// Before anything else of the mod: a game started again from the network page is told by MELEENET_*
// variables what it is in the session; it also waits for the game before it to be gone.
// True: this is such a game (cfg now says host or join).
bool prepare(Config& cfg);

void install(const Config& cfg);

// True in a game that was started for a session (even before the players have met, and after the session is over).
bool session_game();

// Before the game's first frame: the host waits for its guests, a guest finds the host; the save is passed over.
void meet();

// The frame gate, after the game read its devices for game frame `frame`. In a session: this PC's pad
// is sent, the call returns when every player's pad for the frame is known, and the game's pads are
// written. False: no session decides the pads (the game is played alone).
bool frame(uint32_t frame);

// A pass of the game's system update that is not a game frame (a loading screen's picture): the pads
// stay what the session's last frame made them.
void hold();

// This game is closing (or leaves the session for another reason): the others are told.
void leave(const char* why);

bool active();
// This PC's player in a session (0 = the host); 0 outside.
int me();
// Players in the session (1 outside).
int players();
// A player's name ("" for an empty place).
const char* name(int player);
// The session's players other than `lead` who are in the game at the frame being played: the same
// answer on every PC at the same frame. Their numbers go to out[]; returns how many.
int partners(int lead, int out[3]);

}  // namespace netplay
