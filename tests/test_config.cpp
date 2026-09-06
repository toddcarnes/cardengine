// GameConfig validation: ranges, deck math, unimplemented structures.
#include <iostream>
#include <stdexcept>

#include "cardengine/config.h"
#include "helpers.h"

namespace {

using testutil::check;
using testutil::expect_throws;

}  // namespace

int main() {
    using namespace cardengine;

    validate(GameConfig{});  // Defaults are standard Hold'em and valid.

    GameConfig c;
    c.ante = -1;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "negative ante");

    c = GameConfig{};
    c.hole_cards = 0;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "zero hole cards");
    c.hole_cards = 8;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "eight hole cards");

    c = GameConfig{};
    c.board_cards = -1;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "negative board cards");
    c.board_cards = 6;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "six board cards");

    // Showdown needs best 5 of 5..7 total cards.
    c = GameConfig{};
    c.hole_cards = 4;
    c.board_cards = 5;  // 9 total: Omaha needs its own construction rule.
    expect_throws<std::invalid_argument>([&] { validate(c); }, "nine cards no construction rule");
    c.hole_cards = 1;
    c.board_cards = 3;  // 4 total: nothing to make a hand from.
    expect_throws<std::invalid_argument>([&] { validate(c); }, "four cards no hand");
    c.hole_cards = 1;
    c.board_cards = 4;  // 5 total: fine.
    validate(c);
    c.hole_cards = 7;
    c.board_cards = 0;  // Stud-style deal: fine.
    validate(c);

    // Everything must come out of one 52-card deck.
    c = GameConfig{};
    c.num_players = 10;
    c.hole_cards = 5;
    c.board_cards = 5;  // 55 > 52.
    expect_throws<std::invalid_argument>([&] { validate(c); }, "deck math");
    c.hole_cards = 2;
    validate(c);  // 10*2+5 = 25: fine.

    // All three betting structures validate; the engine implements each.
    c = GameConfig{};
    c.betting = BettingStructure::Limit;
    validate(c);
    c.betting = BettingStructure::PotLimit;
    validate(c);

    // Omaha construction needs exactly 4+5.
    c = GameConfig{};
    c.showdown = HandConstruction::OmahaTwoAndThree;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "omaha needs 4 hole cards");
    c.hole_cards = 4;
    validate(c);
    c.board_cards = 4;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "omaha needs 5 board cards");

    c = GameConfig{};
    c.max_raises_per_round = 0;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "max raises positive");

    std::cout << "test_config ok\n";
    return 0;
}
