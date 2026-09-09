#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cardengine/card.h"
#include "cardengine/config.h"
#include "cardengine/deck.h"
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
    // Stud only: face-up cards in deal order (empty for other variants).
    const std::vector<Card>& up_cards(int seat) const;
    // Stud river overflow: a single shared up card when the shoe runs dry
    // 8-handed (empty otherwise). Plays in every remaining hand, like a
    // one-card board that is also public.
    const std::vector<Card>& community() const { return community_; }
    bool in_hand(int seat) const;
    bool has_folded(int seat) const;
    bool is_all_in(int seat) const;
    // Stud only: the third-street bring-in seat (-1 outside stud).
    // Recomputed live from visible upcards (folds don't change it).
    int bring_in_seat() const;
    // Draw games: true once the seat has taken its exchange this hand.
    bool drew(int seat) const;
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
    // The host gave up waiting: folds the acting seat by the clock and logs
    // a timeout event (the seat's cards stay hidden, like any other fold).
    // Stamps `at` (seconds, see clock.h); the plain form reads the wall
    // clock. Throws std::logic_error unless that seat holds the action.
    void timeout(int seat);
    void timeout_at(int seat, std::int64_t /*at*/);
    // Stamp of the last action-clock (re)start: hand deal, a taken action,
    // or a new street (-1 when no action is pending). Seconds, see clock.h.
    // Hosts poll `expired(now, acting_since(), limit)` and call timeout().
    std::int64_t acting_since() const { return acting_since_; }
    // Test seam: re-stamp the action clock (hosts never call this — time
    // only moves forward through act/deal/timeout).
    void set_acting_since_for_tests(std::int64_t at) { acting_since_ = at; }

    // Deals flop (3), turn, or river (1). Throws unless the round is
    // complete and the hand still needs cards.
    void deal_next_street();
    // Draw games only: exchanges discards for replacements off the shoe.
    // The seat must be in the hand and not yet drawn; discards name exact
    // cards by text ("As", "Td") taken from the seat's current hole.
    // Throws std::logic_error outside the draw street (or after drawing),
    // std::invalid_argument on bad text, unknown cards, duplicates, or more
    // than max_draw discards. Stands pat with an empty list.
    void discard(int seat, const std::vector<std::string>& cards);
    // Draw street seats still owed their exchange, in turn order from the
    // button; empty unless the street is Draw (folded and all-in seats are
    // skipped — they keep their pat hands to showdown).
    std::vector<int> draws_pending() const;

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
        bool drew = false;  // Draw games: exchange taken this hand.
        int seen_seq = 0;  // Raise generation this seat has responded to.
        std::vector<Card> hole;  // Down cards (private).
        std::vector<Card> up;    // Up cards (public, stud only).
    };

    void check_seat(int seat) const;
    int next_in_hand(int from) const;  // Next participating seat at/after from.
    // Next live (unfolded) seat at/after from; used for stud opening order.
    int next_live(int from) const;
    bool can_act(int seat) const;      // In hand, not folded, has chips.
    bool needs_action(int seat) const;
    void advance_acting(int from);
    // Stud betting order: third street opens after the bring-in seat (the
    // bring-in posts first, action starts left of them); later streets
    // open on the best visible hand (ties broken clockwise from the
    // button). Live-only: folded and all-in seats never open.
    int stud_opener() const;
    void post_blind(int seat, int amount);
    // Stud up-ranks for action order: high card by rank, then suit
    // (spades > hearts > diamonds > clubs — the classic bring-in/high
    // tiebreak suits, used for ordering only, never for showdown).
    int stud_high_score(int seat) const;
    int stud_low_score(int seat) const;
    // Third-street deal + bring-in: 2 down + 1 up per participant (button-
    // out), antes, then the low upcard's forced bet; action opens left of
    // it. Deck-backed (shuffled) and shoe-backed (testing seam) halves.
    void deal_stud_third(Deck& deck);
    void deal_stud_third_from_shoe();
    void post_stud_bring_in();
    void post_stud_antes();
    void post_stud_forced();
    // One stud card to every live seat (up when face_up, else down).
    // Seventh street goes community when the shoe would run dry: a single
    // shared up card on community_ instead of one per seat.
    void deal_stud_round(bool face_up, Street street);
    Card take_card(Deck* deck);
    // Cards left below the cursor.
    std::size_t shoe_remaining() const { return shoe_.size() - shoe_pos_; }
    // Forced bets for the new hand: antes, blinds, straddle. Returns the
    // straddle seat (or -1): preflop action starts after it.
    int post_forced_bets(int participants);
    void begin_round();
    // Stud variant: same reset, but action opens at the given seat (the
    // bring-in's left on third, the best visible hand after).
    void begin_stud_round(int opener);
    void start_hand_common();
    // Limit betting unit: big blind preflop/flop, twice after.
    int fixed_bet_size() const;
    // Snapshots the just-dealt hand (pre-hand stacks are stack + committed).
    void record_hand_started(std::uint64_t seed, bool seeded);
    // Showdown value under the configured construction rule.
    HandValue showdown_value(const std::vector<Card>& hole,
                             const std::vector<Card>& board) const;
    // Same, on an explicit board (multi-board runouts judge each board).
    HandValue showdown_value_on(const std::vector<Card>& hole,
                                const std::vector<Card>& board) const {
        if (config_.showdown == HandConstruction::OmahaTwoAndThree) {
            return evaluate_omaha(hole, board);
        }
        if (config_.showdown == HandConstruction::OmahaHiLo) {
            return evaluate_omaha_hilo(hole, board).high;
        }
        std::vector<Card> all = board;
        all.insert(all.end(), hole.begin(), hole.end());
        return evaluate_best(all);
    }
    // Best (lowest) 2-7 value on a bare 5-card hole (no board).
    // Throws std::invalid_argument unless hole is exactly 5 cards.
    static DeuceValue deuce_value_on(const std::vector<Card>& hole) {
        if (hole.size() != 5) {
            throw std::invalid_argument("deuce showdown needs 5 hole cards");
        }
        std::array<Card, 5> five{};
        for (std::size_t k = 0; k < 5; ++k) five[k] = hole[k];
        return evaluate_deuce(five);
    }
    // Unmatched top band returns to its lone owner before the award math
    // runs (it was never called, so no winner may take it). Returns the
    // excess to refund (0 when the top band is contested); the caller
    // applies it to the owner's stack and merges the levels.
    int unmatched_top_excess(const std::vector<int>& levels) const;
    // Applies the unmatched-top refund in place: returns the excess to its
    // lone owner and folds the levels down. No-op when contested.
    void refund_unmatched_top(std::vector<int>& levels);
    // Hi-Lo side-pot split: half to the best high hand(s), half to the best
    // qualifying low hand(s) (high scoops when no low qualifies).
    void award_hilo_pot(std::vector<Payout>& payouts,
                        const std::vector<int>& eligible, int amount) const;
    // Multi-board showdown: each contribution band splits across every
    // board (board-major split first, then the per-board rule — hi-lo
    // halves apply per board, not on the total). Levels arrive pre-
    // refunded (settle() returns the unmatched top band first).
    void award_multi_board(std::vector<Payout>& payouts,
                           const std::vector<int>& levels,
                           const std::vector<std::vector<Card>>& boards) const;
    // One board's share of one contribution band to its winners.
    void award_board_share(std::vector<Payout>& payouts,
                           const std::vector<int>& eligible,
                           const std::vector<Card>& board, int amount) const;

    GameConfig config_;
    std::vector<Seat> seats_;
    int button_ = 0;
    bool kill_live_ = false;  // Next hand plays double blinds (full kill).
    Street street_ = Street::None;
    std::vector<Card> board_;
    // Stud river overflow: the shared up card when the shoe runs dry
    // 8-handed. Empty for every other game and street.
    std::vector<Card> community_;
    std::vector<Card> shoe_;  // Full undealt shoe in deal order.
    // Index of the next card to deal (the top). Dealing advances the
    // cursor instead of erasing from the front, so the shoe stays put
    // and deal order is bit-identical to the old erase queue.
    std::size_t shoe_pos_ = 0;
    int acting_ = -1;
    std::int64_t acting_since_ = -1;  // Action-clock start (clock.h seconds).
    int current_bet_ = 0;
    int last_raise_size_ = 0;
    int round_seq_ = 0;  // Bumped by every full raise.
    int raises_this_round_ = 0;  // Complete raises (Limit cap).
    bool showdown_ = false;
    std::vector<Payout> last_payouts_;
    std::vector<Event> events_;
};

}  // namespace cardengine
