// meleenet.ini, read once at start-up. Sections: [log], [video], [input], [net], [debug].
// Unknown keys are ignored; a missing file means defaults.
#pragma once
#include <string>
#include <vector>

#include "common/log.h"

struct Config {
  // [log]
  tlog::Level log_level = tlog::Info;

  // [video]
  // mode = borderless: a window without a frame over the whole monitor (no mode change, Alt+Tab at once).
  //        window: an ordinary window.
  //        game: the game's own exclusive full screen (the mod leaves the screen alone).
  enum class Mode { Game, Borderless, Window };
  Mode mode = Mode::Borderless;
  // resolution: the size of the frame the game renders. auto (0 x 0): the monitor's in borderless mode, the
  // window's in window mode.
  int res_w = 0, res_h = 0;
  // aspect = wide: the frame has the shape of the monitor (or window) and the view is wider at the sides.
  // aspect = 4:3: the game's own 4:3 picture, black bars at the sides.
  bool wide = true;
  // menus = wide: the menus fill a wide picture (their backgrounds are made as wide as the picture);
  // bars: the game's own 4:3 menus with black bars at the sides.
  bool menu_bars = false;
  // Size of the window's client area in window mode; 0 = three quarters of the desktop's height.
  int window_w = 0, window_h = 0;
  // vsync = 1 | 0.
  bool vsync = true;
  // antialiasing = 0 | 2 | 4 | 8: smoothed edges (MSAA); anisotropy = 0 | 2 | 4 | 8 | 16: sharp textures at an angle.
  int antialiasing = 4;
  int anisotropy = 16;
  // The game's own picture options (tmnt.ini keeps them too; the mod's ini wins when the key is there).
  int toon = -1, shadows = -1, particles = -1;  // -1: the game's tmnt.ini decides

  // [input]
  // mouse = 1: the mouse buttons are keys the controls can be bound to (codes 0xF0 left, 0xF1 right,
  // 0xF2 middle, 0xF3 / 0xF4 side buttons).
  bool mouse = true;
  // background = 1: keys are read while the game's window is not in front (lab runs only).
  bool keys_in_background = false;

  // [net]
  // mode = off: the network game is opened from the game's menu. host | join: at once, for the lab.
  enum class Net { Off, Host, Join };
  Net net_mode = Net::Off;
  int net_port = 47830;
  std::string net_connect = "127.0.0.1";  // host[:port]
  std::string net_name = "player";
  int net_delay = 2;     // frames between reading a pad and the frame that uses it
  int net_players = 2;   // host, lab: the session starts when this many are in
  int net_wait_s = 120;
  // transport = auto: Steam when it runs, and the address (ENet) besides; steam | enet: only that one;
  // mock: the lab's stand-in for Steam (several players on one PC, no account).
  std::string net_transport = "auto";
  std::string net_lobby = "friends";  // the host's Steam lobby: friends | public
  std::string net_via;
  unsigned long long net_peer = 0;

  // [debug] - for the lab only; never set in a player's ini.
  int net_loss = 0;
  unsigned net_outage_at_ms = 0, net_outage_ms = 0;
  unsigned net_stall_at_ms = 0, net_stall_ms = 0;
  unsigned net_quit_ms = 0;
  int mock_slot = 1;
  std::string mock_dir;
  int mock_loss_pct = 0;
  int mock_latency_ms = 0;
  int mock_send_rate = 0;
  bool mock_lobby = true;
  int mock_member_delay_ms = 0;
  // autoquit = N: end the process N seconds after the mod started (0 = never).
  int autoquit_s = 0;
  // autoquit_frame = N: end the process when game frame N has been played.
  unsigned autoquit_frame = 0;
  // guard = 1: end the process instead of letting it make a full-screen device.
  bool guard = false;
  // no_activate = 1: the game's window is shown without taking the focus, never on top.
  bool no_activate = false;
  // offscreen = 1: the window is placed outside the desktop.
  bool offscreen = false;
  // shots = 60, 600, ...: game frames saved to screenshots\meleenet_f<frame>.bmp; shot_every = N: every Nth.
  std::vector<unsigned> shots;
  unsigned shot_every = 0;
  // script = <file next to the exe>: presses by game frame, lines of "<frame>[-<frame>] [p<N>.]<control>".
  std::string script;
  // fast = 1: frames are not held to 60 a second.
  bool fast = false;
  // sim_log = 1: one line per game frame in meleenet-sim.log (to compare two runs).
  bool sim_log = false;
  // rng_trace = <from>-<to>: for these game frames every draw of a random number goes to meleenet-rng.log.
  unsigned rng_trace_from = 0, rng_trace_to = 0;
  // mute = 1: the copy's sound is muted in Windows' mixer.
  bool mute = false;
  // args = <text>: added to the game's command line (-noui -level ..., the developers' switches).
  std::string args;
  // free_clock = 1: the game's own clock (real time), as without the mod.
  bool free_clock = false;
  // unlock = 1: everything unlocked for this run (the game's -unlocked).
  bool unlock = false;
  // movies = 0: the game's movies are not played.
  bool movies = true;
  // epoch = 1: when the main menu is up for the first time it is entered anew with the clock and the random
  // numbers reset (what a session's start does); the script's frames and the sim log count from there.
  bool lab_epoch = false;
  // notice = N: a test notice is shown from game frame N for ten seconds.
  unsigned lab_notice = 0;
  // page = 1: the run goes through the network page: autoquit_frame counts in the session's game only.
  bool lab_page = false;
  // address = 1: the network page's address row starts as [net] connect, whatever it is (127.0.0.1 in the lab).
  bool lab_address = false;
  // keep_modes = 1: the modes are not put back at an epoch (to see what that is needed for).
  bool lab_keep_modes = false;
  // epoch_at = N: ... not before game frame N (the copy has a history of its own by then).
  unsigned lab_epoch_at = 0;
  // mode_log = 1: every change of the game's mode goes to the log.
  bool mode_log = true;
};

// Folder of the exe, with a trailing backslash.
std::wstring module_dir();

// Returns false if the file is missing (defaults are used).
bool load_config(const std::wstring& path, Config& cfg);
