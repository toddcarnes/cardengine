// Game file parsing: happy path, defaults, and every error shape.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cardengine/game_file.h"

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

cardengine::GameFile parse_text(const std::string& text) {
    std::istringstream in(text);
    return cardengine::parse_game(in);
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

}  // namespace

int main() {
    using namespace cardengine;

    // Full file parses into metadata plus config.
    {
        const GameFile game = parse_text(
            "# comment line\n"
            "\n"
            "format_version = 1\n"
            "name = \"No-Limit Hold'em 6-max\"\n"
            "description = Cash game\n"
            "num_players = 6\n"
            "starting_stack = 10000\n"
            "small_blind = 50\n"
            "big_blind = 100\n"
            "ante = 25\n"
            "hole_cards = 2\n"
            "board_cards = 5\n"
            "betting = NoLimit\n");
        check(game.format_version == 1, "version");
        check(game.name == "No-Limit Hold'em 6-max", "quoted name");
        check(game.description == "Cash game", "bare description");
        check(game.config.num_players == 6, "seats");
        check(game.config.ante == 25, "ante");
        check(game.config.betting == BettingStructure::NoLimit, "betting");
    }

    // Omitted rules keys fall back to Hold'em defaults.
    {
        const GameFile game = parse_text("format_version = 1\n");
        check(game.config.num_players == 6, "default seats");
        check(game.config.big_blind == 100, "default blind");
        check(game.config.hole_cards == 2, "default hole");
    }

    // CRLF files (hand-edited on Windows) parse cleanly.
    {
        const GameFile game =
            parse_text("format_version = 1\r\nname = Hi\r\n");
        check(game.name == "Hi", "CRLF tolerated");
    }

    // Every failure names its line.
    {
        bool reported = false;
        try {
            parse_text("format_version = 1\nnum_players = six\n");
        } catch (const std::invalid_argument& e) {
            reported = contains(e.what(), "line 2");
        }
        check(reported, "bad integer names line 2");
        expect_throw([] { parse_text("num_players = 6\n"); },
                     "missing version");
        expect_throw(
            [] { parse_text("format_version = 2\n"); }, "version 2 rejected");
        expect_throw(
            [] { parse_text("format_version = 1\nbogus_key = 1\n"); },
            "unknown key");
        expect_throw([] { parse_text("format_version\n"); }, "no equals");
        expect_throw([] { parse_text("format_version = \n"); }, "no value");
        expect_throw(
            [] { parse_text("format_version = 1\nbetting = fixed\n"); },
            "bad betting");
        expect_throw(
            [] { parse_text("format_version = 1\nshowdown = draw\n"); },
            "bad showdown");
        const GameFile omaha = parse_text(
            "format_version = 1\nshowdown = Omaha\nhole_cards = 4\n"
            "betting = PotLimit\nmax_raises = 3\n");
        check(omaha.config.showdown == HandConstruction::OmahaTwoAndThree,
              "omaha parses");
        check(omaha.config.max_raises_per_round == 3, "max raises parses");
    }

    // Struct validation failures surface as config errors, not silence.
    {
        bool reported = false;
        try {
            parse_text("format_version = 1\nnum_players = 11\n");
        } catch (const std::invalid_argument& e) {
            reported = contains(e.what(), "invalid config");
        }
        check(reported, "11 players rejected with context");
    }

    // Save/restore round-trips exactly, including through a real file.
    {
        GameFile game;
        game.name = "Heads-up";
        game.description = "d";
        game.config.num_players = 2;
        game.config.ante = 10;
        game.config.betting = BettingStructure::PotLimit;
        game.config.showdown = HandConstruction::OmahaTwoAndThree;
        game.config.hole_cards = 4;
        game.config.max_raises_per_round = 3;
        std::ostringstream out;
        save_game_file(game, out);
        std::istringstream in(out.str());
        const GameFile back = parse_game(in);
        check(back.name == "Heads-up" && back.config.num_players == 2 &&
                  back.config.ante == 10 &&
                  back.config.betting == BettingStructure::PotLimit &&
                  back.config.showdown ==
                      HandConstruction::OmahaTwoAndThree &&
                  back.config.hole_cards == 4 &&
                  back.config.max_raises_per_round == 3,
              "string round-trip");

        const char* path = "tmp_test_game_roundtrip.txt";
        {
            std::ofstream file(path);
            save_game_file(game, file);
        }
        const GameFile from_disk = load_game_file(path);
        check(from_disk.name == "Heads-up", "file round-trip");
        std::remove(path);

        expect_throw([] { load_game_file("tmp_missing_game_xyz.txt"); },
                     "missing file");
    }

    std::cout << "test_gamefile ok\n";
    return 0;
}
