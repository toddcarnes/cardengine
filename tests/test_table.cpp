// Table engine: blinds, action order, betting rules, pots, showdown.
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/clock.h"
#include "cardengine/config.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using cardengine::Action;
using cardengine::ActionType;
using cardengine::expired;
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
        expect_throws<std::logic_error>([&] { t.timeout(1); }, "timeout out of turn");
        expect_throws<std::invalid_argument>([&] { t.timeout(9); }, "timeout seat range");
    }

    // The action clock stamps every (re)start; timeouts fold by the clock.
    {
        Table t(three_max());
        check(t.acting_since() == -1, "idle clock is -1");
        t.start_hand(7);
        check(t.acting_since() >= 0, "deal starts the clock");
        const int first = t.acting();
        t.set_acting_since_for_tests(1000);
        check(t.acting_since() == 1000, "test seam re-stamps");
        check(expired(1000 + 29, t.acting_since(), 30) == false, "not yet due");
        check(expired(1000 + 30, t.acting_since(), 30), "due at the limit");
        check(expired(999, t.acting_since(), 30) == false, "newer boot never due");
        t.timeout_at(first, 1000 + 30);
        check(t.has_folded(first), "timeout folds the holder");
        check(t.acting() != first, "action moves on");
        check(t.acting_since() >= 0, "timeout restarts the clock");
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

    // Sit-outs skip the hand: no cards, no blinds, no button.
    {
        Table t(three_max());
        t.set_sitting_out(0, true);
        check(t.sitting_out(0) && !t.sitting_out(1) && !t.sitting_out(2),
              "flags read back");
        expect_throws<std::invalid_argument>([&] { t.set_sitting_out(9, true); },
                                "sitout seat range");
        expect_throws<std::invalid_argument>([&] { t.sitting_out(9); },
                                "sitting_out seat range");
        t.start_hand(7);
        check(!t.in_hand(0) && t.in_hand(1) && t.in_hand(2),
              "sitter skips the deal");
        expect_throws<std::logic_error>([&] { t.hole_cards(0); }, "sitter has no cards");
        check(t.hole_cards(1).size() == 2, "live seat dealt two");
        check(t.hole_cards(2).size() == 2, "second live seat dealt two");
        // One participant cannot start a hand: needs at least two.
        Table short_handed(three_max());
        short_handed.set_sitting_out(0, true);
        short_handed.set_sitting_out(1, true);
        expect_throws<std::logic_error>([&] { short_handed.start_hand(7); },
                           "lone seat cannot play");
        // Mid-hand flips wait for the next hand; the running hand is safe.
        t.set_sitting_out(1, true);  // Legal mid-hand, ignored until next deal.
        check(t.in_hand(1), "running hand unaffected");
        t.set_sitting_out(0, false);
        t.set_sitting_out(1, false);
        t.set_sitting_out(2, false);
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
        check(total_chips(t) == 30000, "conserved around sitouts");
        // Button skips sitters: seat 0 out, seats 1+2 play, button stays
        // off the sitter and no blind touches them.
        t.set_sitting_out(0, true);
        t.start_hand(8);
        check(t.button() != 0, "button skips the sitter");
        check(!t.in_hand(0) && t.in_hand(1) && t.in_hand(2),
              "sitter skips the next hand too");
        check(t.committed(0) == 0, "sitter posts nothing");
    }

    // HiLo odd pot: the high half takes the extra chip.
    {
        GameConfig c = three_max();
        c.small_blind = 25;
        c.big_blind = 25;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // 75 pot, all three call: seat1 Ah 2c 7s 8d (A-2-3-4-6 low),
        // seat2 9d Tc Jh Qc (junk: Q-high, no low), seat0 Ks Kd Qs Qd
        // (trip kings high, no low). Board 3h 4d 6s 9c Kh.
        // High takes 38, low takes 37.
        t.start_hand_from_deck(cards({"Ah", "9d", "Ks", "2c", "Tc", "Kd",
                                     "7s", "Jh", "Qs", "8d", "Qc", "Qd",
                                     "3h", "4d", "6s", "9c", "Kh"}));
        call(t, 0);
        chk(t, 1);
        chk(t, 2);
        check_down_streets(t);
        auto payouts = t.settle();
        check(t.went_to_showdown(), "hilo split is a showdown");
        int p0 = -1, p1 = -1;
        for (const auto& p : payouts) {
            if (p.seat == 0) p0 = p.amount;
            if (p.seat == 1) p1 = p.amount;
        }
        check(p0 == 38 && p1 == 37, "odd chip to high");
        check(t.stack(0) == 10013 && t.stack(1) == 10012 &&
                  t.stack(2) == 9975,
              "hilo odd stacks");
        check(total_chips(t) == 30000, "hilo odd chips conserved");
    }

    // A snapshot taken with a pending kill restores without the kill.
    {
        GameConfig c;
        c.num_players = 2;
        c.kill = true;
        Table t(c);
        // Hand 1: shove preflop so the pot clears 1000 (10x 100).
        t.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                     "Jh", "9c", "3d"}));
        raise_to(t, 0, 5000);
        call(t, 1);
        check_down_streets(t);
        t.settle();
        const auto snap = t.snapshot();
        t.restore(snap);
        // Next hand plays normal blinds: SB 50, BB 100.
        t.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                     "Jh", "9c", "3d"}));
        check(t.committed(0) + t.committed(1) == 150, "restore clears kill");
        check(t.current_bet() == 100, "restored BB is 100");
    }

    // Uncontested top band with tied contributors refunds every owner:
    // A and B fold on the flop holding 200 each while live C and D sit
    // all-in at 150, so the top 50x2 was never matched by a live hand.
    // (Regression: the excess used to vanish whenever no single leader
    // held the top level.)
    {
        GameConfig c;
        c.num_players = 4;
        Table t(c);
        t.set_stack(0, 10000);
        t.set_stack(1, 10000);
        t.set_stack(2, 150);
        t.set_stack(3, 150);
        // 13 cards: 4x2 hole + 5 board. Winners don't matter here.
        t.start_hand_from_deck(cards({"As", "Ks", "Qh", "Qd", "Jc", "Jd",
                                      "Tc", "9c", "7c", "7d", "7h", "2c",
                                      "2d"}));
        call(t, 3);          // D calls the 100 BB.
        raise_to(t, 0, 200);  // A raises to 200 total.
        call(t, 1);          // B calls 200 total.
        call(t, 2);          // C calls 50 short, all-in at 150.
        call(t, 3);          // D calls 50 short, all-in at 150.
        check(t.acting() == -1, "short all-ins close the round");
        t.deal_next_street();
        fold(t, t.acting());
        fold(t, t.acting());
        while (!t.hand_complete()) {
            check(t.acting() == -1, "only all-ins remain");
            t.deal_next_street();
        }
        check(t.committed(0) == 200 && t.committed(1) == 200 &&
                  t.committed(2) == 150 && t.committed(3) == 150,
              "tied top above the live cap");
        check(t.went_to_showdown() == false, "no showdown before settle");
        const int before = total_chips(t);
        t.settle();
        check(t.went_to_showdown(), "two live all-ins reach showdown");
        check(total_chips(t) == before, "tied top band is refunded");
        check(t.stack(0) == 9850 && t.stack(1) == 9850,
              "each top owner gets 50 back");
    }

    // A lone live leader's excess is a refund, not a win: A puts in 300
    // alone (B folds preflop at 50, C calls 50 short all-in at 150), so
    // the top 150 was never matched by anyone. Payouts must total the
    // contested 350, never the full 500 pot.
    {
        GameConfig c;
        c.num_players = 3;
        Table t(c);
        t.set_stack(0, 10000);
        t.set_stack(1, 10000);
        t.set_stack(2, 150);
        // 11 cards: 3x2 hole + 5 board. Winners don't matter here.
        t.start_hand_from_deck(cards({"As", "Ks", "Qh", "Qd", "Jc", "Jd",
                                      "Tc", "9c", "7c", "7d", "7h"}));
        raise_to(t, 0, 300);  // A raises to 300 total.
        fold(t, 1);           // B folds the SB (50 committed).
        call(t, 2);           // C calls 50 short, all-in at 150.
        check(t.acting() == -1, "short all-in closes the round");
        check(t.committed(0) == 300 && t.committed(1) == 50 &&
                  t.committed(2) == 150,
              "lone leader above the live cap");
        check_down_streets(t);
        const int pot = t.pot_total();
        check(pot == 500, "pot is 500 pre-settle");
        const auto payouts = t.settle();
        check(t.went_to_showdown(), "A and C reach showdown");
        int paid = 0;
        for (const auto& p : payouts) paid += p.amount;
        check(paid == 350, "uncalled 150 is refunded, not awarded");
        check(total_chips(t) == 10000 + 10000 + 150,
              "chips conserved with lone-leader refund");
    }

    std::cout << "test_table ok\n";
    return 0;
}
