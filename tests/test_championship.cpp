// Championships: validation, generation, bracket math, full bot bracket.
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cardengine/bot.h"
#include "cardengine/championship.h"
#include "helpers.h"

namespace {

using cardengine::Bot;
using cardengine::BracketStage;
using cardengine::Championship;
using cardengine::ChampionshipConfig;
using cardengine::ChampionshipEstimate;
using cardengine::GameConfig;
using cardengine::GeneratedBracket;
using cardengine::Table;
using cardengine::Tournament;
using cardengine::TournamentConfig;
using testutil::cards;
using testutil::check;
using testutil::contains;
using testutil::expect_throws;

GameConfig heads_up() {
    GameConfig game;
    game.num_players = 2;
    return game;
}

cardengine::BotFile heuristic_file() {
    std::istringstream in("format_version = 1\nname = H\nstyle = heuristic\n"
                          "mistake_rate = 0.0\naggression = 0.5\n"
                          "looseness = 0.3\nseed = 11\n");
    return cardengine::parse_bot(in);
}

cardengine::BotFile random_file() {
    std::istringstream in("format_version = 1\nname = R\nstyle = random\n"
                          "seed = 5\n");
    return cardengine::parse_bot(in);
}

}  // namespace

int main() {
    using namespace cardengine;

    // Bracket validation.
    {
        expect_throws<std::invalid_argument>(
            [] { validate_championship(ChampionshipConfig{}); },
            "empty bracket");
        {
            // Feed mismatch: 1 winner cannot fill 6 seats.
            ChampionshipConfig bad;
            BracketStage openers;
            openers.tournament.game = heads_up();
            openers.tables = 1;
            BracketStage final;
            final.tournament.game.num_players = 6;
            final.tables = 1;
            bad.stages = {openers, final};
            expect_throws<std::invalid_argument>(
                [&] { validate_championship(bad); }, "feed mismatch");
        }
        {
            // Multi-table final crowns nobody.
            ChampionshipConfig bad;
            BracketStage openers;
            openers.tournament.game = heads_up();
            openers.tables = 4;
            BracketStage final;
            final.tournament.game = heads_up();
            final.tables = 2;
            bad.stages = {openers, final};
            expect_throws<std::invalid_argument>(
                [&] { validate_championship(bad); }, "multi-table final");
        }
        {
            // Zero tables and negative prizes fail fast.
            ChampionshipConfig bad;
            BracketStage stage;
            stage.tournament.game = heads_up();
            stage.tables = 0;
            bad.stages = {stage};
            expect_throws<std::invalid_argument>(
                [&] { validate_championship(bad); }, "zero tables");
            bad.stages[0].tables = 1;
            bad.champion_prize = -1;
            expect_throws<std::invalid_argument>(
                [&] { validate_championship(bad); }, "negative prize");
        }
        {
            // A bad game inside fails with its context.
            ChampionshipConfig bad;
            BracketStage stage;
            stage.tournament.game.num_players = 11;
            stage.tables = 1;
            bad.stages = {stage};
            expect_throws<std::invalid_argument>(
                [&] { Championship cup(bad); }, "bad stage game");
        }
    }

    // Shortcut generation: depth d opens P^d tables, halving to one final.
    {
        GeneratedBracket spec;
        spec.game = heads_up();
        spec.depth = 0;
        ChampionshipConfig solo = generate_championship(spec);
        check(solo.stages.size() == 1 && solo.stages[0].tables == 1,
              "depth 0 is one table");

        spec.depth = 1;
        ChampionshipConfig l1 = generate_championship(spec);
        check(l1.stages.size() == 2 && l1.stages[0].tables == 2 &&
                  l1.stages[1].tables == 1,
              "depth 1 is 2+1");

        spec.depth = 2;
        ChampionshipConfig l2 = generate_championship(spec);
        check(l2.stages.size() == 3 && l2.stages[0].tables == 4 &&
                  l2.stages[1].tables == 2 && l2.stages[2].tables == 1,
              "depth 2 is 4+2+1");

        GameConfig six;
        six.num_players = 6;
        spec.game = six;
        spec.depth = 1;
        ChampionshipConfig six_one = generate_championship(spec);
        check(six_one.stages[0].tables == 6 && six_one.stages[1].tables == 1,
              "6-max depth 1 is 6+1");

        spec.depth = -1;
        expect_throws<std::invalid_argument>(
            [&] { generate_championship(spec); }, "negative depth");
        spec.depth = 13;
        expect_throws<std::invalid_argument>(
            [&] { generate_championship(spec); }, "absurd depth");
    }

    // Estimates are exact on counts, bounded on hands.
    {
        GeneratedBracket spec;
        spec.game = heads_up();
        spec.depth = 1;
        ChampionshipConfig config = generate_championship(spec);
        ChampionshipEstimate estimate = estimate_championship(config);
        check(estimate.stages == 2, "two stages");
        check(estimate.tournaments == 3, "three tables");
        check(estimate.entrants == 4, "four entrants");
        // 20000 chips per table over 150 blinds = 134 hands max each.
        check(estimate.max_hands == 134 * 3, "hand bound");
    }

    // Bracket math and gating on a level-1 bracket.
    {
        GeneratedBracket spec;
        spec.game = heads_up();
        spec.depth = 1;
        Championship cup(generate_championship(spec));
        check(cup.num_stages() == 2 && cup.num_tables(0) == 2 &&
                  cup.num_tables(1) == 1,
              "bracket shape");
        check(!cup.complete(), "fresh bracket open");
        expect_throws<std::logic_error>(
            [&] { cup.tournament(1, 0); }, "final locked early");
        expect_throws<std::invalid_argument>(
            [&] { cup.tournament(0, 5); }, "table out of range");
        expect_throws<std::invalid_argument>(
            [&] { cup.source(1, 7); }, "seat out of range");
        expect_throws<std::logic_error>(
            [&] { cup.source(1, 0); }, "winners unknown yet");
        BracketSlot origin = cup.source(0, 3);
        check(origin.stage == -1 && origin.table == -1, "stage 0 entrants");
    }

    // A full level-1 bracket, played by bots: semis complete, final
    // completes, one champion. Winners feed seats in table order.
    {
        GeneratedBracket spec;
        spec.game = heads_up();
        spec.depth = 1;
        spec.champion_prize = 5000;
        Championship cup(generate_championship(spec));
        auto hero = make_bot(heuristic_file());
        auto villain = make_bot(random_file());
        std::uint64_t seed = 1000;
        for (int stage = 0; stage < cup.num_stages(); ++stage) {
            for (int table = 0; table < cup.num_tables(stage); ++table) {
                Tournament& event = cup.tournament(stage, table);
                int hands = 0;
                while (!event.complete()) {
                    check(hands < 500, "bracket terminates");
                    event.begin_hand(seed++);
                    Table& felt = event.table();
                    while (!felt.hand_complete()) {
                        if (felt.acting() != -1) {
                            const int seat = felt.acting();
                            Bot* bot =
                                (seat == 0 ? hero.get() : villain.get());
                            felt.act(seat,
                                     bot->decide(make_view(felt, seat)));
                        } else {
                            felt.deal_next_street();
                        }
                    }
                    felt.settle();
                    event.finish_hand();
                    ++hands;
                }
            }
        }
        check(cup.complete(), "bracket completes");
        const int champ = cup.champion();
        check(champ == 0 || champ == 1, "champion is a seat");
        check(cup.champion_prize() == 5000, "champion prize recorded");
        BracketSlot semi = cup.source(1, 1);
        check(semi.stage == 0 && semi.table == 1, "final seat 1 from semi 2");
        BracketSlot champ_slot = cup.source(1, champ);
        check(champ_slot.stage == 0, "champion came through a semi");
    }

    // Files: explicit stages, the shortcut, round-trips, and errors.
    {
        const char* semi_path = "tmp_champ_semi.txt";
        {
            std::ofstream file(semi_path);
            file << "format_version = 1\nname = Semi\nnum_players = 2\n"
                    "buy_in = 100\nprizes = 100\nlevel = 50, 100, 0, 99\n";
        }
        const char* path = "tmp_champ.txt";
        {
            std::ofstream file(path);
            file << "format_version = 1\nname = \"Club Cup\"\n"
                    "champion_prize = 250\n"
                    "stage = tmp_champ_semi.txt, 2\n"
                    "stage = tmp_champ_semi.txt, 1\n";
            // NOTE: final reuses the semi file as its headsup final.
        }
        ChampionshipFile parsed = load_championship_file(path);
        check(parsed.name == "Club Cup", "championship name");
        check(parsed.config.stages.size() == 2, "two stages");
        check(parsed.config.stages[0].tables == 2, "two semis");
        check(parsed.config.champion_prize == 250, "champion prize");
        Championship cup(parsed.config);
        check(cup.num_tables(0) == 2 && cup.num_tables(1) == 1,
              "bracket shape from files");

        std::ostringstream saved;
        save_championship_file(parsed, saved);
        std::istringstream in(saved.str());
        ChampionshipFile back = parse_championship(in);
        check(back.config.stages.size() == 2 &&
                  back.config.champion_prize == 250,
              "championship round-trip");

        // The shortcut: one game file, depth 2 heads-up -> 4+2+1 tables.
        const char* game_path = "tmp_champ_game.txt";
        {
            std::ofstream file(game_path);
            file << "format_version = 1\nname = HU\nnum_players = 2\n"
                    "starting_stack = 5000\n";
        }
        {
            std::ofstream file("tmp_champ_short.txt");
            file << "format_version = 1\nname = Short\n"
                    "game = tmp_champ_game.txt\ndepth = 2\n"
                    "starting_stack = 7000\nbuy_in = 100\n";
        }
        ChampionshipFile generated =
            load_championship_file("tmp_champ_short.txt");
        check(generated.config.stages.size() == 3, "three stages");
        check(generated.config.stages[0].tables == 4, "four openers");
        check(generated.config.stages[0].tournament.game.starting_stack ==
                  7000,
              "override applies");
        check(generated.shortcut_depth == 2, "shortcut remembered");
        expect_throws<std::invalid_argument>(
            [&] {
                std::ostringstream out;
                save_championship_file(generated, out);
            },
            "generated brackets do not save");

        expect_throws<std::invalid_argument>(
            [] {
                std::istringstream bad(
                    "format_version = 1\nstage = tmp_champ_semi.txt, 2\n"
                    "depth = 1\ngame = tmp_champ_game.txt\n");
                parse_championship(bad);
            },
            "stages plus shortcut rejected");
        expect_throws<std::invalid_argument>(
            [] {
                std::istringstream bad(
                    "format_version = 1\ndepth = 1\n");
                parse_championship(bad);
            },
            "depth without game rejected");
        expect_throws<std::invalid_argument>(
            [] { load_championship_file("tmp_missing_champ_xyz.txt"); },
            "missing file");
        std::remove(semi_path);
        std::remove(path);
        std::remove(game_path);
        std::remove("tmp_champ_short.txt");
    }

    // The shipped examples load, validate, and estimate.
    {
        ChampionshipFile winter =
            load_championship_file("../../championships/winter-classic.txt");
        check(winter.config.stages.size() == 2, "winter has two stages");
        ChampionshipEstimate estimate = estimate_championship(winter.config);
        check(estimate.tournaments == 3 && estimate.entrants == 12,
              "winter counts");
        ChampionshipFile cup =
            load_championship_file("../../championships/novice-cup.txt");
        ChampionshipEstimate novice = estimate_championship(cup.config);
        check(novice.tournaments == 7 && novice.entrants == 8,
              "novice counts");
    }

    std::cout << "test_championship ok\n";
    return 0;
}
