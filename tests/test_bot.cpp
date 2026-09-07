// Bots: file parsing, decision sanity, and heuristic-vs-random edge.
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "cardengine/bot.h"
#include "helpers.h"

namespace {

using cardengine::ActionType;
using testutil::cards;
using testutil::check;
using testutil::contains;
using testutil::expect_throws;

cardengine::BotFile parse_text(const std::string& text) {
    std::istringstream in(text);
    return cardengine::parse_bot(in);
}

cardengine::BotFile heuristic_file() {
    return parse_text("format_version = 1\nname = H\nstyle = heuristic\n"
                      "mistake_rate = 0.0\naggression = 0.5\n"
                      "looseness = 0.3\nseed = 11\n");
}

cardengine::BotFile random_file() {
    return parse_text(
        "format_version = 1\nname = R\nstyle = random\nseed = 5\n");
}

void check_legal(const cardengine::Table& table, int seat,
                 const cardengine::Action& action) {
    const cardengine::ActionOptions options = table.options(seat);
    switch (action.type) {
        case cardengine::ActionType::Fold:
        case cardengine::ActionType::Call:
            break;  // Always acceptable (calls cover all-in and zero).
        case cardengine::ActionType::Check:
            check(options.can_check, "bot check is legal");
            break;
        case cardengine::ActionType::Raise:
            check(options.can_raise &&
                      action.amount >= options.min_raise_to &&
                      action.amount <= options.max_raise_to,
                  "bot raise is legal");
            break;
    }
}

}  // namespace

int main() {
    using namespace cardengine;

    // Personality files parse; nonsense fails with context.
    {
        const BotFile bot = parse_text(
            "format_version = 1\nname = \"Tight Ted\"\nstyle = Heuristic\n"
            "mistake_rate = 0.05\naggression = 0.4\nlooseness = 0.15\n"
            "seed = 7\n");
        check(bot.name == "Tight Ted", "bot name");
        check(bot.style == BotStyle::Heuristic, "bot style");
        check(bot.mistake_rate == 0.05, "mistake rate");
        check(bot.seed == 7, "seed");
        const BotFile def = parse_text("format_version = 1\n");
        check(def.style == BotStyle::Heuristic, "default style");

        expect_throws<std::invalid_argument>([] { parse_text("style = heuristic\n"); },
                     "missing version");
        expect_throws<std::invalid_argument>(
            [] { parse_text("format_version = 1\nstyle = psychic\n"); },
            "bad style");
        expect_throws<std::invalid_argument>(
            [] { parse_text("format_version = 1\nmystery = 1\n"); },
            "unknown key");
        bool ranged = false;
        try {
            parse_text("format_version = 1\nmistake_rate = 2.0\n");
        } catch (const std::invalid_argument& e) {
            ranged = contains(e.what(), "invalid bot");
        }
        check(ranged, "mistake rate range");
        expect_throws<std::invalid_argument>([] { load_bot_file("tmp_missing_bot_xyz.txt"); },
                     "missing file");

        // Save/restore round-trips (the GUI designer writes these).
        {
            BotFile original = heuristic_file();
            original.name = "Saver";
            original.aggression = 0.75;
            std::ostringstream out;
            save_bot_file(original, out);
            std::istringstream in(out.str());
            const BotFile back = parse_bot(in);
            check(back.name == "Saver" && back.aggression == 0.75 &&
                      back.style == BotStyle::Heuristic &&
                      back.mistake_rate == 0.0,
                  "bot round-trip");
        }
    }

    // Aces preflop want to raise; trash facing a big bet folds.
    {
        GameConfig config;
        config.num_players = 2;
        Table table(config);
        auto bot = make_bot(heuristic_file());
        // Seat 0 (SB) gets As Ad; seat 1 gets bricks.
        table.start_hand_from_deck(cards({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                         "Jh", "9c", "3d"}));
        check(table.acting() == 0, "SB acts");
        const Action open = bot->decide(make_view(table, 0));
        check(open.type == ActionType::Raise, "aces raise preflop");

        // Same bot, second hand: seat 0 holds 7c 2d and faces a flop bet.
        Table table2(config);
        table2.start_hand_from_deck(cards({"Ks", "7c", "Kd", "2d", "Ah", "Qh",
                                          "Jh", "9c", "3d"}));
        table2.act(0, {ActionType::Call, 0});
        table2.act(1, {ActionType::Check, 0});
        table2.deal_next_street();
        table2.act(1, {ActionType::Raise, 500});
        const Action facing = bot->decide(make_view(table2, 0));
        check(facing.type == ActionType::Fold, "trash folds to 500");
    }

    // A full match: heuristic vs random, 30 seeded hands, fresh stacks each.
    // Skill must tell in total profit; busts can't cut the match short.
    {
        auto vair = [](int hands) {
            auto hero = make_bot(heuristic_file());
            auto villain = make_bot(random_file());
            int hero_profit = 0;
            for (int hand = 1; hand <= hands; ++hand) {
                GameConfig config;
                config.num_players = 2;
                Table table(config);
                table.start_hand(static_cast<std::uint64_t>(hand));
                while (!table.hand_complete()) {
                    if (table.acting() != -1) {
                        const int seat = table.acting();
                        Bot* bot = (seat == 0 ? hero.get() : villain.get());
                        table.act(seat, bot->decide(make_view(table, seat)));
                    } else {
                        table.deal_next_street();
                    }
                }
                table.settle();
                hero_profit += table.stack(0) - 10000;
            }
            return hero_profit;
        };
        const int profit = vair(30);
        std::cout << "  (heuristic profit over 30 hands: " << profit << ")\n";
        check(profit > 0, "heuristic beats random");
        // Deterministic: the same match twice, same result.
        check(vair(30) == profit, "bot match deterministic");
    }

    // Omaha bots decide postflop without crashing: exact-2 evaluation
    // replaces best-any-five (which would throw on 9 cards).
    {
        GameConfig config;
        config.num_players = 2;
        config.hole_cards = 4;
        config.board_cards = 5;
        config.showdown = HandConstruction::OmahaTwoAndThree;
        Table table(config);
        auto bot = make_bot(heuristic_file());
        // seat1: 7c 2d 3h 4s; seat0: As Ah Qd Jh (aces raise preflop).
        // Board: Ks 5d 9c 2h 6d.
        table.start_hand_from_deck(cards({"7c", "As", "2d", "Ah", "3h", "Qd",
                                         "4s", "Jh", "Ks", "5d", "9c", "2h",
                                         "6d"}));
        check(table.hole_cards(0).size() == 4, "omaha deal");
        const Action open = bot->decide(make_view(table, 0));
        check(open.type == ActionType::Raise, "omaha aces raise preflop");
        table.act(0, {ActionType::Call, 0});
        table.act(1, {ActionType::Check, 0});
        table.deal_next_street();
        const int flop_actor = table.acting();
        const Action flop = bot->decide(make_view(table, flop_actor));
        check_legal(table, flop_actor, flop);
        table.act(flop_actor, flop);
    }

    // Hi-Lo bots value the low: A2 raises preflop, a made low bets the flop.
    {
        GameConfig config;
        config.num_players = 2;
        config.hole_cards = 4;
        config.board_cards = 5;
        config.showdown = HandConstruction::OmahaHiLo;
        Table table(config);
        auto bot = make_bot(heuristic_file());
        // seat1: 9c Tc Jd Qh (high-only); seat0: Ah 2c 7s 8d (nut-low draw).
        // Board: 3h 4d 6s 9c Kh.
        table.start_hand_from_deck(cards({"9c", "Ah", "Tc", "2c", "Jd", "7s",
                                         "Qh", "8d", "3h", "4d", "6s", "9c",
                                         "Kh"}));
        const Action open = bot->decide(make_view(table, 0));
        check_legal(table, 0, open);
        check(open.type != ActionType::Fold, "A2 low hand continues preflop");
        table.act(0, {ActionType::Call, 0});
        table.act(1, {ActionType::Check, 0});
        table.deal_next_street();
        const int flop_actor = table.acting();
        const Action flop = bot->decide(make_view(table, flop_actor));
        check_legal(table, flop_actor, flop);
        check(flop.type != ActionType::Fold, "made low does not fold the flop");
    }

    std::cout << "test_bot ok\n";
    return 0;
}
