// Bots: file parsing, decision sanity, and heuristic-vs-random edge.
#include <iostream>
#include <sstream>

#include "cardengine/bot.h"

namespace {

using cardengine::ActionType;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

cardengine::BotFile parse_text(const std::string& text) {
    std::istringstream in(text);
    return cardengine::parse_bot(in);
}

template <typename F>
void expect_throw(F&& f, const char* message) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return;
    }
    std::cerr << "FAIL (expected invalid_argument): " << message << "\n";
    std::exit(1);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
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

std::vector<cardengine::Card> shoe(std::initializer_list<const char*> texts) {
    std::vector<cardengine::Card> out;
    for (const char* t : texts) out.push_back(cardengine::parse_card(t));
    return out;
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

        expect_throw([] { parse_text("style = heuristic\n"); },
                     "missing version");
        expect_throw(
            [] { parse_text("format_version = 1\nstyle = psychic\n"); },
            "bad style");
        expect_throw(
            [] { parse_text("format_version = 1\nmystery = 1\n"); },
            "unknown key");
        bool ranged = false;
        try {
            parse_text("format_version = 1\nmistake_rate = 2.0\n");
        } catch (const std::invalid_argument& e) {
            ranged = contains(e.what(), "invalid bot");
        }
        check(ranged, "mistake rate range");
        expect_throw([] { load_bot_file("tmp_missing_bot_xyz.txt"); },
                     "missing file");
    }

    // Aces preflop want to raise; trash facing a big bet folds.
    {
        GameConfig config;
        config.num_players = 2;
        Table table(config);
        auto bot = make_bot(heuristic_file());
        // Seat 0 (SB) gets As Ad; seat 1 gets bricks.
        table.start_hand_from_deck(shoe({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                         "Jh", "9c", "3d"}));
        check(table.acting() == 0, "SB acts");
        const Action open = bot->decide(make_view(table, 0));
        check(open.type == ActionType::Raise, "aces raise preflop");

        // Same bot, second hand: seat 0 holds 7c 2d and faces a flop bet.
        Table table2(config);
        table2.start_hand_from_deck(shoe({"Ks", "7c", "Kd", "2d", "Ah", "Qh",
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

    std::cout << "test_bot ok\n";
    return 0;
}
