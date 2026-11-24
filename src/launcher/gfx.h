// Drawing for the launcher: Direct2D + DirectWrite, the fonts from the exe's resources, SVG path strings
// as geometry. A Canvas draws to a window's target or to a bitmap (a PNG of a screen: launcher --shot).
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite_3.h>
#include <wincodec.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gfx {

enum Family { Display = 0, Text = 1, Mono = 2 };
struct Font {
  Family family;
  float size;      // px
  int weight;      // 400 .. 700
  float spacing;   // letter spacing, em
};
struct TextOpt {
  float max_w = 0;         // the box's width: wrapping, the ellipsis, centring
  float line_h = 0;        // the line box, px (0: the font's own)
  bool wrap = false;
  bool ellipsis = false;
  bool underline = false;
  bool center = false;
};

bool init();      // factories and fonts, once
void shutdown();
bool display_font_embedded();  // the headings' own font is in the exe (else a system font stands in)
IWICImagingFactory* wic();
ID2D1Factory* d2d();

using Matrix = D2D1::Matrix3x2F;

class Canvas {
 public:
  explicit Canvas(ID2D1RenderTarget* target);
  ~Canvas();
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;

  ID2D1RenderTarget* rt;

  // shapes (colours are 0xRRGGBB)
  void fill(float x, float y, float w, float h, uint32_t rgb, float a = 1);
  void border(float x, float y, float w, float h, float bw, uint32_t rgb, float a = 1);  // inside the box
  void box(float x, float y, float w, float h, uint32_t fill_rgb, float bw, uint32_t border_rgb);
  void round_box(float x, float y, float w, float h, float radius, uint32_t fill_rgb, float bw, uint32_t border_rgb);
  void ellipse(float cx, float cy, float rx, float ry, uint32_t rgb, float a = 1);
  void ring(float cx, float cy, float r, float w, uint32_t rgb, float dash = 0, float gap = 0);
  void poly(const D2D1_POINT_2F* pts, int n, uint32_t rgb, float a = 1);
  void line(float x0, float y0, float x1, float y1, float w, uint32_t rgb, float a = 1);
  void dashed_h(float x, float y, float w, float thick, float dash, float gap, uint32_t rgb, float a);
  void gradient_v(float x, float y, float w, float h, const uint32_t* rgb, const float* pos, int n);
  // SVG path data (a string literal: the geometry is kept by its address)
  void fill_path(const char* svg, uint32_t rgb, float a = 1);
  void stroke_path(const char* svg, float w, uint32_t rgb, bool round = true, float a = 1);
  // an icon drawn in a 24 x 24 box (or `box`), stroked, at x, y with the given size
  void icon(const char* svg, float x, float y, float size, uint32_t rgb, float stroke_w, float box = 24, bool filled = false);

  // transforms: m is in the local space (applied before what is set already)
  void push(const Matrix& m);
  void pop();
  Matrix current() const;
  void clip_poly(const D2D1_POINT_2F* pts, int n);  // until unclip()
  void unclip();

  // text
  float line_h(const Font& f);  // the font's own line box
  D2D1_SIZE_F measure(const std::wstring& s, const Font& f, const TextOpt& o = {});
  // Draws at x, y (the top of the first line box); returns the height taken.
  float text(const std::wstring& s, const Font& f, float x, float y, uint32_t rgb, const TextOpt& o = {}, float a = 1);
  // One line, its box centred on cy.
  float text_mid(const std::wstring& s, const Font& f, float x, float cy, uint32_t rgb, const TextOpt& o = {}, float a = 1);

  // Something that never changes, drawn once into a bitmap of w x h and shown at 0, 0.
  void cached(int key, float w, float h, const std::function<void(Canvas&)>& paint);

 private:
  IDWriteTextLayout* layout(const std::wstring& s, const Font& f, const TextOpt& o);
  ID2D1SolidColorBrush* brush_ = nullptr;
  std::vector<Matrix> stack_;
  std::vector<ID2D1Layer*> layers_;
  std::map<int, ID2D1BitmapRenderTarget*> cache_;
  ID2D1SolidColorBrush* color(uint32_t rgb, float a);
};

// A screen of w x h (in the launcher's units) drawn at `scale` into a PNG file (no path: drawn and dropped).
bool save_png(const wchar_t* path, int w, int h, float scale, const std::function<void(Canvas&)>& paint);
extern long g_last_hr;   // of the step of save_png that failed
extern int g_last_step;

}  // namespace gfx
