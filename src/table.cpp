#include "cardengine/table.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "cardengine/clock.h"
#include "cardengine/deck.h"
#include "cardengine/hand.h"

namespace cardengine {

namespace {

// Merges one award into the payout list (one entry per seat).
void merge_payout(std::vector<Payout>& out, int seat, int amount) {
    const auto it = std::find_if(out.begin(), out.end(), [&](const Payout& p) {
        return p.seat == seat;
    });
    if (it == out.end()) {
        out.push_back({seat, amount});
    } else {
        it->amount += amount;
    }
}

// High half of a hi-lo split: the odd chip goes high first.
int high_half(int amount) { return amount - amount / 2; }

}  // namespace

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

const std::vector<Card>& Table::up_cards(int seat) const {
    check_seat(seat);
    const Seat& s = seats_[static_cast<std::size_t>(seat)];
    if (!s.in_hand) throw std::logic_error("seat not in hand");
    return s.up;
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
    const Seat& broke = seats_[static_cast<std::size_t>(seat)];
    return broke.in_hand && !broke.folded && broke.stack == 0;
}

bool Table::drew(int seat) const {
    check_seat(seat);
    return seats_[static_cast<std::size_t>(seat)].drew;
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
    const Seat& caller = seats_[static_cast<std::size_t>(seat)];
    return current_bet_ > caller.bet ? current_bet_ - caller.bet : 0;
}

int Table::pot_total() const {
    int total = 0;
    for (const Seat& contributor : seats_) total += contributor.committed;
    return total;
}

ActionOptions Table::options(int seat) const {
    ActionOptions out;
    if (seat < 0 || seat >= num_seats()) return out;
    const Seat& cand = seats_[static_cast<std::size_t>(seat)];
    if (street_ == Street::None || street_ == Street::Complete) return out;
    if (street_ == Street::Draw) return out;  // Exchange only, no betting.
    if (!can_act(seat)) return out;
    const int call = to_call(seat);
    out.can_check = (call == 0);
    out.call_amount = call < cand.stack ? call : cand.stack;
    const bool reopened = !cand.acted || cand.seen_seq < round_seq_;
    if (cand.stack == 0 || cand.bet + cand.stack <= current_bet_ ||
        !reopened) {
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
        // Stud third street: completing the bring-in to the small bet is
        // the table minimum — the complete costs exactly the small bet
        // total (fixed_bet_size() already returns the small bet on third).
        if (config_.showdown == HandConstruction::StudSeven &&
            street_ == Street::Third && current_bet_ == config_.bring_in &&
            config_.bring_in > 0) {
            min_full = fixed_bet_size();
        }
    }
    int max_to = cand.bet + cand.stack;
    if (config_.betting == BettingStructure::PotLimit) {
        // Pot-sized raise: call first (pot grows by to_call), then raise
        // the pot on top: max total = bet + call + (pot + call).
        const int pot_max = cand.bet + 2 * call + pot_total();
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
    start_hand_common();
    if (config_.showdown == HandConstruction::StudSeven) {
        deal_stud_third(deck);
    } else {
        const int participants = static_cast<int>(
            std::count_if(seats_.begin(), seats_.end(),
                          [](const Seat& s) { return s.in_hand; }));
        // Deal hole_cards rounds starting left of the button. The inner
        // loop runs participants times, not seats times: busted-out seats
        // sit out and must not duplicate anyone else's cards.
        int s = button_;
        for (int round = 0; round < config_.hole_cards; ++round) {
            for (int k = 0; k < participants; ++k) {
                s = next_in_hand(s + 1);
                Seat& seat = seats_[static_cast<std::size_t>(s)];
                seat.hole.push_back(deck.deal());
            }
        }
        // Keep the stub: draw replacements come off the same shoe, in
        // order.
        while (!deck.empty()) shoe_.push_back(deck.deal());
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
    // Draw games deal replacements off the same shoe, so the seam needs
    // the full worst case (every seat drawing the max) up front. Stud
    // deals third street off the shoe (3 per funded seat); later streets
    // consume the same shoe one round at a time.
    const int stub = (config_.showdown == HandConstruction::DrawFive ||
                      config_.showdown == HandConstruction::DeuceSeven)
                         ? funded * config_.max_draw
                         : 0;
    const int third = (config_.showdown == HandConstruction::StudSeven)
                          ? funded * 3
                          : funded * config_.hole_cards;
    const std::size_t need = static_cast<std::size_t>(third +
                                                      config_.board_cards + stub);
    if (top_first.size() < need) {
        throw std::invalid_argument("deck too small for this hand");
    }
    start_hand_common();
    if (config_.showdown == HandConstruction::StudSeven) {
        // Stud streets need the whole shoe (not a fixed prefix): deal
        // third street off the front into the shoe-backed helper below.
        shoe_ = std::move(top_first);
        deal_stud_third_from_shoe();
    } else {
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
    }
    record_hand_started(0, false);
}

void Table::act(int seat, const Action& action) {
    check_seat(seat);
    if (street_ == Street::None || street_ == Street::Complete) {
        throw std::logic_error("no hand running");
    }
    if (street_ == Street::Draw) throw std::logic_error("draws pending");
    if (seat != acting_) throw std::logic_error("not this seat's turn");
    Seat& actor = seats_[static_cast<std::size_t>(seat)];

    switch (action.type) {
        case ActionType::Fold:
            actor.folded = true;
            actor.acted = true;
            actor.seen_seq = round_seq_;
            break;
        case ActionType::Check:
            if (to_call(seat) != 0) {
                throw std::invalid_argument("cannot check facing a bet");
            }
            actor.acted = true;
            actor.seen_seq = round_seq_;
            break;
        case ActionType::Call: {
            const int call = to_call(seat);
            const int pay = call < actor.stack ? call : actor.stack;
            actor.stack -= pay;
            actor.bet += pay;
            actor.committed += pay;
            actor.acted = true;
            actor.seen_seq = round_seq_;
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
            const int additional = action.amount - actor.bet;
            const int increment = action.amount - current_bet_;
            actor.stack -= additional;
            actor.bet = action.amount;
            actor.committed += additional;
            actor.acted = true;
            // Stud third street: completing the bring-in to the small bet
            // is the table minimum (limit snaps min_raise_to there), but
            // it is not a full raise — it must not reopen betting or burn
            // the limit cap. Detect it by the bring-in still standing as
            // the current bet.
            const bool completing =
                config_.showdown == HandConstruction::StudSeven &&
                street_ == Street::Third && current_bet_ == config_.bring_in &&
                action.amount == opts.min_raise_to;
            if (!completing && increment >= last_raise_size_) {
                // Full raise: reopens betting for everyone else.
                last_raise_size_ = increment;
                ++round_seq_;
                actor.seen_seq = round_seq_;
                if (config_.betting == BettingStructure::Limit) {
                    ++raises_this_round_;  // Short all-ins don't consume cap.
                }
            } else {
                // Short all-in: current bet rises, action stays closed.
                actor.seen_seq = round_seq_;
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
    if (street_ == Street::Draw) throw std::logic_error("draws pending");
    if (seat != acting_) throw std::logic_error("not this seat's turn");
    Seat& timed = seats_[static_cast<std::size_t>(seat)];
    timed.folded = true;
    timed.acted = true;
    timed.seen_seq = round_seq_;
    (void)at;  // The stamp lives in the order of events, not the event.
    advance_acting(seat + 1);
    acting_since_ = now_seconds();
    events_.push_back(TimeoutEvent{seat, pot_total()});
}

void Table::deal_next_street() {
    if (config_.showdown == HandConstruction::StudSeven) {
        if (acting_ != -1) throw std::logic_error("betting round not complete");
        if (hand_complete()) throw std::logic_error("hand already decided");
        // Fourth through sixth add one up card; seventh adds one down
        // card (or a single shared up card when the shoe runs dry).
        if (street_ == Street::Third) {
            deal_stud_round(true, Street::Fourth);
        } else if (street_ == Street::Fourth) {
            deal_stud_round(true, Street::Fifth);
        } else if (street_ == Street::Fifth) {
            deal_stud_round(true, Street::Sixth);
        } else if (street_ == Street::Sixth) {
            deal_stud_round(false, Street::Seventh);
        } else {
            throw std::logic_error("no street left to deal");
        }
        return;
    }
    if (street_ == Street::Draw) {
        // Post-draw betting reuses the flop slot (draw games have no board).
        if (!draws_pending().empty()) {
            throw std::logic_error("draws pending");
        }
        street_ = Street::Flop;
        StreetDealtEvent dealt;
        dealt.street = street_;
        events_.push_back(dealt);
        begin_round();
        acting_since_ = now_seconds();
        return;
    }
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
        if (config_.showdown == HandConstruction::DrawFive ||
            config_.showdown == HandConstruction::DeuceSeven) {
            // Draw games exchange before any more betting: no cards, no
            // betting round — seats take their discards in turn order.
            street_ = Street::Draw;
            StreetDealtEvent dealt;
            dealt.street = street_;
            events_.push_back(dealt);
            acting_since_ = now_seconds();
            return;
        }
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

std::vector<int> Table::draws_pending() const {
    std::vector<int> out;
    if (street_ != Street::Draw) return out;
    for (int k = 1; k <= num_seats(); ++k) {
        const int s = (button_ + k) % num_seats();
        const Seat& seat = seats_[static_cast<std::size_t>(s)];
        if (seat.in_hand && !seat.folded && seat.stack > 0 && !seat.drew) {
            out.push_back(s);
        }
    }
    return out;
}

void Table::discard(int seat, const std::vector<std::string>& discards) {
    check_seat(seat);
    if (street_ != Street::Draw) throw std::logic_error("no draw pending");
    Seat& s = seats_[static_cast<std::size_t>(seat)];
    if (!s.in_hand || s.folded) throw std::logic_error("seat not in hand");
    if (s.drew) throw std::logic_error("seat already drew");
    const std::vector<int> pending = draws_pending();
    if (pending.empty() || pending[0] != seat) {
        throw std::logic_error("not this seat's draw");
    }
    if (discards.size() > static_cast<std::size_t>(config_.max_draw)) {
        throw std::invalid_argument("too many discards");
    }
    if (discards.size() > shoe_.size()) {
        // Check the shoe before touching the hole: a short shoe refuses
        // the whole exchange rather than dealing half a draw.
        throw std::logic_error("shoe too short for the draw");
    }
    // Resolve texts against the current hole (exact cards, no duplicates).
    std::vector<bool> used(s.hole.size(), false);
    std::vector<std::size_t> drop;
    for (const std::string& text : discards) {
        Card want;
        try {
            want = parse_card(text);
        } catch (const std::exception&) {
            throw std::invalid_argument("bad card '" + text + "'");
        }
        bool found = false;
        for (std::size_t k = 0; k < s.hole.size(); ++k) {
            if (!used[k] && s.hole[k] == want) {
                used[k] = true;
                drop.push_back(k);
                found = true;
                break;
            }
        }
        if (!found) throw std::invalid_argument("bad discard '" + text + "'");
    }
    // Throw away highest indices first so erases don't shift the rest.
    std::sort(drop.begin(), drop.end(), std::greater<std::size_t>());
    for (std::size_t k : drop) {
        s.hole.erase(s.hole.begin() + static_cast<std::ptrdiff_t>(k));
    }
    for (std::size_t k = 0; k < drop.size(); ++k) {
        s.hole.push_back(shoe_.front());
        shoe_.erase(shoe_.begin());
    }
    s.drew = true;
    DrawEvent drew;
    drew.seat = seat;
    drew.drew = static_cast<int>(drop.size());
    events_.push_back(drew);
    acting_since_ = now_seconds();
}

bool Table::hand_complete() const {
    if (street_ == Street::None || street_ == Street::Complete) return false;
    int remaining = 0;
    for (const Seat& s : seats_) {
        if (s.in_hand && !s.folded) ++remaining;
    }
    if (remaining <= 1) return true;
    if (config_.showdown == HandConstruction::StudSeven) {
        return street_ == Street::Seventh && acting_ == -1;
    }
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
        // rather than dealing half a runout. A lone unmatched top band
        // refunds whole first (never called on any board).
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
        std::vector<int> levels;
        for (int i = 0; i < num_seats(); ++i) {
            const Seat& s = seats_[static_cast<std::size_t>(i)];
            if (s.in_hand && s.committed > 0) levels.push_back(s.committed);
        }
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        refund_unmatched_top(levels);
        award_multi_board(payouts, levels, boards);
    } else {
        showdown_ = true;
        // Contribution levels, low to high; each band forms one pot.
        // A lone unmatched top band is never contested: it returns to its
        // owner first (so winners only ever split what was actually called).
        std::vector<int> levels;
        for (int i = 0; i < num_seats(); ++i) {
            const Seat& s = seats_[static_cast<std::size_t>(i)];
            if (s.in_hand && s.committed > 0) levels.push_back(s.committed);
        }
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        // A lone unmatched top band was never called: refund it first so
        // winners only split what was actually contested.
        refund_unmatched_top(levels);

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
            if (config_.showdown == HandConstruction::DeuceSeven) {
                // 2-7 lowball: the worst poker hand wins, so the best
                // DeuceValue (lowest) takes the pot. Ties split, odd chips
                // clockwise from the button like every other split.
                std::vector<DeuceValue> values;
                for (int i : eligible) {
                    const Seat& s = seats_[static_cast<std::size_t>(i)];
                    values.push_back(deuce_value_on(s.hole));
                }
                DeuceValue best = values[0];
                for (const DeuceValue& value : values) {
                    if (value < best) best = value;
                }
                std::vector<int> winners;
                for (std::size_t k = 0; k < eligible.size(); ++k) {
                    if (!(values[k] < best) && !(best < values[k])) {
                        winners.push_back(eligible[k]);
                    }
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
                    const int award =
                        share + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
                    merge_payout(payouts, winners[w], award);
                }
                continue;
            }
            std::vector<HandValue> values;
            for (int i : eligible) {
                const Seat& s = seats_[static_cast<std::size_t>(i)];
                if (config_.showdown == HandConstruction::StudSeven) {
                    // Best five of the seat's own seven (down + up, plus
                    // the shared river card when 8-handed play overflowed).
                    std::vector<Card> seven = s.hole;
                    seven.insert(seven.end(), s.up.begin(), s.up.end());
                    seven.insert(seven.end(), community_.begin(),
                                 community_.end());
                    values.push_back(evaluate_best(seven));
                } else {
                    values.push_back(showdown_value(s.hole, board_));
                }
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
                const int award =
                    share + (w < static_cast<std::size_t>(remainder) ? 1 : 0);
                merge_payout(payouts, winners[w], award);
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

// Unmatched top band returns to its lone owner before the award math
// runs (it was never called, so no winner may take it). Returns the
// excess to refund (0 when the top band is contested); the caller applies
// it to the owner's stack and merges the levels.
int Table::unmatched_top_excess(const std::vector<int>& levels) const {
    if (levels.size() <= 1) return 0;
    const int top = levels.back();
    int leaders = 0;
    int leader = -1;
    for (int i = 0; i < num_seats(); ++i) {
        const Seat& s = seats_[static_cast<std::size_t>(i)];
        if (s.in_hand && s.committed == top) {
            ++leaders;
            leader = i;
        }
    }
    if (leaders != 1) return 0;
    (void)leader;
    return top - levels[levels.size() - 2];
}

void Table::refund_unmatched_top(std::vector<int>& levels) {
    const int excess = unmatched_top_excess(levels);
    if (excess <= 0) return;
    for (int i = 0; i < num_seats(); ++i) {
        Seat& s = seats_[static_cast<std::size_t>(i)];
        if (s.in_hand && s.committed == levels.back()) {
            s.stack += excess;
            s.committed -= excess;
            break;
        }
    }
    levels.back() -= excess;
    if (levels.size() > 1 && levels.back() == levels[levels.size() - 2]) {
        levels.pop_back();
    }
}

HandValue Table::showdown_value(const std::vector<Card>& hole,
                                 const std::vector<Card>& board) const {
    if (config_.showdown == HandConstruction::OmahaTwoAndThree) {
        return evaluate_omaha(hole, board);
    }
    if (config_.showdown == HandConstruction::OmahaHiLo) {
        return evaluate_omaha_hilo(hole, board).high;
    }
    // DeuceSeven never reaches here: settle() ranks 2-7 directly. Reaching
    // this path means a board-share helper (runouts) was called on a
    // boardless game — config validation keeps that combination out, but
    // fail loud rather than award a pot on the wrong rule.
    if (config_.showdown == HandConstruction::DeuceSeven) {
        throw std::logic_error("deuce showdown has no board");
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
                merge_payout(payouts, ordered[w], award);
            }
        };
        if (low_winners.empty()) {
            pay_share(high_winners, amount);
            return;
        }
        const int high = high_half(amount);  // Odd chip goes high.
        pay_share(high_winners, high);
        pay_share(low_winners, amount - high);
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
        merge_payout(payouts, winners[w], award);
    }
}

// Every contribution band, split across boards first: each board decides
// its equal share of the band. Odd chips stay board-major — the first
// boards in order absorb the remainder one chip each. Takes the pre-
// refunded levels (settle() returns the unmatched top band first, since
// a const method cannot touch stacks).
void Table::award_multi_board(
    std::vector<Payout>& payouts, const std::vector<int>& levels,
    const std::vector<std::vector<Card>>& boards) const {
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
    // Eligibility comes from the contribution bands, as in settle().
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
            merge_payout(payouts, ordered[w], award);
        }
    };
    if (low_winners.empty()) {
        pay_share(high_winners, amount);  // No low: high scoops.
        return;
    }
    const int high = high_half(amount);  // Odd chip goes high.
    pay_share(high_winners, high);
    pay_share(low_winners, amount - high);
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
        Seat& restored = seats_[static_cast<std::size_t>(i)];
        restored.stack = saved.stacks[static_cast<std::size_t>(i)];
        restored.bet = 0;
        restored.committed = 0;
        restored.in_hand = false;
        restored.folded = false;
        restored.acted = false;
        restored.drew = false;
        restored.sitting_out = saved.sitting_out[static_cast<std::size_t>(i)];
        restored.seen_seq = 0;
        restored.hole.clear();
        restored.up.clear();
    }
    button_ = saved.button;
    street_ = Street::None;
    board_.clear();
    community_.clear();
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

int Table::next_live(int from) const {
    for (int k = 0; k < num_seats(); ++k) {
        const int s = (from + k) % num_seats();
        const Seat& cand = seats_[static_cast<std::size_t>(s)];
        if (cand.in_hand && !cand.folded) return s;
    }
    throw std::logic_error("no live seat");
}

// Stud ordering suits: spades > hearts > diamonds > clubs. Ordering only
// (never showdown): the bring-in and the opening high hand both break
// ties this way, exactly like a live room ranks door cards.
int Table::stud_high_score(int seat) const {
    const Seat& holder = seats_[static_cast<std::size_t>(seat)];
    int best = -1;
    for (const Card& c : holder.up) {
        const int suit_rank = static_cast<int>(c.suit);  // C<D<H<S.
        const int score = static_cast<int>(c.rank) * 4 + suit_rank;
        if (score > best) best = score;
    }
    return best;
}

int Table::stud_low_score(int seat) const {
    const Seat& holder = seats_[static_cast<std::size_t>(seat)];
    int best = std::numeric_limits<int>::max();
    for (const Card& c : holder.up) {
        const int suit_rank = static_cast<int>(c.suit);
        const int score = static_cast<int>(c.rank) * 4 + suit_rank;
        if (score < best) best = score;
    }
    return best;
}

int Table::bring_in_seat() const {
    if (config_.showdown != HandConstruction::StudSeven) return -1;
    if (street_ != Street::Third) return -1;
    int best_seat = -1;
    int best_score = std::numeric_limits<int>::max();
    for (int i = 0; i < num_seats(); ++i) {
        const Seat& cand = seats_[static_cast<std::size_t>(i)];
        if (!cand.in_hand || cand.folded || cand.up.empty()) continue;
        const int score = stud_low_score(i);
        bool nearer = false;
        if (best_seat < 0 || score < best_score) {
            nearer = true;
        } else if (score == best_score) {
            // Clockwise from the button breaks exact ties (same rank and
            // suit can't repeat — one deck — so this only orders equal
            // ranks across suits past the suit rank in the score).
            const int da = (i - button_ + num_seats()) % num_seats();
            const int db = (best_seat - button_ + num_seats()) % num_seats();
            nearer = da < db;
        }
        if (nearer) {
            best_seat = i;
            best_score = score;
        }
    }
    return best_seat;
}

int Table::stud_opener() const {
    if (street_ == Street::Third) {
        const int bring = bring_in_seat();
        if (bring < 0) return -1;
        // Action opens left of the bring-in (they already posted).
        return next_live(bring + 1);
    }
    // Fourth street on: best visible hand opens (high first, then the
    // same clockwise tiebreak). Only live seats can hold the action.
    int best_seat = -1;
    int best_score = -1;
    for (int i = 0; i < num_seats(); ++i) {
        const Seat& cand = seats_[static_cast<std::size_t>(i)];
        if (!cand.in_hand || cand.folded || cand.stack == 0 ||
            cand.up.empty()) {
            continue;
        }
        const int score = stud_high_score(i);
        bool nearer = false;
        if (best_seat < 0 || score > best_score) {
            nearer = true;
        } else if (score == best_score) {
            const int da = (i - button_ + num_seats()) % num_seats();
            const int db = (best_seat - button_ + num_seats()) % num_seats();
            nearer = da < db;
        }
        if (nearer) {
            best_seat = i;
            best_score = score;
        }
    }
    return best_seat;
}

Card Table::take_card(Deck* deck) {
    if (deck != nullptr) return deck->deal();
    if (shoe_.empty()) throw std::logic_error("shoe too short for stud");
    Card c = shoe_.front();
    shoe_.erase(shoe_.begin());
    return c;
}

void Table::deal_stud_third(Deck& deck) {
    // Third street: 2 down + 1 up per participant, button-out, then
    // antes and the bring-in. No blinds, no straddle in stud.
    int cur = button_;
    const int participants = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(),
                      [](const Seat& cand) { return cand.in_hand; }));
    for (int round = 0; round < 2; ++round) {
        for (int k = 0; k < participants; ++k) {
            cur = next_in_hand(cur + 1);
            seats_[static_cast<std::size_t>(cur)].hole.push_back(deck.deal());
        }
    }
    for (int k = 0; k < participants; ++k) {
        cur = next_in_hand(cur + 1);
        seats_[static_cast<std::size_t>(cur)].up.push_back(deck.deal());
    }
    while (!deck.empty()) shoe_.push_back(deck.deal());
    post_stud_bring_in();
}

void Table::deal_stud_third_from_shoe() {
    int cur = button_;
    const int participants = static_cast<int>(
        std::count_if(seats_.begin(), seats_.end(),
                      [](const Seat& cand) { return cand.in_hand; }));
    for (int round = 0; round < 2; ++round) {
        for (int k = 0; k < participants; ++k) {
            cur = next_in_hand(cur + 1);
            seats_[static_cast<std::size_t>(cur)].hole.push_back(
                take_card(nullptr));
        }
    }
    for (int k = 0; k < participants; ++k) {
        cur = next_in_hand(cur + 1);
        seats_[static_cast<std::size_t>(cur)].up.push_back(take_card(nullptr));
    }
    post_stud_bring_in();
}

void Table::post_stud_bring_in() {
    // Antes first (dead money, like every variant), then the low upcard's
    // forced bet. With bring_in = 0 the high hand opens with no forced
    // bet (current_bet stays 0 and the opener checks or bets).
    post_stud_antes();
    street_ = Street::Third;
    round_seq_ = 0;
    raises_this_round_ = 0;
    current_bet_ = 0;
    last_raise_size_ = config_.big_blind;
    post_stud_forced();
    const int opener = stud_opener();
    advance_acting(opener);
    acting_since_ = now_seconds();
}

void Table::post_stud_antes() {
    if (config_.ante <= 0) return;
    if (config_.ante_from == AnteSource::ButtonOnly) {
        Seat& dealer = seats_[static_cast<std::size_t>(button_)];
        if (dealer.in_hand) {
            int participants = 0;
            for (const Seat& cand : seats_) {
                if (cand.in_hand) ++participants;
            }
            const int total = config_.ante * participants;
            const int pay = total < dealer.stack ? total : dealer.stack;
            dealer.stack -= pay;
            dealer.committed += pay;
        }
        return;
    }
    for (int i = 0; i < num_seats(); ++i) {
        Seat& punter = seats_[static_cast<std::size_t>(i)];
        if (!punter.in_hand) continue;
        const int pay =
            config_.ante < punter.stack ? config_.ante : punter.stack;
        punter.stack -= pay;
        punter.committed += pay;
    }
}

void Table::post_stud_forced() {
    const int bring = bring_in_seat();
    if (bring < 0 || config_.bring_in <= 0) return;
    Seat& bseat = seats_[static_cast<std::size_t>(bring)];
    const int pay =
        config_.bring_in < bseat.stack ? config_.bring_in : bseat.stack;
    bseat.stack -= pay;
    bseat.bet += pay;
    bseat.committed += pay;
    current_bet_ = pay;
    last_raise_size_ = config_.big_blind;
}

void Table::deal_stud_round(bool face_up, Street street) {
    // One card to every live seat, button-out (all-in seats are dealt
    // like everyone else — their cards play at showdown). Seventh street
    // goes community when the shoe can't cover the live seats: a single
    // shared up card instead of one per seat (classic 8-handed fallback).
    std::vector<int> live;
    for (int k = 1; k <= num_seats(); ++k) {
        const int cur = (button_ + k) % num_seats();
        const Seat& cand = seats_[static_cast<std::size_t>(cur)];
        if (cand.in_hand && !cand.folded) live.push_back(cur);
    }
    StudDealtEvent dealt;
    dealt.street = street;
    dealt.face_up = face_up;
    if (!face_up && street == Street::Seventh && shoe_.size() < live.size()) {
        community_.push_back(take_card(nullptr));
        dealt.community = true;
        dealt.face_up = true;  // The shared river card is face-up.
        dealt.cards = community_;
    } else {
        for (int live_seat : live) {
            Card c = take_card(nullptr);
            if (face_up) {
                seats_[static_cast<std::size_t>(live_seat)].up.push_back(c);
            } else {
                seats_[static_cast<std::size_t>(live_seat)].hole.push_back(c);
            }
            dealt.per_seat.push_back({live_seat, c});
        }
        if (face_up) {
            for (const auto& sc : dealt.per_seat) dealt.cards.push_back(sc.card);
        }
    }
    events_.push_back(dealt);
    street_ = street;
    begin_stud_round(stud_opener());
    acting_since_ = now_seconds();
}

bool Table::can_act(int seat) const {
    const Seat& cand = seats_[static_cast<std::size_t>(seat)];
    return cand.in_hand && !cand.folded && cand.stack > 0;
}

bool Table::needs_action(int seat) const {
    if (!can_act(seat)) return false;
    const Seat& cand = seats_[static_cast<std::size_t>(seat)];
    return !cand.acted || to_call(seat) > 0;
}

void Table::advance_acting(int from) {
    acting_ = -1;
    int remaining = 0;
    for (const Seat& cand : seats_) {
        if (cand.in_hand && !cand.folded) ++remaining;
    }
    if (remaining <= 1) return;  // Last player wins immediately.
    for (int k = 0; k < num_seats(); ++k) {
        const int cur = (from + k) % num_seats();
        if (needs_action(cur)) {
            acting_ = cur;
            return;
        }
    }
}

void Table::post_blind(int seat, int amount) {
    Seat& blind = seats_[static_cast<std::size_t>(seat)];
    const int pay = amount < blind.stack ? amount : blind.stack;
    blind.stack -= pay;
    blind.bet += pay;
    blind.committed += pay;
}

void Table::begin_round() {
    ++round_seq_;  // New betting round: everyone owes fresh action.
    for (Seat& seat : seats_) {
        if (!seat.in_hand) continue;
        seat.bet = 0;
        seat.acted = false;
        seat.seen_seq = round_seq_ - 1;
    }
    current_bet_ = 0;
    last_raise_size_ = fixed_bet_size();
    raises_this_round_ = 0;
    advance_acting(button_ + 1);
}

void Table::begin_stud_round(int opener) {
    // Stud opens by hand strength (bring-in's left on third, best visible
    // hand after), not position: same reset, action starts at the opener.
    ++round_seq_;
    for (Seat& seat : seats_) {
        if (!seat.in_hand) continue;
        seat.bet = 0;
        seat.acted = false;
        seat.seen_seq = round_seq_ - 1;
    }
    current_bet_ = 0;
    last_raise_size_ = fixed_bet_size();
    raises_this_round_ = 0;
    advance_acting(opener);
}

int Table::fixed_bet_size() const {
    // Stud limit: small bets on third/fourth streets, big bets from fifth
    // on (the classic small-big split). Button games use the flop/turn
    // split instead.
    if (config_.showdown == HandConstruction::StudSeven) {
        if (street_ == Street::Fifth || street_ == Street::Sixth ||
            street_ == Street::Seventh) {
            return 2 * config_.big_blind;
        }
        return config_.big_blind;
    }
    if (street_ == Street::Turn || street_ == Street::River) {
        return 2 * config_.big_blind;
    }
    return config_.big_blind;
}

void Table::start_hand_common() {
    if (street_ != Street::None && street_ != Street::Complete) {
        throw std::logic_error("hand already running");
    }
    for (Seat& seat : seats_) {
        seat.bet = 0;
        seat.committed = 0;
        seat.in_hand = seat.stack > 0 && !seat.sitting_out;
        seat.folded = false;
        seat.acted = false;
        seat.seen_seq = 0;
        seat.drew = false;
        seat.hole.clear();
        seat.up.clear();
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
    community_.clear();
    shoe_.clear();

    // Stud posts no blinds: the deal below antes up and brings in.
    if (config_.showdown == HandConstruction::StudSeven) {
        return;
    }

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
            Seat& dealer = seats_[static_cast<std::size_t>(button_)];
            if (dealer.in_hand) {
                const int total = config_.ante * participants;
                const int pay = total < dealer.stack ? total : dealer.stack;
                dealer.stack -= pay;
                dealer.committed += pay;
            }
        } else {
            for (int i = 0; i < num_seats(); ++i) {
                Seat& antes = seats_[static_cast<std::size_t>(i)];
                if (!antes.in_hand) continue;
                const int pay =
                    config_.ante < antes.stack ? config_.ante : antes.stack;
                antes.stack -= pay;
                antes.committed += pay;
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
