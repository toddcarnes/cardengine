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

    // Hi-Lo shares the Omaha deal (20 cards + 5 board, exact 2+3 both ways).
    c = GameConfig{};
    c.showdown = HandConstruction::OmahaHiLo;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "hilo needs 4 hole cards");
    c.hole_cards = 4;
    validate(c);

    // Forced-bet options: straddle is 2× BB or off, kill/button-ante valid.
    c = GameConfig{};
    c.straddle = -10;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "negative straddle");
    c.straddle = 150;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "straddle not 2x BB");
    c.straddle = 200;
    validate(c);
    c.straddle = 0;
    c.kill = true;
    c.ante_from = AnteSource::ButtonOnly;
    c.ante = 10;
    validate(c);

    // Runouts: 1..3 boards, classic by default.
    c = GameConfig{};
    check(c.runouts == 1, "classic default");
    c.runouts = 0;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "zero runouts");
    c.runouts = 4;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "four runouts");
    c.runouts = 3;
    validate(c);
    // Spare boards come off the same shoe: a shoe that cannot deal them
    // fails up front instead of falling back to one board at settle.
    {
        GameConfig r;
        r.num_players = 10;
        r.hole_cards = 4;
        r.board_cards = 5;
        r.runouts = 1;
        r.showdown = HandConstruction::OmahaTwoAndThree;
        validate(r);  // 10*4 + 5 = 45: fits without spares.
        r.runouts = 3;
        expect_throws<std::invalid_argument>([&] { validate(r); },
                                             "runouts need shoe room");
    }

    // Stud: 7 down/up cards, no board, 4 up, bring-in below the small
    // blind, 2..8 seats (8-max is the standard full table — street
    // dealing plus the community river fit it in one deck).
    {
        GameConfig s;
        s.showdown = HandConstruction::StudSeven;
        s.hole_cards = 7;
        s.board_cards = 0;
        s.upcards = 4;
        s.bring_in = 10;
        validate(s);
        s.num_players = 8;
        validate(s);
        s.num_players = 9;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "stud caps at 8");
        s.num_players = 6;
        s.upcards = 3;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "stud needs 4 up");
        s.upcards = 4;
        s.bring_in = 50;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "bring-in below SB");
        s.bring_in = 10;
        s.runouts = 2;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "stud single board");
        s.runouts = 1;
        s.straddle = 200;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "stud no straddle");
        s.straddle = 0;
        s.kill = true;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "stud no kill");
    }
    {
        GameConfig s;
        s.upcards = 1;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "upcards need stud");
        s = GameConfig{};
        s.bring_in = 10;
        expect_throws<std::invalid_argument>([&] { validate(s); }, "bring-in needs stud");
    }

    // Draw games: 5 private cards, no board, one discard round (1..5 cap).
    // Replacements come off the same shoe, so full draws must fit 52.
    {
        GameConfig d;
        d.showdown = HandConstruction::DrawFive;
        d.hole_cards = 5;
        d.board_cards = 0;
        d.max_draw = 3;
        validate(d);
        d.max_draw = 0;
        expect_throws<std::invalid_argument>([&] { validate(d); }, "draw needs 1..5");
        d.max_draw = 6;
        expect_throws<std::invalid_argument>([&] { validate(d); }, "draw max 5");
        d.max_draw = 5;
        d.num_players = 6;
        expect_throws<std::invalid_argument>([&] { validate(d); },
                     "6-handed full draws overflow the deck");
        d.num_players = 5;
        validate(d);
        d = GameConfig{};
        d.showdown = HandConstruction::DeuceSeven;
        d.hole_cards = 5;
        d.board_cards = 0;
        d.max_draw = 3;
        validate(d);
        d.hole_cards = 4;
        expect_throws<std::invalid_argument>([&] { validate(d); }, "deuce needs 5 hole");
        d = GameConfig{};
        d.max_draw = 3;
        expect_throws<std::invalid_argument>([&] { validate(d); }, "max_draw needs draw");
    }

    // Boardless games cannot run it twice (no felt to deal spares from).
    {
        GameConfig r;
        r.hole_cards = 5;
        r.board_cards = 0;
        r.runouts = 2;
        expect_throws<std::invalid_argument>([&] { validate(r); }, "boardless runouts");
    }

    c = GameConfig{};
    c.max_raises_per_round = 0;
    expect_throws<std::invalid_argument>([&] { validate(c); }, "max raises positive");

    std::cout << "test_config ok\n";
    return 0;
}
