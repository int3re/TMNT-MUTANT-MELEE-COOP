#include "mod/rewind.h"

#include <windows.h>

#include <psapi.h>

#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "common/log.h"
#include "mod/d3d8.h"
#include "mod/game.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace anew {
namespace {

Config g_cfg;
bool g_ready = false;     // there is a kept memory to go back to
bool g_captured = false;  // capture() has run: what is made from now on was not there at the game's start
CRITICAL_SECTION g_lock;  // the two tables of outside objects

// --- the game's memory ------------------------------------------------------------------------------
// Everything the game's logic lives in:
// - the exe's writable sections, .data with its bss and RenderWare's _rwdseg (the image is never moved);
// - the game's own heap: one block of 64 MB taken at start-up, every allocation of the game and of
//   RenderWare is in it (its base and end are kept at 0x5B10A8 / 0x5B10A4);
// - a handful of blocks the sound code took from the C library's heap at start-up.
constexpr uintptr_t kGlobalsBegin = 0x00583000, kGlobalsEnd = 0x0068A000;
constexpr uintptr_t kArenaBase = 0x005B10A8, kArenaEnd = 0x005B10A4;
constexpr uintptr_t kUseMalloc = 0x005B10C5;  // uint8: the developers' -usemalloc (no heap of the game's own)
constexpr uintptr_t kBigLock = 0x005AE7BC;    // the game's lock object; its CRITICAL_SECTION is 0x18 bytes at +4
constexpr uintptr_t kCrtHeap = 0x00688644;    // HANDLE of the C library's heap
// The sound code's blocks on that heap: voices, banks, the loader's and the lines' buffers.
constexpr uintptr_t kSoundBlocks[] = {0x00680A20, 0x00680A24, 0x00680A28, 0x00680A3C, 0x00680A40, 0x00680A44, 0x00681FFC, 0x00682738};
constexpr uintptr_t kSoundBanks = 0x00680A28;  // a table of three pointers to the banks' buffer lists (blocks too)
// The save's stream stays open from one frame to the next while the save is read or written.
constexpr uintptr_t kSaveStream = 0x00682340, kStreamClose = 0x004310E0;
// Events (objects of {vtable, HANDLE}): "the fade is over", and six of the loading thread.
constexpr uintptr_t kFadeEvent = 0x005B0958, kThreadEvents = 0x0066E130;
// RenderWare's Direct3D device and the two surfaces it draws to.
constexpr uintptr_t kDevice = 0x006853A8, kRenderTarget = 0x006853B0, kDepthSurface = 0x006853AC;

std::vector<uint8_t> g_globals, g_arena;
uint8_t* g_arena_at = nullptr;
size_t g_arena_size = 0;
struct Block {
  uint8_t* at;
  std::vector<uint8_t> bytes;
};
std::vector<Block> g_blocks;

// The end of the last byte that is not zero.
size_t used_bytes(const uint8_t* base, size_t size) {
  size_t n = size & ~static_cast<size_t>(3);
  auto words = reinterpret_cast<const uint32_t*>(base);
  while (n >= 4 && words[n / 4 - 1] == 0) n -= 4;
  return n;
}

void keep_block(uintptr_t pointer_at) {
  auto p = *reinterpret_cast<uint8_t**>(pointer_at);
  HANDLE heap = *reinterpret_cast<HANDLE*>(kCrtHeap);
  if (!p || !heap) return;
  for (const Block& b : g_blocks)
    if (b.at == p) return;
  SIZE_T n = HeapSize(heap, 0, p);
  if (n == static_cast<SIZE_T>(-1) || n == 0 || n > 0x100000) return;
  Block b;
  b.at = p;
  b.bytes.assign(p, p + n);
  g_blocks.push_back(std::move(b));
}

// --- objects outside the game's memory ------------------------------------------------------------------
// The kept memory points at Direct3D and DirectSound objects. Those that were alive when it was kept are
// held (one reference of the mod's own) and get their count of references back at a restore; those made
// later are nobody's after a restore and are let go by the mod.
struct Outside {
  bool born_before = false;  // alive when the memory was kept
  ULONG count = 0;           // its references then, the mod's own among them
};
std::unordered_map<void*, Outside> g_d3d, g_sound;
std::unordered_map<DWORD, bool> g_vertex_shaders, g_pixel_shaders;  // handle -> made before the memory was kept
DWORD g_state_block = 0;

struct Locked {
  Locked() { EnterCriticalSection(&g_lock); }
  ~Locked() { LeaveCriticalSection(&g_lock); }
};

ULONG add_ref(void* obj) { return d3d8::call<ULONG>(obj, 1); }

// The object's references are brought to `want`.
void set_count(void* obj, ULONG want) {
  ULONG c = add_ref(obj);
  while (c > want) c = d3d8::release(obj);
  while (c < want) c = add_ref(obj);
}

// A Direct3D resource tells when it is destroyed through a piece of private data that is an object:
// the resource releases it with itself.
struct Witness {
  void** table;
  LONG refs;
  void* resource;
};
HRESULT __stdcall witness_query(Witness*, const GUID*, void** out) {
  if (out) *out = nullptr;
  return E_NOINTERFACE;
}
ULONG __stdcall witness_add(Witness* w) { return static_cast<ULONG>(InterlockedIncrement(&w->refs)); }
ULONG __stdcall witness_release(Witness* w) {
  LONG n = InterlockedDecrement(&w->refs);
  if (n == 0) {
    {
      Locked lock;
      g_d3d.erase(w->resource);
    }
    delete w;
  }
  return static_cast<ULONG>(n);
}
void* g_witness_table[3] = {(void*)&witness_query, (void*)&witness_add, (void*)&witness_release};
const GUID kWitnessId = {0x6d656c65, 0x6e65, 0x7461, {0x61, 0x6e, 0x65, 0x77, 0x2d, 0x64, 0x33, 0x64}};

void watch_resource(void* resource) {
  if (!resource) return;
  auto w = new Witness{g_witness_table, 1, resource};
  {
    Locked lock;
    g_d3d[resource] = Outside{!g_captured, 0};
  }
  // IDirect3DResource8 / IDirect3DSurface8::SetPrivateData(guid, data, size, D3DSPD_IUNKNOWN)
  HRESULT hr = d3d8::call<HRESULT>(resource, 4, &kWitnessId, static_cast<const void*>(w), static_cast<DWORD>(sizeof(void*)),
                                   static_cast<DWORD>(1));
  if (FAILED(hr)) {
    // (nothing would tell when it is gone: better never let go by the mod than let go twice)
    Locked lock;
    g_d3d.erase(resource);
    delete w;
    return;
  }
  witness_release(w);  // the resource holds it now
}

using CreateTexture_t = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
using CreateCube_t = HRESULT(WINAPI*)(void*, UINT, UINT, DWORD, DWORD, DWORD, void**);
using CreateBuffer_t = HRESULT(WINAPI*)(void*, UINT, DWORD, DWORD, DWORD, void**);
using CreateDepth_t = HRESULT(WINAPI*)(void*, UINT, UINT, DWORD, DWORD, void**);
using CreateImage_t = HRESULT(WINAPI*)(void*, UINT, UINT, DWORD, void**);
using CreateVertexShader_t = HRESULT(WINAPI*)(void*, const DWORD*, const DWORD*, DWORD*, DWORD);
using CreatePixelShader_t = HRESULT(WINAPI*)(void*, const DWORD*, DWORD*);
using DeleteShader_t = HRESULT(WINAPI*)(void*, DWORD);
CreateTexture_t oCreateTexture = nullptr;
CreateCube_t oCreateCube = nullptr;
CreateBuffer_t oCreateVertexBuffer = nullptr, oCreateIndexBuffer = nullptr;
CreateDepth_t oCreateDepth = nullptr;
CreateImage_t oCreateImage = nullptr;
CreateVertexShader_t oCreateVertexShader = nullptr;
CreatePixelShader_t oCreatePixelShader = nullptr;
DeleteShader_t oDeleteVertexShader = nullptr, oDeletePixelShader = nullptr;

HRESULT WINAPI hkCreateTexture(void* dev, UINT w, UINT h, UINT levels, DWORD usage, DWORD format, DWORD pool, void** out) {
  HRESULT hr = oCreateTexture(dev, w, h, levels, usage, format, pool, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateCube(void* dev, UINT edge, UINT levels, DWORD usage, DWORD format, DWORD pool, void** out) {
  HRESULT hr = oCreateCube(dev, edge, levels, usage, format, pool, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateVertexBuffer(void* dev, UINT length, DWORD usage, DWORD fvf, DWORD pool, void** out) {
  HRESULT hr = oCreateVertexBuffer(dev, length, usage, fvf, pool, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateIndexBuffer(void* dev, UINT length, DWORD usage, DWORD format, DWORD pool, void** out) {
  HRESULT hr = oCreateIndexBuffer(dev, length, usage, format, pool, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateDepth(void* dev, UINT w, UINT h, DWORD format, DWORD samples, void** out) {
  HRESULT hr = oCreateDepth(dev, w, h, format, samples, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateImage(void* dev, UINT w, UINT h, DWORD format, void** out) {
  HRESULT hr = oCreateImage(dev, w, h, format, out);
  if (SUCCEEDED(hr) && out) watch_resource(*out);
  return hr;
}
HRESULT WINAPI hkCreateVertexShader(void* dev, const DWORD* decl, const DWORD* code, DWORD* handle, DWORD usage) {
  HRESULT hr = oCreateVertexShader(dev, decl, code, handle, usage);
  if (SUCCEEDED(hr) && handle) {
    Locked lock;
    g_vertex_shaders[*handle] = !g_captured;
  }
  return hr;
}
HRESULT WINAPI hkCreatePixelShader(void* dev, const DWORD* code, DWORD* handle) {
  HRESULT hr = oCreatePixelShader(dev, code, handle);
  if (SUCCEEDED(hr) && handle) {
    Locked lock;
    g_pixel_shaders[*handle] = !g_captured;
  }
  return hr;
}
// A shader the kept memory knows stays: the game deletes it again and again after every restore.
bool keep_shader(std::unordered_map<DWORD, bool>& shaders, DWORD handle) {
  Locked lock;
  auto it = shaders.find(handle);
  if (it == shaders.end()) return false;
  if (it->second && g_captured) return true;
  shaders.erase(it);
  return false;
}
HRESULT WINAPI hkDeleteVertexShader(void* dev, DWORD handle) {
  return keep_shader(g_vertex_shaders, handle) ? S_OK : oDeleteVertexShader(dev, handle);
}
HRESULT WINAPI hkDeletePixelShader(void* dev, DWORD handle) {
  return keep_shader(g_pixel_shaders, handle) ? S_OK : oDeletePixelShader(dev, handle);
}

// --- DirectSound ----------------------------------------------------------------------------------------
// A sound buffer has no private data: the mod sees its last Release instead.
using SoundCreate_t = HRESULT(WINAPI*)(const GUID*, void**, void*);
using SoundBuffer_t = HRESULT(WINAPI*)(void*, const void*, void**, void*);
using SoundDuplicate_t = HRESULT(WINAPI*)(void*, void*, void**);
using Release_t = ULONG(WINAPI*)(void*);
SoundCreate_t oSoundCreate = nullptr;
SoundBuffer_t oSoundBuffer = nullptr;
SoundDuplicate_t oSoundDuplicate = nullptr;
// (the primary buffer and the others may be two classes: two Release functions at most)
uintptr_t g_release_at[2] = {};
Release_t oSoundRelease[2] = {};

ULONG sound_released(int which, void* buffer) {
  ULONG n = oSoundRelease[which](buffer);
  if (n == 0) {
    Locked lock;
    g_sound.erase(buffer);
  }
  return n;
}
ULONG WINAPI hkSoundRelease0(void* buffer) { return sound_released(0, buffer); }
ULONG WINAPI hkSoundRelease1(void* buffer) { return sound_released(1, buffer); }

void watch_buffer(void* buffer) {
  if (!buffer) return;
  uintptr_t release = reinterpret_cast<uintptr_t>(d3d8::vtable(buffer)[2]);
  int which = -1;
  for (int i = 0; i < 2; ++i) {
    if (g_release_at[i] == release) which = i;
    if (which < 0 && !g_release_at[i]) {
      if (patch::hook("a sound buffer's Release", release, i ? (void*)&hkSoundRelease1 : (void*)&hkSoundRelease0,
                      reinterpret_cast<void**>(&oSoundRelease[i]))) {
        g_release_at[i] = release;
        which = i;
      }
      break;
    }
  }
  if (which < 0) return;  // (a third class: left alone)
  Locked lock;
  g_sound[buffer] = Outside{!g_captured, 0};
}

HRESULT WINAPI hkSoundBuffer(void* ds, const void* desc, void** out, void* outer) {
  HRESULT hr = oSoundBuffer(ds, desc, out, outer);
  if (SUCCEEDED(hr) && out) watch_buffer(*out);
  return hr;
}
HRESULT WINAPI hkSoundDuplicate(void* ds, void* original, void** out) {
  HRESULT hr = oSoundDuplicate(ds, original, out);
  if (SUCCEEDED(hr) && out) watch_buffer(*out);
  return hr;
}
HRESULT WINAPI hkSoundCreate(const GUID* device, void** out, void* outer) {
  HRESULT hr = oSoundCreate(device, out, outer);
  if (SUCCEEDED(hr) && out && *out && !oSoundBuffer) {
    void** vt = d3d8::vtable(*out);
    patch::hook("IDirectSound::CreateSoundBuffer", reinterpret_cast<uintptr_t>(vt[3]), (void*)&hkSoundBuffer, (void**)&oSoundBuffer);
    patch::hook("IDirectSound::DuplicateSoundBuffer", reinterpret_cast<uintptr_t>(vt[5]), (void*)&hkSoundDuplicate,
                (void**)&oSoundDuplicate);
  }
  return hr;
}

// Objects of a table made after the memory was kept (they are nobody's after a restore).
std::vector<void*> orphans(std::unordered_map<void*, Outside>& table) {
  std::vector<void*> out;
  Locked lock;
  for (auto& e : table)
    if (!e.second.born_before) out.push_back(e.first);
  return out;
}

HANDLE event_handle(uintptr_t object_at) {
  auto obj = *reinterpret_cast<uint8_t**>(object_at);
  return obj ? *reinterpret_cast<HANDLE*>(obj + 4) : nullptr;
}

}  // namespace

bool ready() { return g_ready; }

void on_device(void* dev) {
  if (oCreateTexture || !dev) return;
  void** vt = d3d8::vtable(dev);
  auto at = [vt](int slot) { return reinterpret_cast<uintptr_t>(vt[slot]); };
  patch::hook("IDirect3DDevice8::CreateTexture", at(20), (void*)&hkCreateTexture, (void**)&oCreateTexture);
  patch::hook("IDirect3DDevice8::CreateCubeTexture", at(22), (void*)&hkCreateCube, (void**)&oCreateCube);
  patch::hook("IDirect3DDevice8::CreateVertexBuffer", at(23), (void*)&hkCreateVertexBuffer, (void**)&oCreateVertexBuffer);
  patch::hook("IDirect3DDevice8::CreateIndexBuffer", at(24), (void*)&hkCreateIndexBuffer, (void**)&oCreateIndexBuffer);
  patch::hook("IDirect3DDevice8::CreateDepthStencilSurface", at(26), (void*)&hkCreateDepth, (void**)&oCreateDepth);
  patch::hook("IDirect3DDevice8::CreateImageSurface", at(27), (void*)&hkCreateImage, (void**)&oCreateImage);
  patch::hook("IDirect3DDevice8::CreateVertexShader", at(75), (void*)&hkCreateVertexShader, (void**)&oCreateVertexShader);
  patch::hook("IDirect3DDevice8::DeleteVertexShader", at(78), (void*)&hkDeleteVertexShader, (void**)&oDeleteVertexShader);
  patch::hook("IDirect3DDevice8::CreatePixelShader", at(87), (void*)&hkCreatePixelShader, (void**)&oCreatePixelShader);
  patch::hook("IDirect3DDevice8::DeletePixelShader", at(90), (void*)&hkDeletePixelShader, (void**)&oDeletePixelShader);
}

void before_reset() {
  if (!g_ready) return;
  g_ready = false;
  std::vector<void*> held;
  {
    Locked lock;
    for (auto& e : g_d3d)
      if (e.second.born_before) held.push_back(e.first);
    for (auto& e : g_sound)
      if (e.second.born_before) held.push_back(e.first);
  }
  for (void* obj : held) d3d8::release(obj);
  if (void* dev = *reinterpret_cast<void**>(kDevice)) d3d8::call<HRESULT>(dev, 56 /* DeleteStateBlock */, g_state_block);
  g_state_block = 0;
  g_arena.clear();
  g_arena.shrink_to_fit();
  LOGW("anew: the device is being reset: the game's start is no longer kept (a session will start a new process)");
}

void capture() {
  if (g_captured) return;
  g_captured = true;
  if (g_cfg.lab_no_rewind) return;
  auto base = *reinterpret_cast<uint8_t**>(kArenaBase);
  auto end = *reinterpret_cast<uint8_t**>(kArenaEnd);
  void* dev = *reinterpret_cast<void**>(kDevice);
  if (*reinterpret_cast<const uint8_t*>(kUseMalloc) || !base || end <= base || static_cast<size_t>(end - base) > 0x10000000 ||
      !dev || !oCreateTexture || !oSoundCreate) {
    LOGW("anew: the game's memory is not laid out as expected: the game cannot begin anew in place (a session starts a new process)");
    return;
  }
  // The memory.
  g_arena_at = base;
  g_arena_size = static_cast<size_t>(end - base);
  size_t used = used_bytes(base, g_arena_size);
  g_arena.assign(base, base + used);
  g_globals.assign(reinterpret_cast<const uint8_t*>(kGlobalsBegin), reinterpret_cast<const uint8_t*>(kGlobalsEnd));
  for (uintptr_t at : kSoundBlocks) keep_block(at);
  if (auto banks = *reinterpret_cast<uint8_t**>(kSoundBanks))
    for (int b = 0; b < 3; ++b) keep_block(reinterpret_cast<uintptr_t>(banks + 4 * b));
  // What it points at outside itself.
  size_t resources = 0, buffers = 0;
  {
    Locked lock;
    for (void* surface : {*reinterpret_cast<void**>(kRenderTarget), *reinterpret_cast<void**>(kDepthSurface)})
      if (surface && !g_d3d.count(surface)) g_d3d[surface] = Outside{true, 0};
    for (auto& e : g_d3d) {
      e.second.born_before = true;
      e.second.count = add_ref(e.first);
      ++resources;
    }
    for (auto& e : g_sound) {
      e.second.born_before = true;
      e.second.count = add_ref(e.first);
      ++buffers;
    }
  }
  HRESULT hr = d3d8::call<HRESULT>(dev, 57 /* CreateStateBlock */, static_cast<DWORD>(1) /* D3DSBT_ALL */, &g_state_block);
  if (FAILED(hr) || !g_state_block) {
    LOGW("anew: the device gave no state block (0x%08X): the game cannot begin anew in place", static_cast<unsigned>(hr));
    return;
  }
  g_ready = true;
  LOGI("anew: the game's start is kept: %u KB of its variables, %u MB of its heap (of %u MB), %u sound blocks; held: %u "
       "Direct3D objects, %u sound buffers, %u + %u shaders",
       static_cast<unsigned>(g_globals.size() >> 10), static_cast<unsigned>(used >> 20), static_cast<unsigned>(g_arena_size >> 20),
       static_cast<unsigned>(g_blocks.size()), static_cast<unsigned>(resources), static_cast<unsigned>(buffers),
       static_cast<unsigned>(g_vertex_shaders.size()), static_cast<unsigned>(g_pixel_shaders.size()));
}

bool restore() {
  if (!g_ready) return false;
  auto base = *reinterpret_cast<uint8_t**>(kArenaBase);
  void* dev = *reinterpret_cast<void**>(kDevice);
  if (base != g_arena_at || !dev) return false;
  // The loading thread draws only while a level loads; it must be out of the game's frame.
  HANDLE running = event_handle(kThreadEvents);
  for (int i = 0; i < 200 && ((running && WaitForSingleObject(running, 0) == WAIT_OBJECT_0) || sim::background_frames() > 0); ++i) Sleep(10);
  if ((running && WaitForSingleObject(running, 0) == WAIT_OBJECT_0) || sim::background_frames() > 0) {
    LOGW("anew: a level is being loaded: the game does not begin anew now");
    return false;
  }
  DWORD t0 = GetTickCount();
  // A save being written is finished first (the kept memory knows no open stream).
  if (void* stream = *reinterpret_cast<void**>(kSaveStream)) {
    reinterpret_cast<int(__cdecl*)(void*)>(kStreamClose)(stream);
    *reinterpret_cast<void**>(kSaveStream) = nullptr;
  }
  // Sounds: everything stops; buffers made after the game's start go.
  size_t let_go = 0;
  {
    std::vector<void*> all;
    {
      Locked lock;
      for (auto& e : g_sound) all.push_back(e.first);
    }
    for (void* b : all) d3d8::call<HRESULT>(b, 18 /* Stop */);
    for (void* b : orphans(g_sound)) {
      while (d3d8::release(b) > 0) {
      }
      ++let_go;
    }
  }
  // The memory. The game's lock is the one thing in it that must stay as it is now.
  uint8_t lock_now[0x18];
  auto big_lock = *reinterpret_cast<uint8_t**>(kBigLock);
  if (big_lock) memcpy(lock_now, big_lock + 4, sizeof(lock_now));
  size_t dirty = used_bytes(base, g_arena_size);
  memcpy(reinterpret_cast<void*>(kGlobalsBegin), g_globals.data(), g_globals.size());
  memcpy(base, g_arena.data(), g_arena.size());
  if (dirty > g_arena.size()) memset(base + g_arena.size(), 0, dirty - g_arena.size());
  for (const Block& b : g_blocks) memcpy(b.at, b.bytes.data(), b.bytes.size());
  if (big_lock) memcpy(big_lock + 4, lock_now, sizeof(lock_now));
  // Events are not memory: a game that has just started has none of them set.
  if (HANDLE h = event_handle(kFadeEvent)) ResetEvent(h);
  for (int i = 0; i < 6; ++i)
    if (HANDLE h = event_handle(kThreadEvents + 4 * i)) ResetEvent(h);
  // The device is told what the kept memory believes it is set to; then what was made later goes.
  d3d8::call<HRESULT>(dev, 54 /* ApplyStateBlock */, g_state_block);
  d3d8::call<HRESULT>(dev, 31 /* SetRenderTarget */, *reinterpret_cast<void**>(kRenderTarget), *reinterpret_cast<void**>(kDepthSurface));
  for (void* r : orphans(g_d3d)) {
    while (d3d8::release(r) > 0) {
    }
    ++let_go;
  }
  std::vector<DWORD> vs, ps;
  {
    Locked lock;
    for (auto it = g_vertex_shaders.begin(); it != g_vertex_shaders.end();) {
      if (!it->second) {
        vs.push_back(it->first);
        it = g_vertex_shaders.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = g_pixel_shaders.begin(); it != g_pixel_shaders.end();) {
      if (!it->second) {
        ps.push_back(it->first);
        it = g_pixel_shaders.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (DWORD h : vs) oDeleteVertexShader(dev, h);
  for (DWORD h : ps) oDeletePixelShader(dev, h);
  // What was there at the start has its references of then.
  std::vector<std::pair<void*, ULONG>> held;
  {
    Locked lock;
    for (auto& e : g_d3d)
      if (e.second.born_before) held.push_back({e.first, e.second.count});
    for (auto& e : g_sound)
      if (e.second.born_before) held.push_back({e.first, e.second.count});
  }
  for (auto& h : held) set_count(h.first, h.second);
  PROCESS_MEMORY_COUNTERS_EX mem{};
  mem.cb = sizeof(mem);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem));
  LOGI("anew: the game is a just started game again: %u MB of heap put back, %u MB cleared, %u later objects let go, %u ms; "
       "the process holds %u MB",
       static_cast<unsigned>(g_arena.size() >> 20), static_cast<unsigned>((dirty > g_arena.size() ? dirty - g_arena.size() : 0) >> 20),
       static_cast<unsigned>(let_go + vs.size() + ps.size()), static_cast<unsigned>(GetTickCount() - t0),
       static_cast<unsigned>(mem.PrivateUsage >> 20));
  return true;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  InitializeCriticalSection(&g_lock);
  // DirectSoundCreate is the exe's import by number (DSOUND.dll #1): its slot is taken over.
  constexpr uintptr_t kSoundCreateSlot = 0x0055C008;
  auto now = patch::read<uintptr_t>(kSoundCreateSlot);
  if (now) {
    oSoundCreate = reinterpret_cast<SoundCreate_t>(now);
    uintptr_t mine = reinterpret_cast<uintptr_t>(&hkSoundCreate);
    if (!patch::write(kSoundCreateSlot, &mine, sizeof(mine))) oSoundCreate = nullptr;
  }
  if (!oSoundCreate) LOGW("anew: the game's DirectSound import is not where it should be");
}

}  // namespace anew
