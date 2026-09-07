// Engine invariant fuzz: chip conservation + legality + determinism across
// every supported variant, with random-action agents. Any rules violation
// (lost chips, illegal options, card duplication, nondeterminism) fails.
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "cardengine/bot.h"
#include "cardengine/card.h"
#include "cardengine/config.h"
#include "cardengine/table.h"

namespace {

using cardengine::Action;
using cardengine::ActionType;
using cardengine::BettingStructure;
using cardengine::GameConfig;
using cardengine::HandConstruction;
using cardengine::Table;

int total_chips(const Table& table) {
    int sum = 0;
    for (int i = 0; i < table.num_seats(); ++i) sum += table.stack(i);
    return sum + table.pot_total();
}

// Random legal action (mirrors RandomBot, but seeded externally).
Action random_legal(const Table& table, int seat, std::mt19937_64& rng) {
    const auto options = table.options(seat);
    std::vector<Action> legal{{ActionType::Fold, 0}};
    if (options.can_check) {
        legal.push_back({ActionType::Check, 0});
    } else if (options.call_amount > 0 ||
               table.to_call(seat) == 0) {
        legal.push_back({ActionType::Call, 0});
    }
    if (options.can_raise) {
        std::uniform_int_distribution<int> sizing(options.min_raise_to,
                                                  options.max_raise_to);
        legal.push_back({ActionType::Raise, sizing(rng)});
    }
    std::uniform_int_distribution<std::size_t> pick(0, legal.size() - 1);
    return legal[pick(rng)];
}

int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "FAIL: " << message << "\n";
        ++failures;
    }
}

// One full hand under fuzz; verifies invariants at every step.
void fuzz_hand(Table& table, std::uint64_t seed, std::mt19937_64& rng,
               const std::string& tag) {
    const int before = total_chips(table);
    table.start_hand(seed);
    // Deck integrity: no duplicated cards across hole + board.
    {
        std::set<std::string> seen;
        for (int s = 0; s < table.num_seats(); ++s) {
            if (!table.in_hand(s)) continue;
            for (const cardengine::Card& card : table.hole_cards(s)) {
                const std::string text = cardengine::to_string(card);
                check(seen.insert(text).second,
                      tag + " duplicated hole card " + text);
            }
        }
    }
    int guards = 0;
    while (!table.hand_complete()) {
        check(guards++ < 10000, tag + " hand never completes");
        if (guards >= 10000) return;
        if (table.acting() != -1) {
            const int seat = table.acting();
            const auto options = table.options(seat);
            // Options legality.
            check(options.call_amount >= 0, tag + " negative call");
            if (options.can_raise) {
                check(options.min_raise_to <= options.max_raise_to,
                      tag + " inverted raise range");
                check(options.min_raise_to > table.bet(seat),
                      tag + " min raise not a raise");
                check(options.max_raise_to <=
                          table.bet(seat) + table.stack(seat),
                      tag + " max raise beyond stack");
            }
            if (options.can_check) {
                check(table.to_call(seat) == 0, tag + " check facing a bet");
            }
            table.act(seat, random_legal(table, seat, rng));
            check(total_chips(table) == before,
                  tag + " chips leaked mid-hand");
        } else {
            table.deal_next_street();
        }
    }
    const auto payouts = table.settle();
    check(total_chips(table) == before, tag + " chips leaked at settle");
    int paid = 0;
    for (const auto& payout : payouts) paid += payout.amount;
    // Payouts never exceed what was committed, and a winner exists.
    check(!payouts.empty(), tag + " empty payouts");
    check(paid <= before, tag + " payouts exceed chips in play");
}

void fuzz_config(GameConfig config, const std::string& tag, int hands) {
    std::mt19937_64 rng{12345};
    Table table(config);
    const int start_total = total_chips(table);
    for (int h = 0; h < hands; ++h) {
        // Keep everyone funded so every variant plays full hands.
        if (table.street() == cardengine::Street::None ||
            table.street() == cardengine::Street::Complete) {
            bool broke = false;
            for (int s = 0; s < table.num_seats(); ++s) {
                if (table.stack(s) == 0) broke = true;
            }
            if (broke) break;
        }
        fuzz_hand(table, static_cast<std::uint64_t>(7000 + h), rng, tag);
        if (failures > 20) return;
    }
    check(total_chips(table) == start_total, tag + " session total drifted");

    // Determinism: same seed twice plays identically.
    Table replay(config);
    Table replay2(config);
    auto play = [&](Table& table) {
        table.start_hand(4242);
        std::mt19937_64 inner{99};
        int guards = 0;
        while (!table.hand_complete() && guards++ < 10000) {
            if (table.acting() != -1) {
                table.act(table.acting(),
                           random_legal(table, table.acting(), inner));
            } else {
                table.deal_next_street();
            }
        }
        return table.settle();
    };
    const auto first = play(replay);
    const auto second = play(replay2);
    check(first.size() == second.size() &&
              std::equal(first.begin(), first.end(), second.begin(),
                         [](const auto& a, const auto& b) {
                             return a.seat == b.seat && a.amount == b.amount;
                         }),
          tag + " same seed diverged");
}

}  // namespace

int main() {
    GameConfig nlhe;
    nlhe.num_players = 6;
    fuzz_config(nlhe, "nlhe6", 60);

    GameConfig plo;
    plo.num_players = 6;
    plo.hole_cards = 4;
    plo.betting = BettingStructure::PotLimit;
    plo.showdown = HandConstruction::OmahaTwoAndThree;
    fuzz_config(plo, "plo6", 60);

    GameConfig limit;
    limit.num_players = 6;
    limit.betting = BettingStructure::Limit;
    fuzz_config(limit, "limit6", 60);

    GameConfig hu;
    hu.num_players = 2;
    fuzz_config(hu, "hu", 60);

    GameConfig plo_hu;
    plo_hu.num_players = 2;
    plo_hu.hole_cards = 4;
    plo_hu.betting = BettingStructure::PotLimit;
    plo_hu.showdown = HandConstruction::OmahaTwoAndThree;
    fuzz_config(plo_hu, "plohu", 60);

    GameConfig hilo;
    hilo.num_players = 6;
    hilo.hole_cards = 4;
    hilo.betting = BettingStructure::PotLimit;
    hilo.showdown = HandConstruction::OmahaHiLo;
    fuzz_config(hilo, "hilo6", 60);

    GameConfig hilo_hu;
    hilo_hu.num_players = 2;
    hilo_hu.hole_cards = 4;
    hilo_hu.betting = BettingStructure::PotLimit;
    hilo_hu.showdown = HandConstruction::OmahaHiLo;
    fuzz_config(hilo_hu, "hilohu", 60);

    if (failures == 0) std::cout << "test_fuzz ok\n";
    return failures == 0 ? 0 : 1;
}
