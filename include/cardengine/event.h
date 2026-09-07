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

// One stud card per live seat (or a single shared river card 8-handed).
// Up cards are public — every seat's card is listed — so GUI replay and
// per-seat filters can show exactly what the table saw. Down cards never
// appear here (they ride in HandStarted and stay private).
struct StudDealtEvent {
    Street street = Street::Fourth;
    bool face_up = true;
    bool community = false;  // True: `cards` is the shared river card.
    std::vector<Card> cards;  // Face-up cards in seat order (community: 1).
    struct SeatCard {
        int seat = -1;
        Card card{};
    };
    std::vector<SeatCard> per_seat;  // Seat-by-seat deal order.
};

// One seat's exchange in a five-card draw game: the count thrown away
// (and replaced off the top of the shoe). Counts are public — a real
// table sees how many you draw — but the card identities stay private:
// folded and discarded cards never reappear after HandStarted.
struct DrawEvent {
    int seat = -1;
    int drew = 0;  // Replacement cards dealt (0 = stood pat).
};

// An extra runout board for run-it-twice (and triple): board 2+ dealt
// from the remaining shoe at settle time, deciding its share of every
// pot. Board 1 is the felt board (the `street` lines); these are the
// spares, in order.
struct RunoutDealtEvent {
    int board = 2;  // 2-based: the second board is board 2.
    std::vector<Card> cards;
};

struct HandSettledEvent {
    bool showdown = false;
    std::vector<Payout> payouts;
    std::vector<int> committed;  // Per seat, pre-award (pot accounting).
    int boards = 1;              // Runouts this showdown ran (1 = classic).
};

// A forced fold by the clock: seat timed out holding the action. Not a
// decision — the host gave up waiting (disconnect, stalled client) and the
// engine recorded who folded and what the pot stood at. Bots read it as a
// fold by that seat.
struct TimeoutEvent {
    int seat = -1;
    int pot_after = 0;
};

using Event = std::variant<HandStartedEvent, ActionTakenEvent,
                           StreetDealtEvent, StudDealtEvent, DrawEvent,
                           RunoutDealtEvent, HandSettledEvent,
                           TimeoutEvent>;

const char* event_name(const Event& event);

// One line per event, for the `log` command (spec: docs/PROTOCOL.md).
std::string format_event(const Event& event);

// Inverse of format_event: parses one log line back into an event.
// The game config rides along because begin_hand lines carry stacks and
// hole cards but not the rules they were dealt under. Throws
// std::invalid_argument on any malformed line or card text.
Event parse_event(const std::string& line, const GameConfig& config);

}  // namespace cardengine
