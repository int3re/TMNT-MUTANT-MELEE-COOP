// The story for two to four.
//
// The game's story is for one: a hero against the story's matches. But a story match is built from the
// same parts as a versus fight: up to eight fighters in two teams, the hero's team with scripted
// "friends" (led by the computer) that cannot hurt each other. In a network session the mod adds the
// other players to every match as such friends, and hands their fighters to their pads.
#pragma once
#include "mod/config.h"

namespace story {

void install(const Config& cfg);

// The game's memory was put back to its beginning: nothing of the mod is in any match.
void reset();

// Lab: every stage of the story being played is open.
void lab_open_stages();
// Lab, every game frame: [debug] story_open, ko_enemies / ko_hero / ko_partners.
void lab_frame(unsigned frame);

}  // namespace story
