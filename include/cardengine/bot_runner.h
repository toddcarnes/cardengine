#pragma once

#include <string>

#include "cardengine/bot.h"

namespace cardengine {

// Out-of-process bot contract. The host relays one `state <seat>` block plus
// one decision line; the runner prints one reply line back. Betting
// decisions take an `options` line and answer `act ...`; draw exchanges
// take a `draws` line (pending seats in turn order) and answer
// `discard ...` (or bare `discard` to stand pat). This unit is the whole
// contract except process plumbing, so the thin exe loop stays trivially
// correct and this is tested directly.
// Returns e.g. "act call", "act raise 300", or "discard As Td".
// Throws std::invalid_argument on malformed input.
std::string decide_from_text(Bot& bot, int seat,
                             const std::string& state_text,
                             const std::string& options_text);
// Draw exchange half of the contract: parses the seat's hole cards and
// house cap out of a `state <seat>` block and asks the bot what to throw.
// The state block carries the house cap on a `max_draw N` line (emitted
// for draw games; absent means the 5-card default).
// Returns e.g. "discard As Td" (bare "discard" stands pat).
// Throws std::invalid_argument on malformed input.
std::string discard_from_text(Bot& bot, int seat,
                              const std::string& state_text);

}  // namespace cardengine
