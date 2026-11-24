// Steam start-up for the Steam transport, as "online-fix" builds do it: the
// test AppID 480 (Spacewar), steam_api.dll loaded on demand (delay-load), no
// SteamAPI_RestartAppIfNecessary, overlay drawing off.
//
// [net] transport=mock swaps real Steam for the lab mock (steam_mock.cpp):
// fake SteamIDs, localhost UDP and lobbies as files, so the Steam code paths
// can be tested with two players on one PC.
//
// Threading: every function here, and every other Steam call in meleenet, runs
// on the network thread that called boot(). Nothing touches the game thread.
#pragma once
#include <cstdint>
#include <string>

#include "common/config.h"

namespace steam {

constexpr uint32_t kAppId = 480;  // Spacewar: Valve's public test app

// Loads steam_api.dll and initialises Steam (or the mock). Returns false, with
// one clear log line, when Steam cannot be used (no DLL, Steam not running,
// ...): the caller stays on ENet. Safe without the SDK in the build.
bool boot(const Config& cfg);
bool ready();
bool is_mock();   // the lab mock is in use
void pump();      // SteamAPI_RunCallbacks; call ~100 times a second
void shutdown();  // SteamAPI_Shutdown

// Why boot() failed, in Russian for the UI ("" when ready).
std::string unavailable_reason();

uint64_t my_id();         // own SteamID64
std::string my_name();    // own persona name (UTF-8)

// Lobby ids are chat-type SteamIDs; 0 if s holds none.
uint64_t parse_lobby_id(const std::string& s);

}  // namespace steam
