#pragma once

// Append-only hand history. Every state change in a hand lands here as one
// event, in order — GUI replay, analysis tools, and bot training all read
// this instead of scraping state.
//
// Trust model matches `state`: the log is omniscient (it records dealt hole
// cards at hand start), so it is for local eyes only until per-seat views
// filter it. One deliberate omission: folded players' cards never appear
// again after HandStarted — exactly what a real showdown would reveal.
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "cardengine/card.h"
#include "cardengine/config.h"
#include "cardengine/types.h"

namespace cardengine {

struct HandStartedEvent {
    GameConfig config;
    std::vector<int> stacks;  // Pre-hand, one per seat.
    int button = 0;
    std::uint64_t seed = 0;
    bool seeded = false;  // False for from-deck (testing) deals.
    std::vector<std::vector<Card>> hole;  // One entry per seat.
};

struct ActionTakenEvent {
    int seat = -1;
    Action action{ActionType::Fold, 0};
    int pot_after = 0;
};

struct StreetDealtEvent {
    Street street = Street::Flop;
    std::vector<Card> cards;  // Only the newly dealt cards.
};

struct HandSettledEvent {
    bool showdown = false;
    std::vector<Payout> payouts;
    std::vector<int> committed;  // Per seat, pre-award (pot accounting).
};

using Event = std::variant<HandStartedEvent, ActionTakenEvent,
                           StreetDealtEvent, HandSettledEvent>;

const char* event_name(const Event& event);

// One line per event, for the `log` command (spec: docs/PROTOCOL.md).
std::string format_event(const Event& event);

}  // namespace cardengine
