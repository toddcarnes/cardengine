// M3 variants: antes, nonstandard hole/board counts driven by GameConfig.
#include <iostream>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/table.h"

namespace {

using cardengine::Action;
using cardengine::ActionType;
using cardengine::GameConfig;
using cardengine::Table;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::vector<cardengine::Card> shoe(std::initializer_list<const char*> texts) {
    std::vector<cardengine::Card> out;
    for (const char* t : texts) out.push_back(cardengine::parse_card(t));
    return out;
}

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
        t.start_hand_from_deck(shoe({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                                     "9s", "Tc", "Jd", "Qh"}));
        check(t.committed(0) == 10, "UTG ante only");
        check(t.committed(1) == 60, "SB ante plus blind");
        check(t.committed(2) == 110, "BB ante plus blind");
        check(t.pot_total() == 180, "antes in the pot");
    }

    // A short stack antes itself all in and can still win the hand.
    {
        GameConfig c;
        c.num_players = 2;
        c.ante = 10;
        Table t(c);
        t.set_stack(0, 10);
        // seat1: 7c 2d; seat0: As Ad. Board bricks for seat 1.
        t.start_hand_from_deck(shoe({"7c", "As", "2d", "Ad", "Ks", "Qh", "Jh",
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
        t.start_hand_from_deck(shoe({"7c", "As", "7d", "Ks", "7h", "Qs", "Js",
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
        t.start_hand_from_deck(shoe({"2c", "As", "3d", "Ks", "4h", "Qs", "5s",
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
        t.start_hand_from_deck(shoe({"Qh", "As", "Qd", "Ks", "4c", "2c",
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

    std::cout << "test_variants ok\n";
    return 0;
}
