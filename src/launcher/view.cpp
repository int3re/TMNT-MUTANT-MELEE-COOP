// The launcher's picture: every screen, drawn from the model (a 960 x 600 window: header 48, body 508,
// footer 44). The look is Mutant Melee's own menus: a honeycomb that brightens to the right; black pipes; a
// wall of blue bricks along the bottom with a ramp at its right end; black windows and plates in a steel
// rim; headings on black banners with a white edge; what is chosen stands on a pale plate, as a hero's name
// does in the game. The colour is a bandana's: the player picks one of the four turtles', it paints the
// honeycomb and the accents, and the mask itself is tied beside the title.
#include <algorithm>
#include <cmath>

#include "launcher/gameset.h"
#include "launcher/launcher.h"

namespace lz {
namespace view {
namespace {

using gfx::Canvas;
using gfx::Font;
using gfx::Matrix;
using gfx::TextOpt;

// BAR: the pipes' black; PANEL: the black of the game's buttons and windows; EDGE: their steel rim, RIM: the
// same steel in the shade; PLATE: the pale plate under what is chosen; the LOGO colours: the game's title.
const uint32_t BAR = 0x030708, PANEL = 0x070D0F, LINE = 0x1D3336, EDGE = 0x8FA3B8, RIM = 0x3F5463, PAPER = 0xF3F8F6,
               TEXT2 = 0xC5DAD6, MUTED = 0x8FAAA6, PLATE = 0xA9E6DD, PLATE_HOT = 0xDCFFF9, PLATE_DOWN = 0x86CFC4,
               OK = 0x62E08A, INK = 0x04100E, WAIT = 0xF2C94C, RED = 0xEF5A5A, RED_TEXT = 0xFF8A8A, RED_EDGE = 0xC24A4A,
               BLACK = 0x000000, FIELD = 0x03080A, TRACK = 0x12262A, DIM = 0x5E7774, LIVE = 0x0E2A28,
               LOGO_GREEN = 0x4BCB3C, LOGO_ORANGE = 0xF7941D, LOGO_RED = 0x8A1810;
// The four turtles' bandanas: Leonardo's blue, Raphael's red, Donatello's purple, Michelangelo's orange. The
// one the player chose paints the honeycomb and the launcher's accents.
const uint32_t kBandana[4] = {0x2E86E8, 0xE5423E, 0x9A5CF2, 0xF28C1E};

Font D(float size, float spacing = 0) { return {gfx::Display, size, 400, spacing}; }
Font T(float size, int weight = 400, float spacing = 0) { return {gfx::Text, size, weight, spacing}; }
Font Mo(float size, int weight = 500) { return {gfx::Mono, size, weight, 0}; }

// icons, in a 24 x 24 box
const char kSliders[] = "M4 6h8M18 6h2M4 12h2M12 12h8M4 18h10";
const char kMin[] = "M6 17h12";
const char kClose[] = "M6 6l12 12M18 6L6 18";
const char kArrow[] = "M3 12h17M14 6l6 6-6 6";
const char kEqual[] = "M5 9h14M5 15h14";
const char kCheck[] = "M5 12.5l4.5 4.5L19 7.5";
const char kWarn[] = "M12 4l9 16H3ZM12 10v5M12 17.6v0.4";
const char kSpin[] = "M12 4a8 8 0 1 0 8 8";
const char kDownload[] = "M12 4v11M7 11l5 5 5-5M5 20h14";
const char kPlay[] = "M8 5l11 7-11 7Z";
const char kFolder[] = "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z";
const char kExternal[] = "M13 5h6v6M19 5l-9 9M10 7H5v12h12v-5";
const char kCopy[] = "M9 9h11v11H9ZM5 15H4V4h11v1";
const char kBack[] = "M20 12H5M11 6l-6 6 6 6";
const char kPrev[] = "M14 6l-6 6 6 6";
const char kNext[] = "M10 6l6 6-6 6";
const char kMinus[] = "M6 12h12";
const char kPlus[] = "M6 12h12M12 6v12";
const char kNote[] = "M12 11v6M12 7.4v0.4";  // in a ring of 9 around 12, 12
// the mark: a cell of the honeycomb, in a 28 x 28 box
const char kShellOuter[] = "M14 2l10.4 6v12L14 26 3.6 20V8Z";
const char kShellInner[] = "M14 8.5l4.8 2.75v5.5L14 19.5l-4.8-2.75v-5.5Z";
// a turtle's mask, in a 272 x 90 box: the band with its two eye holes, the knot at its right end, the two
// loose ends flying off
const char kMaskBand[] = "M4 30Q100 10 196 30L196 62Q100 44 4 62Z";
const char kMaskSheen[] = "M18 30Q100 14 182 30";
const char kMaskEyeL[] = "M50 34Q70 27 90 38Q72 50 54 46Z";
const char kMaskEyeR[] = "M150 34Q130 27 110 38Q128 50 146 46Z";
const char kMaskKnot[] = "M190 34L208 30L212 58L194 62Z";
const char kMaskEndHigh[] = "M206 36L262 12L254 28L270 36L210 50Z";
const char kMaskEndLow[] = "M206 48L256 60L246 70L258 84L204 60Z";

struct Ctx {
  Canvas& c;
  const Model& m;
  std::vector<Hit>* hits;
  uint32_t accent;
};

void hit(Ctx& x, int id, float l, float t, float w, float h) {
  if (!x.hits) return;
  Matrix inv = x.c.current();
  if (!inv.Invert()) return;
  x.hits->push_back({id, D2D1::RectF(l, t, l + w, t + h), inv});
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

std::wstring megabytes(uint64_t bytes) {
  wchar_t buf[32];
  swprintf(buf, 32, L"%.1f", bytes / 1000000.0);
  for (wchar_t* p = buf; *p; ++p)
    if (*p == L'.') *p = L',';
  return buf;
}

// --- what a screen says --------------------------------------------------------------------------

struct Vals {
  bool offer = false, arrow = false, equal = false, has_new = false;
  std::wstring installed, installed_note, latest, latest_note, latest_label;
  uint32_t latest_border = RIM;
  int compat = 0;  // 1 warning (accent), 2 fine (green), 3 waiting, 4 bad (red), 5 plain
  std::wstring compat_text;
  bool notice = false, notice_bad = false;
  std::wstring notice_title, notice_text;
  std::wstring primary, secondary, hint;
  int icon = 0;  // 1 download, 2 play, 3 folder
  bool primary_on = true;
  std::wstring tab, meta, title, link, link_url;
  std::vector<std::wstring> items;
  bool copy = false;
  std::wstring net;
  int net_state = 0;
  std::wstring path;
};

std::wstring proto_note(int protocol) { return protocol ? L"протокол " + std::to_wstring(protocol) : L""; }

void news(const Release& r, Vals& v, const wchar_t* tab) {
  v.tab = tab;
  v.meta = r.version + (r.date.empty() ? L"" : L" · " + r.date);
  v.title = r.title;
  v.items = r.notes;
  if (v.items.empty()) {  // (a release without its description: an old one)
    v.title = L"Версия " + r.version;
    v.items = {L"Что в ней изменилось — на странице релиза на GitHub."};
  }
  v.link = L"Все изменения на GitHub";
  v.link_url = std::wstring(kRepoUrl) + L"/releases";
}

void how_it_works(Vals& v) {
  v.tab = L"КАК ЭТО РАБОТАЕТ";
  v.meta = L"meleenet";
  v.title = L"Лаунчер мода";
  v.items = {L"Мод лежит внутри лаунчера: ставится в папку игры одной кнопкой.",
             L"При запуске лаунчер спрашивает у GitHub, не вышла ли версия новее.",
             L"Игра по сети — пункт СЕТЕВАЯ ИГРА в главном меню игры."};
  v.link = L"Страница мода на GitHub";
  v.link_url = kRepoUrl;
}

// The notes of the version that is installed: GitHub's or this launcher's if they are of that version,
// else the ones kept from the day it was installed.
void installed_news(const Model& m, Vals& v) {
  if (m.inst.present && m.rel.known && m.rel.version == m.inst.version && !m.rel.notes.empty()) {
    news(m.rel, v, L"В ЭТОЙ ВЕРСИИ");
  } else if (m.inst.present && m.pack.known && m.pack.version == m.inst.version && !m.pack.notes.empty()) {
    news(m.pack, v, L"В ЭТОЙ ВЕРСИИ");
  } else if (m.inst.present && !m.set.notes.empty() && m.set.notes_version == m.inst.version) {
    v.tab = L"В ЭТОЙ ВЕРСИИ";
    v.meta = m.inst.version + L" · установлена";
    v.title = m.set.notes_title;
    v.items = m.set.notes;
    v.link = L"Все изменения на GitHub";
    v.link_url = std::wstring(kRepoUrl) + L"/releases";
  } else {
    how_it_works(v);
  }
}

// What there is to install: GitHub's version when it is newer than the one inside this launcher and its
// file can be had (it is downloaded then), else the one inside.
bool from_github(const Model& m) {
  return m.rel.known && !m.assets_blocked && (!m.pack.known || core::compare_versions(m.rel.version, m.pack.version) > 0);
}
const Release& offer(const Model& m) { return from_github(m) ? m.rel : m.pack; }

Vals vals(const Model& m) {
  Vals v;
  Screen s = m.screen == Screen::Settings ? m.back : m.screen;
  const uint32_t accent = kBandana[m.set.accent & 3];
  const Release& t = offer(m);
  const bool download = from_github(m);
  std::wstring have = m.inst.version.empty() ? L"" : m.inst.version;
  std::wstring play_on = have.empty() ? L"ИГРАТЬ" : L"ИГРАТЬ НА " + have;
  v.installed = !m.inst.present ? L"нет" : have.empty() ? L"?" : have;
  v.installed_note = !m.inst.present ? L"мод не установлен" : have.empty() ? L"версия неизвестна" : proto_note(m.inst.protocol);
  v.latest_label = L"НА GITHUB";
  v.latest = m.rel.known ? m.rel.version : L"—";
  v.latest_note = m.rel.known ? proto_note(m.rel.protocol) : L"нет ответа";
  v.path = m.has_game ? m.set.game_dir : L"(папка не выбрана)";
  std::wstring how = download ? (t.pkg_size ? megabytes(t.pkg_size) + L" МБ с GitHub" : std::wstring(L"Файл с GitHub")) +
                                    (t.pkg_sha256.empty() ? L" · в релизе нет контрольной суммы" : L" · после загрузки сверяется SHA-256")
                              : L"Версия " + t.version + L" уже внутри лаунчера: скачивать ничего не нужно.";
  std::wstring checked = m.checked_at.empty() ? L"GitHub: проверено только что"
                                             : L"GitHub: проверено в " + m.checked_at + (m.via.empty() ? L"" : L", " + m.via);
  // The footer's line where an answer of GitHub is not what the screen is about.
  auto github = [&]() {
    if (!m.repo) {
      v.net = L"Мод: внутри этого лаунчера";
    } else if (m.rechecking) {
      v.net = L"GitHub: проверяю";
      v.net_state = 1;
    } else if (m.rel.known && !m.assets_blocked) {
      v.net = checked;
    } else {
      v.net = m.assets_blocked ? L"GitHub: файлы не скачиваются" : L"GitHub: нет ответа";
      v.net_state = 2;
    }
  };
  // The right box, when the version to install is the one inside this launcher.
  auto inside = [&]() {
    if (download || (m.rel.known && m.rel.version == t.version)) return;
    v.latest_label = L"В ЭТОМ ЛАУНЧЕРЕ";
    v.latest = t.version;
    v.latest_note = proto_note(t.protocol);
  };
  switch (s) {
    case Screen::Checking:
      v.offer = v.arrow = true;
      v.latest = L"· · ·";
      v.latest_note = L"жду ответа";
      v.latest_border = EDGE;
      v.compat = 3;
      v.compat_text = L"Спрашиваю у GitHub, какая версия последняя.";
      v.primary = play_on;
      v.icon = 2;
      v.primary_on = m.inst.present;
      v.hint = L"Играть можно сразу: проверка идёт в фоне и запуск не задерживает.";
      installed_news(m, v);
      v.net = L"GitHub: проверяю";
      v.net_state = 1;
      break;
    case Screen::Update:
      v.offer = v.arrow = v.has_new = true;
      v.latest_border = accent;
      inside();
      if (m.inst.protocol && t.protocol && m.inst.protocol != t.protocol) {
        v.compat = 1;
        v.compat_text = L"Протокол новый: с " + have + L" по сети не соединяется. Обновитесь всей командой.";
      } else if (m.inst.protocol && m.inst.protocol == t.protocol) {
        v.compat = 2;
        v.compat_text = L"Протокол тот же: с друзьями на " + have + L" играть можно.";
      } else {
        v.compat = 5;
        v.compat_text = L"Есть версия мода новее установленной.";
      }
      v.primary = L"ОБНОВИТЬ ДО " + t.version;
      v.icon = 1;
      v.secondary = play_on;
      v.hint = how;
      news(t, v, L"ЧТО НОВОГО");
      v.copy = true;
      github();
      break;
    case Screen::Install:
      v.offer = v.arrow = v.has_new = true;
      v.latest_border = accent;
      inside();
      v.compat = 2;
      v.compat_text = L"Файлы игры не меняются: мод ставится рядом и убирается одной кнопкой.";
      v.primary = L"УСТАНОВИТЬ МОД " + t.version;
      v.icon = 1;
      v.hint = how;
      news(t, v, L"ЧТО НОВОГО");
      github();
      break;
    case Screen::Downloading:
      if (m.pending == Pending::Uninstall) installed_news(m, v);
      else news(m.pending == Pending::Launcher ? m.rel : t, v, L"ЧТО НОВОГО");
      v.copy = m.rel.known;
      v.net = !m.total ? L"Мод: из этого лаунчера" : m.stage == 0 ? L"GitHub: идёт загрузка" : L"GitHub: файл получен";
      if (m.pending == Pending::Uninstall) v.net = L"Мод убирается из папки игры";
      break;
    case Screen::Ready:
      v.offer = true;
      if (!m.repo) {  // (nothing to ask: what is installed is what this launcher carries)
        v.equal = true;
        v.latest_label = L"В ЭТОМ ЛАУНЧЕРЕ";
        v.latest = m.pack.known ? m.pack.version : L"—";
        v.latest_note = proto_note(m.pack.protocol);
        v.compat = 2;
        v.compat_text = L"Мод установлен. Обновления придут с новым лаунчером.";
        v.net = L"Мод: внутри этого лаунчера";
      } else if (m.unchecked) {
        v.arrow = true;
        v.latest_note = L"не проверялось";
        v.latest_border = EDGE;
        v.compat = 5;
        v.compat_text = L"Проверка при запуске выключена в настройках.";
        v.net = L"GitHub: не проверялось";
        v.net_state = 1;
      } else {
        v.equal = true;
        v.compat = 2;
        v.compat_text = L"У вас последняя версия. Новее на GitHub нет.";
        v.net = checked;
      }
      v.primary = m.launching ? L"ИГРА ЗАПУСКАЕТСЯ" : L"ИГРАТЬ";
      v.icon = 2;
      if (m.repo) v.secondary = m.rechecking ? L"ПРОВЕРЯЮ GITHUB" : L"ПРОВЕРИТЬ ОБНОВЛЕНИЯ";
      v.hint = L"Игра по сети — пункт СЕТЕВАЯ ИГРА в главном меню игры.";
      installed_news(m, v);
      v.copy = m.rel.known;
      if (m.rechecking) {
        v.net = L"GitHub: проверяю";
        v.net_state = 1;
      }
      break;
    case Screen::Offline:
      v.offer = v.arrow = true;
      v.latest_border = EDGE;
      v.compat = 4;
      if (m.assets_blocked) {
        v.latest_note = L"файл недоступен";
        v.compat_text = L"GitHub отвечает, но файлы релиза не отдаёт. Включите прокси в настройках.";
      } else {
        v.compat_text = L"GitHub не отвечает. Какая версия последняя — неизвестно.";
      }
      v.primary = play_on;
      v.icon = 2;
      v.secondary = L"ПОВТОРИТЬ ПРОВЕРКУ";
      v.hint = L"Проверка обновлений игре не нужна: мод работает как раньше.";
      v.tab = L"НЕТ СВЯЗИ";
      v.meta = m.assets_blocked ? L"файлы GitHub не скачиваются" : L"github.com не отвечает";
      v.title = L"Что можно сделать";
      v.items = {L"Повторить позже: лаунчер проверяет обновления при каждом запуске.",
                 L"Включить прокси в настройках — автоматически или свой.",
                 L"Скачать новый лаунчер в браузере: мод уже внутри него."};
      v.link = L"Страница релиза в браузере";
      v.link_url = std::wstring(kRepoUrl) + L"/releases/latest";
      v.net = m.assets_blocked ? L"GitHub: файлы не скачиваются" : L"GitHub: нет ответа";
      v.net_state = 2;
      break;
    case Screen::NoGame:
      v.notice = true;
      v.notice_title = L"Игра не найдена";
      v.notice_text = L"Лаунчер не нашёл игру на этом компьютере. Укажите папку, где лежит ztmnt.exe.";
      v.primary = L"ВЫБРАТЬ ПАПКУ ИГРЫ";
      v.icon = 3;
      v.hint = L"Нужна своя копия игры: файлы игры в мод не входят.";
      v.tab = L"ТРИ ШАГА";
      v.meta = L"первый запуск";
      v.title = L"Как это работает";
      v.items = {L"Укажите папку игры.", L"Лаунчер поставит мод: он уже внутри. Если на GitHub есть новее — скачает.",
                 L"Запустите игру: в главном меню появится СЕТЕВАЯ ИГРА."};
      v.link = L"Инструкция на GitHub";
      v.link_url = kRepoUrl;
      github();
      if (m.rel.known && !m.rechecking) v.net = L"GitHub: последняя версия " + m.rel.version;
      break;
    case Screen::Running:
      v.notice = true;
      v.notice_title = L"Игра сейчас открыта";
      if (m.pending == Pending::Uninstall) {
        v.notice_text = L"Пока игра открыта, мод из её папки не убрать. Закройте игру — удаление начнётся само.";
        v.primary = L"ЗАКРЫТЬ ИГРУ И УДАЛИТЬ МОД";
        v.net = L"Мод ждёт, пока игра закроется";
        v.net_state = 1;
      } else {
        v.notice_text = L"Версия " + t.version + (m.total ? L" скачана и проверена" : L" готова к установке") +
                        L". Закройте игру — установка начнётся сама.";
        v.primary = L"ЗАКРЫТЬ ИГРУ И ОБНОВИТЬ";
        v.icon = 1;
        v.net = m.total ? L"GitHub: " + t.version + L" скачана и проверена" : L"Мод " + t.version + L" ждёт, пока игра закроется";
      }
      v.secondary = L"ПОЗЖЕ";
      v.hint = L"В сетевой игре остальные останутся без вас: сначала предупредите команду.";
      v.tab = L"ПОЧЕМУ ТАК";
      v.meta = L"ztmnt.exe открыт";
      v.title = L"Мод занят игрой";
      v.items = {L"Пока игра открыта, файлы мода в её папке нельзя заменить.",
                 L"Скачивать заново не придётся: осталась только установка.", L"Настройки, клавиши и сохранения останутся."};
      v.link = L"Все изменения на GitHub";
      v.link_url = std::wstring(kRepoUrl) + L"/releases";
      break;
    case Screen::Problem:
    default:
      v.notice = v.notice_bad = true;
      v.notice_title = m.problem_title;
      v.notice_text = m.problem_text;
      v.primary = m.problem_primary;
      v.secondary = m.problem_secondary;
      v.hint = L"Игра и её файлы не тронуты: мод либо стоит прежний, либо не стоит вовсе.";
      if (t.known) news(t, v, L"ЧТО НОВОГО");
      else how_it_works(v);
      v.net = L"Не получилось";
      v.net_state = 2;
      break;
  }
  if (!m.net.empty()) {
    v.net = m.net;
    v.net_state = m.net_state;
  }
  if (m.has_game && !m.exe_ok && (s == Screen::Update || s == Screen::Install || s == Screen::Ready || s == Screen::Checking))
    v.hint = L"ztmnt.exe в этой папке не тот, для которого сделан мод: с ним мод не включится.";
  if (!m.repo) {  // (no page to send anybody to)
    v.link.clear();
    v.link_url.clear();
    v.copy = false;
  }
  return v;
}

// --- parts ---------------------------------------------------------------------------------------

uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = clamp01(t);
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    const float v = ((a >> shift) & 255) * (1 - t) + ((b >> shift) & 255) * t;
    out |= static_cast<uint32_t>(v + 0.5f) << shift;
  }
  return out;
}

// A cell of the honeycomb: flat at the top and the bottom, as the game's are.
void hexagon(Canvas& c, float cx, float cy, float r, uint32_t rgb, float a = 1) {
  D2D1_POINT_2F p[6];
  for (int i = 0; i < 6; ++i) {
    const float rad = 1.0471976f * i;
    p[i] = D2D1::Point2F(cx + r * std::cos(rad), cy + r * std::sin(rad));
  }
  c.poly(p, 6, rgb, a);
}

// A bandana's loose end: a strip cut like a swallow's tail.
void ribbon(Canvas& c, float x, float cy, float w, float h, uint32_t rgb) {
  const D2D1_POINT_2F p[5] = {D2D1::Point2F(x, cy - h / 2), D2D1::Point2F(x + w, cy - h / 2), D2D1::Point2F(x + w - h * 0.55f, cy),
                              D2D1::Point2F(x + w, cy + h / 2), D2D1::Point2F(x, cy + h / 2)};
  c.poly(p, 5, rgb);
}

// The four bandanas, as four loose ends in a row.
void cells(Canvas& c, float x, float cy, float r, float gap) {
  for (int i = 0; i < 4; ++i) ribbon(c, x + i * (2 * r + gap), cy, 2 * r, r * 1.2f, kBandana[i]);
}

// A turtle's mask in a bandana's colour: its 272 x 90 drawing with the left top corner at x, y.
void mask(Canvas& c, float x, float y, float scale, uint32_t rgb) {
  c.push(Matrix::Scale(scale, scale) * Matrix::Translation(x, y));
  const float line = 3.0f / scale;
  c.fill_path(kMaskEndLow, mix(rgb, BLACK, 0.24f));
  c.stroke_path(kMaskEndLow, line, BLACK);
  c.fill_path(kMaskEndHigh, rgb);
  c.stroke_path(kMaskEndHigh, line, BLACK);
  c.fill_path(kMaskBand, rgb);
  c.stroke_path(kMaskBand, line, BLACK);
  c.stroke_path(kMaskSheen, line, 0xFFFFFF, true, 0.30f);
  c.fill_path(kMaskEyeL, PAPER);
  c.stroke_path(kMaskEyeL, line * 0.85f, BLACK);
  c.fill_path(kMaskEyeR, PAPER);
  c.stroke_path(kMaskEyeR, line * 0.85f, BLACK);
  c.fill_path(kMaskKnot, rgb);
  c.stroke_path(kMaskKnot, line, BLACK);
  c.pop();
}

// A black pipe with the light along its upper side, and a clamp that holds it.
void pipe(Canvas& c, float x0, float y0, float x1, float y1, float th) {
  c.line(x0, y0, x1, y1, th, 0x04070A);
  c.line(x0, y0 - th * 0.22f, x1, y1 - th * 0.22f, th * 0.16f, 0x55656F);
  c.line(x0, y0 + th * 0.30f, x1, y1 + th * 0.30f, th * 0.10f, 0x161E24);
}

void clamp_ring(Canvas& c, float cx, float cy, float th) {
  c.round_box(cx - 5, cy - th / 2 - 4, 10, th + 8, 3, 0x0A0F13, 1.5f, BLACK);
  c.fill(cx - 3, cy - th / 2 - 2, 6, 2, 0x667784);
}

// The wall of the game's menus: the honeycomb in the colour of the bandana chosen, dark at the left and bright
// at the right; a pipe under the header; along the bottom the blue bricks with a pipe on top that climbs
// a ramp at the right.
void background(Canvas& c, uint32_t accent) {
  const uint32_t base = 0x040B0D;
  c.fill(0, 0, kW, kH, base);
  const float r = 33, dx = 1.5f * r, dy = 1.7320508f * r;
  for (int i = -1; i * dx < kW + r; ++i)
    for (int j = -1; j * dy < kH + dy; ++j) {
      const float cx = i * dx, cy = 30 + j * dy + ((i & 1) ? dy / 2 : 0);
      float t = clamp01((cx / kW - 0.10f) / 0.90f);
      t = t * t * (3 - 2 * t);
      const float shade = 0.05f + 0.50f * t;
      hexagon(c, cx, cy, r - 1.2f, mix(accent, 0xFFFFFF, 0.55f), 0.06f + 0.34f * t);  // the cell's light edge
      hexagon(c, cx, cy, r - 3.4f, mix(base, accent, shade));
      hexagon(c, cx, cy, r - 9.0f, mix(base, accent, shade * 0.72f));                // its darker floor
    }
  // the bricks
  const float foot = kH - 44.0f, low = foot - 58, high = foot - 104, ramp0 = 712, ramp1 = 790;
  const D2D1_POINT_2F wall[6] = {D2D1::Point2F(0, low),          D2D1::Point2F(ramp0, low), D2D1::Point2F(ramp1, high),
                                 D2D1::Point2F(kW + 0.0f, high), D2D1::Point2F(kW + 0.0f, foot), D2D1::Point2F(0, foot)};
  c.clip_poly(wall, 6);
  const float bh = 20, bw = 54;
  for (int row = 0; high + row * bh < foot; ++row) {
    const float by = high + row * bh, depth = clamp01((by - high) / (foot - high));
    for (int k = -1; k * bw < kW + bw; ++k) {
      const float bx = k * bw + ((row & 1) ? bw / 2 : 0);
      unsigned h = static_cast<unsigned>(k + 7) * 73856093u ^ static_cast<unsigned>(row + 3) * 19349663u;
      h = (h ^ (h >> 13)) * 0x5BD1E995u;
      const float n = ((h >> 9) & 255) / 255.0f;
      c.fill(bx, by, bw, bh, 0x03080C);  // the mortar
      c.round_box(bx + 1.5f, by + 1.5f, bw - 3, bh - 3, 3, mix(mix(0x13293A, 0x1E4258, n), 0x050C12, depth * 0.55f), 0, 0);
      c.fill(bx + 4, by + 3, bw - 8, 1.5f, 0x7FB4D6, 0.10f * (1 - depth));
    }
  }
  c.unclip();
  // the pipes
  pipe(c, 0, low, ramp0, low, 13);
  pipe(c, ramp0, low, ramp1, high, 13);
  pipe(c, ramp1, high, kW, high, 13);
  for (float px : {120.0f, 330.0f, 540.0f}) clamp_ring(c, px, low, 13);
  clamp_ring(c, ramp0, low, 13);
  clamp_ring(c, ramp1, high, 13);
  clamp_ring(c, 900, high, 13);
  pipe(c, 0, 54, kW, 54, 12);
  for (float px : {150.0f, 470.0f, 790.0f}) clamp_ring(c, px, 54, 12);
}

void header_button(Ctx& x, int id, float bx, const char* icon, uint32_t rgb, bool danger = false) {
  Canvas& c = x.c;
  bool hover = x.m.hover == id, down = x.m.pressed == id;
  if (hover || down) c.fill(bx, 1, 44, 44, danger ? RED_EDGE : PAPER, danger ? (down ? 1.0f : 0.85f) : (down ? 0.16f : 0.09f));
  c.icon(icon, bx + 12, 13, 20, danger && hover ? PAPER : rgb, 2.2f);
  if (id == H_Settings) {
    const float dots[3][2] = {{15, 6}, {9, 12}, {17, 18}};
    c.push(Matrix::Scale(20.0f / 24, 20.0f / 24) * Matrix::Translation(bx + 12, 13));
    for (auto& d : dots) c.ring(d[0], d[1], 2.4f, 2.2f, rgb);
    c.pop();
  }
  hit(x, id, bx, 1, 44, 44);
}

void header(Ctx& x) {
  Canvas& c = x.c;
  c.fill(0, 0, kW, 48, BAR);
  c.push(Matrix::Scale(26.0f / 28, 26.0f / 28) * Matrix::Translation(16, 10));
  c.fill_path(kShellOuter, x.accent);
  c.stroke_path(kShellOuter, 2, EDGE);
  c.fill_path(kShellInner, PANEL);
  c.stroke_path(kShellInner, 1.6f, BLACK);
  c.pop();
  float tx = 52;
  Font name = D(15, 0.04f);
  c.text_mid(L"MELEENET", name, tx, 23, PAPER);
  tx += c.measure(L"MELEENET", name).width + 10;
  c.text_mid(L"ЛАУНЧЕР", T(11, 600, 0.16f), tx, 23, MUTED);
  header_button(x, H_Settings, 824, kSliders, x.m.screen == Screen::Settings ? x.accent : TEXT2);
  header_button(x, H_Min, 868, kMin, TEXT2);
  header_button(x, H_Close, 912, kClose, TEXT2, true);
}

void footer(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float top = kH - 44.0f, cy = top + 2 + 21;
  c.fill(0, top, kW, 44, BAR);
  c.fill(0, top, kW, 2, RIM);
  bool flash = !m.flash.empty();
  const uint32_t dot[3] = {OK, WAIT, RED};
  c.ellipse(20, cy, 4, 4, dot[v.net_state % 3]);
  Font f = T(12);
  const std::wstring& line = flash ? m.flash : v.net;
  // the right end: the four bandanas, before them a line of small print
  const float sx = kW - 16 - (4 * 14 + 3 * 5);
  cells(c, sx, cy, 7, 5);
  float right = sx;  // where the small print begins
  if (m.swapped) {
    const wchar_t* s = L"Новый лаунчер откроется при следующем запуске";
    right = sx - 14 - c.measure(s, f).width;
    c.text_mid(s, f, right, cy, MUTED);
  } else if (m.rel.full && !from_github(m) && !m.rel.launcher_version.empty() && m.screen != Screen::Downloading &&
             core::compare_versions(m.rel.launcher_version, kLauncherVersion) > 0) {
    // (a newer launcher with the same mod; with a newer mod it comes along with the update)
    std::wstring s = L"Вышел лаунчер " + m.rel.launcher_version + L" — обновить";
    TextOpt u;
    u.underline = true;
    float w = c.measure(s, f).width;
    right = sx - 14 - w;
    c.text_mid(s, f, right, cy, m.hover == H_NewLauncher ? PAPER : x.accent, u);
    hit(x, H_NewLauncher, right, top + 6, w, 34);
  } else {
    const wchar_t* s = L"Неофициальный фанатский мод · MIT";
    right = sx - 14 - c.measure(s, f).width;
    c.text_mid(s, f, right, cy, MUTED);
  }
  TextOpt one;
  one.ellipsis = true;
  one.max_w = right - 14 - 32;
  c.text_mid(line, f, 32, cy, flash ? PAPER : TEXT2, one);
  float px = 32 + std::min(c.measure(line, f).width, one.max_w) + 14;
  // the repository's address after the line, where a long passing note leaves room for it
  Font mono = Mo(12, 500);
  float rw = c.measure(kRepoText, mono).width;
  if (m.repo && px + 1 + 14 + rw + 14 <= right) {
    c.fill(px, cy - 7, 1, 14, LINE);
    px += 1 + 14;
    c.text_mid(kRepoText, mono, px, cy, m.hover == H_Repo ? PAPER : MUTED);
    hit(x, H_Repo, px, top + 6, rw, 34);
  }
}

// The big button: a pale plate in a steel rim. Returns its width.
float primary_button(Ctx& x, const Vals& v, float bx, float by) {
  Canvas& c = x.c;
  Font f = D(17, 0.02f);
  float tw = c.measure(v.primary, f).width;
  float w = 5 + 20 + (v.icon ? 22 + 10 : 0) + tw + 24 + 5, h = 62;
  bool on = v.primary_on, hover = on && x.m.hover == H_Primary, down = on && x.m.pressed == H_Primary;
  c.round_box(bx, by, w, h, 14, on ? EDGE : RIM, 0, 0);
  c.round_box(bx + 3, by + 3, w - 6, h - 6, 11, !on ? PANEL : down ? PLATE_DOWN : hover ? PLATE_HOT : PLATE, 2, BLACK);
  uint32_t ink = on ? INK : MUTED;
  float ix = bx + 5 + 20, cy = by + h / 2;
  if (v.icon == 1) c.icon(kDownload, ix, cy - 11, 22, ink, 2.8f);
  if (v.icon == 2) c.icon(kPlay, ix, cy - 11, 22, ink, 2, 24, true);
  if (v.icon == 3) c.icon(kFolder, ix, cy - 11, 22, ink, 2.6f);
  if (v.icon) ix += 22 + 10;
  c.text_mid(v.primary, f, ix, cy, ink);
  if (on) hit(x, H_Primary, bx, by, w, h);
  return w;
}

// A black button in a steel rim (`tall`: the one beside the big button).
float ghost_button(Ctx& x, int id, const std::wstring& label, float bx, float by, float h, float pad, bool tall,
                   float fixed_w = 0, uint32_t text = PAPER, uint32_t edge = EDGE, uint32_t fill = PANEL) {
  Canvas& c = x.c;
  Font f = T(13, 700, (tall || pad == 18) ? 0.06f : 0);
  float tw = c.measure(label, f).width;
  float w = fixed_w > 0 ? fixed_w : 2 + pad + tw + pad + 2;
  bool hover = x.m.hover == id, down = x.m.pressed == id;
  c.round_box(bx, by, w, h, tall ? 14.0f : 8.0f, down ? mix(fill, PAPER, 0.16f) : hover ? mix(fill, PAPER, 0.08f) : fill, 2,
              hover ? PAPER : edge);
  c.text_mid(label, f, bx + (w - tw) / 2, by + h / 2, text);
  hit(x, id, bx, by, w, h);
  return w;
}

void version_box(Ctx& x, float bx, float by, float w, float h, const wchar_t* label, const std::wstring& number,
                 const std::wstring& note, uint32_t edge) {
  Canvas& c = x.c;
  c.round_box(bx, by, w, h, 10, PANEL, 2, edge);
  float tx = bx + 2 + 14, ty = by + 2 + 10;
  Font lab = T(11, 600, 0.12f);
  ty += c.text(label, lab, tx, ty, MUTED) + 4;
  TextOpt num;
  num.line_h = 26 * 1.1f;
  ty += c.text(number, Mo(26, 700), tx, ty, PAPER, num) + 4;
  c.text(note, T(12), tx, ty, MUTED);
}

void notice_box(Ctx& x, const Vals& v, float bx, float by, float w, float* out_h) {
  Canvas& c = x.c;
  const float tx = bx + 2 + 16 + 24 + 12, tw = w - (tx - bx) - 16 - 2;
  Font title = T(16, 700), body = T(13);
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = tw;
  wrap.line_h = 13 * 1.45f;
  float th = c.line_h(title) + 4 + c.measure(v.notice_text, body, wrap).height;
  float h = 2 + 14 + (th < 24 ? 24 : th) + 14 + 2;
  uint32_t edge = v.notice_bad ? RED_EDGE : x.accent;
  c.round_box(bx, by, w, h, 10, PANEL, 2, edge);
  c.icon(kWarn, bx + 2 + 16, by + 2 + 14, 24, v.notice_bad ? RED_TEXT : x.accent, 2.2f);
  float ty = by + 2 + 14;
  ty += c.text(v.notice_title, title, tx, ty, PAPER) + 4;
  c.text(v.notice_text, body, tx, ty, TEXT2, wrap);
  *out_h = h;
}

void downloading(Ctx& x, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float left = 40, right = 580;
  // the three steps
  const wchar_t* labels[3] = {L"Загрузка", L"Проверка", m.pending == Pending::Uninstall ? L"Удаление"
                                                        : m.pending == Pending::Launcher ? L"Перезапуск" : L"Установка"};
  float sx = left;
  Font lab = T(12, 600);
  for (int i = 0; i < 3; ++i) {
    bool done = i < m.stage, active = i == m.stage;
    float tw = c.measure(labels[i], lab).width;
    float w = 2 + 5 + 20 + 8 + tw + 12 + 2;
    c.round_box(sx, top, w, 34, 8, active ? LIVE : PANEL, 2, active ? PLATE : RIM);
    c.round_box(sx + 7, top + 7, 20, 20, 5, active || done ? PLATE : LINE, 0, 0);
    if (done) {
      c.icon(kCheck, sx + 9, top + 9, 16, INK, 3);
    } else {
      wchar_t n[2] = {static_cast<wchar_t>(L'1' + i), 0};
      Font nf = Mo(11, 700);
      c.text_mid(n, nf, sx + 7 + (20 - c.measure(n, nf).width) / 2, top + 17, active ? INK : TEXT2);
    }
    c.text_mid(labels[i], lab, sx + 7 + 20 + 8, top + 17, active ? PAPER : done ? TEXT2 : MUTED);
    sx += w + 8;
  }
  // how far
  float y = top + 34 + 14;
  float share = m.stage > 0 ? 1.0f : m.total ? clamp01(static_cast<float>(static_cast<double>(m.got) / m.total)) : 0.0f;
  int pct = static_cast<int>(share * 100 + 0.5f);
  TextOpt big;
  big.line_h = 40;
  c.text(std::to_wstring(pct) + L"%", D(40), left, y, PAPER, big);
  std::wstring stage;
  if (m.stage == 0)
    stage = m.total ? megabytes(m.got) + L" из " + megabytes(m.total) + L" МБ" : std::wstring(L"мод уже в лаунчере");
  else if (m.stage == 1)
    stage = m.total ? L"сверяю SHA-256" : L"проверяю, что внутри";
  else
    stage = m.pending == Pending::Uninstall ? L"убираю файлы мода из папки игры"
            : m.pending == Pending::Launcher ? L"меняю файл лаунчера" : L"ставлю файлы в папку игры";
  Font sf = Mo(13, 500);
  c.text(stage, sf, right - c.measure(stage, sf).width, y + 40 - c.line_h(sf), TEXT2);
  // a pipe of the game's wall, filling
  y += 40 + 14;
  const float tx = left + 34, ty = y + 2, tw = right - 40 - tx, th = 46;
  c.round_box(tx, ty, tw, th, 8, 0x050B0D, 3, BLACK);
  const float ix = tx + 3, iy = ty + 3, iw = tw - 6, ih = th - 6;
  for (float t = 0; t < iw; t += 44) c.fill(ix + t, iy + ih - 12, 2, 12, PAPER, 0.24f);
  float lw = iw * share;
  if (lw > 0.5f) {
    c.fill(ix, iy, lw, ih, x.accent);
    float drift = static_cast<float>(std::fmod(m.time * 14.0, 96.0));
    const float bubbles[3][4] = {{18, 27, 3, 0.6f}, {52, 16, 2, 0.45f}, {78, 30, 2.4f, 0.22f}};
    for (float t = -96 + drift; t < lw; t += 96)
      for (int b = 0; b < 3; ++b) {
        float bx = t + bubbles[b][0];
        if (bx < 4 || bx > lw - 7) continue;
        c.ellipse(ix + bx, iy + bubbles[b][1] - 3, bubbles[b][2], bubbles[b][2], b == 2 ? INK : 0xFFFFFF, bubbles[b][3]);
      }
    if (lw > 26) c.round_box(ix + 10, iy + 5 - 3 + 3, lw - 20 - 3, 5, 2.5f, 0xFFFFFF, 0, 0);
    c.fill(ix + lw - 3, iy, 3, ih, BLACK);
  }
  for (int side = 0; side < 2; ++side) {
    float cxp = side == 0 ? tx - 22 : tx + tw + 22 - 26, cyp = ty - 9;
    c.round_box(cxp, cyp, 26, 64, 5, EDGE, 3, BLACK);
    c.ellipse(cxp + 13, cyp + 13, 3, 3, BLACK);
    c.ellipse(cxp + 13, cyp + 64 - 13, 3, 3, BLACK);
  }
  // where from, and the way out
  y += 2 + 46 + 8 + 14;
  Font cf = T(13, 700, 0.06f);
  float cw = 2 + 18 + c.measure(L"ОТМЕНА", cf).width + 18 + 2;
  TextOpt url;
  url.ellipsis = true;
  url.max_w = right - left - 16 - cw;
  c.text_mid(m.total ? std::wstring(kRepoText) + L"/releases/latest" : std::wstring(L"meleenet-launcher.exe: мод внутри"), Mo(12, 500),
             left, y + 22, MUTED, url);
  ghost_button(x, H_Cancel, L"ОТМЕНА", right - cw, y, 44, 18, false);
  y += 44 + 14;
  TextOpt note;
  note.wrap = true;
  note.max_w = right - left;
  note.line_h = 12 * 1.45f;
  c.text(m.pending == Pending::Uninstall ? std::wstring(L"Файлы игры, её сохранения и настройки остаются на месте.")
         : m.pending == Pending::Launcher ? L"На GitHub вышла версия лаунчера " + m.rel.launcher_version +
                                                L": лаунчер обновляет себя и перезапустится сам. Мод в папке игры при этом не меняется."
                                          : std::wstring(L"Перед установкой лаунчер попросит закрыть игру. Настройки и сохранения остаются."),
         T(12), left, y, MUTED, note);
}

// The news: one of the game's windows - black in a steel rim - with its heading on a black banner with a
// white edge, as the game heads its screens. Each note is marked with a bandana's loose end, the four
// bandanas in turn. The window is as tall as what it says.
void paper(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float L = 620, W = 300, top = 48 + 26 + 34, limit = kH - 44 - 22;  // the window; its bottom goes no lower
  const float tx = L + 5 + 18, tw = W - 2 * (5 + 18);
  Font meta_f = Mo(12, 700), title_f = D(17), item_f = T(13), link_f = T(13, 700);
  TextOpt head;
  head.wrap = true;
  head.max_w = tw;
  head.line_h = 17 * 1.3f;
  const float mark_w = 13, mark_gap = 9;
  TextOpt item;
  item.wrap = true;
  item.max_w = tw - mark_w - mark_gap;
  item.line_h = 13 * 1.45f;
  // how much of the list there is room for (a long list: what does not fit is on GitHub)
  const float below = 12 + 2 + 10 + 28 + (v.copy ? 28.0f : 0.0f) + 12 + 5;  // the line, the links, the rim
  float h = 5 + 28 + c.line_h(meta_f) + 12 + c.measure(v.title, title_f, head).height + 12;
  size_t shown = 0;
  for (; shown < v.items.size(); ++shown) {
    const float ih = c.measure(v.items[shown], item_f, item).height + (shown ? 10.0f : 0.0f);
    if (top + h + ih + below > limit) break;
    h += ih;
  }
  h += below;

  c.round_box(L, top, W, h, 14, EDGE, 0, 0);
  c.round_box(L + 3, top + 3, W - 6, h - 6, 11, PANEL, 2, BLACK);
  // the words
  float y = top + 5 + 28;
  y += c.text(v.meta, meta_f, tx, y, MUTED) + 12;
  y += c.text(v.title, title_f, tx, y, PAPER, head) + 12;
  for (size_t i = 0; i < shown; ++i) {
    if (i) y += 10;
    ribbon(c, tx, y + 9.5f, mark_w, 8, kBandana[i & 3]);
    y += c.text(v.items[i], item_f, tx + mark_w + mark_gap, y, TEXT2, item);
  }
  y += 12;
  c.dashed_h(tx, y, tw, 2, 6, 5, EDGE, 0.35f);
  y += 2 + 10;
  // the links
  TextOpt under;
  under.underline = true;
  float lw = c.measure(v.link, link_f).width;
  bool hover = m.hover == H_PaperLink;
  c.text_mid(v.link, link_f, tx, y + 14, hover ? PAPER : x.accent, under);
  c.icon(kExternal, tx + lw + 6, y + 7, 14, hover ? PAPER : x.accent, 2.6f);
  hit(x, H_PaperLink, tx, y, lw + 6 + 14, 28);
  if (v.copy) {
    y += 28;
    const bool copied = m.copied_until != 0;
    const wchar_t* label = copied ? L"Ссылка скопирована" : L"Скопировать ссылку для друзей";
    const float cw = c.measure(label, link_f).width;
    hover = m.hover == H_Copy;
    c.text_mid(label, link_f, tx, y + 14, hover ? PAPER : x.accent, under);
    c.icon(copied ? kCheck : kCopy, tx + cw + 6, y + 7, 14, hover ? PAPER : x.accent, 2.6f);
    hit(x, H_Copy, tx, y, cw + 6 + 14, 28);
  }
  // the heading's banner on the window's upper edge: black, a white edge, its right end cut aslant
  Font tab_f = D(13, 0.06f);
  const float lh = c.line_h(tab_f);
  const float bw = 16 + c.measure(v.tab, tab_f).width + 14, bh = 7 + lh + 8, slant = 13;
  const float bx = L + 16, by = top - bh * 0.55f;
  const D2D1_POINT_2F edge[4] = {D2D1::Point2F(bx - 2.5f, by - 2.5f), D2D1::Point2F(bx + bw + slant + 4.2f, by - 2.5f),
                                 D2D1::Point2F(bx + bw + 1.6f, by + bh + 2.5f), D2D1::Point2F(bx - 2.5f, by + bh + 2.5f)};
  const D2D1_POINT_2F plate[4] = {D2D1::Point2F(bx, by), D2D1::Point2F(bx + bw + slant, by), D2D1::Point2F(bx + bw, by + bh),
                                  D2D1::Point2F(bx, by + bh)};
  c.poly(edge, 4, PAPER);
  c.poly(plate, 4, BLACK);
  c.fill(bx + 8, by + bh - 6, bw - 12, 2, x.accent);
  c.text_mid(v.tab, tab_f, bx + 14, by + 6 + lh / 2, PAPER);
}

void hero(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float left = 40, right = 580, width = right - left;
  float y = 48 + 26;
  // the line above the title
  Font eyebrow = T(12, 600, 0.14f);
  float eh = c.line_h(eyebrow);
  cells(c, left, y + eh / 2, 6, 5);
  c.text(L"СЕТЕВОЙ КООП · ДО 4 ИГРОКОВ", eyebrow, left + 4 * 12 + 3 * 5 + 10, y, TEXT2);
  y += eh + 14;
  // the title
  TextOpt l1, l2;
  l1.line_h = 56;
  l2.line_h = 38;
  // (the game's own title: green letters and orange ones, both on dark red)
  c.text(L"TMNT", D(56), left + 4, y + 4, LOGO_RED, l1);
  c.text(L"TMNT", D(56), left, y, LOGO_GREEN, l1);
  c.text(L"MUTANT MELEE", D(38), left + 3, y + 62 + 3, LOGO_RED, l2);
  c.text(L"MUTANT MELEE", D(38), left, y + 62, LOGO_ORANGE, l2);
  mask(c, left + 262, y - 4, 1.0f, x.accent);  // the bandana chosen, tied beside the title
  y += 56 + 6 + 38 + 22;

  if (m.screen == Screen::Downloading) {
    downloading(x, y);
  } else {
    if (v.offer) {
      const float bw = (width - 28 - 24) / 2;
      float bh = 2 + 10 + c.line_h(T(11, 600)) + 4 + 26 * 1.1f + 4 + c.line_h(T(12)) + 12 + 2;
      version_box(x, left, y, bw, bh, L"УСТАНОВЛЕНО", v.installed, v.installed_note, RIM);
      if (v.arrow) c.icon(kArrow, left + bw + 12, y + bh / 2 - 14, 28, x.accent, 2.6f);
      if (v.equal) c.icon(kEqual, left + bw + 12, y + bh / 2 - 14, 28, OK, 2.6f);
      float rx = left + bw + 12 + 28 + 12;
      version_box(x, rx, y, bw, bh, v.latest_label.c_str(), v.latest, v.latest_note, v.latest_border);
      if (v.has_new) {
        Font bf = T(10, 700, 0.12f);
        float tw = c.measure(L"НОВАЯ", bf).width, bh2 = 2 + 2 + c.line_h(bf) + 2 + 2;
        float bx = rx + bw - 2 - 10 - (tw + 16 + 4);
        c.round_box(bx, y - 9, tw + 16 + 4, bh2, 5, x.accent, 2, BLACK);
        c.text(L"НОВАЯ", bf, bx + 2 + 8, y - 9 + 2 + 2, INK);
      }
      y += bh + 12;
      if (v.compat) {
        const float cy = y + 10;
        uint32_t ic = v.compat == 1 ? x.accent : v.compat == 2 ? OK : v.compat == 3 ? WAIT : v.compat == 4 ? RED_TEXT : MUTED;
        if (v.compat == 3) {
          c.push(Matrix::Rotation(static_cast<float>(std::fmod(m.time * 300.0, 360.0)), D2D1::Point2F(left + 9, cy)));
          c.icon(kSpin, left, cy - 9, 18, ic, 2.6f);
          c.pop();
        } else {
          c.icon(v.compat == 2 ? kCheck : v.compat == 5 ? kArrow : kWarn, left, cy - 9, 18, ic, v.compat == 2 ? 2.6f : 2.4f);
        }
        TextOpt one;
        one.ellipsis = true;
        one.max_w = width - 26;
        c.text_mid(v.compat_text, T(13), left + 18 + 8, cy, TEXT2, one);
      }
      y += 20 + 20;
    } else if (v.notice) {
      float nh = 0;
      notice_box(x, v, left, y, width, &nh);
      y += nh + 20;
    }
    // the buttons
    float pw = primary_button(x, v, left, y);
    if (!v.secondary.empty()) ghost_button(x, H_Secondary, v.secondary, left + pw + 14, y, 62, 20, true);
    y += 62 + 16;
    TextOpt hint;
    hint.wrap = true;
    hint.max_w = width;
    hint.line_h = 12 * 1.45f;
    c.text(v.hint, T(12), left, y, m.has_game && !m.exe_ok && v.offer ? WAIT : MUTED, hint);
  }

  // the game folder, at the bottom
  const float cy = 48 + 508 - 28;
  c.icon(kFolder, left, cy - 9, 18, MUTED, 2.2f);
  Font lab = T(11, 600, 0.12f);
  float px = left + 18 + 10;
  c.text_mid(L"ПАПКА ИГРЫ", lab, px, cy, MUTED);
  px += c.measure(L"ПАПКА ИГРЫ", lab).width + 10;
  Font cf = T(13, 600);
  float cw = c.measure(L"Изменить", cf).width + 16;
  TextOpt path;
  path.ellipsis = true;
  path.max_w = right - px - 10 - cw;
  Font pf = Mo(12, 500);
  float pwid = c.measure(v.path, pf).width;
  if (pwid > path.max_w) pwid = path.max_w;
  c.text_mid(v.path, pf, px, cy, PAPER, path);
  px += pwid + 10;
  TextOpt under;
  under.underline = true;
  c.text_mid(L"Изменить", cf, px + 8, cy, m.hover == H_Folder ? PAPER : TEXT2, under);
  hit(x, H_Folder, px, cy - 22, cw, 44);

  paper(x, v);
}

// --- settings --------------------------------------------------------------------------------

struct Section {
  float x, y, w, cx, cw, cy;  // the box; where its content starts
};

Section section_begin(Ctx& x, float sx, float sy, float w, float h, const wchar_t* title) {
  Canvas& c = x.c;
  c.round_box(sx, sy, w, h, 10, PANEL, 2, RIM);
  Section s{sx, sy, w, sx + 2 + 16, w - 4 - 32, sy + 2 + 14};
  s.cy += c.text(title, D(13, 0.06f), s.cx, s.cy, x.accent);
  return s;
}

void toggle_row(Ctx& x, int id, const wchar_t* label, bool on, float lx, float ty, float w) {
  Canvas& c = x.c;
  const float cy = ty + 22;
  c.text_mid(label, T(14), lx, cy, PAPER);
  const float bx = lx + w - 56, tx = bx, tyy = cy - 13;
  bool hover = x.m.hover == id;
  c.round_box(tx, tyy, 52, 26, 13, on ? PLATE : TRACK, 2, on ? BLACK : hover ? PAPER : RIM);
  c.round_box(tx + (on ? 28 : 4), tyy + 4, 20, 18, 9, on ? INK : EDGE, 0, 0);
  hit(x, id, bx, ty, 56, 44);
}

// One of the four bandanas to choose: a small mask.
void swatch(Ctx& x, int index, float sx, float sy) {
  Canvas& c = x.c;
  int id = H_Accent0 + index;
  bool on = (x.m.set.accent & 3) == index, hover = x.m.hover == id;
  const float w = 48, h = 24, bx = sx - 2, by = sy + 10;
  if (on || hover) c.round_box(bx - 5, by - 5, w + 10, h + 10, (h + 10) / 2, PANEL, 3, on ? PAPER : RIM);
  c.round_box(bx, by, w, h, h / 2, kBandana[index], 2.5f, BLACK);
  for (float at : {0.30f, 0.70f}) {
    c.ellipse(bx + w * at, by + h / 2, 7, 5, BLACK);
    c.ellipse(bx + w * at, by + h / 2, 5.2f, 3.4f, PAPER);
  }
  hit(x, id, sx - 7, sy, 58, 44);
}

// Back, the title, and the pages' tabs at the right.
void settings_head(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  Font bf = T(13, 700);
  float bw = 2 + 10 + 18 + 8 + c.measure(L"Назад", bf).width + 14 + 2;
  bool hover = m.hover == H_Back;
  c.round_box(left, top, bw, 44, 8, PANEL, 2, hover ? PAPER : EDGE);
  c.icon(kBack, left + 12, top + 13, 18, PAPER, 2.4f);
  c.text_mid(L"Назад", bf, left + 12 + 18 + 8, top + 22, PAPER);
  hit(x, H_Back, left, top, bw, 44);
  TextOpt tl;
  tl.line_h = 30;
  Font tf = D(30);
  float tw = c.measure(L"НАСТРОЙКИ", tf, tl).width, tx = left + bw + 18;
  (void)tw;
  c.text(L"НАСТРОЙКИ", tf, tx + 3, top + 7 + 3, BLACK, tl);
  c.text(L"НАСТРОЙКИ", tf, tx, top + 7, PAPER, tl);

  const wchar_t* names[4] = {L"ЛАУНЧЕР", L"ГРАФИКА", L"СЕТЬ", L"УПРАВЛЕНИЕ"};
  Font nf = D(12, 0.06f);
  float widths[4], total = 3 * 10;
  for (int i = 0; i < 4; ++i) {
    widths[i] = 14 + c.measure(names[i], nf).width + 14;
    total += widths[i];
  }
  float px = kW - left - 6 - total;
  for (int i = 0; i < 4; ++i) {
    const float w = widths[i];
    const bool on = static_cast<int>(m.tab) == i, over = m.hover == H_Tab0 + i;
    // (a plate cut aslant at both ends, as the plates of the heroes' names are)
    c.push(Matrix::Skew(-14, 0, D2D1::Point2F(px + w / 2, top + 22)));
    if (on) {
      c.box(px, top, w, 44, x.accent, 3, BLACK);
    } else {
      c.fill(px, top, w, 44, over ? mix(PANEL, PAPER, 0.08f) : PANEL);
      c.border(px, top, w, 44, 2, over ? PAPER : EDGE);
    }
    hit(x, H_Tab0 + i, px, top, w, 44);
    c.pop();
    c.text_mid(names[i], nf, px + 14, top + 22, on ? INK : PAPER);
    px += w + 10;
  }
}

// ЛАУНЧЕР: updates, the network, the game and the mod, the launcher's own look.
void page_launcher(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float col_w = (kW - 80 - 28) / 2.0f, x1 = left, x2 = left + col_w + 28;
  const float cw = col_w - 4 - 32;
  Font tiny = T(12);
  float h3 = c.line_h(D(13, 0.06f)), sm = c.line_h(tiny);
  float h_updates = 2 + 14 + h3 + 2 + 6 + 44 + 6 + 44 + 6 + 44 + 16 + 2;
  float h_net = 2 + 14 + h3 + 8 + sm + 8 + 44 + 8 + sm + 8 + 40 + 16 + 2;
  float row1 = h_updates > h_net ? h_updates : h_net;
  float h_game = 2 + 14 + h3 + 8 + 44 + 8 + 44 + 8 + 44 + 16 + 2;
  float h_look = 2 + 14 + h3 + 8 + sm + 8 + 56 + 8 + 44 + 16 + 2;
  float row2 = h_game > h_look ? h_game : h_look;
  float y1 = top + 44 + 16, y2 = y1 + row1 + 16;

  // updates
  Section s = section_begin(x, x1, y1, col_w, row1, L"ОБНОВЛЕНИЯ");
  float y = s.cy + 2 + 6;
  toggle_row(x, H_Toggle1, L"Проверять при запуске", m.set.auto_check, s.cx, y, cw);
  y += 44 + 6;
  toggle_row(x, H_Toggle4, L"Обновлять лаунчер при запуске", m.set.auto_self, s.cx, y, cw);
  y += 44 + 6;
  toggle_row(x, H_Toggle2, L"Ставить обновления мода без вопроса", m.set.auto_install, s.cx, y, cw);

  // the network
  s = section_begin(x, x2, y1, col_w, row1, L"СЕТЬ");
  y = s.cy + 8;
  c.text(L"Прокси для GitHub — если он не открывается напрямую", tiny, s.cx, y, MUTED);
  y += sm + 8;
  const wchar_t* modes[3] = {L"Авто", L"Нет", L"Свой"};
  const float seg = (cw - 16) / 3;
  for (int i = 0; i < 3; ++i) {
    bool on = m.set.proxy == i;
    ghost_button(x, H_ProxyAuto + i, modes[i], s.cx + i * (seg + 8), y, 44, 0, false, seg, on ? INK : PAPER, on ? BLACK : EDGE,
                 on ? PLATE : PANEL);
  }
  y += 44 + 8;
  c.text(m.set.proxy == 0 ? L"Авто: как в Windows, а если не выходит — VPN-клиент на этом ПК"
                          : m.set.proxy == 1 ? L"Нет: только напрямую" : L"Адрес своего прокси (HTTP)",
         tiny, s.cx, y, MUTED);
  y += sm + 8;
  bool own = m.set.proxy == 2;
  c.box(s.cx, y, cw, 40, FIELD, 2, m.proxy_focus ? PAPER : m.hover == H_ProxyField ? TEXT2 : EDGE);
  Font pf = Mo(13, 500);
  if (m.set.proxy_addr.empty() && !m.proxy_focus) {
    c.text_mid(L"адрес:порт", pf, s.cx + 2 + 12, y + 20, MUTED, {}, 0.7f);
  } else {
    TextOpt clip;
    clip.max_w = cw - 28;
    clip.ellipsis = true;
    c.text_mid(m.set.proxy_addr, pf, s.cx + 2 + 12, y + 20, own ? PAPER : MUTED, clip);
    if (m.proxy_focus && std::fmod(m.time, 1.0) < 0.55) {
      float caret = s.cx + 2 + 12 + c.measure(m.set.proxy_addr, pf).width + 1;
      if (caret < s.cx + cw - 12) c.fill(caret, y + 11, 2, 18, PAPER);
    }
  }
  hit(x, H_ProxyField, s.cx, y, cw, 40);

  // the game and the mod
  s = section_begin(x, x1, y2, col_w, row2, L"ИГРА И МОД");
  y = s.cy + 8;
  c.icon(kFolder, s.cx, y + 13, 18, MUTED, 2.2f);
  Font cf = T(13, 600);
  float chw = c.measure(L"Изменить", cf).width + 8;
  TextOpt path;
  path.ellipsis = true;
  path.max_w = cw - 28 - 10 - chw;
  c.text_mid(m.has_game ? m.set.game_dir : L"(папка не выбрана)", Mo(12, 500), s.cx + 28, y + 22, PAPER, path);
  TextOpt under;
  under.underline = true;
  c.text_mid(L"Изменить", cf, s.cx + cw - chw + 4, y + 22, m.hover == H_FolderSettings ? PAPER : TEXT2, under);
  hit(x, H_FolderSettings, s.cx + cw - chw, y, chw, 44);
  y += 44 + 8;
  const float half = (cw - 8) / 2;
  ghost_button(x, H_OpenLog, L"Открыть журнал", s.cx, y, 44, 0, false, half);
  ghost_button(x, H_DesyncLogs, L"Журнал до сессии", s.cx + half + 8, y, 44, 0, false, half);
  y += 44 + 8;
  ghost_button(x, H_Reinstall, L"Переустановить мод", s.cx, y, 44, 0, false, half);
  if (m.confirm_uninstall)
    ghost_button(x, H_Uninstall, L"Точно удалить?", s.cx + half + 8, y, 44, 0, false, half, PAPER, RED_TEXT, RED_EDGE);
  else
    ghost_button(x, H_Uninstall, L"Удалить мод", s.cx + half + 8, y, 44, 0, false, half, RED_TEXT, RED_EDGE);

  // the launcher itself
  s = section_begin(x, x2, y2, col_w, row2, L"ЛАУНЧЕР");
  y = s.cy + 8;
  c.text(L"Цвет повязки — цвет сот и акцентов лаунчера", tiny, s.cx, y, MUTED);
  y += sm + 8;
  for (int i = 0; i < 4; ++i) swatch(x, i, s.cx + 8 + i * (44 + 18), y + 6);
  y += 56 + 8;
  toggle_row(x, H_Toggle3, L"Закрывать лаунчер, когда игра запущена", m.set.close_on_play, s.cx, y, cw);
}

// --- the game's settings: ГРАФИКА, ЗВУК, УПРАВЛЕНИЕ --------------------------------------------------

// A value with an arrow at each side.
void stepper(Ctx& x, int id_prev, int id_next, const std::wstring& value, float sx, float sy, float w, float h, bool on) {
  Canvas& c = x.c;
  c.box(sx, sy, w, h, FIELD, 2, on ? EDGE : LINE);
  const int ids[2] = {id_prev, id_next};
  for (int side = 0; side < 2; ++side) {
    const float bx = side == 0 ? sx + 2 : sx + w - 2 - 40;
    const bool over = on && x.m.hover == ids[side], down = on && x.m.pressed == ids[side];
    if (over || down) c.fill(bx, sy + 2, 40, h - 4, PAPER, down ? 0.16f : 0.08f);
    c.icon(side == 0 ? kPrev : kNext, bx + 11, sy + (h - 18) / 2, 18, on ? PAPER : DIM, 2.8f);
    c.fill(side == 0 ? sx + 42 : sx + w - 44, sy + 2, 2, h - 4, LINE);
    if (on) hit(x, ids[side], side == 0 ? sx : sx + w - 44, sy, 44, h);
  }
  Font f = T(13, 700);
  TextOpt one;
  one.ellipsis = true;
  one.max_w = w - 88 - 8;
  float tw = c.measure(value, f).width;
  if (tw > one.max_w) tw = one.max_w;
  c.text_mid(value, f, sx + (w - tw) / 2, sy + h / 2, on ? PAPER : DIM, one);
}

// One of the buttons that stand in a row, the chosen one lit.
void seg_button(Ctx& x, int id, const wchar_t* label, float bx, float by, float w, float h, bool chosen, bool on) {
  Canvas& c = x.c;
  const bool over = on && x.m.hover == id, down = on && x.m.pressed == id;
  c.fill(bx, by, w, h, chosen ? (on ? PLATE : LINE) : PANEL);
  if (!chosen && (over || down)) c.fill(bx, by, w, h, PAPER, down ? 0.14f : 0.07f);
  c.border(bx, by, w, h, 2, chosen ? BLACK : over ? PAPER : on ? EDGE : LINE);
  Font f = T(13, 700);
  c.text_mid(label, f, bx + (w - c.measure(label, f).width) / 2, by + h / 2, chosen ? (on ? INK : TEXT2) : on ? PAPER : DIM);
  if (on) hit(x, id, bx, by, w, h);
}

// A sound level: quieter, ten blocks, louder. Ends at `right`.
void level(Ctx& x, gset::Row r, float right, float sy, float h, bool on) {
  Canvas& c = x.c;
  const int base = H_Row + r * kRowParts, v = gset::value(r);
  const float bw = 13, gap = 4, blocks = gset::kLevelMax * bw + (gset::kLevelMax - 1) * gap;
  const float plus = right - 44, first = plus - 8 - 4 - blocks, minus = first - 4 - 8 - 44;
  const struct {
    int id;
    float bx;
    const char* icon;
  } ends[2] = {{base + P_Minus, minus, kMinus}, {base + P_Plus, plus, kPlus}};
  for (const auto& e : ends) {
    const bool over = on && x.m.hover == e.id, down = on && x.m.pressed == e.id;
    if (over || down) c.fill(e.bx, sy, 44, h, PAPER, down ? 0.14f : 0.07f);
    c.border(e.bx, sy, 44, h, 2, over ? PAPER : on ? EDGE : LINE);
    c.icon(e.icon, e.bx + 13, sy + (h - 18) / 2, 18, on ? PAPER : DIM, 2.8f);
    if (on) hit(x, e.id, e.bx, sy, 44, h);
  }
  const float cy = sy + h / 2;
  for (int i = 1; i <= gset::kLevelMax; ++i) {
    const float bx = first + (i - 1) * (bw + gap);
    const bool lit = i <= v, over = on && x.m.hover == base + P_Item + i;
    c.push(Matrix::Skew(-12, 0, D2D1::Point2F(bx + bw / 2, cy)));
    c.box(bx, cy - 11, bw, 22, lit ? (on ? PLATE : LINE) : TRACK, 2, lit && on ? BLACK : over ? PAPER : on ? EDGE : LINE);
    c.pop();
    if (on) hit(x, base + P_Item + i, bx - gap / 2, sy, bw + gap, h);
  }
}

// A row of the game's settings: its name, and at the right what sets it.
void game_row(Ctx& x, gset::Row r, float lx, float ty, float w, float h) {
  Canvas& c = x.c;
  const bool on = gset::usable(r);
  const int base = H_Row + r * kRowParts;
  c.text_mid(gset::label(r), T(14), lx, ty + h / 2, on ? PAPER : DIM);
  switch (gset::kind(r)) {
    case gset::Kind::Choice:
      stepper(x, base + P_Prev, base + P_Next, gset::name(r, gset::value(r)), lx + w - 220, ty, 220, h, on);
      break;
    case gset::Kind::Level:
      level(x, r, lx + w, ty, h, on);
      break;
    case gset::Kind::Pair: {
      const float half = (240 - 8) / 2.0f;
      for (int i = 0; i < 2; ++i)
        seg_button(x, base + P_Item + i, gset::name(r, i).c_str(), lx + w - 240 + i * (half + 8), ty, half, h, gset::value(r) == i, on);
      break;
    }
  }
}

// The line under a page: a note at the left, "По умолчанию" at the right. It stands on the bricks of the
// wall, whatever the height of the boxes above.
void page_foot(Ctx& x, float left, const std::wstring& note, uint32_t rgb, bool reset_on) {
  Canvas& c = x.c;
  const float y = kH - 44 - 5 - 44;
  Font bf = T(13, 700);
  const float bw = 2 + 16 + c.measure(L"По умолчанию", bf).width + 16 + 2, right = kW - left;
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = right - bw - 20 - (left + 18 + 10);
  wrap.line_h = 12 * 1.45f;
  const float nh = c.measure(note, T(12), wrap).height, ny = y + (44 - nh) / 2;
  c.push(Matrix::Scale(18.0f / 24, 18.0f / 24) * Matrix::Translation(left, ny));
  c.ring(12, 12, 9, 2.2f, rgb);
  c.stroke_path(kNote, 2.2f, rgb);
  c.pop();
  c.text(note, T(12), left + 18 + 10, ny, rgb, wrap);
  if (reset_on) {
    ghost_button(x, H_Reset, L"По умолчанию", right - bw, y, 44, 16, false);
  } else {
    c.border(right - bw, y, bw, 44, 2, LINE);
    c.text_mid(L"По умолчанию", bf, right - bw + 18, y + 22, DIM);
  }
}

const wchar_t kNoMod[] = L"Мод не установлен. Эти настройки появятся вместе с ним: они хранятся в meleenet.ini в папке игры.";

// ГРАФИКА and ЗВУК: two boxes of rows.
void page_game(Ctx& x, float left, float top, bool sound) {
  Canvas& c = x.c;
  struct Group {
    const wchar_t* name;
    int from, to;
    std::wstring note;
    uint32_t note_rgb;
  };
  const Group groups[2][2] = {
      {{L"ЭКРАН", gset::kMode, gset::kFirstPicture, L"", MUTED}, {L"КАРТИНКА", gset::kFirstPicture, gset::kFirstSound, L"", MUTED}},
      {{L"СЕТЕВАЯ ИГРА", gset::kFirstSound, gset::kFirstGame,
        std::wstring(L"Над вашим бойцом — ваш ник из Steam. Задержка ввода — у всех игроков та, что у хоста: больше задержка — ровнее игра "
                     L"при плохой связи."), MUTED},
       {L"МЫШЬ", gset::kFirstGame, gset::kRows,
        std::wstring(L"Кнопки мыши назначаются на вкладке «Управление», как клавиши."), MUTED}}};
  const Group* page = groups[sound ? 1 : 0];
  const float col_w = (kW - 2 * left - 28) / 2.0f, cw = col_w - 4 - 32;
  const int most = std::max(page[0].to - page[0].from, page[1].to - page[1].from);
  // (seven rows in a box: they stand a little closer than the six the page was drawn for)
  const float row_h = most > 6 ? 40.0f : 44.0f, row_gap = most > 6 ? 4.0f : 6.0f;
  const float h3 = c.line_h(D(13, 0.06f));
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = cw;
  wrap.line_h = 12 * 1.45f;
  float box_h = 0;
  for (int i = 0; i < 2; ++i) {
    const int n = page[i].to - page[i].from;
    float h = 2 + 14 + h3 + 2 + 6 + n * row_h + (n - 1) * row_gap + 14 + 2;
    if (!page[i].note.empty()) h += row_gap + 4 + c.measure(page[i].note, T(12), wrap).height;
    box_h = std::max(box_h, h);
  }
  const float y1 = top + 44 + 16;
  for (int i = 0; i < 2; ++i) {
    Section s = section_begin(x, left + i * (col_w + 28), y1, col_w, box_h, page[i].name);
    float y = s.cy + 2 + 6;
    for (int r = page[i].from; r < page[i].to; ++r) {
      game_row(x, static_cast<gset::Row>(r), s.cx, y, cw, row_h);
      y += row_h + row_gap;
    }
    if (!page[i].note.empty()) c.text(page[i].note, T(12), s.cx, y + 4, page[i].note_rgb, wrap);
  }
  const bool mod = gset::has_mod();
  const wchar_t* note = !mod ? kNoMod
                        : sound ? L"Настройки мода хранятся в meleenet.ini в папке игры. Сама сетевая игра — в главном меню игры: "
                                  L"СЕТЕВАЯ ИГРА. Действуют со следующего запуска игры."
                                : L"Настройки мода хранятся в meleenet.ini в папке игры. Действуют со следующего запуска игры.";
  page_foot(x, left, note, mod ? MUTED : WAIT, mod);
}

// УПРАВЛЕНИЕ: who plays at the keyboard, the layout, a cell for every action's key.
void page_controls(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const bool on = gset::has_mod(), two = gset::keyboard_players() == 2;
  const int rows = two ? 4 : 5;
  const float cell_gap = two ? 6.0f : 4.0f, grid_h = rows * 44 + (rows - 1) * cell_gap;
  const float h3 = c.line_h(D(13, 0.06f)), y1 = top + 44 + 16;
  const float box_h = 2 + 14 + h3 + 10 + 44 + 10 + 2 + 10 + grid_h + 14 + 2;
  Section s = section_begin(x, left, y1, kW - 2 * left, box_h, L"КЛАВИАТУРА И МЫШЬ");
  float y = s.cy + 10;
  const float half = (240 - 8) / 2.0f;
  (void)half;
  c.text_mid(L"Игрок 1 на этом компьютере", T(14), s.cx, y + 22, on ? TEXT2 : DIM);
  if (two) {
    const wchar_t* legend = L"левая половина клавиатуры · правая половина";
    c.text_mid(legend, T(13), s.cx + s.cw - c.measure(legend, T(13)).width, y + 22, TEXT2);
  } else {
    const wchar_t* names[3] = {L"Как в игре", L"WASD + мышь", L"Своя"};
    stepper(x, H_LayoutPrev, H_LayoutNext, names[gset::layout() % 3], s.cx + s.cw - 220, y, 220, 44, on);
    c.text_mid(L"Раскладка", T(14), s.cx + s.cw - 220 - 14 - c.measure(L"Раскладка", T(14)).width, y + 22, on ? PAPER : DIM);
  }
  y += 44 + 10;
  c.fill(s.cx, y, s.cw, 2, LINE);
  y += 2 + 10;
  const float col_w = (s.cw - 2 * 28) / 3;
  Font kf = Mo(12, 700);
  if (two) {
    for (int i = 0; i < gset::kHalfRows; ++i) {
      const float cx = s.cx + (i / rows) * (col_w + 28), cy = y + (i % rows) * (44 + cell_gap);
      c.text_mid(gset::half_label(i), T(14), cx, cy + 22, on ? PAPER : DIM);
      const std::wstring keys = gset::half_keys(i);
      const float bx = cx + col_w - 150;
      c.border(bx, cy, 150, 44, 2, LINE);
      c.text_mid(keys, kf, bx + (150 - c.measure(keys, kf).width) / 2, cy + 22, on ? TEXT2 : DIM);
    }
  } else {
    for (int a = 0; a < gset::kKeyActions; ++a) {
      const float cx = s.cx + (a / rows) * (col_w + 28), cy = y + (a % rows) * (44 + cell_gap);
      const bool waiting = on && m.capture == a, over = on && m.hover == H_Key + a;
      c.text_mid(gset::action_label(a), T(14), cx, cy + 22, on ? PAPER : DIM);
      const float bx = cx + col_w - 128;
      c.box(bx, cy, 128, 44, waiting ? LIVE : FIELD, 2, waiting ? x.accent : over ? PAPER : on ? EDGE : LINE);
      const std::wstring text = waiting ? std::wstring(L"нажмите…") : gset::key_name(gset::key_code(a));
      TextOpt one;
      one.ellipsis = true;
      one.max_w = 128 - 16;
      float tw = c.measure(text, kf).width;
      if (tw > one.max_w) tw = one.max_w;
      c.text_mid(text, kf, bx + (128 - tw) / 2, cy + 22, waiting ? x.accent : on ? PAPER : DIM, one);
      if (on) hit(x, H_Key + a, bx, cy, 128, 44);
    }
  }
  const wchar_t* note = !on ? kNoMod
                        : m.capture >= 0 ? L"Нажмите клавишу или кнопку мыши: правую, среднюю, боковую — или левую по этой же ячейке. Esc — отмена."
                        : two ? L"Двое за одной клавиатурой: у каждой половины свои постоянные клавиши. Enter и Esc остаются для меню. "
                                L"Кнопки геймпада настраиваются в игре."
                              : L"Клавиши записываются в файл привязок самой игры (tmntwire.hks). В меню всегда работают стрелки, Enter и Esc. "
                                L"По сети каждый играет этими клавишами.";
  page_foot(x, left, note, !on ? WAIT : m.capture >= 0 ? TEXT2 : MUTED, on);
}

void settings(Ctx& x) {
  const float left = 40, top = 48 + 20;
  settings_head(x, left, top);
  switch (x.m.tab) {
    case Tab::Graphics: page_game(x, left, top, false); break;
    case Tab::Sound: page_game(x, left, top, true); break;
    case Tab::Controls: page_controls(x, left, top); break;
    default: page_launcher(x, left, top); break;
  }
}

}  // namespace

bool primary_enabled(const Model& m) { return vals(m).primary_on; }
std::wstring link_url(const Model& m) { return vals(m).link_url; }

void draw(gfx::Canvas& c, const Model& m, std::vector<Hit>* hits) {
  if (hits) hits->clear();
  Ctx x{c, m, hits, kBandana[m.set.accent & 3]};
  const uint32_t accent = x.accent;
  c.cached(10 + (m.set.accent & 3), kW, kH, [accent](Canvas& bg) { background(bg, accent); });
  Vals v = vals(m);
  header(x);
  if (m.screen == Screen::Settings) settings(x);
  else hero(x, v);
  footer(x, v);
}

// The screens as the design shows them.
bool demo(const std::wstring& screen, Model& m) {
  m = Model{};
  m.has_game = true;
  m.set.game_dir = L"D:\\Games\\TMNT Mutant Melee";
  m.inst.present = true;
  m.repo = true;
  m.inst.version = L"0.2.0";
  m.inst.protocol = 2;
  m.rel.known = m.rel.full = true;
  m.rel.version = L"0.3.0";
  m.rel.protocol = 3;
  m.rel.date = L"24 ноября 2026";
  m.rel.title = L"Без перезапусков, имена над бойцами";
  m.rel.notes = {L"Сетевая игра начинается прямо в запущенной игре: окна не закрываются.",
                 L"Вход в идущую игру и возврат после обрыва — тоже без перезапуска.",
                 L"Над бойцами — имена игроков: ники из Steam."};
  m.rel.pkg_name = L"meleenet-launcher.exe";
  m.rel.pkg_size = 4500000;
  m.rel.pkg_sha256 = "x";
  m.pack.known = true;
  m.pack.version = L"0.2.0";
  m.pack.protocol = 2;
  m.pack.title = L"«История» вместе, вход в идущую игру";
  m.pack.notes = {L"«История» по сети — вместе: игроки сессии дерутся на стороне героя.",
                  L"Паузу ставит любой игрок; слабый ПК не замедляет бой остальным."};
  m.set.notes_version = L"0.2.0";
  m.set.notes_title = L"«История» вместе, вход в идущую игру";
  m.set.notes = {L"«История» по сети — вместе: игроки сессии дерутся на стороне героя.",
                 L"Паузу ставит любой игрок; слабый ПК не замедляет бой остальным."};
  m.total = 4500000;
  m.got = static_cast<uint64_t>(4500000 * 0.62);
  if (screen == L"update") {
    m.screen = Screen::Update;
  } else if (screen == L"downloading") {
    m.screen = Screen::Downloading;
    m.pending = Pending::Install;
  } else if (screen == L"ready") {
    m.screen = Screen::Ready;
    m.inst.version = L"0.3.0";
    m.inst.protocol = 3;
  } else if (screen == L"checking") {
    m.screen = Screen::Checking;
    m.rel = Release{};
  } else if (screen == L"install") {
    m.screen = Screen::Install;
    m.inst = Installed{};
  } else if (screen == L"nogame") {
    m.screen = Screen::NoGame;
    m.has_game = false;
    m.inst = Installed{};
  } else if (screen == L"offline") {
    m.screen = Screen::Offline;
    m.rel = Release{};
  } else if (screen == L"blocked") {
    m.screen = Screen::Offline;
    m.rel.full = false;
    m.assets_blocked = true;
  } else if (screen == L"running") {
    m.screen = Screen::Running;
    m.pending = Pending::Install;
  } else if (screen == L"problem") {
    m.screen = Screen::Problem;
    m.problem_title = L"Файл повреждён";
    m.problem_text = L"Контрольная сумма скачанного файла не совпала с той, что указана в релизе. Файл удалён — скачайте его ещё раз.";
    m.problem_primary = L"СКАЧАТЬ ЗАНОВО";
    m.problem_secondary = L"НАЗАД";
  } else if (screen == L"settings") {
    m.screen = Screen::Settings;
    m.back = Screen::Update;
  } else if (screen == L"selfupdate") {  // at its start the launcher found a newer one and takes it by itself
    m.screen = Screen::Downloading;
    m.pending = Pending::Launcher;
    m.inst.version = L"0.3.0";
    m.inst.protocol = 3;
    m.pack = m.rel;
    m.rel.launcher_version = L"1.3.1";
  } else if (screen == L"graphics" || screen == L"sound" || screen == L"controls" || screen == L"controls2" ||
             screen == L"capture") {
    m.screen = Screen::Settings;
    m.back = Screen::Ready;
    m.inst.version = L"0.3.0";
    m.inst.protocol = 3;
    m.tab = screen == L"graphics" ? Tab::Graphics : screen == L"sound" ? Tab::Sound : Tab::Controls;
    gset::load_canned(screen == L"controls2");
    if (screen == L"capture") m.capture = 6;
  } else {
    return false;
  }
  return true;
}

}  // namespace view
}  // namespace lz
