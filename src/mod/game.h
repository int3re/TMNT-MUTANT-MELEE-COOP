// Addresses and layouts of ztmnt.exe (TMNT Mutant Melee, PC, the build of Nov 22 2004, 1 753 088 bytes).
// The exe is not packed: every address is valid from the first instruction. Each one is used only
// after the exe's SHA-256 matched (dllmain.cpp), and every patched place is checked byte by byte.
//
// The game is Konami Hawaii's own engine on RenderWare 3.5 (Direct3D 8). Names follow the exe's
// RTTI (TtMode, TtGameMode, ...) and its debug menu's labels (sys_main.cpp).
#pragma once
#include <cstdint>

namespace game {

// SHA-256 of the supported ztmnt.exe.
inline const uint8_t kExeSha256[32] = {0x58, 0x5a, 0x58, 0x09, 0x34, 0x9e, 0x5e, 0x31, 0x35, 0x2f, 0x80,
                                       0xb9, 0x44, 0x45, 0x38, 0x2d, 0x69, 0x82, 0x4d, 0x3e, 0x3c, 0x78,
                                       0xba, 0x84, 0xe3, 0x8c, 0xf5, 0x19, 0xf9, 0xbb, 0xdc, 0xe2};

// --- the main loop (sys_main.cpp) ---------------------------------------------------------------
// WinMain 0x401040 -> init 0x403D40 -> loop 0x405E10: { movie/busy 0x405370; frame 0x405C00(1) }.
// A frame: the mode's Update (vtable +8), the draw 0x4051E0 (mode's Draw, vtable +0xC), a pending
// mode change, then "system update" 0x408CB0 = pads' previous state kept, window messages, the
// clock 0x4062F0, the devices read into the pads 0x408580.
constexpr uintptr_t kSystemUpdate = 0x00408CB0;  // void __cdecl()
constexpr uintptr_t kClockStep = 0x004062F0;     // void __cdecl(): Delta Time for the coming frame
constexpr uintptr_t kReadDevices = 0x00408580;   // void __cdecl(): keyboard + joysticks -> pads
constexpr uintptr_t kFrame = 0x00405C00;         // void __cdecl(char update)
constexpr uintptr_t kDraw = 0x004051E0;          // void __cdecl(int)
// While a level loads on the main thread (inside the load mode's Update), the game's "Background
// Graphics Thread" (loop at 0x4109C0) calls kFrame(0) again and again: the loading screen is drawn and
// the system update runs, without any mode's Update.
// A Bink movie is a loop inside one call: a picture and a system update per movie frame.
constexpr uintptr_t kPlayMovie = 0x00408E50;     // void __cdecl(const char* name): moviesPC/<name>.bik

// In the frame function: "mov eax, [ecx]; call [eax+4]" = the new mode's Enter (ecx = the mode).
constexpr uintptr_t kEnterCall = 0x00405CA3;
// The game's modes are objects made once at start-up (0x405430); pointers to them lie here.
constexpr uintptr_t kModeTableBegin = 0x005B08B4, kModeTableEnd = 0x005B0958;
constexpr uintptr_t kMakeModes = 0x00405430;     // void __cdecl(): makes them all
// Sizes of the mode objects, in the table's order (the factory's allocations).
inline const uint16_t kModeSizes[] = {0x20,  0x2C, 0xB0, 0x7C, 0x18, 0x10, 0x10, 0x24, 0x18, 0x34, 0x14, 0x28, 0x18, 0x18,
                                      0x10,  0x614, 0x54, 0x50, 0x10, 0xA8, 0x14, 0x14, 0x1C, 0x14, 0x20, 0x14, 0x14, 0x14,
                                      0x10,  0x24, 0x24, 0x20, 0x14, 0x40, 0x20, 0x10, 0x31C, 0x14, 0x1C, 0x14, 0x18};

constexpr uintptr_t kQuit = 0x005AEFD0;          // uint8: the main loop ends
constexpr uintptr_t kMode = 0x005AF0F8;          // TtMode*: the mode being played
constexpr uintptr_t kNextMode = 0x005AF0FC;      // TtMode*: the mode asked for (0 = none)

// Stopwatches of real time: start = QueryPerformanceCounter into *stamp, read = seconds since (in ST0).
constexpr uintptr_t kStopwatchStart = 0x00406290;  // void __cdecl(LARGE_INTEGER* stamp)
constexpr uintptr_t kStopwatchRead = 0x004062A0;   // long double __cdecl(LARGE_INTEGER* stamp)

// The clock (floats, seconds). Delta = real time of the last frame x Speed Scale, smoothed
// (Ave = 0.1 * dt + 0.9 * Ave) and, with the "hertz lock" on, the smoothed value itself.
constexpr uintptr_t kDeltaTime = 0x005AEFDC;
constexpr uintptr_t kAveDeltaTime = 0x005AEFD8;
constexpr uintptr_t kElapsedTime = 0x005AEFE0;
constexpr uintptr_t kFrameCount = 0x005AEFE4;    // int: passes of the clock
constexpr uintptr_t kSpeedScale = 0x005B0838;    // float, 0.85 in this build
constexpr uintptr_t kHertzLock = 0x005AEFF0;     // uint8

// --- pads (Pc\sys_input) --------------------------------------------------------------------------
// Eight records of 0xF4 bytes; the first four are the players'. A record: 19 floats now, 19 floats of
// the frame before, 19 floats of difference, then flags. A button is 0 or 1; the stick -1..1.
constexpr uintptr_t kPads = 0x005AE838;
constexpr int kPadSize = 0xF4;
constexpr int kPadValues = 19;
constexpr int kPadCount = 8;
constexpr int kPlayers = 4;
constexpr int kPadPrev = 0x4C;        // the frame before
constexpr int kPadDelta = 0x98;
constexpr int kPadConnected = 0xE8;   // uint8: a device is bound to this pad (tmntwire.hks)
// Indices of the 19 values.
enum PadValue {
  kStickX = 0,     // -1 left .. +1 right
  kStickY = 1,     // +1 up .. -1 down
  kSpecial = 2,    // tmntwire +0x28 (Space)
  kBlock = 3,      // +0x2C (B)
  kAttack = 4,     // +0x18 (N); also "confirm" in menus
  kAltAttack = 5,  // +0x1C (M)
  kJump = 6,       // +0x24 (J)
  kAction = 7,     // +0x20 (H)
  kEscape = 8,     // the Esc key in a fight (pause)
  kEnter = 9,      // "confirm" of menus: the Enter key (repeated every 0.3 s while held); a joystick's attack in menus
  kBack = 10,      // "back" of menus: the Esc key in menus; a joystick's jump in menus
  kStart = 11,     // also confirms
  kSpaceKey = 12,  // the Space key itself, whatever is bound
  kDebugX = 14,    // arrows while the debug menu is up
  kDebugY = 15,
  kDebugMenu = 16, // F1 (pad 0 only): the developers' menu
  kF11 = 17,
};

// The keyboard as DirectInput told it this frame (256 bytes, 0x80 = down).
constexpr uintptr_t kKeyboardState = 0x005B09A0;
// Per player: the binding record in use (a 0x30-byte record of tmntwire.hks) or null.
//   +0 pad number, +4 device (0 = keyboard, n = joystick n), +8 up, +0xC down, +0x10 left, +0x14 right,
//   +0x18 attack, +0x1C alt attack, +0x20 action, +0x24 jump, +0x28 special, +0x2C block
// For the keyboard the values are DirectInput key codes.
constexpr uintptr_t kBindings = 0x005B0738;       // int* [4]
constexpr uintptr_t kBindingRecords = 0x005AF478; // 100 records read from tmntwire.hks
constexpr uintptr_t kKeyboardOn = 0x005AF3D8;     // uint8 (-nokeyboard clears it)
constexpr uintptr_t kPrimaryPad = 0x005B088C;     // int: the pad that leads the menus
// uint8: the keyboard and joysticks are read as menu controls (arrows = stick, Enter, Esc) and not by
// the bindings. Modes switch it.
constexpr uintptr_t kMenuControls = 0x005B0880;

// --- screen (Pc\sys_d3d.cpp) -----------------------------------------------------------------------
constexpr uintptr_t kScreenW = 0x005AE804;       // int
constexpr uintptr_t kScreenH = 0x005AE808;
constexpr uintptr_t kScreenDepth = 0x005AE80C;   // 16 by default
constexpr uintptr_t kFullScreen = 0x005AE829;    // uint8: forced to 1 at start-up; 1 = the game minimizes when not in front
constexpr uintptr_t kWidescreen = 0x005AF3D3;    // uint8: the consoles' 16:9 switch
constexpr uintptr_t kHwnd = 0x005B0F4C;          // HWND
constexpr uintptr_t kLoadSettings = 0x004073D0;  // void __cdecl(): tmntwire.hks and tmnt.ini
constexpr uintptr_t kMakeWindow = 0x00408650;    // void __cdecl(): window, RenderWare's device
constexpr uintptr_t kPickVideoMode = 0x00407290; // int __cdecl(int* w, int* h, int* depth)
constexpr uintptr_t kFocusCheck = 0x00408B50;    // void __cdecl(): full screen: minimize and wait while not in front

// RenderWare's list of video modes: {width, height, depth, flags, refresh, format}.
constexpr uintptr_t kRwNumVideoModes = 0x00537EB0;  // int __cdecl()
constexpr uintptr_t kRwVideoModeInfo = 0x00537F00;  // void* __cdecl(RwVideoMode* out, int index)
struct RwVideoMode {
  int width, height, depth, flags, refresh, format;
};
// The game's probe of the video card's gamma (a second Direct3D device of its own): 0x406F00..0x406FCD.
constexpr uintptr_t kGammaProbeBegin = 0x00406F00, kGammaProbeEnd = 0x00406FD0;

// The consoles' wide screen. With the switch on the 3D view is 16:9 instead of 4:3 (the view window's
// width is aspect x height: wider at the sides), every 2D element's x is drawn towards the middle by
// kUiScaleX (0.75 = 4:3 / 16:9), and menus get two black bars over the sides (0x40AB90).
constexpr uintptr_t kAspectWideImm = 0x004020B5;  // float immediate 1.7777778
constexpr uintptr_t kAspectNarrowImm = 0x004020C1;  // float immediate 1.3333334
constexpr uintptr_t kUiScaleX = 0x00583B50;       // float 0.75
// The same shape told the other way round (1 / kUiScaleX), for what is laid out by the 2D toolkit:
// texts (0x409350: the toolkit's screen is this wide and begins at -(width - 1) / 2) and the black
// sheet of a fade (0x40AAB0).
constexpr uintptr_t kTextScreenWidth = 0x00583B54;  // float 1.3333
constexpr uintptr_t kBlackSheetWidth = 0x00583B58;  // float 1.3333
constexpr uintptr_t kBlackSheetLeft = 0x00583B5C;   // float -0.1667
constexpr uintptr_t kMenuBars = 0x0040AB90;       // void __cdecl(): the two bars
constexpr uintptr_t kBarWidthImm1 = 0x0040ABC6;   // float immediate 0.17: the left bar's width
constexpr uintptr_t kBarLeftImm = 0x0040ABCD;     // float immediate -0.17: the left bar's x
constexpr uintptr_t kBarWidthImm2 = 0x0040ABE1;   // float immediate 0.17: the right bar's width

// The story's map (TtAdventureGameSelectMode, Draw 0x4441A0). The map is one quad larger than the screen
// that follows the cursor (0x443ED0); over it the mode lays four dim strips of the same texture around
// the map's window (0x443D40): bottom, top, left, right, each given in fractions of the 4:3 screen, the
// texture going across the screen once.
constexpr uintptr_t kMapFrame = 0x00443D40;        // void __cdecl(float x, float y)
constexpr uintptr_t kMapTexture = 0x00673318;      // RwTexture* of the map (0: none); +0x50 filter, +0x51 addressing
constexpr uintptr_t kDrawQuad = 0x00409E80;        // void __cdecl(x, y, w, h, u, v, uw, vh, RwTexture*, colour*)
constexpr uintptr_t kWhite = 0x00583B34;           // the colour FFFFFFFF
constexpr uintptr_t kUiFractions = 0x005B109C;     // int: 2D coordinates are fractions of the screen (1), not layout units
constexpr uintptr_t kUiSetFractions = 0x004093D0;  // void __cdecl(int)

// The game's picture options (tmnt.ini).
constexpr uintptr_t kToonShading = 0x005AF460;    // int
constexpr uintptr_t kDynamicShadows = 0x005AF464; // int
constexpr uintptr_t kParticlePercent = 0x005AF468; // int 0..100

// Import slots of the exe.
constexpr uintptr_t kIatDirect3DCreate8 = 0x0055C228;
// RenderWare's Direct3D 8 driver keeps the parameters its device was made with at 0x687080; the picture
// function (0x541E6A..) resets the device when the "wait for the monitor" flag of a picture differs from them.
constexpr uintptr_t kDriverPresentInterval = 0x006870B0;  // FullScreen_PresentationInterval of those parameters

// --- the interface (ui.cpp) and texts ------------------------------------------------------------------
// Screens are scripts (ui/scripts/UI_*.txt in the archive): elements with a picture or a text ($N = string
// N of the language table), places in 640x480 units, an event (enter / exit) and times. The scripts
// singleton holds a list per screen; a mode "enters" its list into the display manager and "exits" it.
constexpr uintptr_t kUi = 0x00673548;            // the display manager*
constexpr uintptr_t kUiScripts = 0x005B08A0;     // the scripts singleton* (0x184 bytes)
constexpr int kUiListMainMenu = 0x10;            // its list of UI_ModeSel_PC.txt
constexpr uintptr_t kUiEnter = 0x0043B670;       // void __cdecl(ui, list): the list's "enter" elements appear
constexpr uintptr_t kUiExit = 0x0043B760;        // void __cdecl(ui, list): its "exit" elements play
constexpr uintptr_t kUiFind = 0x00438FA0;        // element* __cdecl(ui, const char* name); +0x30 chosen, +0x31 dimmed
constexpr uintptr_t kUiAnimating = 0x004391B0;   // bool __cdecl(ui): something still moves
constexpr uintptr_t kMenuUp = 0x0040A720;        // bool __cdecl(): any pad pressed up
constexpr uintptr_t kMenuDown = 0x0040A770;
constexpr uintptr_t kMenuBack = 0x0040A820;
constexpr uintptr_t kSoundMenuMove = 0x0043E260; // void __cdecl()
constexpr uintptr_t kSoundMenuOk = 0x0043E220;   // void __cdecl(int)
// The main menu (TtGameStartMode): leaving flag +0x18, cursor +0x1C (0..4), Update 0x44A660.
constexpr uintptr_t kMainMenuUpdate = 0x0044A660;  // void __thiscall(mode)
constexpr uintptr_t kPlayerSelectReset = 0x0044F770;  // void __thiscall(player select mode)
constexpr uintptr_t kModePlayerSelect = 0x005B08C0, kModeAdventureSelect = 0x005B091C, kModeLibrary = 0x005B0940,
                    kModeOptions = 0x005B08D0;
// The fight (TtMeleeGameMode, Update 0x42F490). Esc of the pad that leads the menus (kPrimaryPad) pauses;
// the pause menu (0x42F160: "go on" / "leave") is read from the same pad: stick up / down, Esc or Enter.
constexpr uintptr_t kModeMeleeGame = 0x005B08F4;
constexpr int kFightPaused = 0x35;       // uint8
constexpr int kFightPauseCursor = 0x38;  // int: 0 = go on, 1 = leave the fight
constexpr uintptr_t kDrawTag = 0x00419FF0;        // void __thiscall(fighter): the tag over a human's fighter ("1P")
constexpr uintptr_t kPlayerObjects = 0x005AF100;  // player*[8]: the fighters' slots
constexpr int kPlayerPad = 0x10B;                 // uint8: the pad that plays the slot (0xFF: none, the computer does)
constexpr int kPlayerTeam = 0x2D4;                // int
constexpr int kPlayerHealth = 0x488;              // int: health now
constexpr int kPlayerAi = 0x2C8;                  // int: the computer's kind of player; -1 = a human
// The story (tuning/adventure/<hero>.txt: stages of matches). A match's fighters are built from its
// records by 0x455F00: slot 0 the hero (a human on the leading pad), then the enemies (team 1), then the
// scripted friends (team 0), each friend and enemy led by the computer.
constexpr uintptr_t kBuildMatch = 0x00455F00;      // void __thiscall(match)
constexpr uintptr_t kResetMatch = 0x00454880;      // void __thiscall(match): every record back on its fighter (a round begins)
constexpr uintptr_t kRecordApply = 0x00453560;     // void __thiscall(record, int slot): health, team, rules of that fighter
constexpr uintptr_t kRecordInit = 0x00452B40;      // record* __thiscall(record): a fighter's record as new
constexpr uintptr_t kCharacterName = 0x0042C8F0;   // const char* __cdecl(int id): "leo", "don", "mike", "raph", ...
constexpr uintptr_t kCharactersToLoad = 0x006753C4;   // int* begin, int* end: the characters a match loads, the i-th for the i-th fighter
constexpr uintptr_t kStory = 0x00673E30;              // the story's object: +0x28 / +0x2C the stages (stage*, begin and end)
constexpr uintptr_t kStoryCharacters = 0x006753D0;    // int[8]: the character of every slot; [0] is the story's hero
constexpr uintptr_t kPlayerCount = 0x005AF1CC;    // int
// The scripts are read through the game's file functions (the archive).
constexpr uintptr_t kFileOpen = 0x00401A30;      // handle __cdecl(const char* name, const char* mode)
constexpr uintptr_t kFileGets = 0x004026D0;      // void __cdecl(char* line, int size, handle)
constexpr uintptr_t kFileEof = 0x00401C50;       // int __cdecl(handle)
constexpr uintptr_t kFileClose = 0x00401AA0;     // void __cdecl(handle)
// Texts: language/TMNT_LANGUAGE.CSV ("key,text" lines, UTF-16); a vector of wchar_t* at +8 of the language
// singleton; string N = line N.
constexpr uintptr_t kLoadLanguage = 0x00438510;  // void __cdecl()
constexpr uintptr_t kLanguage = 0x005B08A4;      // the singleton*
constexpr int kStrQuit = 6, kStrMelee = 390, kStrStory = 391, kStrLibrary = 392, kStrOptions = 393;
constexpr int kStrSpareFirst = 165, kStrSpareLast = 197;  // memory-card messages of another console
// Drawing text and a framed box directly (as the "controller unplugged" box does, 0x4037E0, after the mode's draw).
constexpr uintptr_t kDrawOverlay = 0x004037E0;   // void __cdecl()
constexpr uintptr_t kDraw2dBegin = 0x0040AA00;   // void __cdecl(int)
constexpr uintptr_t kDraw2dEnd = 0x004093F0;     // void __cdecl()
constexpr uintptr_t kDrawBox = 0x0042ECF0;       // void __cdecl(float xywh[4], uint32* colour): 0..1 units
constexpr uintptr_t kDrawText = 0x0040AC30;      // void __cdecl(font, wchar_t*, float size, float xywh[4], int, brush, int shadow, int)
constexpr uintptr_t kFont = 0x00672EC8;          // the menus' font
constexpr uintptr_t kRasterLock = 0x00538990;    // uint8* __cdecl(raster, uint8 level, int mode): 1 write, 2 read
constexpr uintptr_t kRasterUnlock = 0x005387A0;  // raster* __cdecl(raster): mip levels are made anew
constexpr uintptr_t kBrushWhite = 0x005B1078;
constexpr uintptr_t kRwRenderStateSet = 0x00536380;  // void __cdecl(int state, void* value); state 1 = the texture
constexpr uintptr_t kBrushColors = 0x00501190;   // void __cdecl(brush, c*, c*, c*, c*)

// --- the save ------------------------------------------------------------------------------------------
// tmnt.sv next to the exe; the name is pushed at four places of the memory-card code (the address of the
// text is the operand).
constexpr uintptr_t kSaveNameText = 0x0056E3A4;  // "tmnt.sv"
constexpr uintptr_t kSaveName1 = 0x004CA150, kSaveName2 = 0x004CA302, kSaveName3 = 0x004CA4C5, kSaveName4 = 0x004CA55B;

// --- sound ---------------------------------------------------------------------------------------------
// One function takes every request to the sound system: (command, int* args, count). It answers 0 to
// everything on a PC without a sound device.
constexpr uintptr_t kSoundCommand = 0x004C44E0;  // uint __cdecl(int cmd, int* args, int n)
constexpr int kSndVoicePlay = 0x19;              // args[0] = the event whose line is to be said
constexpr int kSndVoiceBusy = 0x1E;              // args[0] = 0: is a line being said (the stream's state is not 2)
// The lines: soundPC\VCLIP.BKT = {size of the head, events, lines, offset of events, offset of lines},
// events of {u16 first line, u8 chance, u8 lines}, lines of {begin, end} in the file: 22050 Hz, 16 bit, mono.
constexpr uintptr_t kVoiceStart = 0x004DEFD0;    // u16 __cdecl(int event): draws a line with rand(), starts the stream
constexpr uintptr_t kVoiceStream = 0x0068274C;   // the stream object (state at +0x80: 2 = idle; handle at +0x94)
constexpr uintptr_t kVoiceFile = 0x00682748;     // the open bank
constexpr uintptr_t kStreamSetup = 0x004DD6D0;   // void __cdecl(stream, file, begin, end, 0, 0, 0)
constexpr uintptr_t kStreamPlay = 0x004DD840;    // void __cdecl(stream)

// --- random numbers ----------------------------------------------------------------------------------
// RenderWare's RpRandom is the game's generator (71 functions draw from it); the C library's rand()
// only varies the pitch of sounds.
constexpr uintptr_t kRpRandom = 0x00500D80;      // uint __cdecl()
constexpr uintptr_t kPtxUpdate = 0x00481A50;     // void __thiscall(TtPtxSystem*): a particle system's frame
constexpr uintptr_t kRpRandomSeed = 0x00500E10;  // void __cdecl(uint)
constexpr uintptr_t kRwGlobals = 0x00682C84;     // RwGlobals* (RwEngineInstance)
constexpr uintptr_t kRandomOffset = 0x00683224;  // int: offset of the plugin's state in RwGlobals
constexpr uintptr_t kCrtRandSeed = 0x005A7080;   // the C library's rand() state

}  // namespace game
