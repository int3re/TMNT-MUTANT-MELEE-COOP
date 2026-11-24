// meleenet wire protocol. Little endian, packed. Bump kVersion on ANY change to a
// struct layout, channel use or field meaning: peers with different versions
// refuse each other.
#pragma once
#include <cstdint>
#include <cstring>

namespace mmp {

constexpr uint32_t kMagic = 0x454C454D;  // bytes 'M','E','L','E'
// 1 (meleenet 0.1.0): the first network game.
// 2 (meleenet 0.2.0): every player's Esc pauses a fight; pictures and particles take no random numbers of the
//    rules; the story is played together; a player who has left is marked in the pads; a game that is being
//    played lets players in (Knock / Lobby / Go between the games of a running session).
constexpr uint16_t kVersion = 2;
constexpr uint16_t kDefaultPort = 47830;

constexpr uint8_t kChannelControl = 0;  // reliable, ordered
constexpr uint8_t kChannelInput = 1;    // unreliable: inputs, hashes
constexpr uint8_t kChannelCount = 2;

constexpr int kPlayers = 4;     // the game's four pads
constexpr int kInputMax = 90;   // frames in one Input / Bundle packet at most
constexpr int kSaveMax = 256 * 1024;

enum class Type : uint8_t {
  Hello = 1,     // client -> host: who I am
  Welcome = 2,   // host -> client: the session's settings
  SaveFile = 3,  // host -> client: the host's save (the session plays on it)
  Start = 4,     // host -> client: everything is sent; play from the first frame
  Input = 5,     // client -> host: the sender's pad for its last frames
  Bundle = 6,    // host -> client: every player's pad for the last frames
  Hash = 7,      // client -> host: a fingerprint of the game's state at a frame
  Bye = 8,       // both ways: leaving
  Reject = 9,    // host -> client: not let in, and why
  Alive = 10,    // both ways: "this game is busy (loading a level) but here"; a Header alone
  Desync = 11,   // host -> clients: the games have parted (player, frame)
  // The network page (the games are not in a session yet):
  Knock = 12,    // guest -> host: let me into your game (a Hello by layout)
  Lobby = 13,    // host -> guest: you are in; who else is; wait for the start
  Go = 14,       // host -> guest: start your game again into the session
  GoAck = 15,    // guest -> host: starting again (a Header alone)
};

#pragma pack(push, 1)

struct Header {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint8_t type = 0;
  uint8_t reserved = 0;
};

struct Hello {
  Header h;
  uint8_t exe_hash[8];   // first bytes of the SHA-256 of ztmnt.exe
  uint8_t data_hash[8];  // first bytes of the SHA-256 of archive.arc (the index of the game's data)
  char mod_version[16];
  char name[32];
};

struct Welcome {
  Header h;
  uint8_t player;        // the client's player number (1..3)
  uint8_t players;       // players in the session
  uint8_t input_delay;   // frames between reading a pad and the frame that uses it
  uint8_t has_save;      // a SaveFile packet follows (0: the host has no save; the client plays without one too)
  uint32_t first_frame;  // the game frame the session starts at
  char names[kPlayers][32];
};

struct SaveFile {
  Header h;
  uint32_t size;
  // `size` bytes follow
};

struct Start {
  Header h;
};

// A player's pad for one frame (input::Pad on the wire).
struct PadWire {
  int8_t x, y;
  uint16_t buttons;
};

struct Input {
  Header h;
  uint8_t player;
  uint8_t count;    // pads that follow, for frames newest - count + 1 .. newest
  uint32_t newest;
  uint32_t ack;     // the newest frame of the host's bundles the sender has
  // PadWire pads[count]
};

struct Bundle {
  Header h;
  uint8_t players;  // pads per frame
  uint8_t count;    // frames that follow, newest - count + 1 .. newest
  uint8_t gone;     // bit n: player n has left (its pads are empty from now on)
  uint8_t reserved;
  uint32_t newest;
  uint32_t ack;     // the newest frame of the receiver's own pads the host has
  // PadWire pads[count][players]
};

struct Hash {
  Header h;
  uint32_t frame;
  uint32_t hash;
};

struct Desync {
  Header h;
  uint8_t player;
  uint8_t reserved[3];
  uint32_t frame;
};

// The host's answer to a Knock, and again whenever the lobby changes.
struct Lobby {
  Header h;
  uint8_t players;   // in the lobby, the host included
  uint8_t you;       // the receiver's place
  uint8_t playing;   // 1: the host's game is being played; the receiver comes in between two fights
  uint8_t reserved;
  char names[kPlayers][32];
};

// The host's start: how the guest's new game finds the host's.
struct Go {
  Header h;
  uint8_t via;       // 1 = by address (ENet), 2 = Steam
  uint8_t players;   // players the session will have
  uint16_t port;     // by address: the host's port
  uint64_t steam_id; // Steam: the host's SteamID64
};

struct Reject {
  Header h;
  char reason[96];
};

#pragma pack(pop)

template <typename T>
T make(Type t) {
  T p{};
  p.h.magic = kMagic;
  p.h.version = kVersion;
  p.h.type = static_cast<uint8_t>(t);
  return p;
}

// The packet's type if it is one of ours (right magic and version), else 0.
inline uint8_t type_of(const void* data, size_t size) {
  if (size < sizeof(Header)) return 0;
  Header h;
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic || h.version != kVersion) return 0;
  return h.type;
}

}  // namespace mmp
