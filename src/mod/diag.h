// Crash diagnostics: an access violation (or another fatal exception) in the
// game is written to meleenet.log with its address, the registers and the code
// addresses on the stack, before the process dies.
#pragma once

#include "mod/config.h"

namespace diag {

void install(const Config& cfg);

}  // namespace diag
