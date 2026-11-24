// Where the players meet before a network game: in their running games, on the network page of the
// main menu. One opens a game (a Steam lobby for friends and a UDP port for an address), up to three
// others find it among friends' games or dial the address; the host's start then closes all the games
// and starts them again into the session (netplay: a session begins with the process, which is what
// makes the games the same from their first frame).
//
// Game thread only. The page (netpage) shows view() and calls the rest.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mod/config.h"

namespace netlobby {

enum class State {
  Closed,    // the page is not up
  Menu,      // looking: friends' games, an address
  Hosting,   // this player's game is open
  Joining,   // knocking at a host's game
  Joined,    // in a host's game, waiting for the start
  Starting,  // the start is given: this game is about to start again
};

struct Game {
  uint64_t lobby = 0;
  std::string host;   // the host's name
  bool ok = false;    // can be joined
  std::string why;    // why not (Russian)
};

struct View {
  State state = State::Closed;
  bool steam = false;         // Steam (or the lab's stand-in) is up
  std::string steam_note;     // why it is not (Russian)
  int players = 0;            // in the open game, the host included
  int me = 0;                 // this player's place
  std::string names[4];
  std::vector<Game> games;    // friends' games
  std::string addresses;      // hosting: where an address player dials
  std::string error;          // the last thing that went wrong (Russian)
  bool playing = false;       // joined: the host's game is being played; this player comes in between two fights
};

void install(const Config& cfg);

void open();
void close();
void tick();  // once a game frame while the page is up

void host();
void join_game(uint64_t lobby);
void join_address(const std::string& address);
void leave();   // back to looking
void start();   // host: the start

const View& view();

// This game starts itself again into a session ("host" / "join"; over "steam", "enet" or "both"), and closes.
// The network page does it at the host's start; a running session does it to let another player in.
void start_again(const wchar_t* mode, const wchar_t* via, uint64_t peer, const std::string& join, int players);

}  // namespace netlobby
