#include "mod/digits.h"

#include <windows.h>

#include <cstring>

#include "common/log.h"
#include "mod/game.h"
#include "mod/gametext.h"
#include "mod/glyph4.h"
#include "mod/patch.h"

namespace digits {
namespace {

// Codes for the new glyphs: control codes, met in no text of the game.
constexpr wchar_t kFour = 0x14, kSix = 0x16;
// Where the 4 is painted: the free cell after the 5 in the font's picture (512 x 256).
constexpr int kCellX = 470, kCellY = 149;

bool g_ready = false;

// --- the glyphs ---------------------------------------------------------------------------------------
// The font is RenderWare's Rt2d bitmap font: a map from a code to a glyph record at +0x18 (128 shorts),
// the number of records at +0x124, the records' list at +0x128 ({data, filled, allocated, entry size},
// 0x20 bytes each: advance, raster, u0, v0, u1, v1, path, page letter).
struct Glyph {
  float advance;
  uint8_t* raster;
  float u0, v0, u1, v1;
  void* path;
  int page;
};

using Lock_t = uint8_t*(__cdecl*)(void*, int, int);
using Unlock_t = void*(__cdecl*)(void*);

bool paint_four(uint8_t* raster) {
  int width = *reinterpret_cast<int*>(raster + 0x0C), height = *reinterpret_cast<int*>(raster + 0x10);
  int format = raster[0x23] & 0x0F;  // 5 = 8888, 3 = 4444, 1 = 1555
  if (width < kCellX + glyph4::kWidth + 2 || height < kCellY + glyph4::kHeight + 2 || (format != 5 && format != 3 && format != 1)) {
    LOGW("text: the font's picture is %dx%d in format %d: no place for a 4", width, height, format);
    return false;
  }
  uint8_t* px = reinterpret_cast<Lock_t>(game::kRasterLock)(raster, 0, 3);
  if (!px) {
    LOGW("text: the font's picture cannot be written");
    return false;
  }
  int stride = *reinterpret_cast<int*>(raster + 0x18);
  int bytes = format == 5 ? 4 : 2;
  // (the cell must be empty: another font picture would have something there)
  bool empty = true;
  for (int y = -1; y <= glyph4::kHeight && empty; ++y)
    for (int x = -2; x < glyph4::kWidth + 2 && empty; ++x) {
      const uint8_t* p = px + (kCellY + y) * stride + (kCellX + x) * bytes;
      int alpha = format == 5 ? p[3] : format == 3 ? p[1] >> 4 : p[1] >> 7;
      empty = alpha == 0;
    }
  if (empty) {
    for (int y = 0; y < glyph4::kHeight; ++y)
      for (int x = 0; x < glyph4::kWidth; ++x) {
        int body = glyph4::kBody[y * glyph4::kWidth + x], alpha = glyph4::kAlpha[y * glyph4::kWidth + x];
        uint8_t* p = px + (kCellY + y) * stride + (kCellX + x) * bytes;
        if (format == 5) {
          p[0] = p[1] = p[2] = static_cast<uint8_t>(body);
          p[3] = static_cast<uint8_t>(alpha);
        } else if (format == 3) {
          uint16_t v = static_cast<uint16_t>((alpha >> 4) << 12 | (body >> 4) << 8 | (body >> 4) << 4 | (body >> 4));
          memcpy(p, &v, 2);
        } else {
          uint16_t v = static_cast<uint16_t>((alpha >= 128 ? 0x8000 : 0) | (body >> 3) << 10 | (body >> 3) << 5 | (body >> 3));
          memcpy(p, &v, 2);
        }
      }
  }
  reinterpret_cast<Unlock_t>(game::kRasterUnlock)(raster);
  if (!empty) LOGW("text: the cell of the font's picture where the 4 would go is not empty");
  return empty;
}

// --- the texts -----------------------------------------------------------------------------------------
// Words of the Russian texts that are made of digit places only (ОБ, ОБО, БОБ, ЗОБ, ОБОЗ).
bool cipher_word(const wchar_t* b, const wchar_t* e) {
  static const wchar_t* const words[] = {L"06", L"060", L"606", L"306", L"0603"};
  wchar_t core[8];
  int n = 0;
  for (const wchar_t* p = b; p < e; ++p) {
    if (*p >= L'0' && *p <= L'9') {
      if (n >= 7) return false;
      core[n++] = *p;
    } else if (*p >= L'A' && *p <= L'Z') {
      return false;
    }
  }
  core[n] = 0;
  for (const wchar_t* w : words)
    if (wcscmp(core, w) == 0) return true;
  return false;
}

bool space(wchar_t c) { return c == L' ' || c == L'\n' || c == L'\r' || c == L'\t'; }

// The word before is ЧТО or ПЛАНА: a lone 6 after it is the letter ("что б", "плана Б").
bool letter_before(const wchar_t* b, const wchar_t* e) {
  while (b < e && (*b == L'"' || *b == L'(')) ++b;
  size_t n = static_cast<size_t>(e - b);
  return (n == 3 && wcsncmp(b, L"4t0", 3) == 0) || (n == 5 && wcsncmp(b, L"zoaha", 5) == 0);
}

// In place: the 4s and 6s that are digits get the new glyphs' codes. A word is a number when it has no
// small Latin letter (those are the Cyrillic letters).
void rewrite(wchar_t* text) {
  const wchar_t *prev_b = nullptr, *prev_e = nullptr;
  for (wchar_t* p = text; *p;) {
    while (space(*p)) ++p;
    wchar_t* b = p;
    bool has = false, letters = false, sure = false;
    int digits = 0;
    for (; *p && !space(*p); ++p) {
      wchar_t c = *p;
      if (c == L'4' || c == L'6') has = true;
      if (c >= L'a' && c <= L'z') letters = true;
      if (c >= L'0' && c <= L'9') ++digits;
      if (c == L'1' || c == L'2' || c == L'5' || c == L'7' || c == L'8' || c == L'9' || c == L':' || c == L'%' ||
          (c >= L'A' && c <= L'Z'))
        sure = true;
    }
    bool number = has && !letters;
    if (number && !sure) {
      if (digits == 1)
        number = !(prev_b && letter_before(prev_b, prev_e));
      else
        number = !cipher_word(b, p);
    }
    if (number)
      for (wchar_t* q = b; q < p; ++q) *q = *q == L'4' ? kFour : *q == L'6' ? kSix : *q;
    if (p > b) prev_b = b, prev_e = p;
  }
}

using DrawText_t = void(__cdecl*)(void*, const wchar_t*, float, float*, int, void*, int, int);
DrawText_t oDrawText = nullptr;

void __cdecl hkDrawText(void* font, const wchar_t* text, float size, float* rect, int a, void* brush, int shadow, int b) {
  if (!g_ready || !text || font != *reinterpret_cast<void**>(game::kFont)) {
    oDrawText(font, text, size, rect, a, brush, shadow, b);
    return;
  }
  // (the game itself draws at most 0x7FF letters of a text; for a text's shadow it calls itself with the
  // text already rewritten, which a second pass leaves as it is)
  wchar_t copy[0x800];
  wcsncpy(copy, text, 0x7FF);
  copy[0x7FF] = 0;
  rewrite(copy);
  oDrawText(font, copy, size, rect, a, brush, shadow, b);
}

}  // namespace

bool ready() { return g_ready; }

void on_fonts_loaded() {
  if (g_ready || !oDrawText || !gametext::russian()) return;
  auto font = *reinterpret_cast<uint8_t**>(game::kFont);
  if (!font || *reinterpret_cast<int*>(font) != 0 || (font[0x10] & 2)) {
    LOGW("text: the menu font is not the bitmap font expected: its 4 and 6 stay as they are");
    return;
  }
  auto map = reinterpret_cast<int16_t*>(font + 0x18);
  auto list = *reinterpret_cast<int**>(font + 0x128);
  int n = *reinterpret_cast<int*>(font + 0x124);
  if (!list || !list[0] || list[3] != 0x20 || n < 10 || n + 2 > list[1] || map[kFour] != -1 || map[kSix] != -1 || map[L'9'] < 0 ||
      map[L'9'] >= n) {
    LOGW("text: the menu font's glyph list is not as expected (%d glyphs): its 4 and 6 stay as they are", n);
    return;
  }
  auto glyphs = reinterpret_cast<Glyph*>(list[0]);
  const Glyph nine = glyphs[map[L'9']];
  if (!nine.raster) return;
  float height = static_cast<float>(*reinterpret_cast<int*>(nine.raster + 0x10));
  float width = static_cast<float>(*reinterpret_cast<int*>(nine.raster + 0x0C));
  // 6: the 9 turned half a turn (the window three rows lower: the digit's ink sits on the others' line).
  Glyph six = nine;
  six.u0 = nine.u1, six.u1 = nine.u0;
  six.v0 = nine.v1 + 3.0f / height, six.v1 = nine.v0 + 3.0f / height;
  glyphs[n] = six;
  map[kSix] = static_cast<int16_t>(n);
  // 4: painted.
  if (paint_four(nine.raster)) {
    Glyph four = nine;
    four.advance = static_cast<float>(glyph4::kWidth) / *reinterpret_cast<float*>(font + 4);
    four.u0 = kCellX / width, four.u1 = (kCellX + glyph4::kWidth) / width;
    four.v0 = kCellY / height, four.v1 = (kCellY + glyph4::kHeight) / height;
    glyphs[n + 1] = four;
    map[kFour] = static_cast<int16_t>(n + 1);
  } else {
    map[kFour] = map[L'4'];  // (the font's own glyph, as before)
  }
  g_ready = true;
  LOGI("text: the menu font has a real 4 and 6 now (glyphs %d and %d of %d)", n + 1, n, list[1]);
}

void install(const Config&) {
  static const uint8_t head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0xB8, 0x34, 0x18, 0x00, 0x00};  // push ebp; mov ebp, esp; and esp, -8; mov eax, 0x1834
  if (!patch::bytes_are(game::kDrawText, head, sizeof(head))) {
    LOGW("text: the game's text drawing is not where it should be: the font's 4 and 6 stay as they are");
    return;
  }
  patch::hook("the game's text drawing (a real 4 and 6)", game::kDrawText, (void*)&hkDrawText, (void**)&oDrawText);
}

}  // namespace digits
