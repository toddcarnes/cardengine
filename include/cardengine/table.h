#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cardengine/card.h"
#include "cardengine/config.h"
#include "cardengine/event.h"
#include "cardengine/hand.h"
#include "cardengine/types.h"

namespace cardengine {

struct ActionOptions {
    bool can_check = false;
    int call_amount = 0;  // Chips a call would cost (capped by stack).
    bool can_raise = false;
    int min_raise_to = 0;  // Valid only if can_raise.
    int max_raise_to = 0;  // bet + stack. Valid only if can_raise.
};

// UI-agnostic table engine: the "dealer". Holds authoritative state,
// validates every action, and reports facts. It never reads input or prints:
// seating a human or a bot in a seat is the protocol layer's decision,
// the engine treats all seats identically.
//
// Standard no-limit rules: min open is the big blind, min re-raise is the
// last full raise size, and an all-in short of a full raise does not reopen
// betting for players who already acted. Uncalled excess is returned
// automatically by pot construction at settle time. Split-pot odd chips go
// clockwise from the button.
class Table {
public:
    explicit Table(const GameConfig& config);

    const GameConfig& config() const { return config_; }
    int num_seats() const;
    int stack(int seat) const;
    // Setup/operator control (tests, tournaments, rebuys). Not part of play.
    void set_stack(int seat, int chips);
    // Sit-out control: a sitting-out seat posts nothing, is dealt nothing,
    // and is skipped for blinds/button until resumed. Applies from the next
    // hand (safe to flip mid-hand: the running hand is unaffected). Stacks,
    // eliminations, and bot seatings are left alone.
    void set_sitting_out(int seat, bool out);
    bool sitting_out(int seat) const;
    // Tournament level changes between hands (never mid-hand).
    void set_blinds(int small, int big);
    void set_ante(int ante);
    int button() const { return button_; }
    void set_button(int seat);
    Street street() const { return street_; }

    const std::vector<Card>& board() const { return board_; }
    const std::vector<Card>& hole_cards(int seat) const;
    bool in_hand(int seat) const;
    bool has_folded(int seat) const;
    bool is_all_in(int seat) const;
    int bet(int seat) const;        // Committed this round.
    int committed(int seat) const;  // Committed this hand.
    int current_bet() const { return current_bet_; }
    int to_call(int seat) const;
    int pot_total() const;

    // Seat whose turn it is, or -1 when no action is pending (round done,
    // everyone all in, or no hand running).
    int acting() const { return acting_; }
    // Legal actions for a seat; all-false when the seat can't act.
    // Never throws: the UI asks first, act() enforces.
    ActionOptions options(int seat) const;

    void start_hand(std::uint64_t seed);
    // Testing seam: deal from the given order (front = top of deck).
    // Needs at least num_players * hole_cards plus board_cards cards.
    void start_hand_from_deck(std::vector<Card> top_first);

    // Applies one action for the acting seat. Throws std::logic_error on a
    // wrong turn and std::invalid_argument on an illegal action.
    void act(int seat, const Action& action);

    // Deals flop (3), turn, or river (1). Throws unless the round is
    // complete and the hand still needs cards.
    void deal_next_street();

    bool hand_complete() const;
    bool went_to_showdown() const { return showdown_; }
    const std::vector<Payout>& payouts() const { return last_payouts_; }
    // Awards pots, advances the button. Returns per-seat payouts.
    // Throws std::logic_error unless the hand is complete.
    std::vector<Payout> settle();

    // Append-only history of every hand since construction or clear_events().
    const std::vector<Event>& events() const { return events_; }
    void clear_events() { events_.clear(); }
    // Restores a log parsed from text (session files). Appends verbatim;
    // the caller guarantees the lines belong to this table's configs.
    void append_events(std::vector<Event> more);

    struct Snapshot {
        GameConfig config;
        std::vector<int> stacks;
        std::vector<bool> sitting_out;
        int button = 0;
    };
    // Between-hands state: stacks, sit-out flags, button. Throws
    // std::logic_error when a hand is running.
    Snapshot snapshot() const;
    // Inverse of snapshot: fresh table, same configs, carried stacks/flags.
    // Throws std::invalid_argument on a mismatched or negative snapshot.
    void restore(const Snapshot& saved);

private:
    struct Seat {
        int stack = 0;
        int bet = 0;
        int committed = 0;
        bool in_hand = false;
        bool folded = false;
        bool acted = false;
        bool sitting_out = false;  // Operator flag: skips future hands.
        int seen_seq = 0;  // Raise generation this seat has responded to.
        std::vector<Card> hole;
    };

    void check_seat(int seat) const;
    int next_in_hand(int from) const;  // Next participating seat at/after from.
    bool can_act(int seat) const;      // In hand, not folded, has chips.
    bool needs_action(int seat) const;
    void advance_acting(int from);
    void post_blind(int seat, int amount);
    void begin_round();
    void start_hand_common();
    // Limit betting unit: big blind preflop/flop, twice after.
    int fixed_bet_size() const;
    // Snapshots the just-dealt hand (pre-hand stacks are stack + committed).
    void record_hand_started(std::uint64_t seed, bool seeded);
    // Showdown value under the configured construction rule.
    HandValue showdown_value(const std::vector<Card>& hole,
                             const std::vector<Card>& board) const;

    GameConfig config_;
    std::vector<Seat> seats_;
    int button_ = 0;
    Street street_ = Street::None;
    std::vector<Card> board_;
    std::vector<Card> shoe_;  // Remaining undealt cards, front = top.
    int acting_ = -1;
    int current_bet_ = 0;
    int last_raise_size_ = 0;
    int round_seq_ = 0;  // Bumped by every full raise.
    int raises_this_round_ = 0;  // Complete raises (Limit cap).
    bool showdown_ = false;
    std::vector<Payout> last_payouts_;
    std::vector<Event> events_;
};

}  // namespace cardengine
