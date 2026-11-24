// Words of the mod in the game's own letters.
//
// The game draws text with one font of capital Latin letters, digits and a little punctuation; small
// letters are drawn as capitals. The Russian release keeps that font file's layout and paints Cyrillic
// shapes in the places of the SMALL Latin letters and of some digits: its texts are written in a
// cipher ("nct0pnr" = ИСТОРИЯ), capitals stay Latin. The mod writes its own texts the same way.
#pragma once
#include <string>

namespace gametext {

// After the game's language table is loaded.
void init();

// True in the Russian release (the cipher is in use).
bool russian();

// T(ru, en): the text for this release, ready for the game's font.
std::wstring T(const wchar_t* ru, const wchar_t* en);

// Any text (a player's name, an address) in the game's font: Cyrillic by the cipher where the release
// has it, Latin as capitals, what the font cannot show as '?'.
std::wstring say(const std::wstring& text);
std::wstring say(const std::string& utf8);

// The game's own string number `id` ("" when there is none).
std::wstring game_string(int id);
// The mod's own texts live in places of the game's language table that only another console uses.
// Place `slot` (0..kSlots-1) is string number slot_id(slot) for the interface's scripts; -1 if there is none.
constexpr int kSlots = 8;
int slot_id(int slot);
void set_slot(int slot, const std::wstring& text);

}  // namespace gametext
