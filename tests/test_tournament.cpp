// Tournament director: levels, busts, prizes, rebuys, files, protocol.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/protocol.h"
#include "cardengine/tournament.h"
#include "helpers.h"

namespace {

using cardengine::BlindLevel;
using cardengine::Table;
using cardengine::Tournament;
using cardengine::TournamentConfig;
using testutil::cards;
using testutil::check;
using testutil::contains;
using testutil::expect_throws;

void play_out(Table& table) {
    using cardengine::ActionType;
    while (!table.hand_complete()) {
        if (table.acting() != -1) {
            const int seat = table.acting();
            if (table.options(seat).can_check) {
                table.act(seat, {ActionType::Check, 0});
            } else {
                table.act(seat, {ActionType::Call, 0});
            }
        } else {
            table.deal_next_street();
        }
    }
    table.settle();
}

TournamentConfig two_player() {
    TournamentConfig config;
    config.game.num_players = 2;
    config.levels = {BlindLevel{50, 100, 0, 2}, BlindLevel{100, 200, 50, 2}};
    return config;
}

}  // namespace

int main() {
    using namespace cardengine;

    // Config validation.
    {
        TournamentConfig bad;
        bad.game.num_players = 2;
        bad.levels.clear();
        expect_throws<std::exception>([&] { validate_tournament(bad); }, "empty levels");
        bad = TournamentConfig{};
        bad.game.num_players = 2;
        bad.levels = {BlindLevel{50, 100, 0, 0}};
        expect_throws<std::exception>([&] { validate_tournament(bad); }, "zero-length level");
        bad.levels = {BlindLevel{100, 50, 0, 5}};
        expect_throws<std::exception>([&] { validate_tournament(bad); }, "inverted blinds");
        bad.levels = {BlindLevel{50, 100, 0, 5}};
        bad.prizes = {60, 50};
        expect_throws<std::exception>([&] { validate_tournament(bad); }, "prizes over 100");
        bad.prizes = {50};
        bad.buy_in = -1;
        expect_throws<std::exception>([&] { validate_tournament(bad); }, "negative buy-in");
        expect_throws<std::exception>(
            [] {
                TournamentConfig c;
                c.game.num_players = 11;
                Tournament t(c);
            },
            "bad game inside tournament");
    }

    // Levels escalate blinds/antes on schedule.
    {
        Tournament tournament(two_player());
        tournament.begin_hand(1);
        check(tournament.table().committed(0) == 50, "level 0 SB");
        check(tournament.table().committed(1) == 100, "level 0 BB");
        play_out(tournament.table());
        tournament.finish_hand();
        check(tournament.level_index() == 0 && tournament.hands_into_level() == 1,
              "one hand into level 0");
        tournament.begin_hand(2);
        play_out(tournament.table());
        tournament.finish_hand();
        check(tournament.level_index() == 1 && tournament.hands_into_level() == 0,
              "advanced to level 1");
        tournament.begin_hand(3);
        check(tournament.table().committed(0) == 150, "level 1 SB plus ante");
        check(tournament.table().committed(1) == 250, "level 1 BB plus ante");
        play_out(tournament.table());
        tournament.finish_hand();
        tournament.advance_level();  // Already last: sticks.
        check(tournament.level_index() == 1, "level capped");
        expect_throws<std::exception>(
            [&] {
                TournamentConfig config;
                config.game.num_players = 2;
                Tournament fresh(config);
                fresh.finish_hand();
            },
            "finish without a hand");
        expect_throws<std::exception>(
            [&] {
                TournamentConfig config;
                config.game.num_players = 2;
                Tournament fresh(config);
                fresh.winner();
            },
            "winner before complete");
        expect_throws<std::exception>(
            [&] {
                TournamentConfig config;
                config.game.num_players = 2;
                Tournament fresh(config);
                finishing_order(fresh);
            },
            "finishing order before complete");
    }

    // Busts take places and prizes; the champion takes the remainder.
    {
        TournamentConfig config;
        config.game.num_players = 3;
        config.prizes = {50, 30, 20};
        config.buy_in = 1000;
        Tournament tournament(config);
        check(tournament.prize_pool() == 3000, "pool is 3 buy-ins");
        tournament.table().set_stack(2, 150);
        // seat1: 3c 4d; seat2: 7c 2d; seat0: As Ad. Board bricks everyone else.
        tournament.begin_hand_from_deck(cards({"3c", "7c", "As", "4d", "2d",
                                              "Ad", "Ks", "Qh", "Jh", "9c",
                                              "3d"}));
        tournament.table().act(0, {ActionType::Raise, 10000});
        tournament.table().act(1, {ActionType::Fold, 0});
        tournament.table().act(2, {ActionType::Call, 0});  // All in short.
        play_out(tournament.table());
        tournament.finish_hand();
        expect_throws<std::exception>([&] { tournament.finish_hand(); }, "double finish");
        auto standings = tournament.standings();
        check(standings[2].eliminated && standings[2].finish_place == 3 &&
                  standings[2].prize == 600,
              "third takes 20%");
        check(!tournament.complete(), "two remain");

        tournament.table().set_stack(1, 150);
        // Heads-up now (button to seat 1): seat1 <- 7c 2d, seat0 <- As Ad.
        // Seat 1 shoves short, seat 0 calls, aces hold.
        tournament.begin_hand_from_deck(cards({"As", "7c", "Ad", "2d", "Ks",
                                              "Qh", "Jh", "9c", "3d"}));
        tournament.table().act(1, {ActionType::Raise, 150});
        tournament.table().act(0, {ActionType::Call, 0});
        play_out(tournament.table());
        tournament.finish_hand();
        check(tournament.complete(), "one remains");
        check(tournament.winner() == 0, "seat 0 wins");
        standings = tournament.standings();
        check(standings[1].finish_place == 2 && standings[1].prize == 900,
              "second takes 30%");
        check(standings[0].finish_place == 1 && standings[0].prize == 1500,
              "champion takes the remainder");
        check(finishing_order(tournament) == std::vector<int>{0, 1, 2},
              "finishing order is champion first");
        expect_throws<std::exception>([&] { tournament.begin_hand(9); }, "no hands when over");
    }

    // Deals split the rest of the pool; places go by stack, leader first.
    {
        TournamentConfig config;
        config.game.num_players = 3;
        config.buy_in = 1000;
        Tournament tournament(config);
        // Bust seat 2 first so the deal must cover two (and must not).
        tournament.table().set_stack(2, 150);
        tournament.begin_hand_from_deck(cards({"3c", "7c", "As", "4d", "2d",
                                              "Ad", "Ks", "Qh", "Jh", "9c",
                                              "3d"}));
        tournament.table().act(0, {ActionType::Raise, 10000});
        tournament.table().act(1, {ActionType::Fold, 0});
        tournament.table().act(2, {ActionType::Call, 0});
        play_out(tournament.table());
        tournament.finish_hand();
        // Three-way split rejected (seat 2 is out); two-way sums must match.
        expect_throws<std::exception>(
            [&] { tournament.chop({{0, 1000}, {1, 1000}, {2, 1000}}); },
            "deal covers the eliminated");
        expect_throws<std::exception>(
            [&] { tournament.chop({{0, 1000}}); }, "deal must cover survivors");
        expect_throws<std::exception>(
            [&] { tournament.chop({{0, 1500}, {0, 900}}); }, "deal lists seat twice");
        expect_throws<std::exception>(
            [&] { tournament.chop({{0, 1500}, {1, 800}}); }, "deal must sum exactly");
        expect_throws<std::exception>(
            [&] { tournament.chop({{0, 1500}, {7, 1500}}); }, "deal seat range");
        // Stacks lean seat 0: it takes first, seat 1 second; pool accounted.
        // (Pool is 3000: seat 2 busted with no prizes configured.)
        tournament.chop({{0, 2000}, {1, 1000}});
        check(tournament.complete(), "deal ends it");
        auto standings = tournament.standings();
        check(standings[0].finish_place == 1, "leader takes place 1");
        check(standings[0].prize == 2000, "leader takes the deal's first share");
        check(standings[1].finish_place == 2 && standings[1].prize == 1000,
              "short stack takes second");
        check(standings[2].finish_place == 3 && standings[2].prize == 0,
              "the earlier bust keeps third");
        check(finishing_order(tournament) == std::vector<int>{0, 1, 2},
              "order is leader first, then the bust");
        expect_throws<std::exception>([&] { tournament.chop({{0, 1}, {1, 1}}); },
                         "no deals when over");
        expect_throws<std::exception>([&] { tournament.begin_hand(9); }, "no hands after deal");
    }

    // Deals refuse mid-hand: the cards decide, not the table talk.
    {
        TournamentConfig config;
        config.game.num_players = 2;
        Tournament tournament(config);
        tournament.begin_hand(1);
        expect_throws<std::exception>([&] { tournament.chop({{0, 0}, {1, 0}}); },
                         "no deals mid-hand");
    }

    // Rebuys restore stacks (and the pool), vacate finishes, never mid-hand.
    {
        TournamentConfig config;
        config.game.num_players = 3;
        config.buy_in = 1000;
        Tournament tournament(config);
        check(tournament.prize_pool() == 3000, "pool is 3 buy-ins");
        // Bust seat 2 first (same rig as the prize test).
        tournament.table().set_stack(2, 150);
        tournament.begin_hand_from_deck(cards({"3c", "7c", "As", "4d", "2d",
                                              "Ad", "Ks", "Qh", "Jh", "9c",
                                              "3d"}));
        tournament.table().act(0, {ActionType::Raise, 10000});
        tournament.table().act(1, {ActionType::Fold, 0});
        tournament.table().act(2, {ActionType::Call, 0});
        play_out(tournament.table());
        tournament.finish_hand();
        check(tournament.standings()[2].eliminated, "seat 2 busts");
        check(!tournament.complete(), "two remain");
        tournament.rebuy(2);
        check(tournament.table().stack(2) == 10000, "stack restored");
        check(tournament.prize_pool() == 4000, "pool grew");
        auto standings = tournament.standings();
        check(!standings[2].eliminated && standings[2].finish_place == 0 &&
                  standings[2].prize == 0,
              "finish vacated");
        // Continuity: the next hand runs with all three seats.
        tournament.begin_hand(2);
        check(tournament.table().in_hand(0) &&
                  tournament.table().in_hand(1) &&
                  tournament.table().in_hand(2),
              "rebought seat plays on");
        expect_throws<std::exception>([&] { tournament.rebuy(0); }, "no rebuys mid-hand");
        // Sitting out blocks rebuys; flags only bite from the next deal.
        tournament.table().set_sitting_out(0, true);
        expect_throws<std::exception>([&] { tournament.rebuy(0); }, "no rebuys while out");
        tournament.table().set_sitting_out(0, false);
        play_out(tournament.table());
        tournament.finish_hand();
        // From the next deal the sitter skips while the live seats play on.
        tournament.table().set_sitting_out(1, true);
        tournament.begin_hand(3);
        check(!tournament.table().in_hand(1), "sitter skips the deal");
        check(tournament.table().in_hand(0) && tournament.table().in_hand(2),
              "live seats play on");
    }

    // Table blind controls validate and refuse mid-hand changes.
    {
        GameConfig game;
        game.num_players = 2;
        Table table(game);
        table.set_blinds(100, 200);
        table.set_ante(25);
        expect_throws<std::exception>([&] { table.set_blinds(200, 100); }, "inverted blinds");
        expect_throws<std::exception>([&] { table.set_ante(-5); }, "negative ante");
        table.start_hand(1);
        expect_throws<std::exception>([&] { table.set_blinds(100, 200); }, "mid-hand blinds");
        expect_throws<std::exception>([&] { table.set_ante(25); }, "mid-hand ante");
    }

    // Tournament files: game refs, overrides, levels, prizes, errors.
    {
        const char* game_path = "tmp_tourney_base_game.txt";
        {
            std::ofstream file(game_path);
            file << "format_version = 1\nname = Base\nnum_players = 2\n";
        }
        const char* path = "tmp_tourney.txt";
        {
            std::ofstream file(path);
            file << "format_version = 1\nname = \"Friday Freezeout\"\n"
                    "game = tmp_tourney_base_game.txt\n"
                    "starting_stack = 5000\n"
                    "buy_in = 5000\n"
                    "prizes = 60, 40\n"
                    "level = 50, 100, 0, 5\n"
                    "level = 100, 200, 25, 5\n";
        }
        TournamentFile parsed = load_tournament_file(path);
        check(parsed.name == "Friday Freezeout", "tournament name");
        check(parsed.config.game.num_players == 2, "base game seats");
        check(parsed.config.game.starting_stack == 5000, "override applies");
        check(parsed.config.buy_in == 5000, "buy-in");
        check(parsed.config.prizes == std::vector<int>({60, 40}), "prizes");
        check(parsed.config.levels.size() == 2, "two levels");
        check(parsed.config.levels[1].ante == 25, "second level ante");

        std::ostringstream saved;
        save_tournament_file(parsed, saved);
        std::istringstream in(saved.str());
        TournamentFile back = parse_tournament(in);
        check(back.config.levels.size() == 2 &&
                  back.config.prizes == std::vector<int>({60, 40}) &&
                  back.config.buy_in == 5000 &&
                  back.config.game.starting_stack == 5000,
              "tournament round-trip");

        expect_throws<std::exception>(
            [] {
                std::istringstream bad("format_version = 1\nlevel = 1, 2\n");
                parse_tournament(bad);
            },
            "short level");
        expect_throws<std::exception>(
            [] {
                std::istringstream bad("format_version = 1\nmystery = 1\n");
                parse_tournament(bad);
            },
            "unknown tournament key");
        expect_throws<std::exception>([] { load_tournament_file("tmp_missing_xyz.txt"); },
                     "missing tournament file");
        std::remove(game_path);
        std::remove(path);
    }

    // Protocol: tload, tstatus, tlevel, trebuy, and auto-finish at settle.
    {
        const char* path = "tmp_proto_tourney.txt";
        {
            std::ofstream file(path);
            file << "format_version = 1\nname = P\nnum_players = 2\n"
                    "starting_stack = 10000\nbuy_in = 100\nprizes = 100\n"
                    "level = 50, 100, 0, 99\n"
                    "level = 100, 200, 25, 99\n";
        }
        Session session;
        check(contains(session.execute("tstatus"), "error"), "no tourney yet");
        check(contains(session.execute("tlevel"), "error"), "tlevel needs tourney");
        check(contains(session.execute("trebuy 0"), "error"), "trebuy needs tourney");
        check(contains(session.execute("tchop 0:100 1:100"), "error"), "tchop needs tourney");
        check(session.execute(std::string("tload ") + path) == "ok", "tload");
        const std::string status = session.execute("tstatus");
        check(contains(status, "tournament level 0/2"), "level line");
        check(contains(status, "blinds 50/100"), "blinds line");
        check(contains(status, "pool 200"), "pool line");
        // Manual clock advance: sticks at the final level, applies next hand.
        check(contains(session.execute("tlevel extra"), "error"), "tlevel takes no args");
        check(session.execute("tlevel") == "ok", "tlevel advances");
        check(contains(session.execute("tstatus"), "tournament level 1/2"),
              "level advanced");
        check(session.execute("tlevel") == "ok", "tlevel sticks at last");
        check(contains(session.execute("tstatus"), "tournament level 1/2"),
              "level capped over the wire");
        // Rebuy wiring: usage, range, live-seat top-up, mid-hand refusal.
        check(contains(session.execute("trebuy"), "usage"), "trebuy usage");
        check(contains(session.execute("trebuy x"), "error"), "trebuy bad seat text");
        check(contains(session.execute("trebuy 7"), "error"), "trebuy seat range");
        check(session.execute("trebuy 0") == "ok", "trebuy tops up live seat");
        check(contains(session.execute("tstatus"), "pool 300"), "pool grew over wire");
        check(session.execute("start 3") == "ok", "tourney start");
        check(contains(session.execute("trebuy 0"), "error"), "no rebuys mid-hand");
        check(session.execute("act fold") == "ok", "SB folds");
        const std::string done = session.execute("settle");
        check(contains(done, "payout 1 350"), "BB wins at level 1 (100/200 + antes)");
        const std::string after = session.execute("tstatus");
        check(contains(after, "hands 1/99"), "hand booked");
        // Chop wiring: usage, shapes, the split, and the closed sign after.
        check(contains(session.execute("tchop"), "usage"), "tchop usage");
        check(contains(session.execute("tchop 0-100"), "error"), "tchop bad pair");
        check(contains(session.execute("tchop 0:100"), "error"), "tchop must cover all");
        check(contains(session.execute("tchop 0:100 1:100"), "error"), "tchop sums exactly");
        check(session.execute("tchop 0:150 1:150") == "ok", "tchop splits it");
        const std::string chopped = session.execute("tstatus");
        check(contains(chopped, "standing 0 stack") && contains(chopped, "prize 150"),
              "deal shares booked");
        check(contains(session.execute("tchop 0:150 1:150"), "error"),
              "no deals when over");
        check(contains(session.execute("start 5"), "error"), "no hands after deal");
        // Cash load leaves tournament mode entirely.
        {
            std::ofstream game("tmp_proto_cash.txt");
            game << "format_version = 1\nnum_players = 2\n";
            game.close();
            check(session.execute("load tmp_proto_cash.txt") == "ok",
                  "cash load");
            check(contains(session.execute("tstatus"), "error"),
                  "tournament mode cleared");
            check(contains(session.execute("tlevel"), "error"),
                  "tlevel cleared with mode");
            check(contains(session.execute("trebuy 0"), "error"),
                  "trebuy cleared with mode");
            check(contains(session.execute("tchop 0:1 1:1"), "error"),
                  "tchop cleared with mode");
            std::remove("tmp_proto_cash.txt");
        }
        std::remove(path);
    }

    std::cout << "test_tournament ok\n";
    return 0;
}
