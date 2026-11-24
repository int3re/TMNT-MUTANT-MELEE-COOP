// Real Steam lobbies (ISteamMatchmaking / ISteamFriends). Built only with the
// Steamworks SDK; the lab mock is in steam_mock.cpp.
#include "common/lobby_steam.h"

#include <windows.h>

#include <steam/steam_api.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/steam_backend.h"
#include "common/steam_boot.h"

namespace steam {
namespace real {
namespace {

using lobby::Found;
using lobby::Friend;
using lobby::JoinResult;

constexpr int kMaxMembers = 4;  // the game's four pads
constexpr uint32_t kCreateRetryMs = 10000;
constexpr uint32_t kFriendsEveryMs = 3000;
constexpr uint32_t kSearchEveryMs = 5000;
constexpr uint32_t kForgetAfterMs = 12000;

std::string lobby_data(CSteamID lobby, const char* key) {
  const char* v = SteamMatchmaking()->GetLobbyData(lobby, key);
  return v ? v : "";
}

std::string persona(CSteamID id) {
  const char* n = SteamFriends()->GetFriendPersonaName(id);
  return n ? n : "";
}

// --- host ----------------------------------------------------------------------

class Host final : public lobby::IHost {
 public:
  Host(const Config& cfg, const std::string& exe16)
      : name_(cfg.net_name), exe16_(exe16), public_(cfg.net_lobby == "public") {
    create();
  }
  ~Host() override {
    if (lobby_.IsValid()) {
      SteamMatchmaking()->LeaveLobby(lobby_);
      LOGI("Steam lobby %llu closed", (unsigned long long)lobby_.ConvertToUint64());
    }
    SteamFriends()->ClearRichPresence();
  }

  void Tick(uint32_t now) override {
    if (!lobby_.IsValid() && !creating_ && now - failed_at_ >= kCreateRetryMs) create();
  }

  bool IsMember(uint64_t id) const override {
    if (!lobby_.IsValid()) return false;
    int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
    for (int i = 0; i < n; ++i)
      if (SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64() == id) return true;
    return false;
  }

  void SetInGame(bool has_p2) override {
    in_game_ = has_p2;
    if (!lobby_.IsValid()) return;
    SteamMatchmaking()->SetLobbyData(lobby_, "state", has_p2 ? "ingame" : "lobby");
    SteamFriends()->SetRichPresence("status", has_p2 ? "TMNT Mutant Melee: игра по сети" : "TMNT Mutant Melee: ждёт игроков");
  }

  uint64_t LobbyId() const override { return lobby_.ConvertToUint64(); }

  bool TakeLeft(uint64_t& id) override {
    if (left_.empty()) return false;
    id = left_.front();
    left_.erase(left_.begin());
    return true;
  }

  std::vector<Friend> OnlineFriends() override {
    std::vector<Friend> out;
    ISteamFriends* fr = SteamFriends();
    int n = fr->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < n; ++i) {
      CSteamID f = fr->GetFriendByIndex(i, k_EFriendFlagImmediate);
      if (fr->GetFriendPersonaState(f) == k_EPersonaStateOffline) continue;
      Friend x;
      x.id = f.ConvertToUint64();
      x.name = persona(f);
      FriendGameInfo_t g{};
      x.playing_480 = fr->GetFriendGamePlayed(f, &g) && g.m_gameID.AppID() == kAppId;
      out.push_back(x);
    }
    std::sort(out.begin(), out.end(), [](const Friend& a, const Friend& b) {
      return a.playing_480 != b.playing_480 ? a.playing_480 : a.name < b.name;
    });
    return out;
  }

  void Invite(uint64_t friend_id) override {
    if (!lobby_.IsValid()) return;
    bool ok = SteamMatchmaking()->InviteUserToLobby(lobby_, CSteamID(friend_id));
    LOGI("Steam: invite to '%s' (%llu) %s", persona(CSteamID(friend_id)).c_str(),
         (unsigned long long)friend_id, ok ? "sent" : "failed");
  }

 private:
  void create() {
    creating_ = true;
    SteamAPICall_t call =
        SteamMatchmaking()->CreateLobby(public_ ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly, kMaxMembers);
    created_.Set(call, this, &Host::OnCreated);
  }

  void OnCreated(LobbyCreated_t* p, bool io_failure) {
    creating_ = false;
    if (io_failure || p->m_eResult != k_EResultOK) {
      failed_at_ = net::now_ms();
      LOGW("Steam lobby creation failed (EResult %d%s); retrying in %u s", io_failure ? -1 : (int)p->m_eResult,
           io_failure ? ", I/O failure" : "", kCreateRetryMs / 1000);
      return;
    }
    if (lobby_.IsValid()) return;  // Steam can report a created lobby twice
    lobby_ = CSteamID(p->m_ulSteamIDLobby);
    ISteamMatchmaking* mm = SteamMatchmaking();
    char buf[64];
    mm->SetLobbyData(lobby_, "meleenet", "1");
    snprintf(buf, sizeof(buf), "%u", mmp::kVersion);
    mm->SetLobbyData(lobby_, "proto", buf);
    mm->SetLobbyData(lobby_, "exe", exe16_.c_str());
    mm->SetLobbyData(lobby_, "host", my_name().c_str());
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)my_id());
    mm->SetLobbyData(lobby_, "hostid", buf);
    mm->SetLobbyData(lobby_, "name", name_.c_str());
    // "Join game" in the friends list then hands the lobby to a running client.
    snprintf(buf, sizeof(buf), "+connect_lobby %llu", (unsigned long long)p->m_ulSteamIDLobby);
    SteamFriends()->SetRichPresence("connect", buf);
    SetInGame(in_game_);
    LOGI("Steam lobby %llu created (%s, %d places); host SteamID64 %llu", (unsigned long long)p->m_ulSteamIDLobby,
         public_ ? "public" : "friends only", kMaxMembers, (unsigned long long)my_id());
  }

  STEAM_CALLBACK(Host, OnChatUpdate, LobbyChatUpdate_t);

  std::string name_, exe16_;
  bool public_;
  bool creating_ = false, in_game_ = false;
  uint32_t failed_at_ = 0;
  CSteamID lobby_;
  std::vector<uint64_t> left_;
  CCallResult<Host, LobbyCreated_t> created_;
};

void Host::OnChatUpdate(LobbyChatUpdate_t* p) {
  if (p->m_ulSteamIDLobby != lobby_.ConvertToUint64()) return;
  CSteamID who(p->m_ulSteamIDUserChanged);
  bool entered = (p->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) != 0;
  LOGI("Steam lobby: '%s' (%llu) %s", persona(who).c_str(), (unsigned long long)p->m_ulSteamIDUserChanged,
       entered ? "joined" : "left");
  if (!entered) left_.push_back(p->m_ulSteamIDUserChanged);
}

// --- client --------------------------------------------------------------------

class Browser final : public lobby::IBrowser {
 public:
  explicit Browser(const std::string& my_exe16) : my_exe16_(my_exe16) {}
  ~Browser() override { Leave(); }

  void Tick(uint32_t now, bool searching) override {
    now_ = now;
    if (!searching) return;
    if (now - last_friends_ >= kFriendsEveryMs) {
      last_friends_ = now;
      scan_friends();
    }
    if (!searching_ && now - last_search_ >= kSearchEveryMs) {
      last_search_ = now;
      search();
    }
    list_.erase(std::remove_if(list_.begin(), list_.end(),
                               [now](const Found& f) { return now - f.seen_ms > kForgetAfterMs; }),
                list_.end());
  }

  std::vector<Found> List() const override { return list_; }

  void Join(uint64_t lobby) override {
    Leave();
    LOGI("Steam: joining lobby %llu", (unsigned long long)lobby);
    entered_.Set(SteamMatchmaking()->JoinLobby(CSteamID(lobby)), this, &Browser::OnEntered);
  }

  void JoinHost(uint64_t id) override {
    FriendGameInfo_t g{};
    CSteamID host(id);
    if (SteamFriends()->GetFriendGamePlayed(host, &g) && g.m_gameID.AppID() == kAppId &&
        g.m_steamIDLobby.IsValid()) {
      LOGI("Steam: host %llu is a friend in lobby %llu", (unsigned long long)id,
           (unsigned long long)g.m_steamIDLobby.ConvertToUint64());
      Join(g.m_steamIDLobby.ConvertToUint64());
      return;
    }
    LOGI("Steam: host %llu has no visible lobby; direct session (the host needs allow_steamid)",
         (unsigned long long)id);
    Leave();
    finish(true, 0, id, "");
  }

  void Leave() override {
    if (!joined_.IsValid()) return;
    SteamMatchmaking()->LeaveLobby(joined_);
    LOGI("Steam: left lobby %llu", (unsigned long long)joined_.ConvertToUint64());
    joined_ = CSteamID();
  }

  bool TakeJoinResult(JoinResult& out) override {
    if (!have_result_) return false;
    out = result_;
    have_result_ = false;
    return true;
  }

 private:
  void finish(bool ok, uint64_t lobby, uint64_t host_id, const std::string& error) {
    result_ = JoinResult{};
    result_.ok = ok;
    result_.lobby = lobby;
    result_.host_id = host_id;
    result_.host = host_id ? persona(CSteamID(host_id)) : "";
    result_.error = error;
    have_result_ = true;
    if (!ok) LOGW("Steam: cannot join lobby %llu: %s", (unsigned long long)lobby, error.c_str());
  }

  // Fills a list entry from lobby data we can read (search results, or
  // lobbies of friends after RequestLobbyData).
  void upsert(CSteamID lobby, bool via_friend) {
    if (lobby_data(lobby, "meleenet") != "1") return;  // somebody's Spacewar lobby
    uint64_t id = lobby.ConvertToUint64();
    auto it = std::find_if(list_.begin(), list_.end(), [id](const Found& f) { return f.lobby == id; });
    if (it == list_.end()) {
      list_.push_back(Found{});
      it = list_.end() - 1;
      it->lobby = id;
    }
    it->host_id = std::strtoull(lobby_data(lobby, "hostid").c_str(), nullptr, 10);
    it->host = lobby_data(lobby, "host");
    it->name = lobby_data(lobby, "name");
    it->state = lobby_data(lobby, "state");
    it->is_friend = it->is_friend || via_friend;
    it->seen_ms = now_;
    std::string proto = lobby_data(lobby, "proto"), exe = lobby_data(lobby, "exe");
    it->compatible = false;
    if (proto != std::to_string(mmp::kVersion))
      it->why_not = "другая версия meleenet (протокол " + proto + ")";
    else if (!my_exe16_.empty() && !exe.empty() && exe != my_exe16_)
      it->why_not = "другой ztmnt.exe";
    else if (it->host_id == my_id())
      it->why_not = "это вы";
    else
      it->compatible = true;
    if (it->compatible) it->why_not.clear();
  }

  void scan_friends() {
    ISteamFriends* fr = SteamFriends();
    int n = fr->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < n; ++i) {
      CSteamID f = fr->GetFriendByIndex(i, k_EFriendFlagImmediate);
      FriendGameInfo_t g{};
      if (!fr->GetFriendGamePlayed(f, &g) || g.m_gameID.AppID() != kAppId || !g.m_steamIDLobby.IsValid())
        continue;
      uint64_t id = g.m_steamIDLobby.ConvertToUint64();
      if (std::find(friend_lobbies_.begin(), friend_lobbies_.end(), id) == friend_lobbies_.end())
        friend_lobbies_.push_back(id);
      SteamMatchmaking()->RequestLobbyData(g.m_steamIDLobby);  // answer: OnLobbyData
    }
  }

  void search() {
    ISteamMatchmaking* mm = SteamMatchmaking();
    mm->AddRequestLobbyListStringFilter("meleenet", "1", k_ELobbyComparisonEqual);
    mm->AddRequestLobbyListNumericalFilter("proto", mmp::kVersion, k_ELobbyComparisonEqual);
    mm->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    mm->AddRequestLobbyListResultCountFilter(50);
    searching_ = true;
    listed_.Set(mm->RequestLobbyList(), this, &Browser::OnList);
  }

  void OnList(LobbyMatchList_t* p, bool io_failure) {
    searching_ = false;
    if (io_failure) return;
    for (uint32 i = 0; i < p->m_nLobbiesMatching; ++i) upsert(SteamMatchmaking()->GetLobbyByIndex((int)i), false);
  }

  void OnEntered(LobbyEnter_t* p, bool io_failure) {
    CSteamID lobby(p->m_ulSteamIDLobby);
    if (io_failure) {
      finish(false, p->m_ulSteamIDLobby, 0, "Steam не ответил при входе в лобби");
      return;
    }
    switch (p->m_EChatRoomEnterResponse) {
      case k_EChatRoomEnterResponseSuccess: break;
      case k_EChatRoomEnterResponseDoesntExist:
        finish(false, p->m_ulSteamIDLobby, 0, "Лобби больше нет: хост вышел из игры.");
        return;
      case k_EChatRoomEnterResponseFull:
        finish(false, p->m_ulSteamIDLobby, 0, "В этой игре уже все четыре игрока.");
        return;
      case k_EChatRoomEnterResponseNotAllowed:
        finish(false, p->m_ulSteamIDLobby, 0, "Лобби только для друзей хоста: добавьте друг друга в друзья Steam.");
        return;
      case k_EChatRoomEnterResponseLimited:
        finish(false, p->m_ulSteamIDLobby, 0, "Ограниченный аккаунт Steam не может входить в лобби.");
        return;
      default:
        finish(false, p->m_ulSteamIDLobby, 0,
               "Steam не пустил в лобби (код " + std::to_string(p->m_EChatRoomEnterResponse) + ").");
        return;
    }
    joined_ = lobby;
    if (lobby_data(lobby, "meleenet") != "1") {
      Leave();
      finish(false, p->m_ulSteamIDLobby, 0, "Это не лобби meleenet: игрок в Steam запустил что-то другое.");
      return;
    }
    CSteamID owner = SteamMatchmaking()->GetLobbyOwner(lobby);
    LOGI("Steam: in lobby %llu, host '%s' (%llu)", (unsigned long long)p->m_ulSteamIDLobby, persona(owner).c_str(),
         (unsigned long long)owner.ConvertToUint64());
    finish(true, p->m_ulSteamIDLobby, owner.ConvertToUint64(), "");
  }

  STEAM_CALLBACK(Browser, OnLobbyData, LobbyDataUpdate_t);
  STEAM_CALLBACK(Browser, OnJoinRequested, GameLobbyJoinRequested_t);
  STEAM_CALLBACK(Browser, OnRichPresenceJoin, GameRichPresenceJoinRequested_t);

  std::string my_exe16_;
  std::vector<Found> list_;
  std::vector<uint64_t> friend_lobbies_;  // asked for data, not answered yet
  uint32_t now_ = 0, last_friends_ = 0, last_search_ = 0;
  bool searching_ = false;
  CSteamID joined_;
  JoinResult result_;
  bool have_result_ = false;
  CCallResult<Browser, LobbyMatchList_t> listed_;
  CCallResult<Browser, LobbyEnter_t> entered_;
};

void Browser::OnLobbyData(LobbyDataUpdate_t* p) {
  if (p->m_ulSteamIDLobby != p->m_ulSteamIDMember || !p->m_bSuccess) return;  // member data / gone
  auto it = std::find(friend_lobbies_.begin(), friend_lobbies_.end(), p->m_ulSteamIDLobby);
  bool via_friend = it != friend_lobbies_.end();
  if (via_friend) friend_lobbies_.erase(it);
  upsert(CSteamID(p->m_ulSteamIDLobby), via_friend);
}

// An accepted invite, or "Join game" on a friend in a lobby.
void Browser::OnJoinRequested(GameLobbyJoinRequested_t* p) {
  LOGI("Steam: lobby %llu from '%s' (invite or \"Join game\")",
       (unsigned long long)p->m_steamIDLobby.ConvertToUint64(), persona(p->m_steamIDFriend).c_str());
  Join(p->m_steamIDLobby.ConvertToUint64());
}

// "Join game" on a friend whose rich presence has connect=+connect_lobby <id>.
void Browser::OnRichPresenceJoin(GameRichPresenceJoinRequested_t* p) {
  uint64_t lobby = parse_lobby_id(p->m_rgchConnect);
  LOGI("Steam: \"Join game\" on '%s': '%s'", persona(p->m_steamIDFriend).c_str(), p->m_rgchConnect);
  if (lobby) Join(lobby);
}

// Steam's join requests while the game plays alone (no browser yet).
class Invites final : public lobby::IInvites {
 public:
  bool Take(uint64_t& lobby) override {
    if (!lobby_) return false;
    lobby = lobby_;
    lobby_ = 0;
    return true;
  }

 private:
  STEAM_CALLBACK(Invites, OnJoinRequested, GameLobbyJoinRequested_t);
  STEAM_CALLBACK(Invites, OnRichPresenceJoin, GameRichPresenceJoinRequested_t);
  uint64_t lobby_ = 0;
};

void Invites::OnJoinRequested(GameLobbyJoinRequested_t* p) {
  LOGI("Steam: lobby %llu from '%s' (invite or \"Join game\") while playing alone",
       (unsigned long long)p->m_steamIDLobby.ConvertToUint64(), persona(p->m_steamIDFriend).c_str());
  lobby_ = p->m_steamIDLobby.ConvertToUint64();
}

void Invites::OnRichPresenceJoin(GameRichPresenceJoinRequested_t* p) {
  LOGI("Steam: \"Join game\" on '%s' while playing alone: '%s'", persona(p->m_steamIDFriend).c_str(),
       p->m_rgchConnect);
  if (uint64_t lobby = parse_lobby_id(p->m_rgchConnect)) lobby_ = lobby;
}

}  // namespace

std::unique_ptr<lobby::IHost> host(const Config& cfg, const std::string& exe16) {
  if (!SteamMatchmaking() || !SteamFriends()) return nullptr;
  return std::make_unique<Host>(cfg, exe16);
}

std::unique_ptr<lobby::IBrowser> browser(const std::string& my_exe16) {
  if (!SteamMatchmaking() || !SteamFriends()) return nullptr;
  return std::make_unique<Browser>(my_exe16);
}

std::unique_ptr<lobby::IInvites> invites() {
  if (!SteamMatchmaking() || !SteamFriends()) return nullptr;
  return std::make_unique<Invites>();
}

}  // namespace real
}  // namespace steam
