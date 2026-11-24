#include "mod/uiwide.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace uiwide {
namespace {

constexpr float kWidth = 640.0f;
constexpr float kNear = 0.5f;

struct Line {
  std::string text;  // with its line end
  std::string key;   // lower case, "" for a line that is not "key = value"
  size_t value_at = 0;  // where the value begins in `text`
};

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::vector<Line> lines_of(const std::string& body) {
  std::vector<Line> out;
  size_t at = 0;
  while (at < body.size()) {
    size_t end = body.find('\n', at);
    end = end == std::string::npos ? body.size() : end + 1;
    Line l;
    l.text = body.substr(at, end - at);
    size_t first = l.text.find_first_not_of(" \t");
    size_t eq = l.text.find('=');
    if (first != std::string::npos && eq != std::string::npos && l.text.compare(first, 2, "//") != 0) {
      std::string key = l.text.substr(first, eq - first);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      l.key = lower(key);
      l.value_at = eq + 1;
    }
    out.push_back(l);
    at = end;
  }
  return out;
}

bool four(const Line& l, float v[4]) {
  const char* p = l.text.c_str() + l.value_at;
  for (int i = 0; i < 4; ++i) {
    char* end = nullptr;
    v[i] = strtof(p, &end);
    if (end == p) return false;
    p = end;
    while (*p == ' ' || *p == '\t' || *p == ',') ++p;
  }
  return true;
}

std::string line_end(const std::string& text) {
  size_t n = text.find_last_not_of("\r\n");
  return n == std::string::npos ? text : text.substr(n + 1);
}

// A screen rectangle: the game reads these as fractions.
void put_four(Line& l, const float v[4]) {
  char buf[128];
  snprintf(buf, sizeof(buf), " %.2f, %.2f, %.2f, %.2f", v[0], v[1], v[2], v[3]);
  l.text = l.text.substr(0, l.value_at) + buf + line_end(l.text);
}

// A texture rectangle: the game reads these as whole numbers (atol).
void put_four_whole(Line& l, const float v[4]) {
  char buf[128];
  snprintf(buf, sizeof(buf), " %ld, %ld, %ld, %ld", lroundf(v[0]), lroundf(v[1]), lroundf(v[2]), lroundf(v[3]));
  l.text = l.text.substr(0, l.value_at) + buf + line_end(l.text);
}

Line* find(std::vector<Line>& ls, const char* key) {
  for (Line& l : ls)
    if (l.key == key) return &l;
  return nullptr;
}

std::string value_of(const Line& l) {
  std::string v = l.text.substr(l.value_at);
  size_t a = v.find_first_not_of(" \t");
  size_t b = v.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

bool one_of(const std::string& name, std::initializer_list<const char*> names) {
  for (const char* n : names)
    if (name == n) return true;
  return false;
}

// How the part of the picture beside an element that ends at the layout's edge is filled.
enum class Fill {
  None,     // left as it is
  Tile,     // the texture goes on, repeating (the element repeats its texture already)
  Stretch,  // the element is a few texels pulled wide: it is pulled wider
  Mirror,   // a second piece with the texture turned around at the edge (a wall, a picture)
  Smear,    // a second piece that repeats the texture's column at the edge (a plate, speed lines)
};

Fill fill_of(const std::string& asset, bool wrap, float w, float uw, bool has_uv) {
  if (wrap) return has_uv ? Fill::Tile : Fill::Stretch;
  if (!has_uv) return Fill::None;
  if (uw >= 0 && uw * 4 <= w) return Fill::Stretch;
  // The brick walls along the bottom (one ends in a transparent piece, the other is a ramp: neither can
  // be repeated) and the picture of the loading screen.
  if (one_of(asset, {"brick", "brick02", "control"})) return Fill::Mirror;
  // Plates of headings and the speed lines behind a hero: the same along their whole length.
  if (one_of(asset, {"sele_wdw", "pol_chara_bg", "stage_name_w", "window01"})) return Fill::Smear;
  return Fill::None;
}

// How far the left / right side of a rectangle that ends at (or just beyond) the layout's edge is from
// the picture's edge. 0: the rectangle does not reach the edge, or it reaches the picture's edge already.
float gap_left(const float r[4], float extra) {
  if (r[2] <= 0 || r[0] > kNear || r[0] + r[2] <= kNear) return 0;
  float d = r[0] + extra;
  return d > kNear ? d : 0;
}

float gap_right(const float r[4], float extra) {
  float right = r[0] + r[2];
  if (r[2] <= 0 || right < kWidth - kNear || r[0] >= kWidth - kNear) return 0;
  float d = kWidth + extra - right;
  return d > kNear ? d : 0;
}

// A rectangle that waits wholly beyond an edge of the layout (to slide in, or after sliding out) waits
// beyond the picture's edge.
bool move_out(float r[4], float extra) {
  if (r[2] <= 0) return false;
  if (r[0] + r[2] <= kNear) {
    r[0] -= extra;
    return true;
  }
  if (r[0] >= kWidth - kNear) {
    r[0] += extra;
    return true;
  }
  return false;
}

struct Pieces {
  // Second pieces of the element: {screen rectangle, texture rectangle}.
  std::vector<std::pair<std::string, std::string>> list;
};

std::string four_text(const float v[4], bool whole) {
  char buf[128];
  if (whole)
    snprintf(buf, sizeof(buf), "%ld, %ld, %ld, %ld", lroundf(v[0]), lroundf(v[1]), lroundf(v[2]), lroundf(v[3]));
  else
    snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f, %.2f", v[0], v[1], v[2], v[3]);
  return buf;
}

// One rectangle of an element (its start or its end) with the texture's rectangle that goes with it.
// `pieces`: where second pieces go, or null if the element cannot have them (it moves).
void widen_rect(std::vector<Line>& ls, const char* xywh, const char* uvwh, const std::string& asset, bool wrap,
                float extra, Pieces* pieces) {
  Line* r_line = find(ls, xywh);
  float r[4];
  if (!r_line || !four(*r_line, r) || r[2] <= 0) return;
  if (move_out(r, extra)) {
    put_four(*r_line, r);
    return;
  }
  float left = gap_left(r, extra);
  float right = gap_right(r, extra);
  if (left <= 0 && right <= 0) return;

  Line* t_line = uvwh ? find(ls, uvwh) : nullptr;
  float t[4] = {0, 0, 0, 0};
  bool has_uv = t_line && four(*t_line, t);
  float per_unit = has_uv ? t[2] / r[2] : 0;  // texels in a unit of the layout
  Fill kind = fill_of(asset, wrap, r[2], t[2], has_uv);
  switch (kind) {
    case Fill::None:
      return;
    case Fill::Tile:
      t[0] -= left * per_unit;
      t[2] += (left + right) * per_unit;
      put_four_whole(*t_line, t);
      [[fallthrough]];
    case Fill::Stretch:
      r[0] -= left;
      r[2] += left + right;
      put_four(*r_line, r);
      return;
    case Fill::Mirror:
    case Fill::Smear: {
      if (!pieces) return;
      bool mirror = kind == Fill::Mirror;
      for (int side = 0; side < 2; ++side) {
        float d = side == 0 ? left : right;
        if (d <= 0) continue;
        if (mirror && d > r[2]) d = r[2];  // no more than the element has to show
        float edge = side == 0 ? r[0] : r[0] + r[2];
        float t_edge = side == 0 ? t[0] : t[0] + t[2];
        float pr[4] = {side == 0 ? edge - d : edge, r[1], d, r[3]};
        float pt[4] = {t_edge, t[1], 0, t[3]};
        if (mirror) {
          // the left piece: from the texel `d` inside the element back to the edge's; the right piece:
          // from the edge's texel back into the element
          float span = d * per_unit;
          pt[0] = side == 0 ? t_edge + span : t_edge;
          pt[2] = -span;
        } else {
          // the element's own column at the edge, one texel wide
          float column = t[2] > 0 ? 1.0f : t[2] < 0 ? -1.0f : 0.0f;
          pt[0] = side == 0 ? t_edge : t_edge - column;
          pt[2] = column;
        }
        pieces->list.push_back({four_text(pr, false), four_text(pt, true)});
      }
      return;
    }
  }
}

// An element's body made wide; `more`: the bodies of the second pieces that go after it.
std::string widen_body(const std::string& body, float extra, std::vector<std::string>& more) {
  std::vector<Line> ls = lines_of(body);
  std::string asset;
  if (Line* a = find(ls, "asset_name")) asset = value_of(*a);
  if (!asset.empty() && asset[0] == '$') {
    // A text is laid out inside its own box: a box as wide as the screen keeps the text in the middle
    // as it is. But a text that waits beyond an edge waits beyond the picture's.
    float r[4];
    for (const char* key : {"start_xywh", "end_xywh"})
      if (Line* l = find(ls, key))
        if (four(*l, r) && move_out(r, extra)) put_four(*l, r);
    std::string out;
    for (const Line& l : ls) out += l.text;
    return out;
  }
  asset = lower(asset);
  bool wrap = false;
  if (Line* w = find(ls, "wrap_uv")) wrap = value_of(*w) == "1";
  bool moves = find(ls, "end_xywh") || find(ls, "end_uvwh");
  Pieces pieces;
  std::vector<Line> original = ls;
  widen_rect(ls, "start_xywh", "start_uvwh", asset, wrap, extra, moves ? nullptr : &pieces);
  widen_rect(ls, "end_xywh", "end_uvwh", asset, wrap, extra, nullptr);
  for (const auto& p : pieces.list) {
    std::string piece;
    for (const Line& l : original) {
      if (l.key == "start_xywh" || l.key == "start_uvwh")
        piece += l.text.substr(0, l.value_at) + " " + (l.key == "start_xywh" ? p.first : p.second) +
                 line_end(l.text);
      else
        piece += l.text;
    }
    more.push_back(piece);
  }
  std::string out;
  for (const Line& l : ls) out += l.text;
  return out;
}

}  // namespace

std::string widen(const std::string& script, float extra) {
  if (extra < 1.0f) return script;
  std::string out;
  size_t pos = 0;
  for (;;) {
    size_t a = script.find("UI_Element", pos);
    if (a == std::string::npos) break;
    size_t open = script.find('{', a);
    size_t close = open == std::string::npos ? std::string::npos : script.find('}', open);
    if (close == std::string::npos) break;
    std::string head = script.substr(a, open + 1 - a);  // "UI_Element <name>" and the brace
    out += script.substr(pos, open + 1 - pos);
    std::vector<std::string> more;
    out += widen_body(script.substr(open + 1, close - open - 1), extra, more);
    out += '}';
    for (const std::string& piece : more) out += "\r\n" + head + piece + "}";
    pos = close + 1;
  }
  out += script.substr(pos);
  return out;
}

}  // namespace uiwide
