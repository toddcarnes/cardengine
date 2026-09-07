// Seven-card stud, wide tables: the 8-max file-game shape and the
// short-shoe community river. Split from test_stud (one Table per block
// still applies — this file owns its Tables, test_stud owns its own).
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

void drive_to_showdown(Table& t) {
    int guards = 0;
    while (!t.hand_complete() && guards++ < 100) {
        if (t.acting() != -1) {
            t.act(t.acting(), {ActionType::Call, 0});
        } else {
            t.deal_next_street();
        }
    }
    check(t.hand_complete(), "hand completes");
}

}  // namespace

int main() {
    using namespace cardengine;

    // 8-handed: the file-game shape loads, deals, and plays (folds shrink
    // demand; the river goes community only if the shoe actually runs dry).
    {
        GameConfig c8 = stud_config(8);
        Table t8(c8);
        t8.start_hand(777);
        check(t8.street() == Street::Third, "8-max opens");
        check(t8.bring_in_seat() >= 0, "8-max bring-in found");
        // Everyone but two folds on third; the rest of the hand must still
        // deal and settle without running dry.
        int guards = 0;
        while (t8.acting() != -1 && guards++ < 20) {
            const int seat = t8.acting();
            if (seat == t8.bring_in_seat() ||
                seat == (t8.bring_in_seat() + 2) % 8) {
                t8.act(seat, {ActionType::Call, 0});
            } else {
                t8.act(seat, {ActionType::Fold, 0});
            }
        }
        drive_to_showdown(t8);
        t8.settle();
        check(t8.went_to_showdown(), "8-max showdown");
    }

    // Community river: force the shoe dry with a short testing shoe —
    // six live seats on seventh street share one up card.
    {
        GameConfig c7 = stud_config(7);
        Table t7(c7);
        // Third street only (21 cards); the shoe-backed deal then runs on
        // exactly what each street needs: one fourth-street fold leaves 6
        // live, 6 ups per street, then 1 card left on seventh -> community.
        t7.start_hand_from_deck(cards(
            {"Ac", "2c", "3c", "4c", "5c", "6c", "7c",
             "Ad", "2d", "3d", "4d", "5d", "6d", "7d",
             "Ah", "2h", "3h", "4h", "5h", "6h", "7h",
             // Fourth: 7 ups.
             "As", "8c", "8d", "8h", "8s", "9c", "9d",
             // Fifth: 6 ups (seat 6 folds on fourth).
             "9h", "9s", "Tc", "Td", "Th", "Ts",
             // Sixth: 6 ups.
             "Jc", "Jd", "Jh", "Js", "Qc", "Qd",
             // Seventh: only 1 card left for 6 live seats -> community.
             "Qh"}));
        drive_betting(t7);
        t7.deal_next_street();  // Fourth.
        // Fold seat 6 on fourth to shape the shoe math.
        {
            int folded = 0;
            int guards = 0;
            while (t7.acting() != -1 && guards++ < 20) {
                const int seat = t7.acting();
                if (seat == 6 && folded == 0) {
                    t7.act(seat, {ActionType::Fold, 0});
                    ++folded;
                } else {
                    t7.act(seat, {ActionType::Call, 0});
                }
            }
        }
        t7.deal_next_street();  // Fifth.
        drive_betting(t7);
        t7.deal_next_street();  // Sixth.
        drive_betting(t7);
        t7.deal_next_street();  // Seventh: shoe has 1 card, 6 live.
        check(t7.street() == Street::Seventh, "seventh deals");
        check(t7.community().size() == 1, "community river");
        check(to_string(t7.community()[0]) == "Qh", "shared queen");
        bool saw_community = false;
        for (const Event& e : t7.events()) {
            if (const auto* s = std::get_if<StudDealtEvent>(&e)) {
                if (s->community) {
                    saw_community = true;
                    check(format_event(e) == "stud seventh community Qh",
                          "community text");
                }
            }
        }
        check(saw_community, "community logged");
        drive_betting(t7);
        t7.settle();
        check(t7.went_to_showdown(), "community showdown");
    }

    std::cout << "test_stud8 ok\n";
    return 0;
}
