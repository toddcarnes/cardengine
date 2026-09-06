// Table engine: blinds, action order, betting rules, pots, showdown.
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using cardengine::Action;
using cardengine::ActionType;
using cardengine::GameConfig;
using cardengine::Table;
using testutil::cards;
using testutil::check;
using testutil::expect_throws;

GameConfig three_max() {
    GameConfig c;
    c.num_players = 3;
    return c;
}

void fold(Table& t, int s) { t.act(s, {ActionType::Fold, 0}); }
void chk(Table& t, int s) { t.act(s, {ActionType::Check, 0}); }
void call(Table& t, int s) { t.act(s, {ActionType::Call, 0}); }
void raise_to(Table& t, int s, int amount) {
    t.act(s, {ActionType::Raise, amount});
}

int total_chips(const Table& t) {
    int sum = 0;
    for (int i = 0; i < t.num_seats(); ++i) sum += t.stack(i);
    return sum + t.pot_total();
}

void check_down_streets(Table& t) {
    // Checks through the rest of the hand; only valid when nobody bets.
    while (!t.hand_complete()) {
        if (t.acting() != -1) {
            chk(t, t.acting());
        } else {
            t.deal_next_street();
        }
    }
}

}  // namespace

int main() {
    using namespace cardengine;

    // Config validation.
    {
        GameConfig bad;
        bad.num_players = 1;
        expect_throws<std::invalid_argument>([&] { validate(bad); }, "1 player");
        bad.num_players = 11;
        expect_throws<std::invalid_argument>([&] { validate(bad); }, "11 players");
        GameConfig ok;
        validate(ok);
        ok.starting_stack = 0;
        expect_throws<std::invalid_argument>([&] { validate(ok); }, "zero stack");
        ok.starting_stack = 50;
        expect_throws<std::invalid_argument>([&] { validate(ok); }, "stack under BB");
        ok = GameConfig{};
        ok.small_blind = 0;
        expect_throws<std::invalid_argument>([&] { validate(ok); }, "zero small blind");
        ok = GameConfig{};
        ok.small_blind = 200;
        expect_throws<std::invalid_argument>([&] { validate(ok); }, "SB bigger than BB");
    }

    // 3-max blinds: SB seat 1, BB seat 2, UTG seat 0 acts first.
    {
        Table t(three_max());
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        check(t.bet(1) == 50 && t.committed(1) == 50, "SB posts 50");
        check(t.bet(2) == 100 && t.committed(2) == 100, "BB posts 100");
        check(t.acting() == 0, "UTG first");
        check(t.current_bet() == 100, "current bet is BB");
    }

    // Heads-up: button is the small blind and acts first preflop,
    // big blind acts first postflop.
    {
        GameConfig c;
        c.num_players = 2;
        Table t(c);
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc"}));
        check(t.bet(0) == 50 && t.bet(1) == 100, "heads-up blinds");
        check(t.acting() == 0, "button acts first preflop heads-up");
        call(t, 0);
        chk(t, 1);
        t.deal_next_street();
        check(t.acting() == 1, "BB acts first postflop heads-up");
    }

    // Everyone folds to the big blind: wins without showdown.
    {
        Table t(three_max());
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        fold(t, 0);
        fold(t, 1);
        check(t.acting() == -1, "no action pending");
        check(t.hand_complete(), "hand complete on fold-out");
        auto payouts = t.settle();
        check(!t.went_to_showdown(), "no showdown on fold-out");
        check(payouts.size() == 1 && payouts[0].seat == 2 &&
                  payouts[0].amount == 150,
              "BB takes the blinds");
        check(t.stack(2) == 10000 - 100 + 150, "BB stack after win");
        check(t.button() == 1, "button advances");
        check(total_chips(t) == 30000, "chips conserved");
    }

    // Minimum raise rules preflop.
    {
        Table t(three_max());
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        const ActionOptions o = t.options(0);
        check(!o.can_check && o.call_amount == 100, "UTG faces 100");
        check(o.can_raise && o.min_raise_to == 200 && o.max_raise_to == 10000,
              "min open is 2x BB");
        expect_throws<std::invalid_argument>([&] { raise_to(t, 0, 150); },
                                "raise below minimum");
        raise_to(t, 0, 200);
        check(t.to_call(1) == 150, "SB faces 150 after min-raise");
    }

    // Aces hold at showdown; winner paid, stacks conserved.
    {
        GameConfig c;
        c.num_players = 2;
        Table t(c);
        // Deal: seat1 <- 7c, seat0 <- As, seat1 <- 2d, seat0 <- Ad.
        // Board: Ks Qh Jh 9c 3d. Seat 0's aces beat seat 1's K-high.
        t.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh", "Jh",
                                     "9c", "3d"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        check(t.hand_complete(), "river checked down");
        auto payouts = t.settle();
        check(t.went_to_showdown(), "showdown happened");
        check(t.stack(0) == 10100 && t.stack(1) == 9900, "aces paid");
        check(payouts.size() == 1 && payouts[0].seat == 0 &&
                  payouts[0].amount == 200,
              "single payout");
    }

    // Side pots: short stack wins main, second-best takes the side.
    {
        Table t(three_max());
        t.set_stack(0, 1000);
        t.set_stack(1, 500);
        t.set_stack(2, 2000);
        // seat1: As Ah; seat2: Qs Qh; seat0: Ks Kd.
        // Board: 2c 5d 9h Jc 3s. Aces > kings > queens.
        t.start_hand_from_deck(cards({"As", "Qs", "Ks", "Ah", "Qh", "Kd", "2c",
                                     "5d", "9h", "Jc", "3s"}));
        raise_to(t, 0, 1000);  // All in.
        call(t, 1);            // All in for 500 total.
        call(t, 2);
        check(t.acting() == -1, "round done with two all in");
        check_down_streets(t);
        t.settle();
        check(t.stack(1) == 1500, "short stack wins 1500 main");
        check(t.stack(0) == 1000, "kings take 1000 side");
        check(t.stack(2) == 1000, "queens lose 1000");
        check(total_chips(t) == 3500, "side-pot chips conserved");
    }

    // Bust-outs don't corrupt the next deal: survivors get exactly
    // hole_cards each (the deal loop counts participants, not seats).
    {
        Table t(three_max());
        t.set_stack(0, 60);
        t.set_button(2);
        // seat0: 7c 2d; seat1: As Ks; seat2: Qd Qh.
        // Board Ah Kh 9c 5d 3s pairs everyone's ace-king except seat 0.
        t.start_hand_from_deck(cards({"7c", "As", "Qd", "2d", "Ks", "Qh",
                                     "Ah", "Kh", "9c", "5d", "3s"}));
        raise_to(t, 2, 5000);
        call(t, 0);  // All in for 60 total.
        call(t, 1);
        check(t.acting() == -1, "round done");
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 0, "short stack busts");

        t.start_hand(99);
        check(!t.in_hand(0), "broke seat sits out");
        check(t.hole_cards(1).size() == 2, "survivor dealt two");
        check(t.hole_cards(2).size() == 2, "survivor dealt two");
        check(total_chips(t) == 20060, "chips conserved around bust");
    }

    // A short all-in raise does not reopen betting for players who acted.
    {
        Table t(three_max());
        t.set_stack(0, 280);
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        call(t, 0);  // 100, stack 180.
        call(t, 1);
        chk(t, 2);
        t.deal_next_street();
        raise_to(t, 1, 100);  // Min open.
        call(t, 2);
        raise_to(t, 0, 180);  // All in; increment 80 < 100: not a full raise.
        const ActionOptions o = t.options(1);
        check(!o.can_check && o.call_amount == 80 && !o.can_raise,
              "action not reopened");
        expect_throws<std::invalid_argument>([&] { raise_to(t, 1, 500); },
                                "re-raise over short all-in");
        call(t, 1);
        call(t, 2);
        check(t.acting() == -1, "round closes");
        t.deal_next_street();  // Throws if the round were still open.
    }

    // Odd-chip split pot goes clockwise from the button.
    {
        GameConfig c = three_max();
        c.small_blind = 25;
        c.big_blind = 25;
        Table t(c);
        // seat1: Ah 3d; seat2: Ks 9d; seat0: As 2d.
        // Board Ac Kd Qh Jc 5s: seats 0+1 tie with AAKQJ, seat 2 has KK.
        // Pot 75 splits 38/37 with the odd chip to seat 0 (the button).
        t.start_hand_from_deck(cards({"Ah", "Ks", "As", "3d", "9d", "2d", "Ac",
                                     "Kd", "Qh", "Jc", "5s"}));
        call(t, 0);
        chk(t, 1);
        chk(t, 2);
        check_down_streets(t);
        auto payouts = t.settle();
        check(t.went_to_showdown(), "split goes to showdown");
        int p0 = -1, p1 = -1;
        for (const auto& p : payouts) {
            if (p.seat == 0) p0 = p.amount;
            if (p.seat == 1) p1 = p.amount;
        }
        check(p0 == 38 && p1 == 37, "odd chip to the button");
        check(t.stack(0) == 10013 && t.stack(1) == 10012 && t.stack(2) == 9975,
              "split stacks");
    }

    // Unmatched excess is returned, not awarded to the winner.
    {
        GameConfig c;
        c.num_players = 2;
        Table t(c);
        t.set_stack(0, 2000);
        t.set_stack(1, 200);
        // seat1: As Ad; seat0: Ks Qd. Board bricks out for seat 0.
        t.start_hand_from_deck(cards({"As", "Ks", "Ad", "Qd", "2c", "5d", "9h",
                                     "Jc", "3s"}));
        call(t, 0);
        chk(t, 1);
        t.deal_next_street();
        chk(t, 1);
        chk(t, 0);
        t.deal_next_street();
        chk(t, 1);
        chk(t, 0);
        t.deal_next_street();
        chk(t, 1);
        raise_to(t, 0, 500);
        call(t, 1);  // All in for 100 total; 400 of seat 0's bet is uncalled.
        t.settle();
        // Seat 1 wins the 400 contested pot; seat 0 gets 400 back.
        check(t.stack(1) == 400, "short winner paid 400");
        check(t.stack(0) == 1800, "excess 400 returned");
        check(total_chips(t) == 2200, "return chips conserved");
    }

    // Postflop action starts left of the button.
    {
        Table t(three_max());
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        call(t, 0);
        call(t, 1);
        chk(t, 2);
        t.deal_next_street();
        check(t.acting() == 1, "flop starts left of button");
    }

    // Turn enforcement and illegal-action errors.
    {
        Table t(three_max());
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        expect_throws<std::logic_error>([&] { chk(t, 1); }, "act out of turn");
        expect_throws<std::invalid_argument>([&] { chk(t, 0); }, "check facing blind");
        expect_throws<std::logic_error>([&] { t.deal_next_street(); }, "deal mid-round");
        expect_throws<std::logic_error>([&] { t.settle(); }, "settle mid-hand");
        expect_throws<std::logic_error>([&] { t.set_stack(0, 500); }, "stack mid-hand");
        expect_throws<std::logic_error>([&] { t.set_button(1); }, "button mid-hand");
        expect_throws<std::logic_error>([&] { t.set_blinds(50, 100); }, "blinds mid-hand");
        expect_throws<std::invalid_argument>([&] { t.act(9, {ActionType::Fold, 0}); },
                                "seat out of range");
    }

    // Chip conservation across full hands and button rotation.
    {
        Table t(three_max());
        for (std::uint64_t seed : {7, 8, 9}) {
            t.start_hand(seed);
            while (!t.hand_complete()) {
                if (t.acting() != -1) {
                    const int a = t.acting();
                    if (t.options(a).can_check) {
                        chk(t, a);
                    } else {
                        call(t, a);
                    }
                } else {
                    t.deal_next_street();
                }
            }
            t.settle();
            check(total_chips(t) == 30000, "conserved across hands");
        }
        check(t.button() == 0, "button rotated 0->1->2->0");
    }

    std::cout << "test_table ok\n";
    return 0;
}
