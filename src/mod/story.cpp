#include "mod/story.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "common/log.h"
#include "mod/game.h"
#include "mod/netplay.h"
#include "mod/patch.h"

namespace story {
namespace {

// A match of the story (0x31EC bytes): the hero's record, eight records of enemies, eight of friends.
constexpr int kRecordSize = 0x2E8;
constexpr int kEnemyCount = 0x2E8, kEnemies = 0x2EC, kFriendCount = 0x1A2C, kFriends = 0x1A30;
constexpr int kType = 0x3170, kTime = 0x3174, kHasFriend = 0x3190;
// A fighter's record.
constexpr int kRecHealth = 0x10, kRecTeam = 0x20, kRecSlot = 0x28, kRecNextLife = 0x30, kRecRules = 0x2B4, kRecHud = 0x2D8;
// A record's rule block: "end:" flags from +0x17 (time, fall, teamfall, ko, teamko, lastman, ...).
constexpr int kEndKo = 0x17 + 3, kEndTeamKo = 0x17 + 4;
// Game types.
constexpr int kLastman = 2, kTeamLastman = 4, kSurvival = 6, kBomb = 9, kBalloon = 10, kMecha = 11, kHill = 12, kAway = 13;
constexpr int kSlots = 8;

// Each player of a session has a turtle of their own (Leonardo, Raphael, Donatello, Michelangelo by the
// player's number); a partner whose turtle is the story's hero takes the first one nobody has.
constexpr int kLeo = 0, kDon = 1, kMike = 2, kRaph = 3;
const int kHome[game::kPlayers] = {kLeo, kRaph, kDon, kMike};

// What the mod added to the match being played, to take it out before the match is built again.
struct Added {
  uint8_t* match = nullptr;
  int count = 0;
  int friends_before = 0, type_before = 0;
  uint8_t has_friend_before = 0, hero_end_ko = 0;
  int team_ko_enemy = -1;  // the enemy record that got "end: teamko"
  int pads[game::kPlayers - 1] = {};
} g_added;

using Build_t = void(__fastcall*)(void*, void*);
using RecordInit_t = void*(__fastcall*)(void*);
Build_t oBuild = nullptr;

int& i32(uint8_t* p, int off) { return *reinterpret_cast<int*>(p + off); }

const char* character_name(int id) { return reinterpret_cast<const char*(__cdecl*)(int)>(game::kCharacterName)(id); }

void undo(uint8_t* match) {
  Added was = g_added;
  g_added = Added{};
  // (the matches are made anew whenever the story's script is read: only what is still there is undone)
  if (!was.count || was.match != match || i32(match, kFriendCount) != was.friends_before + was.count) return;
  i32(match, kFriendCount) = was.friends_before;
  i32(match, kType) = was.type_before;
  match[kHasFriend] = was.has_friend_before;
  match[kRecRules + kEndKo] = was.hero_end_ko;
  if (was.team_ko_enemy >= 0) match[kEnemies + kRecordSize * was.team_ko_enemy + kRecRules + kEndTeamKo] = 0;
}

// The players of the session who fight beside the hero in this match, as friend records at the end of the
// match's list (the story reads "the hero's side has won" from the first two slots: partners come last).
void add_partners(uint8_t* match) {
  int lead = *reinterpret_cast<const int*>(game::kPrimaryPad);
  int pads[game::kPlayers - 1];
  int n = netplay::partners(lead, pads);
  if (n <= 0) return;
  int type = i32(match, kType);
  if (type == kBomb || type == kBalloon || type == kMecha || type == kHill || type == kAway) {
    LOGI("story: a match of type %d is for the hero alone", type);
    return;
  }
  int enemies = i32(match, kEnemyCount), friends = i32(match, kFriendCount);
  if (enemies < 0 || enemies > 8 || friends < 0 || friends > 8) return;
  int fighters = 1 + friends;
  for (int e = 0; e < enemies; ++e) {
    if (i32(match, kEnemies + kRecordSize * e + kRecNextLife) != -1) {
      // (an enemy that comes back as another one: the game counts its records by their place, and a record
      // more would shift them)
      LOGI("story: this match has an enemy with a second life: it is for the hero alone");
      return;
    }
    ++fighters;
  }
  if (n > kSlots - fighters) n = kSlots - fighters;
  if (n <= 0) {
    LOGI("story: this match has no place for partners (%d fighters)", fighters);
    return;
  }

  g_added.match = match;
  g_added.friends_before = friends;
  g_added.type_before = type;
  g_added.has_friend_before = match[kHasFriend];
  g_added.hero_end_ko = match[kRecRules + kEndKo];

  // HUD panels (four): the hero's and the scripted friends' stay theirs; a partner takes a free one, or an enemy's.
  bool taken[4] = {}, enemy_has[4] = {};
  auto mark = [](bool* set, int hud) {
    if (hud >= 0 && hud < 4) set[hud] = true;
  };
  mark(taken, i32(match, kRecHud));
  for (int f = 0; f < friends; ++f) mark(taken, i32(match, kFriends + kRecordSize * f + kRecHud));
  for (int e = 0; e < enemies; ++e) mark(enemy_has, i32(match, kEnemies + kRecordSize * e + kRecHud));

  int hero = *reinterpret_cast<const int*>(game::kStoryCharacters);
  bool used[0x20] = {};
  if (hero >= 0 && hero < 0x20) used[hero] = true;
  for (int k = 0; k < n; ++k) {
    int player = pads[k];
    int who = kHome[player];
    for (int t : {kHome[player], kLeo, kRaph, kDon, kMike})
      if (!used[t]) {
        who = t;
        break;
      }
    used[who] = true;
    uint8_t* rec = match + kFriends + kRecordSize * (friends + k);
    reinterpret_cast<RecordInit_t>(game::kRecordInit)(rec);
    strncpy(reinterpret_cast<char*>(rec), character_name(who), 15);
    rec[15] = 0;
    i32(rec, kRecHealth) = i32(match, kRecHealth);
    i32(rec, kRecTeam) = 0;
    int hud = -1;
    for (int h = 0; h < 4 && hud < 0; ++h)
      if (!taken[h] && !enemy_has[h]) hud = h;
    for (int h = 3; h >= 0 && hud < 0; --h)
      if (!taken[h]) hud = h;
    if (hud >= 0) taken[hud] = true;
    i32(rec, kRecHud) = hud;
    g_added.pads[k] = player;
    LOGI("story: player %d fights beside the hero as %s (HUD panel %d)", player + 1, character_name(who), hud);
  }
  g_added.count = n;
  i32(match, kFriendCount) = friends + n;
  match[kHasFriend] = 1;
  if (type == kLastman) {
    // "The last one standing" never comes with two humans on one side: the match is played by the game's
    // team rules (a side wins when the other is down), and the hero's fall alone does not end it.
    i32(match, kType) = kTeamLastman;
    match[kRecRules + kEndKo] = 0;
  } else if (type == kSurvival && *reinterpret_cast<const float*>(match + kTime) < 0.0f && enemies > 0) {
    // (a survival match without a clock ends when everybody else is down: here, when the enemies' side is)
    if (!match[kEnemies + kRecRules + kEndTeamKo]) {
      match[kEnemies + kRecRules + kEndTeamKo] = 1;
      g_added.team_ko_enemy = 0;
    }
  }
}

// In place of the game's "build the match's fighters" (0x455F00, ecx = the match).
void __fastcall hkBuild(void* self, void* edx) {
  auto match = static_cast<uint8_t*>(self);
  undo(match);
  if (netplay::active()) add_partners(match);
  oBuild(self, edx);
  if (!g_added.count) return;
  auto players = reinterpret_cast<uint8_t**>(game::kPlayerObjects);
  for (int k = 0; k < g_added.count; ++k) {
    const uint8_t* rec = match + kFriends + kRecordSize * (g_added.friends_before + k);
    int slot = *reinterpret_cast<const int*>(rec + kRecSlot);
    if (slot <= 0 || slot >= kSlots || !players[slot]) {
      LOGE("story: a partner's record has no fighter (slot %d)", slot);
      continue;
    }
    i32(players[slot], game::kPlayerAi) = -1;                                   // a human
    players[slot][game::kPlayerPad] = static_cast<uint8_t>(g_added.pads[k]);    // this player's pad
  }
  LOGI("story: the match is built for %d players (type %d, %d fighters)", g_added.count + 1, i32(match, kType),
       *reinterpret_cast<const int*>(game::kPlayerCount));
}

}  // namespace

Config g_cfg;

// Lab: at given game frames the enemies', the hero's or the partners' health is taken away.
void lab_frame(unsigned frame) {
  if (g_cfg.lab_story_open) lab_open_stages();
  auto players = reinterpret_cast<uint8_t**>(game::kPlayerObjects);
  int count = *reinterpret_cast<const int*>(game::kPlayerCount);
  for (int s = 0; s < count && s < kSlots; ++s) {
    if (!players[s]) continue;
    int team = i32(players[s], game::kPlayerTeam);
    bool human = i32(players[s], game::kPlayerAi) == -1;
    bool hit = (g_cfg.lab_ko_enemies && frame == g_cfg.lab_ko_enemies && team != 0) ||
               (g_cfg.lab_ko_hero && frame == g_cfg.lab_ko_hero && s == 0) ||
               (g_cfg.lab_ko_partners && frame == g_cfg.lab_ko_partners && s != 0 && team == 0 && human);
    if (!hit) continue;
    i32(players[s], game::kPlayerHealth) = 0;
    LOGI("story: [lab] slot %d (team %d) has no health left", s, team);
  }
}

void lab_open_stages() {
  // The story's object (0x673E30): the stages of the hero's script as a vector of pointers; a stage's state
  // at +0x194 (0 closed, 1 open, 2 passed).
  auto begin = *reinterpret_cast<uint8_t***>(game::kStory + 0x28);
  auto end = *reinterpret_cast<uint8_t***>(game::kStory + 0x2C);
  if (!begin || end < begin || end - begin > 64) return;
  for (auto s = begin; s < end; ++s)
    if (*s && *reinterpret_cast<int*>(*s + 0x194) == 0) *reinterpret_cast<int*>(*s + 0x194) = 1;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  static const uint8_t build_head[] = {0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x57, 0x8B, 0xF9};  // sub esp, 8; push ...; mov edi, ecx
  static const uint8_t init_head[] = {0x56, 0x8B, 0xF1, 0x57, 0x8D, 0x8E, 0xB4, 0x02, 0x00, 0x00};  // push esi; mov esi, ecx; ...
  if (!patch::bytes_are(game::kBuildMatch, build_head, sizeof(build_head)) ||
      !patch::bytes_are(game::kRecordInit, init_head, sizeof(init_head))) {
    LOGE("story: the game's match builder is not where it should be: the story stays a game for one");
    return;
  }
  patch::hook("the story's match (partners)", game::kBuildMatch, (void*)&hkBuild, (void**)&oBuild);
}

}  // namespace story
