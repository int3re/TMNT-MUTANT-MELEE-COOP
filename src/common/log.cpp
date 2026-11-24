#include "common/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace tlog {
namespace {

CRITICAL_SECTION g_lock;
bool g_lock_init = false;
FILE* g_file = nullptr;
Level g_level = Info;
bool g_console = false;
const std::atomic<uint32_t>* g_frame = nullptr;

const char* level_name(Level l) {
  switch (l) {
    case Error: return "ERROR";
    case Warn: return "WARN ";
    case Info: return "INFO ";
    default: return "DEBUG";
  }
}

}  // namespace

void open(const std::wstring& path, Level level, bool echo_console) {
  if (!g_lock_init) {
    InitializeCriticalSection(&g_lock);
    g_lock_init = true;
  }
  EnterCriticalSection(&g_lock);
  g_level = level;
  g_console = echo_console;
  if (g_file) fclose(g_file);
  // Shared read so the log can be tailed while the game runs.
  g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
  LeaveCriticalSection(&g_lock);
}

void set_frame_counter(const std::atomic<uint32_t>* frame) { g_frame = frame; }

Level parse_level(const std::string& s, Level fallback) {
  if (s == "error") return Error;
  if (s == "warn") return Warn;
  if (s == "info") return Info;
  if (s == "debug") return Debug;
  return fallback;
}

bool enabled(Level level) { return level <= g_level; }

void write(Level level, const char* fmt, ...) {
  if (!enabled(level) || !g_lock_init) return;

  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  SYSTEMTIME st;
  GetLocalTime(&st);
  char line[1200];
  unsigned frame = g_frame ? g_frame->load(std::memory_order_relaxed) : 0;
  snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u][f=%u] %s %s\n", st.wHour,
           st.wMinute, st.wSecond, st.wMilliseconds, frame, level_name(level), msg);

  EnterCriticalSection(&g_lock);
  if (g_file) {
    fputs(line, g_file);
    fflush(g_file);
  }
  if (g_console) {
    fputs("\r\x1b[2K", stdout);  // wipe a status line drawn with '\r'
    fputs(line, stdout);
  }
  LeaveCriticalSection(&g_lock);
  OutputDebugStringA(line);
}

}  // namespace tlog
