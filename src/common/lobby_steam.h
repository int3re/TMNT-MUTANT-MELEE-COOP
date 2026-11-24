// Steam lobbies: how the two players find each other over the internet.
// The host opens a lobby (friends-only by default, 2 places) tagged
// meleenet=1 / proto / exe; the client finds it through friends who are
// "playing" with a lobby, through the lobby search, or by a SteamID64.
// Network thread only, like everything else Steam (steam_boot.h).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/sha256.h"

namespace lobby {

// --- host ----------------------------------------------------------------------
struct Friend {
  uint64_t id = 0;
  std::string name;
  bool playing_480 = false;  // running Spacewar / meleenet right now
};

class IHost {
 public:
  virtual ~IHost() = default;
  virtual void Tick(uint32_t now_ms) = 0;  // retries a failed lobby creation
  virtual bool IsMember(uint64_t steam_id) const = 0;
  virtual void SetInGame(bool has_p2) = 0;  // lobby data state=lobby|ingame
  virtual uint64_t LobbyId() const = 0;     // 0 until created
  // A member that left or dropped out of the lobby since the last call.
  virtual bool TakeLeft(uint64_t& steam_id) = 0;
  // Online friends, for the "invite" list (the Steam overlay's invite dialog
  // does not open in an old D3D9 game, and meleenet keeps the overlay off).
  virtual std::vector<Friend> OnlineFriends() = 0;
  virtual void Invite(uint64_t friend_id) = 0;  // a normal Steam lobby invite
};
// exe16: first 16 hex digits of ztmnt.exe's SHA-256. nullptr without Steam.
std::unique_ptr<IHost> make_host(const Config& cfg, const std::string& exe16);

// --- client --------------------------------------------------------------------
struct Found {
  uint64_t lobby = 0;
  uint64_t host_id = 0;     // the host's SteamID64 (lobby data hostid)
  std::string host;         // the host's Steam name
  std::string name;         // the host's meleenet name ([net] name)
  std::string state;        // "lobby" = waiting for players, "ingame" = playing
  bool is_friend = false;   // found through the friends list
  bool compatible = false;  // same protocol and ztmnt.exe
  std::string why_not;      // Russian, for the UI, when !compatible
  uint32_t seen_ms = 0;
};

struct JoinResult {
  bool ok = false;
  uint64_t lobby = 0;    // 0 = direct connection without a lobby
  uint64_t host_id = 0;  // whom to open the session with
  std::string host;      // the host's Steam name
  std::string error;     // Russian, for the UI
};

class IBrowser {
 public:
  virtual ~IBrowser() = default;
  // Refreshes the list every few seconds while searching; also handles
  // "Join game" from the Steam friends list (works only while running).
  virtual void Tick(uint32_t now_ms, bool searching) = 0;
  virtual std::vector<Found> List() const = 0;
  virtual void Join(uint64_t lobby) = 0;
  // host_steamid: its lobby if it is a friend playing; else a direct session
  // (the host must list us in allow_steamid).
  virtual void JoinHost(uint64_t steam_id) = 0;
  virtual void Leave() = 0;
  // A finished Join/JoinHost (or a friends-list join request), once.
  virtual bool TakeJoinResult(JoinResult& out) = 0;
};
// my_exe16: own ztmnt.exe hash prefix, "" if there is no ztmnt.exe here.
std::unique_ptr<IBrowser> make_browser(const std::string& my_exe16);

// --- invites while no session runs ----------------------------------------------
// An accepted Steam invite or "Join game" on a friend while the game plays alone: the lobby to
// join (the game then restarts into it). nullptr without real Steam.
class IInvites {
 public:
  virtual ~IInvites() = default;
  virtual bool Take(uint64_t& lobby) = 0;
};
std::unique_ptr<IInvites> make_invites();

// "" for an all-zero hash (no ztmnt.exe found).
inline std::string exe16(const uint8_t sha256[32]) {
  return is_zero(sha256, 32) ? std::string() : to_hex(sha256, 8);
}

}  // namespace lobby
