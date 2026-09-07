#pragma once

// Shared value types: streets, actions, payouts. Split out of table.h so
// event.h (the hand-history schema) can use them without an include cycle.
#include <cstdint>

namespace cardengine {

enum class Street : std::uint8_t {
    None,
    Preflop,
    Flop,
    Turn,
    River,
    Complete,
    // Five-card draw's exchange street: after the preflop betting round,
    // live seats discard and redraw, then betting resumes. Reuses the
    // flop/turn/river betting slots (betting code never names a street),
    // so it sorts between Preflop and Flop for street comparisons but
    // deal/hand-complete logic names streets explicitly.
    Draw
};

enum class ActionType { Fold, Check, Call, Raise };

// Raise covers the opening bet too: amount is always the player's target
// TOTAL bet for the round (e.g. raise-to-200 preflop means 200 total).
struct Action {
    ActionType type;
    int amount = 0;  // Raise only; ignored otherwise.
};

struct Payout {
    int seat;
    int amount;
};

}  // namespace cardengine
