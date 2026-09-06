// GameConfig validation: ranges, deck math, unimplemented structures.
#include <iostream>

#include "cardengine/config.h"

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
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

}  // namespace

int main() {
    using namespace cardengine;

    validate(GameConfig{});  // Defaults are standard Hold'em and valid.

    GameConfig c;
    c.ante = -1;
    expect_throw([&] { validate(c); }, "negative ante");

    c = GameConfig{};
    c.hole_cards = 0;
    expect_throw([&] { validate(c); }, "zero hole cards");
    c.hole_cards = 8;
    expect_throw([&] { validate(c); }, "eight hole cards");

    c = GameConfig{};
    c.board_cards = -1;
    expect_throw([&] { validate(c); }, "negative board cards");
    c.board_cards = 6;
    expect_throw([&] { validate(c); }, "six board cards");

    // Showdown needs best 5 of 5..7 total cards.
    c = GameConfig{};
    c.hole_cards = 4;
    c.board_cards = 5;  // 9 total: Omaha needs its own construction rule.
    expect_throw([&] { validate(c); }, "nine cards no construction rule");
    c.hole_cards = 1;
    c.board_cards = 3;  // 4 total: nothing to make a hand from.
    expect_throw([&] { validate(c); }, "four cards no hand");
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
    expect_throw([&] { validate(c); }, "deck math");
    c.hole_cards = 2;
    validate(c);  // 10*2+5 = 25: fine.

    // Named-but-unimplemented structures fail loudly, not silently.
    c = GameConfig{};
    c.betting = BettingStructure::Limit;
    expect_throw([&] { validate(c); }, "limit not implemented");
    c.betting = BettingStructure::PotLimit;
    expect_throw([&] { validate(c); }, "pot-limit not implemented");

    std::cout << "test_config ok\n";
    return 0;
}
