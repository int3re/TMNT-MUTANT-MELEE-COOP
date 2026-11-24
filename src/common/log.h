// Line log: "[HH:MM:SS.mmm][f=frame] LEVEL message", fflush after each line,
// mirrored to OutputDebugStringA (and to the console if enabled).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace tlog {

enum Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void open(const std::wstring& path, Level level, bool echo_console);
void set_frame_counter(const std::atomic<uint32_t>* frame);
Level parse_level(const std::string& s, Level fallback);
bool enabled(Level level);
void write(Level level, const char* fmt, ...);

}  // namespace tlog

#define LOGE(...) ::tlog::write(::tlog::Error, __VA_ARGS__)
#define LOGW(...) ::tlog::write(::tlog::Warn, __VA_ARGS__)
#define LOGI(...) ::tlog::write(::tlog::Info, __VA_ARGS__)
#define LOGD(...) do { if (::tlog::enabled(::tlog::Debug)) ::tlog::write(::tlog::Debug, __VA_ARGS__); } while (0)
