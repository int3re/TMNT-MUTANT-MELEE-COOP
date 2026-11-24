#include "mod/nametag.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "common/log.h"
#include "mod/game.h"
#include "mod/gametext.h"
#include "mod/netplay.h"
#include "mod/patch.h"

namespace nametag {
namespace {

Config g_cfg;
const uint8_t* g_fighter = nullptr;  // the fighter whose tag the game is drawing right now

using Tag_t = void(__fastcall*)(void*, void*);
Tag_t oTag = nullptr;

// In place of the game's "draw this fighter's tag" (0x419FF0, ecx = the fighter).
void __fastcall hkTag(void* fighter, void* edx) {
  g_fighter = static_cast<const uint8_t*>(fighter);
  oTag(fighter, edx);
  g_fighter = nullptr;
}

}  // namespace

bool substitute(const wchar_t* text, const float* rect, std::wstring& name, float wide[4]) {
  if (!g_fighter || !text || !rect) return false;
  // (the tag's own words, "1P" .. "4P": the game draws them twice, the second time as a shadow - by then
  // the words are the name already)
  if (!(text[0] >= L'1' && text[0] <= L'4' && text[1] == L'P' && text[2] == 0)) return false;
  if (netplay::players() < 2) return false;  // (no session: the game's own tag)
  int pad = static_cast<int8_t>(g_fighter[game::kPlayerPad]);
  if (pad < 0 || pad >= netplay::players()) return false;
  std::string raw = netplay::name(pad);
  if (raw.empty() || raw == "player") {
    wchar_t numbered[32];
    swprintf(numbered, 32, gametext::russian() ? L"ИГРОК %d" : L"PLAYER %d", pad + 1);
    name = gametext::say(std::wstring(numbered));
  } else {
    name = gametext::say(raw);
    if (name.size() > 16) name.resize(16);
  }
  // The tag's rectangle is 0.2 of the screen wide, centred over the head: a name gets twice that.
  wide[0] = rect[0] - 0.1f;
  wide[1] = rect[1];
  wide[2] = rect[2] + 0.2f;
  wide[3] = rect[3];
  return true;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  static const uint8_t head[] = {0x83, 0xEC, 0x54, 0xA1, 0xE8, 0x6F, 0x5A, 0x00, 0x55};  // sub esp, 0x54; mov eax, [0x5A6FE8]; push ebp
  if (!patch::bytes_are(game::kDrawTag, head, sizeof(head))) {
    LOGW("tags: the game's fighter tag is not where it should be: no names over the fighters");
    return;
  }
  patch::hook("the fighters' tags (players' names)", game::kDrawTag, (void*)&hkTag, (void**)&oTag);
}

}  // namespace nametag
