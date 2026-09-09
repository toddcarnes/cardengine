// Limit and pot-limit betting: fixed sizes, caps, pot-sized max.
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using cardengine::ActionOptions;
using cardengine::ActionType;
using cardengine::BettingStructure;
using cardengine::GameConfig;
using cardengine::Table;
using testutil::cards;
using testutil::check;
using testutil::expect_throws;

// 11-card shoe; contents never reach showdown in these tests.
std::vector<cardengine::Card> any_shoe() {
    return cards({"2c", "3d", "4h", "5s", "6c", "7d", "8h",
                  "9s", "Tc", "Jd", "Qh"});
}

void chk(Table& t, int s) { t.act(s, {ActionType::Check, 0}); }
void call(Table& t, int s) { t.act(s, {ActionType::Call, 0}); }
void raise_to(Table& t, int s, int amount) {
    t.act(s, {ActionType::Raise, amount});
}

GameConfig limit_game() {
    GameConfig c;
    c.num_players = 3;
    c.betting = cardengine::BettingStructure::Limit;
    return c;
}

GameConfig plo_game() {
    GameConfig c;
    c.num_players = 3;
    c.betting = cardengine::BettingStructure::PotLimit;
    return c;
}

}  // namespace

int main() {
    using namespace cardengine;

    // Limit: fixed sizes, exactly one legal raise amount, cap of 4.
    {
        Table t(limit_game());
        t.start_hand_from_deck(any_shoe());
        // Preflop unit is 100: open to 200, then 300, 400, 500, then capped.
        auto opts0 = t.options(0);
        check(opts0.can_raise && opts0.min_raise_to == 200 &&
                  opts0.max_raise_to == 200,
              "limit open is exactly 200");
        expect_throws<std::invalid_argument>([&] { raise_to(t, 0, 250); },
                                "limit off-size raise");
        raise_to(t, 0, 200);
        check(t.options(1).min_raise_to == 300, "second raise 300");
        raise_to(t, 1, 300);
        check(t.options(2).min_raise_to == 400, "third raise 400");
        raise_to(t, 2, 400);
        check(t.options(0).min_raise_to == 500, "fourth raise 500");
        raise_to(t, 0, 500);
        const ActionOptions capped = t.options(1);
        check(!capped.can_raise && capped.call_amount == 200, "cap reached");
        expect_throws<std::invalid_argument>([&] { raise_to(t, 1, 600); },
                                "raise past cap");
        call(t, 1);
        call(t, 2);
        check(t.acting() == -1, "round closes at cap");
        check(t.pot_total() == 1500, "capped pot");
        t.deal_next_street();
        const ActionOptions flop = t.options(1);
        check(flop.can_check && flop.min_raise_to == 100 &&
                  flop.max_raise_to == 100,
              "flop unit is 100");
    }

    // Limit turn/river unit doubles to 200.
    {
        Table t(limit_game());
        t.start_hand_from_deck(any_shoe());
        call(t, 0);
        call(t, 1);
        chk(t, 2);
        t.deal_next_street();
        chk(t, 1);
        chk(t, 2);
        chk(t, 0);
        t.deal_next_street();
        const ActionOptions turn = t.options(1);
        check(turn.min_raise_to == 200 && turn.max_raise_to == 200,
              "turn unit is 200");
    }

    // Limit short all-in: allowed, but consumes no cap and reopens nothing.
    {
        Table t(limit_game());
        t.set_stack(0, 280);
        t.start_hand_from_deck(any_shoe());
        call(t, 0);  // 100, stack 180.
        call(t, 1);
        chk(t, 2);
        t.deal_next_street();
        raise_to(t, 1, 100);
        call(t, 2);
        raise_to(t, 0, 180);  // All in, 80 short of a full raise.
        check(!t.options(1).can_raise, "short all-in reopens nothing");
        call(t, 1);
        call(t, 2);
        check(t.acting() == -1, "round closes");
    }

    // Pot-limit: max is the pot-sized raise, min is standard.
    {
        Table t(plo_game());
        t.start_hand_from_deck(any_shoe());
        // Pot 150, UTG to_call 100: call (pot 250) + raise 250 = 350 total.
        const ActionOptions u = t.options(0);
        check(u.can_raise && u.min_raise_to == 200 && u.max_raise_to == 350,
              "plo max 350");
        expect_throws<std::invalid_argument>([&] { raise_to(t, 0, 400); },
                                "plo over max");
        raise_to(t, 0, 350);
        call(t, 1);
        call(t, 2);
        // Pot is now 350+350+350 = 1050; flop opener faces empty bet:
        // max = pot = 1050, min = 100.
        t.deal_next_street();
        const ActionOptions f = t.options(1);
        check(f.min_raise_to == 100 && f.max_raise_to == 1050, "flop pot bet");
        raise_to(t, 1, 1050);
        // Next player: to_call 1050, pot 2100: max = 0 + 2100 + 2100 = 4200.
        const ActionOptions f2 = t.options(2);
        check(f2.max_raise_to == 4200, "pot-sized reraise max");
    }

    // Both structures conserve chips across check/call hands.
    for (BettingStructure structure :
         {BettingStructure::Limit, BettingStructure::PotLimit}) {
        GameConfig c;
        c.num_players = 3;
        c.betting = structure;
        Table t(c);
        int initial = 0;
        for (int i = 0; i < t.num_seats(); ++i) initial += t.stack(i);
        for (std::uint64_t seed : {11, 12}) {
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
            int after = 0;
            for (int i = 0; i < t.num_seats(); ++i) after += t.stack(i);
            check(initial == after, "structured chips conserved");
        }
    }

    std::cout << "test_betting ok\n";
    return 0;
}
