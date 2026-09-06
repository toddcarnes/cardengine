#pragma once

#include <string>

#include "cardengine/bot.h"

namespace cardengine {

// Out-of-process bot contract. The host relays one `state <seat>` block plus
// one `options` line; the runner prints one `act ...` line back. This unit
// is the whole contract except process plumbing, so the thin exe loop stays
// trivially correct and this is tested directly.
// Returns e.g. "act call" or "act raise 300".
// Throws std::invalid_argument on malformed input.
std::string decide_from_text(Bot& bot, int seat,
                             const std::string& state_text,
                             const std::string& options_text);

}  // namespace cardengine
