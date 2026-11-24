cmake_minimum_required(VERSION 3.20)
project(melee_netplay C CXX RC)

# ztmnt.exe is a 32-bit process: everything that goes into it must be x86.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure with -A Win32: ztmnt.exe is a 32-bit process")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: players do not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

# --- third_party: MinHook (x86 hooks) ----------------------------------------
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# --- third_party: ENet (UDP with reliable/unreliable channels), from the TMNT1 tree --
set(TMNT1_DIR "${CMAKE_SOURCE_DIR}/../tmnt2003-netplay" CACHE PATH "tmnt2003-netplay checkout (third_party/enet)")
set(ENET_DIR ${TMNT1_DIR}/third_party/enet)
if(NOT EXISTS ${ENET_DIR}/host.c)
  message(FATAL_ERROR "ENet source not found in ${ENET_DIR}")
endif()
add_library(enet STATIC
  ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c ${ENET_DIR}/packet.c
  ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/win32.c)
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
target_compile_options(enet PRIVATE /wd5287)
target_link_libraries(enet PUBLIC ws2_32 winmm)

# --- our code ------------------------------------------------------------------
set(OUR_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS= _WINSOCK_DEPRECATED_NO_WARNINGS UNICODE _UNICODE)
set(OUR_OPTS /W4 /permissive- /utf-8)

add_library(meleenet_common STATIC
  src/common/log.cpp
  src/common/sha256.cpp
  src/common/net.cpp
  src/common/transport_enet.cpp
  src/common/transport_steam.cpp
  src/common/steam_boot.cpp
  src/common/steam_mock.cpp)
target_include_directories(meleenet_common PUBLIC src)
target_compile_definitions(meleenet_common PUBLIC ${OUR_DEFS})
target_compile_options(meleenet_common PUBLIC ${OUR_OPTS})
target_link_libraries(meleenet_common PUBLIC enet bcrypt ws2_32 iphlpapi)

# --- optional: Steam (Steamworks SDK; never part of the repo) -----------------------
#   cmake -B build -A Win32 -DSTEAMWORKS_SDK_DIR=C:/sdk/steamworks_sdk/sdk
# Without the SDK everything still builds: the address (ENet) and the lab's mock only.
set(STEAMWORKS_SDK_DIR "${CMAKE_SOURCE_DIR}/../steamworks_sdk_165/sdk" CACHE PATH "Steamworks SDK (public/steam, redistributable_bin)")
set(STEAM_API_DLL ${STEAMWORKS_SDK_DIR}/redistributable_bin/steam_api.dll)
if(EXISTS ${STEAMWORKS_SDK_DIR}/public/steam/isteamnetworkingmessages.h AND
   EXISTS ${STEAMWORKS_SDK_DIR}/redistributable_bin/steam_api.lib AND EXISTS ${STEAM_API_DLL})
  message(STATUS "Steam: ON (${STEAMWORKS_SDK_DIR})")
  add_library(steam_api SHARED IMPORTED)
  set_target_properties(steam_api PROPERTIES
    IMPORTED_IMPLIB ${STEAMWORKS_SDK_DIR}/redistributable_bin/steam_api.lib
    IMPORTED_LOCATION ${STEAM_API_DLL}
    INTERFACE_INCLUDE_DIRECTORIES ${STEAMWORKS_SDK_DIR}/public)
  target_sources(meleenet_common PRIVATE src/common/steam_real.cpp src/common/lobby_steam.cpp)
  target_compile_definitions(meleenet_common PUBLIC MELEENET_STEAM=1)
  file(STRINGS ${STEAMWORKS_SDK_DIR}/public/steam/steam_api.h STEAM_INITEX REGEX "SteamAPI_InitEx")
  if(STEAM_INITEX)
    target_compile_definitions(meleenet_common PRIVATE MELEENET_STEAM_INITEX=1)
  endif()
  target_link_libraries(meleenet_common PUBLIC steam_api delayimp)
  set(MELEENET_STEAM ON)
else()
  message(WARNING "Steam: OFF (no Steamworks SDK at ${STEAMWORKS_SDK_DIR}); the address only")
  target_sources(meleenet_common PRIVATE src/common/steam_stub.cpp)
  set(MELEENET_STEAM OFF)
endif()

# meleenet.asi: loaded into ztmnt.exe by Ultimate ASI Loader (dinput8.dll)
add_library(meleenet MODULE
  src/mod/dllmain.cpp
  src/mod/config.cpp
  src/mod/patch.cpp
  src/mod/diag.cpp
  src/mod/video.cpp
  src/mod/input.cpp
  src/mod/sim.cpp
  src/mod/sound.cpp
  src/mod/notice.cpp
  src/mod/gametext.cpp
  src/mod/menu.cpp
  src/mod/uiwide.cpp
  src/mod/netlobby.cpp
  src/mod/netplay.cpp)
target_link_libraries(meleenet PRIVATE meleenet_common minhook ole32 gdi32 user32)
if(MELEENET_STEAM)
  # steam_api.dll is loaded only when the network page or a session asks for Steam (from the game folder).
  target_link_options(meleenet PRIVATE /DELAYLOAD:steam_api.dll)
endif()
set_target_properties(meleenet PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "meleenet")

get_filename_component(MSVC_BIN_DIR ${CMAKE_LINKER} DIRECTORY)
find_program(DUMPBIN dumpbin HINTS ${MSVC_BIN_DIR})

# --- dist: what goes into the game folder, in build/dist/<config> ----------------
# Ultimate ASI Loader (ThirteenAG) as dinput8.dll: ztmnt.exe imports DINPUT8.dll, and the
# loader hands DirectInput8Create on to the system's. The 32-bit build from the TMNT1 project is reused.
set(UAL_DLL "${CMAKE_SOURCE_DIR}/../tmnt2003-netplay/third_party/ultimate-asi-loader/bin/Win32/Release/dinput8.dll"
    CACHE FILEPATH "32-bit Ultimate ASI Loader dll (shipped as dinput8.dll)")
set(DIST ${CMAKE_BINARY_DIR}/dist/$<CONFIG>)
add_custom_command(TARGET meleenet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:meleenet> ${DIST}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/dist/meleenet.ini ${DIST}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/dist/README.md ${DIST}/meleenet-README.md
  COMMAND ${CMAKE_COMMAND} -DDUMPBIN=${DUMPBIN} -DFILE=$<TARGET_FILE:meleenet> -P ${CMAKE_SOURCE_DIR}/tools/check_x86.cmake)
if(MELEENET_STEAM)
  add_custom_command(TARGET meleenet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STEAM_API_DLL} ${DIST}/steam_api.dll)
endif()
if(EXISTS ${UAL_DLL})
  add_custom_command(TARGET meleenet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UAL_DLL} ${DIST}/dinput8.dll)
else()
  message(WARNING "Ultimate ASI Loader not found at ${UAL_DLL}: dist has no dinput8.dll")
endif()

# --- the mod's files as the launcher carries them (build/payload) ----------------------------------------
if(EXISTS ${UAL_DLL})
  set(PAYLOAD_DIR ${CMAKE_BINARY_DIR}/payload)
  set(PAYLOAD_FILES ${PAYLOAD_DIR}/dinput8.dll ${PAYLOAD_DIR}/meleenet.asi ${PAYLOAD_DIR}/meleenet.ini
                    ${PAYLOAD_DIR}/README.md)
  set(PKG_STEAM_RC "")
  set(PKG_STEAM_INDEX "")
  set(PAYLOAD_STEAM_COPY "")
  if(MELEENET_STEAM)
    list(APPEND PAYLOAD_FILES ${PAYLOAD_DIR}/steam_api.dll)
    set(PKG_STEAM_RC "303 RCDATA \"${PAYLOAD_DIR}/steam_api.dll\"")
    # (Steam knows meleenet as Valve's test app Spacewar)
    set(PKG_STEAM_INDEX "file steam_api.dll 303\ntext steam_appid.txt 480 ifmissing\n")
    set(PAYLOAD_STEAM_COPY COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STEAM_API_DLL} ${PAYLOAD_DIR}/)
  endif()
  add_custom_target(mod_payload
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PAYLOAD_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UAL_DLL} ${PAYLOAD_DIR}/dinput8.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:meleenet> ${PAYLOAD_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/dist/meleenet.ini ${PAYLOAD_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/dist/README.md ${PAYLOAD_DIR}/
    ${PAYLOAD_STEAM_COPY}
    BYPRODUCTS ${PAYLOAD_FILES})
  add_dependencies(mod_payload meleenet)
endif()

# --- meleenet-launcher.exe: the one file a player gets -------------------------------------------------------
# It carries the mod inside (the files above, as resources): finds the game, puts the mod in and takes it out,
# asks GitHub for a newer release - a newer launcher with a newer mod inside - and starts the game.
# Direct2D + DirectWrite + WinHTTP: nothing but Windows. The fonts are in the exe too (src/launcher/assets/fonts,
# made by tools/launcher_fonts.py); without the file of the headings' font, Dela Gothic One, a system font stands in.
if(EXISTS ${UAL_DLL})
  set(LAUNCHER_FONTS ${CMAKE_SOURCE_DIR}/src/launcher/assets/fonts)
  set(LAUNCHER_FONT_FILES ${LAUNCHER_FONTS}/Onest-Regular.ttf ${LAUNCHER_FONTS}/Onest-Medium.ttf ${LAUNCHER_FONTS}/Onest-SemiBold.ttf
                          ${LAUNCHER_FONTS}/Onest-Bold.ttf ${LAUNCHER_FONTS}/JetBrainsMono-Medium.ttf
                          ${LAUNCHER_FONTS}/JetBrainsMono-Bold.ttf)
  set(LAUNCHER_DISPLAY_RC "")
  if(EXISTS ${LAUNCHER_FONTS}/DelaGothicOne-Regular.ttf)
    set(LAUNCHER_DISPLAY_RC "RES_FONT_DISPLAY RCDATA \"${LAUNCHER_FONTS}/DelaGothicOne-Regular.ttf\"")
    list(APPEND LAUNCHER_FONT_FILES ${LAUNCHER_FONTS}/DelaGothicOne-Regular.ttf)
  else()
    message(STATUS "Launcher: no DelaGothicOne-Regular.ttf in ${LAUNCHER_FONTS}: headings in a system font")
  endif()

  # What the package inside says of itself: the mod's version and protocol, the launcher's own version.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    src/common/version.h src/common/protocol.h src/launcher/launcher.h dist/release.json)
  file(STRINGS src/common/version.h _line REGEX "define MELEENET_VERSION \"")
  string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" MOD_VERSION "${_line}")
  file(STRINGS src/common/protocol.h _line REGEX "kVersion = [0-9]+")
  string(REGEX REPLACE ".*kVersion = ([0-9]+).*" "\\1" MOD_PROTOCOL "${_line}")
  file(STRINGS src/launcher/launcher.h _line REGEX "kLauncherVersion.. = L\"")
  string(REGEX REPLACE ".*L\"([0-9.]+)\".*" "\\1" LAUNCHER_VERSION "${_line}")
  string(REPLACE "." "," LAUNCHER_VERSION_COMMAS "${LAUNCHER_VERSION}")
  file(READ dist/release.json _notes)
  if(NOT _notes MATCHES "\"version\": *\"${MOD_VERSION}\"")
    message(WARNING "dist/release.json is not for ${MOD_VERSION}: the launcher will show no notes for the mod inside")
  endif()
  message(STATUS "Launcher ${LAUNCHER_VERSION} with the mod ${MOD_VERSION} (protocol ${MOD_PROTOCOL}) inside")
  file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/package-index.txt CONTENT
"meleenet-package 1
version ${MOD_VERSION}
protocol ${MOD_PROTOCOL}
launcher ${LAUNCHER_VERSION}
file dinput8.dll 301 loader
file meleenet.asi 302
${PKG_STEAM_INDEX}file meleenet.ini 304 keep
file meleenet-README.md 305
" @ONLY NEWLINE_STYLE LF)

  configure_file(src/launcher/launcher.rc.in ${CMAKE_BINARY_DIR}/launcher.rc @ONLY)
  set_source_files_properties(${CMAKE_BINARY_DIR}/launcher.rc PROPERTIES OBJECT_DEPENDS
    "${LAUNCHER_FONT_FILES};${CMAKE_SOURCE_DIR}/src/launcher/assets/launcher.ico;${PAYLOAD_FILES};${CMAKE_BINARY_DIR}/package-index.txt;${CMAKE_SOURCE_DIR}/dist/release.json")
  add_executable(meleenet_launcher WIN32
    src/launcher/main.cpp
    src/launcher/view.cpp
    src/launcher/gfx.cpp
    src/launcher/core.cpp
    src/launcher/gameset.cpp
    src/launcher/package.cpp
    src/launcher/net.cpp
    src/common/sha256.cpp
    src/launcher/launcher.manifest
    ${CMAKE_BINARY_DIR}/launcher.rc)
  target_include_directories(meleenet_launcher PRIVATE src)
  target_compile_definitions(meleenet_launcher PRIVATE ${OUR_DEFS})
  target_compile_options(meleenet_launcher PRIVATE ${OUR_OPTS})
  target_link_libraries(meleenet_launcher PRIVATE d2d1 dwrite windowscodecs winhttp ws2_32 dwmapi ole32 shell32 advapi32 bcrypt)
  set_target_properties(meleenet_launcher PROPERTIES OUTPUT_NAME "meleenet-launcher")
  add_dependencies(meleenet_launcher mod_payload)
  add_custom_command(TARGET meleenet_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist/$<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:meleenet_launcher> ${CMAKE_BINARY_DIR}/dist/$<CONFIG>/)
else()
  message(WARNING "No Ultimate ASI Loader: the launcher is not built")
endif()
