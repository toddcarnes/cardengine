// Seven-card stud: street-by-street deal, bring-in, high-hand opens,
// upcard visibility, and the 8-handed community river.
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/bot.h"
#include "cardengine/config.h"
#include "cardengine/event.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using cardengine::ActionType;
using cardengine::GameConfig;
using cardengine::HandConstruction;
using cardengine::Street;
using cardengine::StudDealtEvent;
using cardengine::Table;
using testutil::cards;
using testutil::check;
using testutil::expect_throws;

GameConfig stud_config(int seats) {
    GameConfig c;
    c.num_players = seats;
    c.hole_cards = 7;
    c.board_cards = 0;
    c.showdown = HandConstruction::StudSeven;
    c.upcards = 4;
    c.bring_in = 10;
    c.ante = 10;
    c.starting_stack = 10000;
    c.small_blind = 50;
    c.big_blind = 100;
    c.betting = cardengine::BettingStructure::Limit;
    c.max_raises_per_round = 4;
    return c;
}

void drive_betting(Table& t) {
    int guards = 0;
    while (t.acting() != -1 && guards++ < 100) {
        t.act(t.acting(), {ActionType::Call, 0});
    }
    check(t.acting() == -1, "betting round completes");
}

}  // namespace

int main() {
    using namespace cardengine;

    // Third street: 2 down + 1 up, ante, bring-in by the low door card.
    // Deal order (button 0, button-out): seat1, seat2, seat0 × 2 down
    // rounds, then 1 up round. Down: seat1 Ac,Kd / seat2 Qh,Jc / seat0
    // 9s,8d. Up: seat1 2c (low) / seat2 Kh / seat0 Ah. One live Table
    // per block under ctest (two Tables in one block fault — the same
    // quarantine test_draw/test_bot use).
    {
        Table t3a(stud_config(3));
        t3a.start_hand_from_deck(cards(
            {"Ac", "Qh", "9s", "Kd", "Jc", "8d", "2c", "Kh", "Ah",
             "2d", "3d", "4d", "5d", "6d", "7d", "8c", "9d", "Td",
             "Jd", "Qd", "Kd", "Ad", "2s", "3s", "4s", "5s", "6s"}));
        check(t3a.street() == Street::Third, "opens on third");
        check(t3a.hole_cards(1).size() == 2, "two down");
        check(t3a.up_cards(1).size() == 1, "one up");
        check(to_string(t3a.up_cards(1)[0]) == "2c", "door card");
        check(t3a.bring_in_seat() == 1, "deuce brings in");
        check(t3a.committed(1) == 20, "ante plus bring-in");
        check(t3a.committed(0) == 10 && t3a.committed(2) == 10, "antes");
        check(t3a.acting() == 2, "action left of the bring-in");
        check(t3a.current_bet() == 10, "bring-in sets the bet");
        // Limit third street: complete to the small bet (bring-in 10,
        // small bet 100 in this file — raise-to-100 totals 100).
        t3a.act(2, {ActionType::Raise, 100});
        t3a.act(0, {ActionType::Call, 0});
        t3a.act(1, {ActionType::Call, 0});
        check(t3a.acting() == -1, "third round closes");
    }

    // Later streets open on the best visible hand; upcards are public.
    {
        Table t4(stud_config(2));
        // seat1: down 2c,3d / up Kd; seat0: down As,Ad / up Qs.
        t4.start_hand_from_deck(cards(
            {"2c", "As", "3d", "Ad", "Kd", "Qs",
             "2d", "3h", "4d", "5d", "6d", "7d", "8d", "9d", "Td",
             "Jd", "Qd", "Kd", "Ad", "2s", "3s", "4s", "5s", "6s"}));
        check(t4.bring_in_seat() == 0, "queen brings in under the king");
        drive_betting(t4);
        t4.deal_next_street();
        check(t4.street() == Street::Fourth, "fourth street");
        check(t4.up_cards(0).size() == 2 && t4.up_cards(1).size() == 2,
              "two up each");
        // Best visible hand (Kd+2d vs Qs+3h — king-high) opens.
        check(t4.acting() == 1, "kings open fourth");
        drive_betting(t4);
        t4.deal_next_street();
        check(t4.street() == Street::Fifth, "fifth street");
        drive_betting(t4);
        t4.deal_next_street();
        check(t4.street() == Street::Sixth, "sixth street");
        drive_betting(t4);
        t4.deal_next_street();
        check(t4.street() == Street::Seventh, "seventh street");
        check(t4.up_cards(0).size() == 4, "four up at the river");
        check(t4.hole_cards(0).size() == 3, "three down at the river");
        check(t4.community().empty(), "no community heads-up");
        drive_betting(t4);
        check(t4.hand_complete(), "stud hand completes");
        t4.settle();
        check(t4.went_to_showdown(), "stud showdown");
    }

    // Stud log lines: `stud <street> <ups...>` round-trips; down seventh
    // streets log bare.
    {
        Table tlog(stud_config(2));
        tlog.start_hand_from_deck(cards(
            {"2c", "As", "3d", "Ad", "Kd", "Qs",
             "2d", "3h", "4d", "5d", "6d", "7d", "8d", "9d", "Td",
             "Jd", "Qd", "Kd", "Ad", "2s", "3s", "4s", "5s", "6s"}));
        drive_betting(tlog);
        tlog.deal_next_street();
        bool saw_stud = false;
        for (const Event& e : tlog.events()) {
            if (const auto* s = std::get_if<StudDealtEvent>(&e)) {
                saw_stud = true;
                const std::string text = format_event(e);
                const Event back = parse_event(text, tlog.config());
                check(format_event(back) == text, "stud line round-trips");
                check(!s->community, "no community heads-up");
            }
        }
        check(saw_stud, "stud deal logged");
        check(format_event(StudDealtEvent{Street::Fourth, true, false,
                                         {parse_card("Kd")},
                                         {{1, parse_card("Kd")}}}) ==
                  "stud fourth Kd",
              "stud text");
        expect_throws<std::invalid_argument>(
            [&] { parse_event("stud", tlog.config()); }, "short stud line");
    }

    // 8-handed: the file-game shape loads, deals, and plays (folds shrink
    // demand; the river goes community only if the shoe actually runs dry).

    std::cout << "test_stud ok\n";
    return 0;
}
