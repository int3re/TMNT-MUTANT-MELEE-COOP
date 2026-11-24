#include "mod/sound.h"

#include <windows.h>

#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace sound {
namespace {

Config g_cfg;

// --- the voices, by the game's clock -------------------------------------------------------------------
// The heroes' lines are one stream of sound: a new line is asked for only when no line is being said,
// and the game's rules ask "is a line being said?" all the time (who speaks next, when a screen may go
// on). The honest answer depends on the sound card: a line ends a frame earlier on one PC than on
// another, or never plays on a PC without sound - and the games part. So the mod answers by the game's
// clock: a line lasts exactly its length in the bank of lines (soundPC\VCLIP.BKT), counted in game
// frames from the frame it was asked for. Which of an event's lines is said is drawn from the mod's own
// numbers (the game draws it with the C library's rand(), which its sound code shares).
struct Event {
  uint16_t first;
  uint8_t chance, count;
};
struct Clip {
  uint32_t begin, end;
};
std::vector<Event> g_events;
std::vector<Clip> g_clips;
uint32_t g_busy_until = 0;  // game frame until which a line is being said
uint32_t g_seed = 1;
int g_picked = -1;          // the line chosen for the request being handled
bool g_handling = false;
// 22050 Hz, 16 bits, one channel.
constexpr uint32_t kBytesPerSecond = 44100;

uint32_t draw() {
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 16;
}

bool busy() { return sim::frame() < g_busy_until; }

void load_bank() {
  FILE* f = _wfopen((module_dir() + L"soundPC\\VCLIP.BKT").c_str(), L"rb");
  if (!f) {
    LOGW("sound: soundPC\\VCLIP.BKT cannot be read: the heroes' lines are left to the sound card");
    return;
  }
  uint32_t head[5] = {};
  if (fread(head, 4, 5, f) == 5 && head[0] < 0x100000 && head[1] < 5000 && head[2] < 50000 &&
      head[3] + head[1] * 4ull <= head[0] && head[4] + head[2] * 8ull <= head[0]) {
    std::vector<uint8_t> all(head[0]);
    fseek(f, 0, SEEK_SET);
    if (fread(all.data(), 1, all.size(), f) == all.size()) {
      g_events.resize(head[1]);
      g_clips.resize(head[2]);
      memcpy(g_events.data(), all.data() + head[3], head[1] * 4);
      memcpy(g_clips.data(), all.data() + head[4], head[2] * 8);
    }
  }
  fclose(f);
  LOGI("sound: the bank of lines: %u events, %u lines", (unsigned)g_events.size(), (unsigned)g_clips.size());
}

// The game asks for a line of `event`: which line it is (or -1: none), and the stream is taken for its length.
int pick(int event) {
  if (event < 0 || event >= static_cast<int>(g_events.size()) || busy()) return -1;
  const Event& e = g_events[event];
  if (!e.count) return -1;
  if ((draw() & 0xFF) > e.chance) return -1;
  uint32_t clip = e.first + draw() % e.count;
  if (clip >= g_clips.size() || g_clips[clip].end <= g_clips[clip].begin) return -1;
  uint32_t bytes = g_clips[clip].end - g_clips[clip].begin;
  uint32_t frames = static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 60 + kBytesPerSecond - 1) / kBytesPerSecond);
  g_busy_until = sim::frame() + frames + 6;  // (and a breath between two lines)
  LOGD("sound: line %u of event %d, %u bytes = %u frames", clip, event, bytes, frames);
  return static_cast<int>(clip);
}

using Command_t = uint32_t(__cdecl*)(int, int*, int);
using VoiceStart_t = uint16_t(__cdecl*)(int);
Command_t oCommand = nullptr;
VoiceStart_t oVoiceStart = nullptr;

// The sound system's one door (0x4C44E0): command, arguments.
uint32_t __cdecl hkCommand(int cmd, int* args, int n) {
  if (g_events.empty() || !args) return oCommand(cmd, args, n);
  if (cmd == game::kSndVoiceBusy && args[0] == 0) return busy() ? 1 : 0;
  if (cmd == game::kSndVoicePlay) {
    g_picked = pick(args[0]);
    g_handling = true;
    uint32_t r = oCommand(cmd, args, n);  // (does nothing on a PC without sound)
    g_handling = false;
    return r;
  }
  return oCommand(cmd, args, n);
}

// The game's choice of a line and start of the stream (0x4DEFD0), reached from the command above.
uint16_t __cdecl hkVoiceStart(int event) {
  if (g_events.empty() || !g_handling) return oVoiceStart(event);
  if (g_picked < 0) return 0xFF;
  auto stream = *reinterpret_cast<uint8_t**>(game::kVoiceStream);
  if (!stream || *reinterpret_cast<int*>(stream + 0x80) != 2) return 0xFF;  // this PC's card is still at the line before
  const Clip& c = g_clips[g_picked];
  reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, int, int, int)>(game::kStreamSetup)(
      stream, *reinterpret_cast<uint32_t*>(game::kVoiceFile), c.begin, c.end, 0, 0, 0);
  reinterpret_cast<void(__cdecl*)(void*)>(game::kStreamPlay)(stream);
  return *reinterpret_cast<uint16_t*>(stream + 0x94);
}

// [debug] mute: this process's audio session is muted (Windows keeps that per exe path:
// a lab copy's, never the player's game).
void mute_process() {
  CoInitialize(nullptr);
  IMMDeviceEnumerator* devices = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager* manager = nullptr;
  ISimpleAudioVolume* volume = nullptr;
  bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&devices))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&manager))) &&
            SUCCEEDED(manager->GetSimpleAudioVolume(nullptr, FALSE, &volume)) && SUCCEEDED(volume->SetMute(TRUE, nullptr));
  LOGI("sound: this copy is %s", ok ? "muted ([debug] mute)" : "NOT muted: the audio session could not be reached");
  if (volume) volume->Release();
  if (manager) manager->Release();
  if (device) device->Release();
  if (devices) devices->Release();
}

}  // namespace

void on_boot() {
  g_busy_until = 0;
  g_seed = 1;
}

void on_epoch(uint32_t seed) {
  g_busy_until = 0;
  g_seed = seed ^ 0x564F4943;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  if (cfg.mute) mute_process();
  if (cfg.free_clock) return;
  load_bank();
  static const uint8_t cmd_head[] = {0x55, 0x56, 0x8B, 0x74, 0x24, 0x10};          // push ebp; push esi; mov esi, [esp+0x10]
  static const uint8_t voice_head[] = {0xA1, 0x4C, 0x27, 0x68, 0x00, 0x83, 0xB8};  // mov eax, [0x68274C]; cmp [eax+..
  if (!patch::bytes_are(game::kSoundCommand, cmd_head, sizeof(cmd_head)) ||
      !patch::bytes_are(game::kVoiceStart, voice_head, sizeof(voice_head))) {
    LOGE("sound: the game's sound commands are not where they should be: the heroes' lines are left to the sound card");
    g_events.clear();
    return;
  }
  patch::hook("the game's sound commands", game::kSoundCommand, (void*)&hkCommand, (void**)&oCommand);
  patch::hook("the heroes' lines", game::kVoiceStart, (void*)&hkVoiceStart, (void**)&oVoiceStart);
}

}  // namespace sound
