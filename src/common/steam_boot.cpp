// Picks real Steam or the lab mock and hands out the matching transport and
// lobby objects. Real Steam lives in steam_real.cpp / lobby_steam.cpp (or
// steam_stub.cpp without the SDK), the mock in steam_mock.cpp.
#include "common/steam_boot.h"

#include <cctype>
#include <cstdlib>

#include "common/steam_backend.h"
#include "common/transport_steam.h"

namespace steam {
namespace {
enum class Mode { Off, Real, Mock } g_mode = Mode::Off;
std::string g_reason = "Steam ещё не запускался";
}  // namespace

bool boot(const Config& cfg) {
  if (g_mode != Mode::Off) return true;
  if (cfg.net_transport == "mock") {
    if (!mock::boot(cfg)) return false;
    g_mode = Mode::Mock;
  } else {
    if (!real::boot()) {
      g_reason = real::unavailable_reason();
      return false;
    }
    g_mode = Mode::Real;
  }
  g_reason.clear();
  return true;
}

bool ready() { return g_mode != Mode::Off; }
bool is_mock() { return g_mode == Mode::Mock; }

void pump() {
  if (g_mode == Mode::Real) real::pump();
  if (g_mode == Mode::Mock) mock::pump();
}

void shutdown() {
  if (g_mode == Mode::Real) real::shutdown();
  if (g_mode == Mode::Mock) mock::shutdown();
  g_mode = Mode::Off;
}

std::string unavailable_reason() { return g_reason; }

uint64_t my_id() {
  return g_mode == Mode::Real ? real::my_id() : g_mode == Mode::Mock ? mock::my_id() : 0;
}

std::string my_name() {
  return g_mode == Mode::Real ? real::my_name() : g_mode == Mode::Mock ? mock::my_name() : "";
}

uint64_t parse_lobby_id(const std::string& s) {
  // The first run of 17-18 digits whose top bits say "public universe, chat".
  for (size_t i = 0; i < s.size(); ++i) {
    size_t j = i;
    while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
    if (j - i >= 17 && j - i <= 18) {
      uint64_t id = std::strtoull(s.substr(i, j - i).c_str(), nullptr, 10);
      if ((id >> 52) == 0x018) return id;
    }
    if (j > i) i = j;
  }
  return 0;
}

}  // namespace steam

namespace transport {
std::unique_ptr<ITransport> make_steam_transport(AcceptFn accept) {
  if (!steam::ready()) return nullptr;
  auto msgs = steam::is_mock() ? steam::mock::messages() : steam::real::messages();
  if (!msgs) return nullptr;
  return make_steam_transport_over(std::move(msgs), std::move(accept));
}
}  // namespace transport

namespace lobby {
std::unique_ptr<IHost> make_host(const Config& cfg, const std::string& exe16) {
  if (!steam::ready()) return nullptr;
  return steam::is_mock() ? steam::mock::host(cfg, exe16) : steam::real::host(cfg, exe16);
}
std::unique_ptr<IBrowser> make_browser(const std::string& my_exe16) {
  if (!steam::ready()) return nullptr;
  return steam::is_mock() ? steam::mock::browser(my_exe16) : steam::real::browser(my_exe16);
}
std::unique_ptr<IInvites> make_invites() {
  if (!steam::ready() || steam::is_mock()) return nullptr;
  return steam::real::invites();
}
}  // namespace lobby
