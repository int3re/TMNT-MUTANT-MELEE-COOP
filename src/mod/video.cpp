#include "mod/video.h"

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/log.h"
#include "mod/d3d8.h"
#include "mod/game.h"
#include "mod/menu.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace video {
namespace {

Config g_cfg;
std::atomic<HWND> g_hwnd{nullptr};
void* g_dev = nullptr;             // RenderWare's IDirect3DDevice8
std::atomic<unsigned> g_frames{0};
int g_frame_w = 0, g_frame_h = 0;  // the game's back buffer
int g_want_w = 0, g_want_h = 0;    // the frame the mod asks the game for
bool g_copy = false;               // the swap chain can present into a part of the window
RECT g_dest{}, g_last_client{};
int g_bar_frames = 0;
unsigned g_shot_frame = 0;         // a game frame whose picture is to be saved at the next Present
DWORD g_aniso = 0;
float g_ui_scale = 1.0f;         // how much narrower the game draws 2D in a wide picture
unsigned g_rt_calls = 0, g_rt_failed = 0;  // render targets set / refused by the device

bool windowed() { return g_cfg.mode != Config::Mode::Game; }

MONITORINFO primary_monitor() {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  POINT origin{0, 0};
  GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
  return mi;
}

DWORD window_style() {
  return g_cfg.mode == Config::Mode::Window ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) : WS_POPUP;
}

// Where the window goes for a frame of fw x fh.
RECT placement(int fw, int fh) {
  RECT r{};
  MONITORINFO mi = primary_monitor();
  if (g_cfg.mode == Config::Mode::Window) {
    int cw = g_cfg.window_w > 0 && g_cfg.window_h > 0 ? g_cfg.window_w : fw;
    int ch = g_cfg.window_w > 0 && g_cfg.window_h > 0 ? g_cfg.window_h : fh;
    RECT c{0, 0, cw, ch};
    AdjustWindowRectEx(&c, window_style(), FALSE, 0);
    int w = c.right - c.left, h = c.bottom - c.top;
    r.left = mi.rcWork.left + std::max(0L, ((mi.rcWork.right - mi.rcWork.left) - w) / 2);
    r.top = mi.rcWork.top + std::max(0L, ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2);
    r.right = r.left + w;
    r.bottom = r.top + h;
  } else {
    r = mi.rcMonitor;
  }
  if (g_cfg.offscreen) OffsetRect(&r, -32000 - r.left, -32000 - r.top);
  return r;
}

// The frame the mod asks the game for. aspect = wide: the monitor's own size without a frame, a
// window of the monitor's shape at three quarters of the desktop's height (or the window's own
// size). aspect = 4:3: the largest 4:3 frame that fits, shown with black bars.
void choose_frame() {
  if (!windowed()) return;
  MONITORINFO mi = primary_monitor();
  int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
  int w = mw, h = mh;
  if (g_cfg.res_w > 0) {
    w = g_cfg.res_w;
    h = g_cfg.res_h;
  } else if (g_cfg.mode == Config::Mode::Window) {
    if (g_cfg.window_w > 0 && g_cfg.window_h > 0) {
      w = g_cfg.window_w;
      h = g_cfg.window_h;
    } else {
      h = (mi.rcWork.bottom - mi.rcWork.top) * 3 / 4;
      w = g_cfg.wide ? h * mw / mh : h * 4 / 3;  // a window of the monitor's own shape
    }
  }
  if (!g_cfg.wide) {
    if (w * 3 > h * 4) w = h * 4 / 3;  // wider than 4:3: bars at the sides
    if (w * 3 < h * 4) h = w * 3 / 4;  // taller: bars above and below
  } else if (w * 3 < h * 4) {
    h = w * 3 / 4;  // (a frame taller than 4:3 would be the 4:3 picture cut at the sides)
  }
  g_want_w = w & ~1;
  g_want_h = h & ~1;
}

// The frame fitted into the client area with its shape kept; the bars painted black.
const RECT* fit(HWND hwnd) {
  RECT c{};
  if (!GetClientRect(hwnd, &c) || c.right <= 0 || c.bottom <= 0 || g_frame_w <= 0) return nullptr;
  double s = std::min(double(c.right) / g_frame_w, double(c.bottom) / g_frame_h);
  int w = (int)(g_frame_w * s + 0.5), h = (int)(g_frame_h * s + 0.5);
  if (std::abs(w - c.right) <= 1 && std::abs(h - c.bottom) <= 1) {
    g_dest = c;
    return &g_dest;
  }
  g_dest = RECT{(c.right - w) / 2, (c.bottom - h) / 2, 0, 0};
  g_dest.right = g_dest.left + w;
  g_dest.bottom = g_dest.top + h;
  bool changed = memcmp(&c, &g_last_client, sizeof(c)) != 0;
  if (changed || ++g_bar_frames >= 60) {
    g_bar_frames = 0;
    g_last_client = c;
    if (HDC dc = GetDC(hwnd)) {
      HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
      RECT bars[4] = {{0, 0, c.right, g_dest.top},
                      {0, g_dest.bottom, c.right, c.bottom},
                      {0, g_dest.top, g_dest.left, g_dest.bottom},
                      {g_dest.right, g_dest.top, c.right, g_dest.bottom}};
      for (const RECT& b : bars)
        if (b.right > b.left && b.bottom > b.top) FillRect(dc, &b, black);
      ReleaseDC(hwnd, dc);
    }
  }
  return &g_dest;
}

void place(HWND hwnd, int fw, int fh) {
  if (!windowed() || !hwnd) return;
  RECT r = placement(fw, fh);
  SetWindowLongA(hwnd, GWL_STYLE, static_cast<LONG>(window_style() | (IsWindowVisible(hwnd) ? WS_VISIBLE : 0)));
  LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
  if (g_cfg.no_activate) ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  SetWindowLongA(hwnd, GWL_EXSTYLE, ex);
  SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  LOGI("video: window at %ld,%ld size %ldx%ld for a frame of %dx%d", r.left, r.top, r.right - r.left, r.bottom - r.top, fw,
       fh);
}

// --- the game's wide screen, for any shape of the frame -----------------------------------------
void set_shape(int w, int h) {
  if (!windowed() || w <= 0 || h <= 0) return;
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  bool wide = g_cfg.wide && aspect > 1.34f;
  if (!wide) aspect = 4.0f / 3.0f;
  float scale = (4.0f / 3.0f) / aspect;        // 2D elements keep their shape, drawn towards the middle
  float bar = (1.0f / scale - 1.0f) * 0.5f + 0.004f;  // the side bars of menus, in the 2D units
  float neg = -bar;
  patch::write(game::kAspectWideImm, &aspect, 4);
  patch::write(game::kAspectNarrowImm, &aspect, 4);
  patch::write(game::kBarWidthImm1, &bar, 4);
  patch::write(game::kBarLeftImm, &neg, 4);
  patch::write(game::kBarWidthImm2, &bar, 4);
  *reinterpret_cast<float*>(game::kUiScaleX) = scale;
  // (texts and the black sheet of a fade have the same shape in constants of their own: left at the
  // consoles' 16:9, the words of a 21:9 picture stand beside their plates)
  float across = 1.0f / scale;
  *reinterpret_cast<float*>(game::kTextScreenWidth) = across;
  *reinterpret_cast<float*>(game::kBlackSheetWidth) = across;
  *reinterpret_cast<float*>(game::kBlackSheetLeft) = -(across - 1.0f) * 0.5f;
  g_ui_scale = scale;
  *reinterpret_cast<uint8_t*>(game::kWidescreen) = wide ? 1 : 0;
  LOGI("video: the picture is %dx%d, %s (view %.4f : 1, 2D drawn at %.4f of the width)", w, h,
       wide ? "wide" : "4:3", aspect, scale);
}

// --- Direct3D 8 -------------------------------------------------------------------------------------
using Direct3DCreate8_t = void*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
using Reset_t = HRESULT(WINAPI*)(void*, d3d8::PresentParameters*);
using Present_t = HRESULT(WINAPI*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*);
using GetAdapterModeCount_t = UINT(WINAPI*)(void*, UINT);
using EnumAdapterModes_t = HRESULT(WINAPI*)(void*, UINT, UINT, d3d8::DisplayMode*);
using SetTextureStageState_t = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD);
Direct3DCreate8_t oDirect3DCreate8 = nullptr;
CreateDevice_t oCreateDevice = nullptr;
Reset_t oReset = nullptr;
Present_t oPresent = nullptr;
GetAdapterModeCount_t oGetAdapterModeCount = nullptr;
EnumAdapterModes_t oEnumAdapterModes = nullptr;
SetTextureStageState_t oSetTextureStageState = nullptr;

void* g_d3d = nullptr;
UINT g_adapter = 0;
DWORD g_msaa = 0;

void log_params(const char* what, const d3d8::PresentParameters& p) {
  LOGI("d3d8 %s: %ux%u fmt %lu, %s, swap %lu x%u, msaa %lu, depth %s %lu, flags 0x%lX, %u Hz, interval 0x%X", what,
       p.BackBufferWidth, p.BackBufferHeight, p.BackBufferFormat, p.Windowed ? "windowed" : "fullscreen", p.SwapEffect,
       p.BackBufferCount, p.MultiSampleType, p.EnableAutoDepthStencil ? "auto" : "own", p.AutoDepthStencilFormat, p.Flags,
       p.FullScreen_RefreshRateInHz, p.FullScreen_PresentationInterval);
}

DWORD desktop_format(void* d3d, UINT adapter) {
  d3d8::DisplayMode dm{};
  if (SUCCEEDED(d3d8::call<HRESULT>(d3d, d3d8::kD3dGetAdapterDisplayMode, adapter, &dm))) return dm.Format;
  return d3d8::kFmtX8R8G8B8;
}

// Antialiasing (MSAA) needs the DISCARD swap effect, which presents the whole back buffer onto the
// whole client area: only when the frame fills the window (no bars, no scaling).
DWORD msaa_for(const d3d8::PresentParameters& p, HWND hwnd) {
  if (g_cfg.antialiasing < 2 || !g_d3d || !hwnd) return 0;
  RECT c{};
  if (!GetClientRect(hwnd, &c) || static_cast<UINT>(c.right) != p.BackBufferWidth ||
      static_cast<UINT>(c.bottom) != p.BackBufferHeight) {
    LOGI("video: antialiasing off: the frame %ux%u does not fill the window (%ldx%ld)", p.BackBufferWidth, p.BackBufferHeight,
         c.right, c.bottom);
    return 0;
  }
  for (DWORD n = g_cfg.antialiasing > 8 ? 8 : static_cast<DWORD>(g_cfg.antialiasing); n >= 2; n /= 2) {
    if (FAILED(d3d8::call<HRESULT>(g_d3d, d3d8::kD3dCheckDeviceMultiSampleType, g_adapter, (DWORD)d3d8::kDevHal,
                                   p.BackBufferFormat, TRUE, n)))
      continue;
    if (p.EnableAutoDepthStencil &&
        FAILED(d3d8::call<HRESULT>(g_d3d, d3d8::kD3dCheckDeviceMultiSampleType, g_adapter, (DWORD)d3d8::kDevHal,
                                   p.AutoDepthStencilFormat, TRUE, n)))
      continue;
    return n;
  }
  LOGI("video: antialiasing is not supported by this video card");
  return 0;
}

bool adjust(const d3d8::PresentParameters& game, d3d8::PresentParameters& out, HWND hwnd) {
  if (!windowed()) return false;
  out = game;
  out.Windowed = TRUE;
  out.FullScreen_RefreshRateInHz = 0;
  out.FullScreen_PresentationInterval = 0;
  out.BackBufferFormat = g_d3d ? desktop_format(g_d3d, g_adapter) : d3d8::kFmtX8R8G8B8;
  out.BackBufferCount = 1;
  if (g_want_w > 0) {
    out.BackBufferWidth = static_cast<UINT>(g_want_w);
    out.BackBufferHeight = static_cast<UINT>(g_want_h);
  }
  if (out.EnableAutoDepthStencil && out.AutoDepthStencilFormat == d3d8::kFmtD16) out.AutoDepthStencilFormat = d3d8::kFmtD24S8;
  g_msaa = msaa_for(out, hwnd);
  if (g_msaa) {  // the whole frame onto the whole window, smoothed edges
    g_copy = false;
    out.MultiSampleType = g_msaa;
    out.SwapEffect = d3d8::kSwapDiscard;
    out.Flags &= ~static_cast<DWORD>(d3d8::kPresentFlagLockable);
  } else {
    g_copy = true;
    out.MultiSampleType = 0;
    // The mod holds the frames at 60 a second itself (sim.cpp); a window is shown through the
    // desktop compositor, which does not tear.
    out.SwapEffect = d3d8::kSwapCopy;
  }
  return true;
}

// --- [debug] shots: the frame from the back buffer, never the desktop ---------------------------
void save_shot(void* dev, unsigned frame) {
  void* bb = nullptr;
  if (FAILED(d3d8::call<HRESULT>(dev, d3d8::kDevGetBackBuffer, 0u, 0u, &bb)) || !bb) return;
  d3d8::SurfaceDesc d{};
  d3d8::call<HRESULT>(bb, d3d8::kSurfGetDesc, &d);
  void* sys = nullptr;
  bool ok = (d.Format == d3d8::kFmtX8R8G8B8 || d.Format == d3d8::kFmtA8R8G8B8) && d.MultiSampleType == 0 &&
            SUCCEEDED(d3d8::call<HRESULT>(dev, d3d8::kDevCreateImageSurface, d.Width, d.Height, d.Format, &sys)) && sys &&
            SUCCEEDED(d3d8::call<HRESULT>(dev, d3d8::kDevCopyRects, bb, (const RECT*)nullptr, 0u, sys, (const POINT*)nullptr));
  d3d8::LockedRect lr{};
  if (ok && SUCCEEDED(d3d8::call<HRESULT>(sys, d3d8::kSurfLockRect, &lr, (const RECT*)nullptr, (DWORD)d3d8::kLockReadOnly))) {
    std::wstring dir = module_dir() + L"screenshots";
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t name[64];
    swprintf(name, 64, L"\\meleenet_f%06u.bmp", frame);
    std::wstring path = dir + name;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = (LONG)d.Width;
    ih.biHeight = -(LONG)d.Height;  // top-down
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    DWORD row = d.Width * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + row * d.Height;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD w = 0;
      WriteFile(f, &fh, sizeof(fh), &w, nullptr);
      WriteFile(f, &ih, sizeof(ih), &w, nullptr);
      for (UINT y = 0; y < d.Height; ++y)
        WriteFile(f, static_cast<const uint8_t*>(lr.pBits) + (size_t)y * lr.Pitch, row, &w, nullptr);
      CloseHandle(f);
      LOGI("debug: frame %u saved (%ux%u)", frame, d.Width, d.Height);
    }
    d3d8::call<HRESULT>(sys, d3d8::kSurfUnlockRect);
  } else {
    LOGW("debug: frame %u not saved (back buffer format %lu, msaa %lu)", frame, d.Format, d.MultiSampleType);
  }
  if (sys) d3d8::release(sys);
  d3d8::release(bb);
}

HRESULT WINAPI hkPresent(void* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
  if (dev != g_dev) return oPresent(dev, src, dst, wnd, dirty);
  unsigned f = ++g_frames;
  if (f == 1 || f == 60) LOGI("video: %u pictures shown", f);
  if (f % 1800 == 0) {  // (and how fast: what a player's log has to say about a game that "lags")
    static DWORD mark = 0;
    DWORD t = GetTickCount();
    if (mark && t != mark)
      LOGI("video: %u pictures shown, %.1f a second lately, %u frames played without a picture; render targets set %u times, "
           "refused %u",
           f, 1800 * 1000.0 / (t - mark), sim::pictures_skipped(), g_rt_calls, g_rt_failed);
    else
      LOGI("video: %u pictures shown", f);
    mark = t;
  }
  if (g_shot_frame) {
    save_shot(dev, g_shot_frame);
    g_shot_frame = 0;
  }
  if (g_cfg.lab_draw_cost_ms) Sleep(g_cfg.lab_draw_cost_ms);
  if (windowed() && g_copy && !dst) {
    HWND hwnd = wnd ? wnd : g_hwnd.load();
    if (hwnd && !IsIconic(hwnd))
      if (const RECT* d = fit(hwnd)) dst = d;
  }
  return oPresent(dev, src, dst, wnd, dirty);
}

HRESULT WINAPI hkReset(void* dev, d3d8::PresentParameters* pp) {
  if (dev != g_dev) return oReset(dev, pp);
  d3d8::PresentParameters mine{};
  if (pp) log_params("Reset (game)", *pp);
  if (pp && adjust(*pp, mine, g_hwnd.load())) {
    log_params("Reset (meleenet)", mine);
    HRESULT hr = oReset(dev, &mine);
    if (SUCCEEDED(hr)) {
      g_frame_w = (int)mine.BackBufferWidth;
      g_frame_h = (int)mine.BackBufferHeight;
      return hr;
    }
    LOGE("d3d8 Reset failed (0x%08X) with meleenet's parameters", (unsigned)hr);
    return hr;
  }
  return oReset(dev, pp);
}

// Anisotropic filtering: the game's linear filtering becomes anisotropic (sharper floors and walls at
// an angle), as far as the card allows.
HRESULT WINAPI hkSetTextureStageState(void* dev, DWORD stage, DWORD type, DWORD value) {
  if (g_aniso > 1 && dev == g_dev && (type == d3d8::kTssMinFilter || type == d3d8::kTssMagFilter) &&
      value == d3d8::kTexfLinear) {
    oSetTextureStageState(dev, stage, d3d8::kTssMaxAnisotropy, g_aniso);
    value = d3d8::kTexfAnisotropic;
  }
  return oSetTextureStageState(dev, stage, type, value);
}

// A render target the device refuses (a texture with the smoothed frame's depth buffer, say) is a part
// of the picture that silently goes missing: counted, and said in the log.
using SetRenderTarget_t = HRESULT(WINAPI*)(void*, void*, void*);
SetRenderTarget_t oSetRenderTarget = nullptr;

HRESULT WINAPI hkSetRenderTarget(void* dev, void* target, void* depth) {
  HRESULT hr = oSetRenderTarget(dev, target, depth);
  ++g_rt_calls;
  if (FAILED(hr) && ++g_rt_failed <= 3) LOGW("d3d8: SetRenderTarget failed: 0x%08X (call %u)", (unsigned)hr, g_rt_calls);
  return hr;
}

void hook_device(void* dev) {
  g_dev = dev;
  if (oPresent) return;
  void** vt = d3d8::vtable(dev);
  patch::hook("IDirect3DDevice8::SetRenderTarget", reinterpret_cast<uintptr_t>(vt[31]), (void*)&hkSetRenderTarget,
              (void**)&oSetRenderTarget);
  if (g_cfg.anisotropy > 1) {
    d3d8::Caps caps{};
    if (SUCCEEDED(d3d8::call<HRESULT>(dev, d3d8::kDevGetDeviceCaps, &caps)) &&
        (caps.TextureFilterCaps & d3d8::kFilterCapMinAniso) && caps.MaxAnisotropy > 1) {
      g_aniso = std::min<DWORD>(static_cast<DWORD>(g_cfg.anisotropy), caps.MaxAnisotropy);
      if (!(caps.TextureFilterCaps & d3d8::kFilterCapMagAniso)) LOGI("video: no anisotropic magnification on this card");
      patch::hook("IDirect3DDevice8::SetTextureStageState (anisotropic filtering)",
                  reinterpret_cast<uintptr_t>(vt[d3d8::kDevSetTextureStageState]), (void*)&hkSetTextureStageState,
                  (void**)&oSetTextureStageState);
      LOGI("video: anisotropic filtering x%u", static_cast<unsigned>(g_aniso));
    }
  }
  patch::hook("IDirect3DDevice8::Reset", reinterpret_cast<uintptr_t>(vt[d3d8::kDevReset]), (void*)&hkReset, (void**)&oReset);
  patch::hook("IDirect3DDevice8::Present", reinterpret_cast<uintptr_t>(vt[d3d8::kDevPresent]), (void*)&hkPresent,
              (void**)&oPresent);
}

HRESULT WINAPI hkCreateDevice(void* d3d, UINT adapter, DWORD type, HWND focus, DWORD flags, d3d8::PresentParameters* pp,
                              void** out) {
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (windowed() && ra >= game::kGammaProbeBegin && ra < game::kGammaProbeEnd) {
    // The game's own probe of the card's gamma ramp: a second, full-screen device. Not in a window.
    LOGD("d3d8: the game's gamma probe device is refused");
    if (out) *out = nullptr;
    return static_cast<HRESULT>(0x8876086C);  // D3DERR_INVALIDCALL
  }
  d3d8::PresentParameters mine{};
  d3d8::PresentParameters* use = pp;
  HWND hwnd = pp && pp->hDeviceWindow ? pp->hDeviceWindow : focus;
  if (hwnd) g_hwnd = hwnd;
  g_d3d = d3d;
  g_adapter = adapter;
  if (pp) {
    log_params("CreateDevice (game)", *pp);
    if (windowed()) {
      // (the window takes its place before the device is made: antialiasing asks whether the frame fills it)
      place(hwnd, g_want_w > 0 ? g_want_w : (int)pp->BackBufferWidth, g_want_h > 0 ? g_want_h : (int)pp->BackBufferHeight);
      if (adjust(*pp, mine, hwnd)) {
        use = &mine;
        log_params("CreateDevice (meleenet)", mine);
      }
    }
  }
  if (g_cfg.guard && use && !use->Windowed) {
    // Lab runs must never switch the owner's desktop to another resolution.
    LOGE("debug: guard: the game asks for a full-screen device; ending the run");
    TerminateProcess(GetCurrentProcess(), 4);
  }
  // The game draws its loading screen on a second thread while the first one loads the level (makes
  // textures and buffers): two threads in one device. Direct3D is safe with that only when it is told
  // so; the game does not tell it (flags as the game gave them are in the log).
  LOGI("d3d8 CreateDevice: behaviour flags 0x%lX%s", flags, (flags & 4) ? "" : " + multithreaded");
  flags |= 4;  // D3DCREATE_MULTITHREADED
  HRESULT hr = oCreateDevice(d3d, adapter, type, focus, flags, use, out);
  if (FAILED(hr) && use == &mine && g_msaa) {
    LOGW("d3d8 CreateDevice failed (0x%08X) with antialiasing; without it", (unsigned)hr);
    mine.MultiSampleType = 0;
    mine.SwapEffect = d3d8::kSwapCopy;
    g_msaa = 0;
    g_copy = true;
    hr = oCreateDevice(d3d, adapter, type, focus, flags, use, out);
  }
  if (FAILED(hr)) {
    LOGE("d3d8 CreateDevice failed: 0x%08X", (unsigned)hr);
    return hr;
  }
  if (use) {
    g_frame_w = (int)use->BackBufferWidth;
    g_frame_h = (int)use->BackBufferHeight;
    // (what the game gave is what it goes on to believe)
    if (use == &mine) {
      pp->BackBufferWidth = mine.BackBufferWidth;
      pp->BackBufferHeight = mine.BackBufferHeight;
    }
  }
  LOGI("video: frame %dx%d%s", g_frame_w, g_frame_h, g_msaa ? ", antialiasing on" : "");
  if (out && *out) hook_device(*out);
  return hr;
}

// The game takes its frame's size from the list of the monitor's display modes. The mod's own size
// (the device is a windowed one: any size goes) is one more mode at the list's end.
UINT WINAPI hkGetAdapterModeCount(void* d3d, UINT adapter) {
  UINT n = oGetAdapterModeCount(d3d, adapter);
  return g_want_w > 0 ? n + 1 : n;
}

HRESULT WINAPI hkEnumAdapterModes(void* d3d, UINT adapter, UINT mode, d3d8::DisplayMode* out) {
  if (g_want_w > 0 && out && mode == oGetAdapterModeCount(d3d, adapter)) {
    out->Width = static_cast<UINT>(g_want_w);
    out->Height = static_cast<UINT>(g_want_h);
    out->RefreshRate = 60;
    out->Format = d3d8::kFmtX8R8G8B8;
    return S_OK;
  }
  return oEnumAdapterModes(d3d, adapter, mode, out);
}

void* WINAPI hkDirect3DCreate8(UINT sdk) {
  void* d3d = oDirect3DCreate8(sdk);
  LOGI("d3d8: Direct3DCreate8(%u) -> %p, asked from 0x%08X", sdk, d3d, (unsigned)(uintptr_t)_ReturnAddress());
  if (d3d && !oCreateDevice) {
    void** vt = d3d8::vtable(d3d);
    patch::hook("IDirect3D8::CreateDevice", reinterpret_cast<uintptr_t>(vt[d3d8::kD3dCreateDevice]), (void*)&hkCreateDevice,
                (void**)&oCreateDevice);
    if (g_want_w > 0) {
      patch::hook("IDirect3D8::GetAdapterModeCount", reinterpret_cast<uintptr_t>(vt[d3d8::kD3dGetAdapterModeCount]),
                  (void*)&hkGetAdapterModeCount, (void**)&oGetAdapterModeCount);
      patch::hook("IDirect3D8::EnumAdapterModes", reinterpret_cast<uintptr_t>(vt[d3d8::kD3dEnumAdapterModes]),
                  (void*)&hkEnumAdapterModes, (void**)&oEnumAdapterModes);
    }
  }
  return d3d;
}

// --- the game's start-up ---------------------------------------------------------------------------
using Void_t = void(__cdecl*)();
using PickVideoMode_t = int(__cdecl*)(int*, int*, int*);
Void_t oLoadSettings = nullptr;
Void_t oMakeWindow = nullptr;
PickVideoMode_t oPickVideoMode = nullptr;

void __cdecl hkLoadSettings() {
  oLoadSettings();
  if (g_cfg.toon >= 0) *reinterpret_cast<int*>(game::kToonShading) = g_cfg.toon ? 1 : 0;
  if (g_cfg.shadows >= 0) *reinterpret_cast<int*>(game::kDynamicShadows) = g_cfg.shadows ? 1 : 0;
  if (g_cfg.particles >= 0) *reinterpret_cast<int*>(game::kParticlePercent) = std::clamp(g_cfg.particles, 0, 100);
  // (how many particles are made is drawn from the game's random numbers: the same on every PC of a session)
  if (netplay::session_game()) *reinterpret_cast<int*>(game::kParticlePercent) = 100;
  if (!windowed() || g_want_w <= 0) return;
  *reinterpret_cast<int*>(game::kScreenW) = g_want_w;
  *reinterpret_cast<int*>(game::kScreenH) = g_want_h;
  *reinterpret_cast<int*>(game::kScreenDepth) = 32;
}

// The game's own choice is always 640x480 in 16 bits. The mod's: its frame, in 32 bits.
int __cdecl hkPickVideoMode(int* w, int* h, int* depth) {
  if (!windowed() || g_want_w <= 0) return oPickVideoMode(w, h, depth);
  auto count = reinterpret_cast<int(__cdecl*)()>(game::kRwNumVideoModes)();
  auto info = reinterpret_cast<void*(__cdecl*)(game::RwVideoMode*, int)>(game::kRwVideoModeInfo);
  int found = -1;
  for (int i = 0; i < count; ++i) {
    game::RwVideoMode m{};
    info(&m, i);
    if (m.width == g_want_w && m.height == g_want_h && m.depth == 32 && (m.flags & 1)) {
      found = i;
      if (m.refresh == 60) break;
    }
  }
  if (found < 0) {
    LOGE("video: RenderWare has no %dx%d mode in its list (%d modes); the game's own choice", g_want_w, g_want_h, count);
    return oPickVideoMode(w, h, depth);
  }
  *w = g_want_w;
  *h = g_want_h;
  *depth = 32;
  static bool said = false;
  if (!said) LOGI("video: RenderWare's mode %d of %d is the mod's frame %dx%d", found, count, g_want_w, g_want_h);
  said = true;
  return found;
}

// The two black bars the game puts over the sides of its menus in a wide picture (0x40AB90). The menus'
// backgrounds are drawn over the whole frame all the same, so without the bars a menu fills a wide screen.
Void_t oMenuBars = nullptr;
void __cdecl hkMenuBars() {
  if (g_cfg.menu_bars || !windowed()) oMenuBars();
}

// The story's map in wide menus. The mode draws the map as one big quad and lays four dim strips of the
// same texture around the map's window; the strips end at the 4:3 edges, so beside them the bare map
// would be seen. Here the same four strips go on to the picture's edges (the texture repeats).
using MapFrame_t = void(__cdecl*)(float, float);
MapFrame_t oMapFrame = nullptr;
void __cdecl hkMapFrame(float x, float y) {
  float extra = menu_extra();
  auto* texture = *reinterpret_cast<uint8_t**>(game::kMapTexture);
  if (extra < 1.0f || !texture) {
    oMapFrame(x, y);
    return;
  }
  using Quad_t = void(__cdecl*)(float, float, float, float, float, float, float, float, void*, void*);
  using Set_t = void(__cdecl*)(int);
  using State_t = void(__cdecl*)(int, int);
  auto quad = reinterpret_cast<Quad_t>(game::kDrawQuad);
  auto fractions = reinterpret_cast<Set_t>(game::kUiSetFractions);
  auto state = reinterpret_cast<State_t>(game::kRwRenderStateSet);
  int was = *reinterpret_cast<int*>(game::kUiFractions);
  fractions(1);
  const int* raster = *reinterpret_cast<int**>(texture);
  float w = static_cast<float>(raster[3]);
  float h = static_cast<float>(raster[4]);
  texture[0x51] = 0x11;  // repeats both ways
  texture[0x50] = 2;     // smoothed
  float u = -(x * w * 0.5f);
  float v = y * h * 0.5f;
  state(9, 2);
  state(2, 1);
  float e = extra / 640.0f;  // the picture's extra width at each side, in screens
  void* white = reinterpret_cast<void*>(game::kWhite);
  quad(-e, 0.0f, 1.0f + 2 * e, 0.32f, u - w * e, v - h * 0.32f, w * (1.0f + 2 * e), h * 0.32f, texture, white);
  quad(-e, 0.7f, 1.0f + 2 * e, 0.3f, u - w * e, v, w * (1.0f + 2 * e), h - h * 0.7f, texture, white);
  quad(-e, 0.0f, 0.11f + e, 1.0f, u - w * e, v, w * (0.11f + e), h, texture, white);
  quad(0.86f, 0.0f, 0.14f + e, 1.0f, w * 0.86f + u, v, w * (0.14f + e), h, texture, white);
  fractions(was);
}

void __cdecl hkMakeWindow() {
  oMakeWindow();
  if (!windowed()) return;
  // The game's "full screen" flag: with it the game minimizes itself and waits whenever another
  // window is in front, and sets the video card's gamma. Not in a window.
  *reinterpret_cast<uint8_t*>(game::kFullScreen) = 0;
  set_shape(g_frame_w > 0 ? g_frame_w : g_want_w, g_frame_h > 0 ? g_frame_h : g_want_h);
}

// --- the window -----------------------------------------------------------------------------------------
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
CreateWindowExA_t oCreateWindowExA = nullptr;
BOOL(WINAPI* oShowWindow)(HWND, int) = nullptr;
BOOL(WINAPI* oSetForegroundWindow)(HWND) = nullptr;
HWND(WINAPI* oSetFocus)(HWND) = nullptr;
int(WINAPI* oShowCursor)(BOOL) = nullptr;
BOOL(WINAPI* oCloseWindow)(HWND) = nullptr;
WNDPROC g_game_proc = nullptr;

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // An address being typed on the network page takes the keys.
  if (msg == WM_KEYDOWN && menu::on_key(static_cast<unsigned>(wp))) return 0;
  if (msg == WM_CHAR && menu::on_char(static_cast<wchar_t>(wp))) return 0;
  if (windowed()) {
    if (msg == WM_ACTIVATE) {
      bool front = LOWORD(wp) != WA_INACTIVE;
      LOGI("video: the window is %s", front ? "active" : "not active");
      if (front) SetFocus(hwnd);
      if (g_cfg.mode == Config::Mode::Borderless && !g_cfg.no_activate) {
        static bool hidden = false;  // one hidden / shown step each way (ShowCursor counts)
        if (front != hidden) {
          ShowCursor(front ? FALSE : TRUE);
          hidden = front;
        }
      }
      return 0;
    }
    // The game's own answer to a new size is to make its frame anew at the window's size. The mod's
    // frame keeps its size whatever the window does.
    if (msg == WM_SIZE) return DefWindowProcA(hwnd, msg, wp, lp);
    if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) != SC_CLOSE) return DefWindowProcA(hwnd, msg, wp, lp);
  }
  return CallWindowProcA(g_game_proc, hwnd, msg, wp, lp);
}

HWND WINAPI hkCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                              HMENU menu, HINSTANCE inst, LPVOID param) {
  bool game_window = cls && !IS_INTRESOURCE(cls) && strcmp(cls, "TMNT CLASS") == 0;
  if (game_window && windowed()) {
    RECT r = placement(g_want_w > 0 ? g_want_w : w, g_want_h > 0 ? g_want_h : h);
    ex &= ~static_cast<DWORD>(WS_EX_TOPMOST);
    if (g_cfg.no_activate) ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    style = (style & ~static_cast<DWORD>(WS_POPUP | WS_OVERLAPPEDWINDOW)) | window_style();
    x = r.left;
    y = r.top;
    w = r.right - r.left;
    h = r.bottom - r.top;
    parent = nullptr;  // (the game makes its window a child of the desktop window)
    title = "TMNT Mutant Melee";
  }
  HWND hwnd = oCreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
  if (game_window && hwnd) {
    g_hwnd = hwnd;
    if (!g_game_proc)
      g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongA(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(&window_proc)));
    LOGI("video: the game's window %p at %d,%d size %dx%d", (void*)hwnd, x, y, w, h);
  }
  return hwnd;
}

BOOL WINAPI hkShowWindow(HWND hwnd, int cmd) {
  if (hwnd == g_hwnd.load() && g_cfg.no_activate && cmd != SW_HIDE) cmd = SW_SHOWNOACTIVATE;
  return oShowWindow(hwnd, cmd);
}

BOOL WINAPI hkSetForegroundWindow(HWND hwnd) {
  if (g_cfg.no_activate) return TRUE;
  return oSetForegroundWindow(hwnd);
}

HWND WINAPI hkSetFocus(HWND hwnd) {
  if (g_cfg.no_activate) return nullptr;
  return oSetFocus(hwnd);
}

int WINAPI hkShowCursor(BOOL show) {
  if (g_cfg.no_activate) return 0;  // a lab window never touches the cursor
  if (g_cfg.mode == Config::Mode::Window) return 0;  // an ordinary window keeps the ordinary cursor
  return oShowCursor(show);
}

BOOL WINAPI hkCloseWindow(HWND hwnd) {
  if (windowed()) return TRUE;  // (the game minimizes itself when it loses the screen: not in a window)
  return oCloseWindow(hwnd);
}

DWORD WINAPI quit_watch(void*) {
  Sleep(4000);
  LOGW("video: the game did not close by itself; ending it");
  TerminateProcess(GetCurrentProcess(), 0);
  return 0;
}

}  // namespace

HWND window() { return g_hwnd.load(); }
unsigned frames() { return g_frames.load(); }
int frame_w() { return g_frame_w; }
int frame_h() { return g_frame_h; }
float menu_extra() {
  if (!windowed() || g_cfg.menu_bars || g_ui_scale <= 0.0f || g_ui_scale > 0.999f) return 0.0f;
  return 320.0f * (1.0f / g_ui_scale - 1.0f);
}

bool in_front() {
  HWND hwnd = g_hwnd.load();
  return hwnd && GetForegroundWindow() == hwnd;
}

void on_frame(unsigned frame) {
  if ((g_cfg.shot_every && frame % g_cfg.shot_every == 0) ||
      std::find(g_cfg.shots.begin(), g_cfg.shots.end(), frame) != g_cfg.shots.end())
    g_shot_frame = frame;
  // (the game's options screen and a loaded save set the consoles' wide-screen switch: the frame's shape decides)
  if (windowed() && g_frame_w > 0) {
    bool wide = g_cfg.wide && static_cast<float>(g_frame_w) / static_cast<float>(g_frame_h) > 1.34f;
    *reinterpret_cast<uint8_t*>(game::kWidescreen) = wide ? 1 : 0;
  }
}

void request_quit() {
  static bool asked = false;
  if (asked) return;
  asked = true;
  *reinterpret_cast<uint8_t*>(game::kQuit) = 1;
  if (HANDLE t = CreateThread(nullptr, 0, quit_watch, nullptr, 0, nullptr)) CloseHandle(t);
}

void install(const Config& cfg) {
  g_cfg = cfg;
  choose_frame();
  LOGI("video: mode %s%s%s, frame %dx%d", cfg.mode == Config::Mode::Game ? "game (exclusive full screen)"
                                          : cfg.mode == Config::Mode::Window ? "window" : "borderless",
       cfg.offscreen ? ", off screen" : "", cfg.no_activate ? ", never activated" : "", g_want_w, g_want_h);
  patch::import("d3d8.dll", "Direct3DCreate8", (void*)&hkDirect3DCreate8, (void**)&oDirect3DCreate8);
  patch::import("USER32.dll", "CreateWindowExA", (void*)&hkCreateWindowExA, (void**)&oCreateWindowExA);
  patch::import("USER32.dll", "ShowWindow", (void*)&hkShowWindow, (void**)&oShowWindow);
  patch::import("USER32.dll", "SetForegroundWindow", (void*)&hkSetForegroundWindow, (void**)&oSetForegroundWindow);
  patch::import("USER32.dll", "SetFocus", (void*)&hkSetFocus, (void**)&oSetFocus);
  patch::import("USER32.dll", "ShowCursor", (void*)&hkShowCursor, (void**)&oShowCursor);
  patch::import("USER32.dll", "CloseWindow", (void*)&hkCloseWindow, (void**)&oCloseWindow);
  patch::hook("the game's settings (tmnt.ini)", game::kLoadSettings, (void*)&hkLoadSettings, (void**)&oLoadSettings);
  patch::hook("the game's choice of a video mode", game::kPickVideoMode, (void*)&hkPickVideoMode, (void**)&oPickVideoMode);
  patch::hook("the game's window", game::kMakeWindow, (void*)&hkMakeWindow, (void**)&oMakeWindow);
  patch::hook("the menus' side bars", game::kMenuBars, (void*)&hkMenuBars, (void**)&oMenuBars);
  patch::hook("the frame around the story's map", game::kMapFrame, (void*)&hkMapFrame, (void**)&oMapFrame);
  if (HMODULE winmm = LoadLibraryA("winmm.dll"))
    if (auto begin = reinterpret_cast<UINT(WINAPI*)(UINT)>(GetProcAddress(winmm, "timeBeginPeriod"))) begin(1);
}

}  // namespace video
