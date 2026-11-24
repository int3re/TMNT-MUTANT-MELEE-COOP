#include "mod/notice.h"

#include <windows.h>

#include <mutex>
#include <string>

#include "common/log.h"

namespace notice {
namespace {

std::mutex g_mu;
std::wstring g_text;
DWORD g_until = 0;

}  // namespace

void install(const Config&) {}

void say(const wchar_t* text, int seconds) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_text = text ? text : L"";
  g_until = GetTickCount() + static_cast<DWORD>(seconds) * 1000;
  std::string flat;
  for (wchar_t c : g_text) flat += c == L'\n' ? '|' : (c < 128 ? static_cast<char>(c) : '?');
  LOGI("notice: %s", flat.c_str());
}

const wchar_t* current() {
  std::lock_guard<std::mutex> lk(g_mu);
  static std::wstring shown;
  shown = g_text.empty() || static_cast<int>(g_until - GetTickCount()) <= 0 ? L"" : g_text;
  return shown.c_str();
}

}  // namespace notice
