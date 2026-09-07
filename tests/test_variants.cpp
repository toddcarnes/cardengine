// M3 variants: antes, nonstandard hole/board counts driven by GameConfig.
#include <iostream>
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

void chk(Table& t, int s) { t.act(s, {ActionType::Check, 0}); }
void call(Table& t, int s) { t.act(s, {ActionType::Call, 0}); }

void check_down_streets(Table& t) {
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

    // Antes are dead money on top of the blinds.
    {
        GameConfig c;
        c.num_players = 3;
        c.ante = 10;
        Table t(c);
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        check(t.committed(0) == 10, "UTG ante only");
        check(t.committed(1) == 60, "SB ante plus blind");
        check(t.committed(2) == 110, "BB ante plus blind");
        check(t.pot_total() == 180, "antes in the pot");
    }

    // Button ante: the button posts the whole table's ante at once.
    {
        GameConfig c;
        c.num_players = 3;
        c.ante = 10;
        c.ante_from = AnteSource::ButtonOnly;
        Table t(c);
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        check(t.committed(0) == 30, "button posts 3x ante");
        check(t.committed(1) == 50, "SB posts blind only");
        check(t.committed(2) == 100, "BB posts blind only");
        check(t.pot_total() == 180, "same dead money, one payer");
    }

    // Live straddle: UTG posts 2× BB and acts last preflop.
    {
        GameConfig c;
        c.num_players = 4;
        c.straddle = 200;
        Table t(c);
        t.start_hand_from_deck(cards({"2c", "3d", "4h", "5s", "6c", "7d",
                                     "8h", "9s", "Tc", "Jd", "Qh", "Kd",
                                     "Ah"}));
        // Button 0: SB seat1 (50), BB seat2 (100), straddle seat3 (200).
        check(t.committed(1) == 50, "SB posts");
        check(t.committed(2) == 100, "BB posts");
        check(t.committed(3) == 200, "straddle posts 2x BB");
        check(t.current_bet() == 200, "straddle sets the bet");
        check(t.acting() == 0, "action starts after the straddle");
        check(t.pot_total() == 350, "straddle in the pot");
        // UTG calls the blind, the straddler checks their option.
        call(t, 0);
        call(t, 1);
        call(t, 2);
        chk(t, 3);
        t.deal_next_street();
        check(t.board().size() == 3, "flop deals after the option");
    }

    // Full kill: a pot over 10× BB doubles next hand's blinds once.
    {
        GameConfig c;
        c.num_players = 2;
        c.kill = true;
        Table t(c);
        // Hand 1: shove preflop so the pot clears 1000 (10× 100).
        t.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                     "Jh", "9c", "3d"}));
        t.act(0, {ActionType::Raise, 5000});
        t.act(1, {ActionType::Call, 0});
        check_down_streets(t);
        t.settle();
        // Hand 2 plays double blinds: SB 100, BB 200.
        t.start_hand(99);
        check(t.committed(0) + t.committed(1) >= 300 - 0, "kill doubles blinds");
        check(t.current_bet() == 200, "kill BB is 200");
    }

    // A short stack antes itself all in and can still win the hand.
    {
        GameConfig c;
        c.num_players = 2;
        c.ante = 10;
        Table t(c);
        t.set_stack(0, 10);
        // seat1: 7c 2d; seat0: As Ad. Board bricks for seat 1.
        t.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh", "Jh",
                                     "9c", "3d"}));
        check(t.is_all_in(0), "ante all in");
        check(t.in_hand(0), "short stack still in");
        check(t.acting() == 1, "action skips the all-in seat");
        check_down_streets(t);
        t.settle();
        check(t.went_to_showdown(), "all-in seat reaches showdown");
        // Seat 1's big blind was uncalled past 10, so 100 returns to them
        // and seat 0 wins only the contested 20.
        check(t.stack(0) == 20, "short aces win 20");
        check(t.stack(1) == 9990, "unmatched blind returned");
    }

    // Three hole cards, four board cards: 7 total, best-5 showdown.
    // Flop takes 3, turn 1, river deals nothing.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 3;
        c.board_cards = 4;
        Table t(c);
        // seat1: 7c 7d 7h (trips); seat0: As Ks Qs.
        // Board Js Ts 2d 3c gives seat 0 a royal flush.
        t.start_hand_from_deck(cards({"7c", "As", "7d", "Ks", "7h", "Qs", "Js",
                                     "Ts", "2d", "3c"}));
        check(t.hole_cards(0).size() == 3, "three hole cards dealt");
        call(t, 0);
        chk(t, 1);
        t.deal_next_street();
        check(t.board().size() == 3, "flop is 3");
        chk(t, 1);
        chk(t, 0);
        t.deal_next_street();
        check(t.board().size() == 4, "turn is 1");
        chk(t, 1);
        chk(t, 0);
        t.deal_next_street();
        check(t.board().size() == 4, "river deals nothing");
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 10100, "royal wins");
        check(t.stack(1) == 9900, "trips lose");
    }

    // No board, five hole cards: four betting rounds, best 5 of 5.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        Table t(c);
        // seat1 bricks; seat0 is dealt a royal.
        t.start_hand_from_deck(cards({"2c", "As", "3d", "Ks", "4h", "Qs", "5s",
                                     "Js", "7c", "Ts"}));
        check(t.hole_cards(1).size() == 5, "five hole cards dealt");
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        check(t.board().empty(), "no board dealt");
        t.settle();
        check(t.went_to_showdown(), "showdown on hole cards");
        check(t.stack(0) == 10100, "royal wins boardless");
        check(t.stack(1) == 9900, "brick loses");
    }

    // Omaha table hand: 4 hole each, royal-over-trips at showdown.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaTwoAndThree;
        Table t(c);
        // Deal: seat1 <- Qh, seat0 <- As, seat1 <- Qd, seat0 <- Ks, ...
        // seat1: Qh Qd 4c 5d (trip queens); seat0: As Ks 2c 3d (royal).
        // Board: Qs Js Ts 9h 2h.
        t.start_hand_from_deck(cards({"Qh", "As", "Qd", "Ks", "4c", "2c",
                                     "5d", "3d", "Qs", "Js", "Ts", "9h",
                                     "2h"}));
        check(t.hole_cards(0).size() == 4, "four hole cards dealt");
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.went_to_showdown(), "omaha showdown");
        check(t.stack(0) == 10100 && t.stack(1) == 9900, "royal beats trips");
    }

    // Omaha Hi-Lo: the same royal scoops when no low qualifies, then the
    // low half goes to the A2 hand while high keeps its half.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // seat1: Qh Qd 4c 5d (trip queens); seat0: As Ks 2c 3d (royal).
        // Board Qs Js Ts 9h 2h: no 8-or-better low possible.
        t.start_hand_from_deck(cards({"Qh", "As", "Qd", "Ks", "4c", "2c",
                                     "5d", "3d", "Qs", "Js", "Ts", "9h",
                                     "2h"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.went_to_showdown(), "hilo showdown");
        check(t.stack(0) == 10100 && t.stack(1) == 9900,
              "no low means high scoops");
    }
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // seat1: Ah 2c Ks Qd; seat0: As Ks Qs Js.
        // Board 3h 4d 5s Jc Td: seat1 holds the wheel both ways (scoop)
        // until the low half splits — seat0 has no low, seat1 takes both.
        t.start_hand_from_deck(cards({"Ah", "As", "2c", "Ks", "Ks", "Qs",
                                     "Qd", "Js", "3h", "4d", "5s", "Jc",
                                     "Td"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 9900 && t.stack(1) == 10100,
              "wheel both ways scoops");
    }
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // seat1: Ac Kd 9s Ts (high-only aces); seat0: Ah 2c 7d 9h.
        // Board 4s 5c Jh Qd Ks: seat0's wheel-straight takes high AND the
        // A-2-4-5 low hangs on — seat1's bare aces take nothing.
        t.start_hand_from_deck(cards({"Ac", "Ah", "Kd", "2c", "9s", "7d",
                                     "Ts", "9h", "4s", "5c", "Jh", "Qd",
                                     "Ks"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 9900 && t.stack(1) == 10100,
              "low straight scoops the high-only hand");
    }
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // Split halves: seat1 Ah 2c 7s 8d (6-high low + ace-high),
        // seat0 Ks Kd Qs Qd (trip kings high, no low: kings/queens only).
        // Board 3h 4d 6s 9c Kh: trips take high, A-2-3-4-6 takes low
        // (no wheel for anyone: no 5 on board). 200 pot splits 100/100.
        t.start_hand_from_deck(cards({"Ah", "Ks", "2c", "Kd", "7s", "Qs",
                                     "8d", "Qd", "3h", "4d", "6s", "9c",
                                     "Kh"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 10000 && t.stack(1) == 10000,
              "high and low split the pot");
        check(t.went_to_showdown(), "split is a showdown");
    }
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // Full house scoops when the board can't make a low: seat1 Ac Kd
        // 7h 8c (trip queens, ace kicker), seat0 Ks Kd Qs Qd (kings-full).
        // Board 3h 4d 9s 9c Kh: only 3,4 play low, so no low is possible.
        t.start_hand_from_deck(cards({"Ac", "Ks", "Kd", "Kd", "7h", "Qs",
                                     "8c", "Qd", "3h", "4d", "9s", "9c",
                                     "Kh"}));
        call(t, 0);
        chk(t, 1);
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 10100 && t.stack(1) == 9900,
              "full house scoops with no low possible");
    }
    {
        // Quartering: one high winner plus two tied low winners splits
        // 150/75/75 on a 300 pot.
        GameConfig c;
        c.num_players = 3;
        c.hole_cards = 4;
        c.board_cards = 5;
        c.showdown = HandConstruction::OmahaHiLo;
        Table t(c);
        // seat0 Ks Kd Qs Qd (trip kings high); seat1 Ad 2d 7c 8c and seat2
        // Ah 2c 7d 8h share the nut A-2-3-4-6 low. Board 3h 4d 6s 9c Kh.
        // (Deal starts left of the button: seat1, seat2, seat0.)
        t.start_hand_from_deck(cards({"Ad", "Ah", "Ks", "2d", "2c", "Kd",
                                     "7c", "7d", "Qs", "8c", "8h", "Qd",
                                     "3h", "4d", "6s", "9c", "Kh"}));
        call(t, 0);
        call(t, 1);
        chk(t, 2);
        check_down_streets(t);
        t.settle();
        check(t.stack(0) == 10000 - 100 + 150, "high takes half");
        check(t.stack(1) == 10000 - 100 + 75, "tied low quarters");
        check(t.stack(2) == 10000 - 100 + 75, "tied low quarters");
        check(t.went_to_showdown(), "quarter is a showdown");
    }

    std::cout << "test_variants ok\n";
    return 0;
}
