// The mod as it travels: inside a launcher exe, as resources. This launcher carries one version of the mod;
// a newer one comes as a newer launcher file, which is opened as data (never run) and installed from.
//   resource RES_PKG_INDEX: a text list of what the package holds (below)
//   resource RES_PKG_INFO:  dist/release.json of that version (title, notes, protocols)
//   resources RES_PKG_FILES..: the files
// Installing puts the files next to ztmnt.exe; ztmnt.exe and the game's data are never touched. Every
// installed file and every backup is listed in meleenet-install.txt in the game folder; removing the mod
// removes exactly those and puts the backups back.
#include "launcher/launcher.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include "common/sha256.h"
#include "launcher/res.h"

namespace lz {
namespace pkg {
namespace {

const wchar_t kList[] = L"meleenet-install.txt";
const wchar_t kBackupDir[] = L"meleenet-backup";
const wchar_t kExe[] = L"ztmnt.exe";

using core::exists;
using core::join;

std::wstring error_text(DWORD err) {
  wchar_t* buf = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                 reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
  std::wstring s = buf ? buf : L"";
  if (buf) LocalFree(buf);
  while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L'.')) s.pop_back();
  return s + L" (" + std::to_wstring(err) + L")";
}

std::string read_all(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool write_file(const std::wstring& path, const void* data, DWORD size, DWORD& err) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    err = GetLastError();
    return false;
  }
  DWORD written = 0;
  bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
  err = ok ? 0 : GetLastError();
  CloseHandle(f);
  return ok;
}

bool plain_name(const std::wstring& n) {  // a file's own name, nothing of a path in it
  if (n.empty() || n.size() > 64 || n[0] == L'.') return false;
  for (wchar_t ch : n)
    if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'.' || ch == L'-' ||
          ch == L'_'))
      return false;
  return true;
}

// What the launcher put into a game folder, and what it moved aside to do so.
struct List {
  std::vector<std::wstring> files;
  std::vector<std::pair<std::wstring, std::wstring>> backups;  // original -> backup
  std::wstring version;  // of the mod that was put there (empty: not known)

  bool load(const std::wstring& dir) {
    std::ifstream in(join(dir, kList), std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
      if (line.rfind("# meleenet ", 0) == 0) version = core::wide(line.substr(11, line.find(' ', 11) - 11));
      if (line.rfind("file\t", 0) == 0) files.push_back(core::wide(line.substr(5)));
      if (line.rfind("backup\t", 0) == 0) {
        size_t tab = line.find('\t', 7);
        if (tab != std::string::npos) backups.emplace_back(core::wide(line.substr(7, tab - 7)), core::wide(line.substr(tab + 1)));
      }
    }
    return true;
  }
  bool save(const std::wstring& dir, const Package& p) const {
    std::ofstream out(join(dir, kList), std::ios::binary);
    out << "# meleenet " << core::narrow(p.version) << " (protocol " << p.protocol
        << "). meleenet-launcher.exe removes exactly these files.\n";
    for (auto& f : files) out << "file\t" << core::narrow(f) << "\n";
    for (auto& b : backups) out << "backup\t" << core::narrow(b.first) << "\t" << core::narrow(b.second) << "\n";
    return static_cast<bool>(out);
  }
  bool has(const std::wstring& name) const {
    for (auto& f : files)
      if (_wcsicmp(f.c_str(), name.c_str()) == 0) return true;
    return false;
  }
};

bool same_bytes(const std::wstring& path, const void* data, DWORD size) {
  if (!data) return false;
  std::string have = read_all(path);
  return have.size() == size && std::memcmp(have.data(), data, size) == 0;
}

// Puts one file into the folder. A file of that name that is not ours is moved to meleenet-backup\ first.
Result put(const std::wstring& dir, const std::wstring& name, const void* data, DWORD size, List& list, std::wstring& log) {
  std::wstring dst = join(dir, name);
  bool ours = list.has(name) || _wcsicmp(name.c_str(), L"meleenet.asi") == 0 || _wcsicmp(name.c_str(), L"meleenet-README.md") == 0 ||
              same_bytes(dst, data, size);
  if (exists(dst) && !ours) {
    std::wstring bdir = join(dir, kBackupDir);
    CreateDirectoryW(bdir.c_str(), nullptr);
    if (!MoveFileExW(dst.c_str(), join(bdir, name).c_str(), MOVEFILE_REPLACE_EXISTING)) {
      DWORD e = GetLastError();
      log += L"не удалось сохранить прежний " + name + L": " + error_text(e) + L"\n";
      return e == ERROR_ACCESS_DENIED ? Result::Denied : Result::Failed;
    }
    list.backups.emplace_back(name, std::wstring(kBackupDir) + L"\\" + name);
    log += L"прежний " + name + L" сохранён в " + kBackupDir + L"\\\n";
  }
  DWORD err = 0;
  if (!write_file(dst, data, size, err)) {
    log += L"не удалось записать " + name + L": " + error_text(err) + L"\n";
    return err == ERROR_ACCESS_DENIED ? Result::Denied : Result::Failed;
  }
  if (!list.has(name)) list.files.push_back(name);
  log += name + L"\n";
  return Result::Ok;
}

}  // namespace

Package::~Package() {
  if (mod_ && owned_) FreeLibrary(mod_);
}

bool Package::bytes(int id, const void*& data, DWORD& size) const {
  HRSRC r = FindResourceW(mod_, MAKEINTRESOURCEW(id), RT_RCDATA);
  HGLOBAL g = r ? LoadResource(mod_, r) : nullptr;
  data = g ? LockResource(g) : nullptr;
  size = r ? SizeofResource(mod_, r) : 0;
  return data && size;
}

// The index: lines of words.
//   meleenet-package 1
//   version 0.17.0 | protocol 15 | launcher 1.1.0
//   file <name> <resource id> [loader] [keep]   loader: an Ultimate ASI Loader already there is used as it is
//                                               keep: a file already there stays (the player's settings)
//   text <name> <content> [ifmissing]           a small file written from the line itself
bool Package::open(const std::wstring& file) {
  if (mod_ && owned_) FreeLibrary(mod_);
  mod_ = nullptr;
  owned_ = false;
  entries.clear();
  version.clear();
  if (!file.empty()) {
    mod_ = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!mod_) return false;
    owned_ = true;
  }
  const void* data = nullptr;
  DWORD size = 0;
  if (!bytes(RES_PKG_INDEX, data, size) || size > 16384) return false;
  std::istringstream in(std::string(static_cast<const char*>(data), size));
  std::string line;
  bool head = false;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string key, a, b, flag;
    words >> key >> a;
    if (key == "meleenet-package") {
      head = a == "1";
    } else if (key == "version") {
      version = core::wide(a);
    } else if (key == "protocol") {
      protocol = atoi(a.c_str());
    } else if (key == "launcher") {
      launcher_version = core::wide(a);
    } else if (key == "file" || key == "text") {
      Entry e;
      e.name = core::wide(a);
      words >> b;
      if (key == "file") e.id = atoi(b.c_str());
      else e.text = b;
      while (words >> flag) {
        if (flag == "loader") e.loader = true;
        if (flag == "keep") e.keep = true;
        if (flag == "ifmissing") e.if_missing = true;
      }
      const void* body = nullptr;
      DWORD body_size = 0;
      if (!plain_name(e.name) || entries.size() >= 64) return false;
      if (key == "file" && (e.id < RES_PKG_FILES || e.id > RES_PKG_FILES + 99 || !bytes(e.id, body, body_size))) return false;
      entries.push_back(e);
    }
  }
  if (bytes(RES_PKG_INFO, data, size) && size < 65536) info.assign(static_cast<const char*>(data), size);
  bool has_mod = false;
  for (auto& e : entries) has_mod = has_mod || _wcsicmp(e.name.c_str(), L"meleenet.asi") == 0;
  return head && has_mod && core::compare_versions(version, L"0.0.1") >= 0 && !version.empty();
}

// A meleenet.ini that stays from 0.1.0 says "menus = bars". Nobody chose that: it was that version's
// default, written by its own file while the wide menus were unfinished. Since 0.1.1 the menus fill a
// wide picture, so the line of such a file is brought to the new default once (with its note); from
// then on the row is the player's own.
void refresh_settings(const std::wstring& ini, const std::wstring& was, std::wstring& log) {
  if (was.empty() || core::compare_versions(was, L"0.1.1") >= 0) return;
  std::string text = read_all(ini);
  const std::string old_line = "\nmenus = bars", new_line = "\nmenus = wide";
  const std::string old_note = "; bars: menus as the game draws them (4:3 with black sides); wide: without the bars (not finished)";
  const std::string new_note = "; wide: the menus fill a wide picture; bars: the game's own 4:3 menus with black bars at the sides";
  size_t at = text.find(old_line);
  if (at == std::string::npos) return;
  text.replace(at, old_line.size(), new_line);
  if ((at = text.find(old_note)) != std::string::npos) text.replace(at, old_note.size(), new_note);
  DWORD err = 0;
  if (write_file(ini, text.data(), static_cast<DWORD>(text.size()), err))
    log += L"meleenet.ini: меню теперь на весь широкий кадр (menus = wide)\n";
}

Result install(const Package& p, const std::wstring& dir, std::wstring& log) {
  if (!exists(join(dir, kExe))) {
    log += L"в папке нет ztmnt.exe\n";
    return Result::Failed;
  }
  if (core::game_running(dir)) {
    log += L"игра сейчас запущена: закройте её и повторите\n";
    return Result::Failed;
  }
  log += core::exe_supported(dir) ? L"ztmnt.exe: та версия, с которой работает мод\n"
                                  : L"ztmnt.exe другого издания: мод его не изменит и в игре ничего не включит\n";
  List list;
  list.load(dir);
  Result r = Result::Ok;
  for (const Entry& e : p.entries) {
    if (r != Result::Ok) break;
    std::wstring dst = join(dir, e.name);
    const void* data = e.text.data();
    DWORD size = static_cast<DWORD>(e.text.size());
    if (e.id && !p.bytes(e.id, data, size)) {
      log += L"в лаунчере нет файла " + e.name + L"\n";
      r = Result::Failed;
      break;
    }
    if (e.loader && exists(dst) && !list.has(e.name) && !same_bytes(dst, data, size) &&
        read_all(dst).find("IsUltimateASILoader") != std::string::npos) {
      log += e.name + L": уже стоит Ultimate ASI Loader, используется он\n";
    } else if (e.keep && exists(dst)) {
      log += e.name + L": оставлены ваши настройки\n";
      if (!list.has(e.name)) list.files.push_back(e.name);
      if (_wcsicmp(e.name.c_str(), L"meleenet.ini") == 0) refresh_settings(dst, list.version, log);
    } else if (e.if_missing && exists(dst)) {
      // (there already: left as it is)
    } else {
      r = put(dir, e.name, data, size, list, log);
    }
  }
  list.save(dir, p);
  return r;
}

Result uninstall(const std::wstring& dir, std::wstring& log) {
  List list;
  if (!list.load(dir)) {
    log += std::wstring(L"в папке нет ") + kList + L": мод сюда этим способом не ставился\n";
    return Result::Failed;
  }
  if (core::game_running(dir)) {
    log += L"игра сейчас запущена: закройте её и повторите\n";
    return Result::Failed;
  }
  Result r = Result::Ok;
  for (auto& f : list.files) {
    std::wstring p = join(dir, f);
    if (!plain_name(f)) continue;
    if (exists(p) && !DeleteFileW(p.c_str())) {
      DWORD e = GetLastError();
      log += L"не удалось удалить " + f + L": " + error_text(e) + L"\n";
      r = e == ERROR_ACCESS_DENIED ? Result::Denied : Result::Failed;
    } else {
      log += L"удалён " + f + L"\n";
    }
  }
  if (r == Result::Ok) {
    for (auto& b : list.backups)
      if (plain_name(b.first) && MoveFileExW(join(dir, b.second).c_str(), join(dir, b.first).c_str(), MOVEFILE_REPLACE_EXISTING))
        log += L"возвращён прежний " + b.first + L"\n";
    // What the mod made itself while the game ran (the player's own save stays).
    for (const wchar_t* extra : {L"meleenet.log", L"meleenet-menu.log", L"meleenet-sim.log", L"tmnt-net.sv"})
      if (DeleteFileW(join(dir, extra).c_str())) log += std::wstring(L"удалён ") + extra + L"\n";
    RemoveDirectoryW(join(dir, kBackupDir).c_str());  // only if empty
    DeleteFileW(join(dir, kList).c_str());
  }
  return r;
}

}  // namespace pkg
}  // namespace lz
