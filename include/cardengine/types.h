#pragma once

// Shared value types: streets, actions, payouts. Split out of table.h so
// event.h (the hand-history schema) can use them without an include cycle.
#include <cstdint>

namespace cardengine {

// Betting streets, in deal order within each variant. Values are explicit
// (not positional): Draw and the stud streets were appended after the
// button-game streets, and reordering would renumber every saved log.
// Never compare streets across variants — deal/hand-complete logic names
// each variant's streets explicitly.
enum class Street : std::uint8_t {
    None = 0,
    Preflop = 1,
    Flop = 2,
    Turn = 3,
    River = 4,
    Complete = 5,
    // Five-card draw's exchange street: after the preflop betting round,
    // live seats discard and redraw, then betting resumes on the flop
    // slot (draw games have no board).
    Draw = 6,
    // Seven-card stud's five betting streets, in deal order: Third deals
    // 2 down + 1 up per seat (the bring-in opens), Fourth through Sixth
    // add one up card each, Seventh adds one down card (or a single shared
    // up card when the shoe runs dry 8-handed).
    Third = 7,
    Fourth = 8,
    Fifth = 9,
    Sixth = 10,
    Seventh = 11
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
