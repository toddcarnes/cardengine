#include "cardengine/table.h"

#include <algorithm>
#include <stdexcept>

#include "cardengine/clock.h"
#include "cardengine/deck.h"
#include "cardengine/hand.h"

namespace cardengine {

Table::Table(const GameConfig& config) : config_(config) {
    validate(config_);
    seats_.resize(static_cast<std::size_t>(config_.num_players));
    for (Seat& s : seats_) s.stack = config_.starting_stack;
}

int Table::num_seats() const { return config_.num_players; }

int Table::stack(int seat) const {
    check_seat(seat);
    return seats_[seat].stack;
}

void Table::set_stack(int seat, int chips) {
    check_seat(seat);
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot change stacks mid-hand");
    }
    if (chips < 0) throw std::invalid_argument("chips cannot be negative");
    seats_[static_cast<std::size_t>(seat)].stack = chips;
}

void Table::set_sitting_out(int seat, bool out) {
    check_seat(seat);
    seats_[static_cast<std::size_t>(seat)].sitting_out = out;
}

bool Table::sitting_out(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].sitting_out;
}

void Table::set_blinds(int small, int big) {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot change blinds mid-hand");
    }
    if (small < 1 || big < 1) throw std::invalid_argument("blinds must be positive");
    if (small > big) throw std::invalid_argument("small blind exceeds big blind");
    config_.small_blind = small;
    config_.big_blind = big;
}

void Table::set_ante(int ante) {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot change ante mid-hand");
    }
    if (ante < 0) throw std::invalid_argument("ante cannot be negative");
    config_.ante = ante;
}

void Table::set_button(int seat) {
    check_seat(seat);
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot move the button mid-hand");
    }
    button_ = seat;
}

const std::vector<Card>& Table::hole_cards(int seat) const {
    check_seat(seat);
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    if (!s.in_hand) throw std::logic_error("seat not in hand");
    return s.hole;
}

bool Table::in_hand(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].in_hand;
}

bool Table::has_folded(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].folded;
}

bool Table::is_all_in(int seat) const {
    check_seat(seat);
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    return s.in_hand && !s.folded && s.stack == 0;
}

int Table::bet(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].bet;
}

int Table::committed(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].committed;
}

int Table::to_call(int seat) const {
    check_seat(seat);
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    return current_bet_ > s.bet ? current_bet_ - s.bet : 0;
}

int Table::pot_total() const {
    int total = 0;
    for (const Seat& s : seats_) total += s.committed;
    return total;
}

ActionOptions Table::options(int seat) const {
    ActionOptions out;
    if (seat < 0 || seat >= num_seats()) return out;
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    if (street_ == Street::None || street_ == Street::Complete) return out;
    if (!can_act(seat)) return out;
    const int call = to_call(seat);
    out.can_check = (call == 0);
    out.call_amount = call < s.stack ? call : s.stack;
    const bool reopened = !s.acted || s.seen_seq < round_seq_;
    if (s.stack == 0 || s.bet + s.stack <= current_bet_ || !reopened) {
        return out;
    }
    if (config_.betting == BettingStructure::Limit &&
        raises_this_round_ >= config_.max_raises_per_round) {
        return out;  // Capped: call or fold only.
    }
    out.can_raise = true;
    int min_full = current_bet_ + last_raise_size_;
    if (config_.betting == BettingStructure::Limit) {
        min_full = current_bet_ + fixed_bet_size();
    }
    int max_to = s.bet + s.stack;
    if (config_.betting == BettingStructure::PotLimit) {
        // Pot-sized raise: call first (pot grows by to_call), then raise
        // the pot on top: max total = bet + call + (pot + call).
        const int pot_max = s.bet + 2 * call + pot_total();
        if (pot_max < max_to) max_to = pot_max;
    }
    if (config_.betting == BettingStructure::Limit && max_to >= min_full) {
        // Exactly one legal raise size — unless the stack can't reach it,
        // in which case the all-in short below stands.
        max_to = min_full;
    }
    // An all-in short of a full raise is still legal (it just doesn't
    // reopen betting for others).
    out.min_raise_to = max_to < min_full ? max_to : min_full;
    out.max_raise_to = max_to;
    return out;
}

void Table::start_hand(std::uint64_t seed) {
    Deck deck;
    deck.shuffle(seed);
    std::vector<Card> shoe;
    while (!deck.empty()) shoe.push_back(deck.deal());
    start_hand_common();
    shoe_ = std::move(shoe);
    const int participants = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(),
                      [](const Seat& s) { return s.in_hand; }));
    // Deal hole_cards rounds starting left of the button. The inner loop
    // runs participants times, not seats times: busted-out seats sit out
    // and must not duplicate anyone else's cards.
    int s = button_;
    for (int round = 0; round < config_.hole_cards; ++round) {
        for (int k = 0; k < participants; ++k) {
            s = next_in_hand(s + 1);
            Seat& seat = seats_[static_cast<std::size_t>(s)];
            seat.hole.push_back(shoe_.front());
            shoe_.erase(shoe_.begin());
        }
    }
    record_hand_started(seed, true);
}

void Table::start_hand_from_deck(std::vector<Card> top_first) {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("hand already running");
    }
    const int funded = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(), [](const Seat& s) {
            return s.stack > 0 && !s.sitting_out;
        }));
    if (funded < 2) throw std::logic_error("need at least 2 players");
    const std::size_t need = static_cast<std::size_t>(funded * config_.hole_cards +
                                                      config_.board_cards);
    if (top_first.size() < need) {
        throw std::invalid_argument("deck too small for this hand");
    }
    start_hand_common();
    shoe_ = std::move(top_first);
    const int participants = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(),
                      [](const Seat& s) { return s.in_hand; }));
    int s = button_;
    for (int round = 0; round < config_.hole_cards; ++round) {
        for (int k = 0; k < participants; ++k) {
            s = next_in_hand(s + 1);
            Seat& seat = seats_[static_cast<std::size_t>(s)];
            seat.hole.push_back(shoe_.front());
            shoe_.erase(shoe_.begin());
        }
    }
    record_hand_started(0, false);
}

void Table::act(int seat, const Action& action) {
    check_seat(seat);
    if (street_ == Street::None || street_ == Street::Complete) {
        throw std::logic_error("no hand running");
    }
    if (seat != acting_) throw std::logic_error("not this seat's turn");
    Seat& s = seats_[static_cast<std::size_t>(seat)];

    switch (action.type) {
        case ActionType::Fold:
            s.folded = true;
            s.acted = true;
            s.seen_seq = round_seq_;
            break;
        case ActionType::Check:
            if (to_call(seat) != 0) {
                throw std::invalid_argument("cannot check facing a bet");
            }
            s.acted = true;
            s.seen_seq = round_seq_;
            break;
        case ActionType::Call: {
            const int call = to_call(seat);
            const int pay = call < s.stack ? call : s.stack;
            s.stack -= pay;
            s.bet += pay;
            s.committed += pay;
            s.acted = true;
            s.seen_seq = round_seq_;
            break;
        }
        case ActionType::Raise: {
            const ActionOptions opts = options(seat);
            if (!opts.can_raise) {
                throw std::invalid_argument("raise not available");
            }
            if (action.amount < opts.min_raise_to ||
                action.amount > opts.max_raise_to) {
                throw std::invalid_argument("raise amount out of range");
            }
            const int additional = action.amount - s.bet;
            const int increment = action.amount - current_bet_;
            s.stack -= additional;
            s.bet = action.amount;
            s.committed += additional;
            s.acted = true;
            if (increment >= last_raise_size_) {
                // Full raise: reopens betting for everyone else.
                last_raise_size_ = increment;
                ++round_seq_;
                s.seen_seq = round_seq_;
                if (config_.betting == BettingStructure::Limit) {
                    ++raises_this_round_;  // Short all-ins don't consume cap.
                }
            } else {
                // Short all-in: current bet rises, action stays closed.
                s.seen_seq = round_seq_;
            }
            current_bet_ = action.amount;
            break;
        }
    }
    advance_acting(seat + 1);
    acting_since_ = now_seconds();
    events_.push_back(ActionTakenEvent{seat, action, pot_total()});
}

void Table::timeout(int seat) { timeout_at(seat, now_seconds()); }

void Table::timeout_at(int seat, std::int64_t at) {
    check_seat(seat);
    if (street_ == Street::None || street_ == Street::Complete) {
        throw std::logic_error("no hand running");
    }
    if (seat != acting_) throw std::logic_error("not this seat's turn");
    Seat& s = seats_[static_cast<std::size_t>(seat)];
    s.folded = true;
    s.acted = true;
    s.seen_seq = round_seq_;
    (void)at;  // The stamp lives in the order of events, not the event.
    advance_acting(seat + 1);
    acting_since_ = now_seconds();
    events_.push_back(TimeoutEvent{seat, pot_total()});
}

void Table::deal_next_street() {
    if (street_ != Street::Preflop && street_ != Street::Flop &&
        street_ != Street::Turn) {
        throw std::logic_error("no street left to deal");
    }
    if (acting_ != -1) throw std::logic_error("betting round not complete");
    if (hand_complete()) throw std::logic_error("hand already decided");
    // Board cards are dealt 3-1-1 across flop/turn/river, scaled to however
    // many the config asks for (fewer cards, or none, just deal fewer).
    const int remaining =
        config_.board_cards - static_cast<int>(board_.size());
    int deal_now = 0;
    if (street_ == Street::Preflop) {
        street_ = Street::Flop;
        deal_now = remaining < 3 ? remaining : 3;
    } else {
        street_ = (street_ == Street::Flop) ? Street::Turn : Street::River;
        deal_now = remaining < 1 ? remaining : 1;
    }
    StreetDealtEvent dealt;
    dealt.street = street_;
    for (int i = 0; i < deal_now; ++i) {
        dealt.cards.push_back(shoe_.front());
        board_.push_back(shoe_.front());
        shoe_.erase(shoe_.begin());
    }
    events_.push_back(dealt);
    begin_round();
    acting_since_ = now_seconds();
}

bool Table::hand_complete() const {
    if (street_ == Street::None || street_ == Street::Complete) return false;
    int remaining = 0;
    for (const Seat& s : seats_) {
        if (s.in_hand && !s.folded) ++remaining;
    }
    if (remaining <= 1) return true;
    return street_ == Street::River && acting_ == -1;
}

std::vector<Payout> Table::settle() {
    if (!hand_complete()) throw std::logic_error("hand not complete");

    std::vector<int> alive;
    for (int i = 0; i < num_seats(); ++i) {
        const Seat& s = seats_[static_cast<std::size_t>(i)];
        if (s.in_hand && !s.folded) alive.push_back(i);
    }

    std::vector<Payout> payouts;
    int boards_run = 1;
    if (alive.size() == 1) {
        showdown_ = false;
        payouts.push_back({alive[0], pot_total()});
    } else if (config_.runouts > 1) {
        // Run-it-twice (or triple): every side pot splits across N boards.
        // Board 1 is the felt; boards 2+ come off the remaining shoe, in
        // order, and cannot duplicate the felt or each other (they are real
        // cards from the same deck). A short shoe falls back to one board
        // rather than dealing half a runout.
        showdown_ = true;
        std::vector<std::vector<Card>> boards;
        boards.push_back(board_);
        const std::size_t need =
            static_cast<std::size_t>(config_.board_cards) *
            static_cast<std::size_t>(config_.runouts - 1);
        if (shoe_.size() >= need) {
            for (int b = 2; b <= config_.runouts; ++b) {
                RunoutDealtEvent runout;
                runout.board = b;
                for (int k = 0; k < config_.board_cards; ++k) {
                    runout.cards.push_back(shoe_.front());
                    shoe_.erase(shoe_.begin());
                }
                boards.push_back(runout.cards);
                events_.push_back(runout);
            }
        }
        boards_run = static_cast<int>(boards.size());
        award_multi_board(payouts, alive, boards);
    } else {
        showdown_ = true;
        // Contribution levels, low to high; each band forms one pot.
        std::vector<int> levels;
        for (int i = 0; i < num_seats(); ++i) {
            const Seat& s = seats_[static_cast<std::size_t>(i)];
            if (s.in_hand && s.committed > 0) levels.push_back(s.committed);
        }
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

        int prev = 0;
        for (int level : levels) {
            int contributors = 0;
            std::vector<int> eligible;
            for (int i = 0; i < num_seats(); ++i) {
                const Seat& s = seats_[static_cast<std::size_t>(i)];
                if (s.in_hand && s.committed >= level) {
                    ++contributors;
                    if (!s.folded) eligible.push_back(i);
                }
            }
            const int amount = (level - prev) * contributors;
            prev = level;
            if (amount == 0 || eligible.empty()) continue;

            if (config_.showdown == HandConstruction::OmahaHiLo) {
                award_hilo_pot(payouts, eligible, amount);
                continue;
            }
            std::vector<HandValue> values;
            for (int i : eligible) {
                const Seat& s = seats_[static_cast<std::size_t>(i)];
                values.push_back(showdown_value(s.hole, board_));
            }
            HandValue best = values[0];
            for (const HandValue& value : values) {
                if (best < value) best = value;
            }
            std::vector<int> winners;
            for (std::size_t k = 0; k < eligible.size(); ++k) {
                if (values[k] == best) winners.push_back(eligible[k]);
            }
            // Odd chips go clockwise from the button.
            std::sort(winners.begin(), winners.end(), [&](int a, int b) {
                const int da = (a - button_ + num_seats()) % num_seats();
                const int db = (b - button_ + num_seats()) % num_seats();
                return da < db;
            });
            const int share = amount / static_cast<int>(winners.size());
            const int remainder =
                amount % static_cast<int>(winners.size());
            for (std::size_t w = 0; w < winners.size(); ++w) {
                int award = share + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
                auto it = std::find_if(payouts.begin(), payouts.end(),
                                       [&](const Payout& p) {
                                           return p.seat == winners[w];
                                       });
                if (it == payouts.end()) {
                    payouts.push_back({winners[w], award});
                } else {
                    it->amount += award;
                }
            }
        }
    }

    for (const Payout& p : payouts) {
        seats_[static_cast<std::size_t>(p.seat)].stack += p.amount;
    }
    // Full kill: a pot over 10× the big blind doubles next hand's blinds.
    // The trigger hand's winner posts the extra blind (tracked live).
    if (config_.kill && pot_total() > 10 * config_.big_blind) {
        kill_live_ = true;
    }
    HandSettledEvent settled;
    settled.showdown = showdown_;
    settled.payouts = payouts;
    for (const Seat& s : seats_) settled.committed.push_back(s.committed);
    settled.boards = boards_run;
    // The pot has been awarded; commitments no longer exist.
    for (Seat& s : seats_) {
        s.bet = 0;
        s.committed = 0;
    }
    last_payouts_ = payouts;
    events_.push_back(settled);
    street_ = Street::Complete;
    acting_ = -1;
    acting_since_ = -1;
    // Advance the button to the next seated player with chips who is not
    // sitting out.
    for (int k = 1; k <= num_seats(); ++k) {
        const int s = (button_ + k) % num_seats();
        if (seats_[static_cast<std::size_t>(s)].stack > 0 &&
            !seats_[static_cast<std::size_t>(s)].sitting_out) {
            button_ = s;
            break;
        }
    }
    return payouts;
}

HandValue Table::showdown_value(const std::vector<Card>& hole,
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

// One board's share of one contribution band: hi-lo halves apply per
// board, otherwise plain best-hand-takes-it, odd chips clockwise.
void Table::award_board_share(std::vector<Payout>& payouts,
                              const std::vector<int>& eligible,
                              const std::vector<Card>& board,
                              int amount) const {
    if (config_.showdown == HandConstruction::OmahaHiLo) {
        // Hi-Lo per board: half to best high, half to best low on THIS
        // board (high scoops the share when no low qualifies on it).
        std::vector<OmahaHiLoValue> values;
        for (int i : eligible) {
            const Seat& s = seats_[static_cast<std::size_t>(i)];
            values.push_back(evaluate_omaha_hilo(s.hole, board));
        }
        HandValue best_high = values[0].high;
        for (const OmahaHiLoValue& value : values) {
            if (best_high < value.high) best_high = value.high;
        }
        std::vector<int> high_winners;
        for (std::size_t k = 0; k < eligible.size(); ++k) {
            if (values[k].high == best_high) high_winners.push_back(eligible[k]);
        }
        std::vector<int> low_winners;
        bool low_set = false;
        LowValue best_low;
        for (std::size_t k = 0; k < eligible.size(); ++k) {
            if (!values[k].low.qualifies) continue;
            if (!low_set || values[k].low < best_low) {
                best_low = values[k].low;
                low_set = true;
            }
        }
        if (low_set) {
            for (std::size_t k = 0; k < eligible.size(); ++k) {
                if (values[k].low.qualifies &&
                    !(best_low < values[k].low) &&
                    !(values[k].low < best_low)) {
                    low_winners.push_back(eligible[k]);
                }
            }
        }
        auto clockwise = [&](int a, int b) {
            const int da = (a - button_ + num_seats()) % num_seats();
            const int db = (b - button_ + num_seats()) % num_seats();
            return da < db;
        };
        auto pay_share = [&](const std::vector<int>& winners, int share) {
            std::vector<int> ordered = winners;
            std::sort(ordered.begin(), ordered.end(), clockwise);
            const int each = share / static_cast<int>(ordered.size());
            const int remainder = share % static_cast<int>(ordered.size());
            for (std::size_t w = 0; w < ordered.size(); ++w) {
                const int award =
                    each + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
                auto it = std::find_if(payouts.begin(), payouts.end(),
                                       [&](const Payout& p) {
                                           return p.seat == ordered[w];
                                       });
                if (it == payouts.end()) {
                    payouts.push_back({ordered[w], award});
                } else {
                    it->amount += award;
                }
            }
        };
        if (low_winners.empty()) {
            pay_share(high_winners, amount);
            return;
        }
        const int low_half = amount / 2;
        pay_share(high_winners, amount - low_half);
        pay_share(low_winners, low_half);
        return;
    }
    std::vector<HandValue> values;
    for (int i : eligible) {
        const Seat& s = seats_[static_cast<std::size_t>(i)];
        values.push_back(showdown_value_on(s.hole, board));
    }
    HandValue best = values[0];
    for (const HandValue& value : values) {
        if (best < value) best = value;
    }
    std::vector<int> winners;
    for (std::size_t k = 0; k < eligible.size(); ++k) {
        if (values[k] == best) winners.push_back(eligible[k]);
    }
    std::sort(winners.begin(), winners.end(), [&](int a, int b) {
        const int da = (a - button_ + num_seats()) % num_seats();
        const int db = (b - button_ + num_seats()) % num_seats();
        return da < db;
    });
    const int share = amount / static_cast<int>(winners.size());
    const int remainder = amount % static_cast<int>(winners.size());
    for (std::size_t w = 0; w < winners.size(); ++w) {
        const int award =
            share + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
        auto it = std::find_if(payouts.begin(), payouts.end(),
                               [&](const Payout& p) {
                                   return p.seat == winners[w];
                               });
        if (it == payouts.end()) {
            payouts.push_back({winners[w], award});
        } else {
            it->amount += award;
        }
    }
}

// Every contribution band, split across boards first: each board decides
// its equal share of the band. Odd chips stay board-major — the first
// boards in order absorb the remainder one chip each.
void Table::award_multi_board(
    std::vector<Payout>& payouts, const std::vector<int>& alive,
    const std::vector<std::vector<Card>>& boards) const {
    std::vector<int> levels;
    for (int i = 0; i < num_seats(); ++i) {
        const Seat& s = seats_[static_cast<std::size_t>(i)];
        if (s.in_hand && s.committed > 0) levels.push_back(s.committed);
    }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    int prev = 0;
    for (int level : levels) {
        int contributors = 0;
        std::vector<int> eligible;
        for (int i = 0; i < num_seats(); ++i) {
            const Seat& s = seats_[static_cast<std::size_t>(i)];
            if (s.in_hand && s.committed >= level) {
                ++contributors;
                if (!s.folded) eligible.push_back(i);
            }
        }
        const int amount = (level - prev) * contributors;
        prev = level;
        if (amount == 0 || eligible.empty()) continue;
        const int n = static_cast<int>(boards.size());
        const int each = amount / n;
        const int extra = amount % n;
        for (int b = 0; b < n; ++b) {
            award_board_share(payouts, eligible, boards[static_cast<std::size_t>(b)],
                              each + (b < extra ? 1 : 0));
        }
    }
    (void)alive;  // Eligibility comes from the bands, as in settle().
}

// Splits one side pot's worth of chips between the best high hand(s) and
// the best qualifying low hand(s), each half paid clockwise from the
// button. No qualifying low means high scoops the whole pot. Odd chips on
// a split go to high first (standard cardroom rule), then clockwise.
void Table::award_hilo_pot(std::vector<Payout>& payouts,
                           const std::vector<int>& eligible, int amount) const {
    std::vector<OmahaHiLoValue> values;
    for (int i : eligible) {
        const Seat& s = seats_[static_cast<std::size_t>(i)];
        values.push_back(evaluate_omaha_hilo(s.hole, board_));
    }
    HandValue best_high = values[0].high;
    for (const OmahaHiLoValue& value : values) {
        if (best_high < value.high) best_high = value.high;
    }
    std::vector<int> high_winners;
    for (std::size_t k = 0; k < eligible.size(); ++k) {
        if (values[k].high == best_high) high_winners.push_back(eligible[k]);
    }
    // Best qualifying low; empty when nobody makes 8-or-better.
    std::vector<int> low_winners;
    bool low_set = false;
    LowValue best_low;
    for (std::size_t k = 0; k < eligible.size(); ++k) {
        if (!values[k].low.qualifies) continue;
        if (!low_set || values[k].low < best_low) {
            best_low = values[k].low;
            low_set = true;
        }
    }
    if (low_set) {
        for (std::size_t k = 0; k < eligible.size(); ++k) {
            if (values[k].low.qualifies && !(best_low < values[k].low) &&
                !(values[k].low < best_low)) {
                low_winners.push_back(eligible[k]);
            }
        }
    }
    auto clockwise = [&](int a, int b) {
        const int da = (a - button_ + num_seats()) % num_seats();
        const int db = (b - button_ + num_seats()) % num_seats();
        return da < db;
    };
    auto pay_share = [&](const std::vector<int>& winners, int share) {
        std::vector<int> ordered = winners;
        std::sort(ordered.begin(), ordered.end(), clockwise);
        const int each = share / static_cast<int>(ordered.size());
        const int remainder = share % static_cast<int>(ordered.size());
        for (std::size_t w = 0; w < ordered.size(); ++w) {
            const int award =
                each + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
            auto it = std::find_if(payouts.begin(), payouts.end(),
                                   [&](const Payout& p) {
                                       return p.seat == ordered[w];
                                   });
            if (it == payouts.end()) {
                payouts.push_back({ordered[w], award});
            } else {
                it->amount += award;
            }
        }
    };
    if (low_winners.empty()) {
        pay_share(high_winners, amount);  // No low: high scoops.
        return;
    }
    const int low_half = amount / 2;
    const int high_half = amount - low_half;  // Odd chip goes high.
    pay_share(high_winners, high_half);
    pay_share(low_winners, low_half);
}

void Table::record_hand_started(std::uint64_t seed, bool seeded) {
    HandStartedEvent started;
    started.config = config_;
    started.button = button_;
    started.seed = seed;
    started.seeded = seeded;
    for (const Seat& s : seats_) {
        // Nothing is created or destroyed mid-hand, so pre-hand stacks are
        // exactly what's left plus what's committed.
        started.stacks.push_back(s.stack + s.committed);
        started.hole.push_back(s.hole);
    }
    events_.push_back(started);
}

void Table::append_events(std::vector<Event> more) {
    for (Event& e : more) events_.push_back(std::move(e));
}

Table::Snapshot Table::snapshot() const {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot snapshot mid-hand");
    }
    Snapshot saved;
    saved.config = config_;
    for (const Seat& s : seats_) {
        saved.stacks.push_back(s.stack);
        saved.sitting_out.push_back(s.sitting_out);
    }
    saved.button = button_;
    return saved;
}

void Table::restore(const Snapshot& saved) {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("cannot restore mid-hand");
    }
    if (saved.stacks.size() != seats_.size() ||
        saved.sitting_out.size() != seats_.size()) {
        throw std::invalid_argument("snapshot does not match this table");
    }
    for (int chips : saved.stacks) {
        if (chips < 0) throw std::invalid_argument("snapshot stack negative");
    }
    if (saved.button < 0 || saved.button >= num_seats()) {
        throw std::invalid_argument("snapshot button out of range");
    }
    config_ = saved.config;
    validate(config_);
    for (int i = 0; i < num_seats(); ++i) {
        Seat& s = seats_[static_cast<std::size_t>(i)];
        s.stack = saved.stacks[static_cast<std::size_t>(i)];
        s.bet = 0;
        s.committed = 0;
        s.in_hand = false;
        s.folded = false;
        s.acted = false;
        s.sitting_out = saved.sitting_out[static_cast<std::size_t>(i)];
        s.seen_seq = 0;
        s.hole.clear();
    }
    button_ = saved.button;
    street_ = Street::None;
    board_.clear();
    shoe_.clear();
    acting_ = -1;
    acting_since_ = -1;
    current_bet_ = 0;
    last_raise_size_ = 0;
    round_seq_ = 0;
    raises_this_round_ = 0;
    showdown_ = false;
    last_payouts_.clear();
}

void Table::check_seat(int seat) const {
    if (seat < 0 || seat >= num_seats()) {
        throw std::invalid_argument("seat out of range");
    }
}

int Table::next_in_hand(int from) const {
    for (int k = 0; k < num_seats(); ++k) {
        const int s = (from + k) % num_seats();
        if (seats_[static_cast<std::size_t>(s)].in_hand) return s;
    }
    throw std::logic_error("no participating seat");
}

bool Table::can_act(int seat) const {
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    return s.in_hand && !s.folded && s.stack > 0;
}

bool Table::needs_action(int seat) const {
    if (!can_act(seat)) return false;
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    return !s.acted || to_call(seat) > 0;
}

void Table::advance_acting(int from) {
    acting_ = -1;
    int remaining = 0;
    for (const Seat& s : seats_) {
        if (s.in_hand && !s.folded) ++remaining;
    }
    if (remaining <= 1) return;  // Last player wins immediately.
    for (int k = 0; k < num_seats(); ++k) {
        const int s = (from + k) % num_seats();
        if (needs_action(s)) {
            acting_ = s;
            return;
        }
    }
}

void Table::post_blind(int seat, int amount) {
    Seat& s = seats_[static_cast<std::size_t>(seat)];
    const int pay = amount < s.stack ? amount : s.stack;
    s.stack -= pay;
    s.bet += pay;
    s.committed += pay;
}

void Table::begin_round() {
    ++round_seq_;  // New betting round: everyone owes fresh action.
    for (Seat& s : seats_) {
        if (!s.in_hand) continue;
        s.bet = 0;
        s.acted = false;
        s.seen_seq = round_seq_ - 1;
    }
    current_bet_ = 0;
    last_raise_size_ = fixed_bet_size();
    raises_this_round_ = 0;
    advance_acting(button_ + 1);
}

int Table::fixed_bet_size() const {
    if (street_ == Street::Turn || street_ == Street::River) {
        return 2 * config_.big_blind;
    }
    return config_.big_blind;
}

void Table::start_hand_common() {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("hand already running");
    }
    for (Seat& s : seats_) {
        s.bet = 0;
        s.committed = 0;
        s.in_hand = s.stack > 0 && !s.sitting_out;
        s.folded = false;
        s.acted = false;
        s.seen_seq = 0;
        s.hole.clear();
    }
    const int participants = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(),
                      [](const Seat& s) { return s.in_hand; }));
    if (participants < 2) throw std::logic_error("need at least 2 players");
    if (!seats_[static_cast<std::size_t>(button_)].in_hand) {
        button_ = next_in_hand(button_ + 1);
    }
    showdown_ = false;
    last_payouts_.clear();
    board_.clear();

    const int open_from = post_forced_bets(participants);

    street_ = Street::Preflop;
    // post_forced_bets sets the live bet (straddle/kill-aware blind levels).
    round_seq_ = 0;
    raises_this_round_ = 0;
    advance_acting(open_from + 1);
    acting_since_ = now_seconds();
}

// Antes, blinds, and the optional live straddle, in posting order. Blind
// levels double while a kill is live (consumed by this hand). Returns the
// seat preflop action starts after: the straddler when live, else the big
// blind. Heads-up the button is the small blind; otherwise SB/BB are the
// first two participants left of the button, and the straddle is the next
// participant after the big blind.
int Table::post_forced_bets(int participants) {
    const int small = config_.small_blind * (kill_live_ ? 2 : 1);
    const int big = config_.big_blind * (kill_live_ ? 2 : 1);
    kill_live_ = false;
    // Antes are dead money: everyone pays before the blinds go in.
    // Short stacks ante what they have and play on from there. Button-ante
    // games charge the whole table's ante to the button at once.
    if (config_.ante > 0) {
        if (config_.ante_from == AnteSource::ButtonOnly) {
            Seat& button = seats_[static_cast<std::size_t>(button_)];
            if (button.in_hand) {
                const int total = config_.ante * participants;
                const int pay = total < button.stack ? total : button.stack;
                button.stack -= pay;
                button.committed += pay;
            }
        } else {
            for (int i = 0; i < num_seats(); ++i) {
                Seat& s = seats_[static_cast<std::size_t>(i)];
                if (!s.in_hand) continue;
                const int pay =
                    config_.ante < s.stack ? config_.ante : s.stack;
                s.stack -= pay;
                s.committed += pay;
            }
        }
    }

    int sb = button_;
    int bb = button_;
    if (participants == 2) {
        sb = button_;
        bb = next_in_hand(button_ + 1);
    } else {
        sb = next_in_hand(button_ + 1);
        bb = next_in_hand(sb + 1);
    }
    post_blind(sb, small);
    post_blind(bb, big);
    current_bet_ = big;
    last_raise_size_ = big;
    // Live straddle: UTG (next participant after the BB) posts 2× BB and
    // acts last preflop — action opens after them, and the straddle itself
    // is a live bet they may raise when it returns. Needs 3+ players (a
    // heads-up straddle would be the button betting into themselves).
    if (config_.straddle > 0 && participants > 2) {
        const int st = next_in_hand(bb + 1);
        // The straddle counts as a full raise for min-raise purposes:
        // current 2×BB over the 1×BB blind, so the next raise re-raises.
        const int stab = config_.straddle * (small != config_.small_blind ? 2 : 1);
        post_blind(st, stab);
        current_bet_ = stab;
        last_raise_size_ = stab - big;
        return st;
    }
    return bb;
}

}  // namespace cardengine
