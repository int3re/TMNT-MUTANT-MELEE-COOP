#include "mod/gametext.h"

#include <windows.h>

#include <cstring>

#include "common/log.h"
#include "mod/game.h"

namespace gametext {
namespace {

bool g_russian = false;

// The language table is a vector of pointers to the texts (wide, zero-ended); string N is element N.
wchar_t** string_at(int id) {
  auto lang = *reinterpret_cast<uint8_t**>(game::kLanguage);
  if (!lang || id < 0) return nullptr;
  auto begin = *reinterpret_cast<wchar_t***>(lang + 8);
  auto end = *reinterpret_cast<wchar_t***>(lang + 12);
  if (!begin || begin + id >= end || !begin[id]) return nullptr;
  return begin + id;
}

// The mod's own strings: texts of the table that only another console shows (its memory-card messages).
// The mod writes over their letters in place, so only as many letters as the old text had.
struct Slot {
  int id = -1;
  size_t room = 0;
};
Slot g_slots[kSlots];

// Cyrillic capital (U+0410..U+042F) -> the Latin small letter or digit whose place its shape has.
const wchar_t kCipher[33] = L"a6bfdeg3nnkomh0zpctyvxq4wwjsj3ur";

}  // namespace

void init() {
  std::wstring story = game_string(game::kStrStory);
  g_russian = story.compare(0, 7, L"nct0pnr") == 0;
  LOGI("text: the game's texts are %s", g_russian ? "the Russian release's (Cyrillic shapes for small Latin letters)" : "Latin");
  // The longest of the other console's messages become the mod's.
  int n = 0;
  for (size_t need : {size_t(90), size_t(60), size_t(40)}) {
    for (int id = game::kStrSpareFirst; id <= game::kStrSpareLast && n < kSlots; ++id) {
      wchar_t** s = string_at(id);
      if (!s) continue;
      bool taken = false;
      for (int k = 0; k < n; ++k) taken = taken || g_slots[k].id == id;
      size_t len = wcslen(*s);
      if (taken || len < need) continue;
      g_slots[n].id = id;
      g_slots[n].room = len;
      ++n;
    }
  }
  LOGD("text: 390 '%ls' 391 '%ls' 392 '%ls' 6 '%ls'; places %d %d %d", game_string(390).c_str(), game_string(391).c_str(),
       game_string(392).c_str(), game_string(6).c_str(), g_slots[0].id, g_slots[1].id, g_slots[6].id);
  if (n < kSlots) LOGE("text: only %d of %d places for the mod's own texts", n, kSlots);
  for (int k = 0; k < n; ++k) set_slot(k, L"");
}

bool russian() { return g_russian; }

std::wstring game_string(int id) {
  wchar_t** s = string_at(id);
  std::wstring t = s ? std::wstring(*s) : std::wstring();
  while (!t.empty() && (t.back() == 13 || t.back() == 10)) t.pop_back();  // (the table keeps the line's end)
  return t;
}

int slot_id(int slot) { return slot >= 0 && slot < kSlots ? g_slots[slot].id : -1; }

void set_slot(int slot, const std::wstring& text) {
  if (slot < 0 || slot >= kSlots || g_slots[slot].id < 0) return;
  wchar_t** s = string_at(g_slots[slot].id);
  if (!s) return;
  size_t n = text.size() < g_slots[slot].room ? text.size() : g_slots[slot].room;
  if (wcsncmp(*s, text.c_str(), n) == 0 && (*s)[n] == 0) return;
  wmemcpy(*s, text.c_str(), n);
  (*s)[n] = 0;
}

std::wstring say(const std::wstring& text) {
  std::wstring out;
  for (wchar_t c : text) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    if (c >= 0x0430 && c <= 0x044F) c = static_cast<wchar_t>(c - 0x20);  // small Cyrillic -> capital
    if (c == 0x0451 || c == 0x0401) c = 0x0415;                           // Ё -> Е
    if (c >= 0x0410 && c <= 0x042F) {
      out += g_russian ? kCipher[c - 0x0410] : L'?';
    } else if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || wcschr(L" !?'+./:;-()\n,", c)) {
      out += c;
    } else if (c == L'_') {
      out += L'-';
    } else {
      out += L'?';
    }
  }
  return out;
}

std::wstring say(const std::string& utf8) {
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &w[0], n);
  return say(w);
}

std::wstring T(const wchar_t* ru, const wchar_t* en) { return say(std::wstring(g_russian ? ru : en)); }

}  // namespace gametext
