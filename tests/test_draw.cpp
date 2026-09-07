// Five-card draw exchange: discards, replacements, events, and bots.
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/bot.h"
#include "cardengine/config.h"
#include "cardengine/event.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using cardengine::ActionType;
using cardengine::DrawEvent;
using cardengine::GameConfig;
using cardengine::HandConstruction;
using cardengine::Street;
using cardengine::Table;
using testutil::cards;
using testutil::check;
using testutil::expect_throws;

void drive_betting(Table& t) {
    int guards = 0;
    while (t.acting() != -1 && guards++ < 100) {
        t.act(t.acting(), {ActionType::Check, 0});
    }
    check(t.acting() == -1, "betting round completes");
}

// 2-handed draw deck with a full replacement stub (10 deal + 10 draw,
// all 20 unique — duplicates would abort the hand, see test_bot).
std::vector<cardengine::Card> draw_deck() {
    return cards({"2h", "As", "3d", "Ks", "4c", "Qs", "5s", "Js", "9c", "Ts",
                  "2d", "3c", "4d", "5d", "6d", "7d", "8h", "9d", "Th", "Jh"});
}

}  // namespace

int main() {
    using namespace cardengine;

    // Preflop betting completes, then the draw street opens in turn order.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.showdown = HandConstruction::DrawFive;
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        check(t.street() == Street::Preflop, "draw opens preflop");
        check(t.draws_pending().empty(), "no draws before the street");
        t.act(0, {ActionType::Call, 0});
        t.act(1, {ActionType::Check, 0});
        t.deal_next_street();
        check(t.street() == Street::Draw, "exchange street");
        check(t.draws_pending() == std::vector<int>{1, 0},
              "draws run button-out");
        // Betting during the draw is refused; the hand is not complete.
        expect_throws<std::logic_error>(
            [&] { t.act(1, {ActionType::Check, 0}); }, "no betting in draw");
        check(!t.hand_complete(), "draw street is not showdown");
    }

    // Discards resolve against the hole: exact cards, capped at max_draw,
    // replacements off the shoe in order, pat hands keep five cards.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DrawFive;
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        t.act(0, {ActionType::Call, 0});
        t.act(1, {ActionType::Check, 0});
        t.deal_next_street();
        // seat1 holds 2h 3d 4c 5s 9c: throws the 9c, draws the 2d.
        const std::vector<Card> before = t.hole_cards(1);
        check(before.size() == 5, "five before the draw");
        t.discard(1, {"9c"});
        const std::vector<Card> after = t.hole_cards(1);
        check(after.size() == 5, "five after the draw");
        check(to_string(after.back()) == "2d", "replacement off the shoe");
        check(t.drew(1) && !t.drew(0), "draw flags");
        check(t.draws_pending() == std::vector<int>{0}, "one drawer left");
        // Out-of-turn, double, unknown, duplicate, and over-cap draws fail.
        expect_throws<std::logic_error>([&] { t.discard(1, {}); },
                 "already drew");
        expect_throws<std::invalid_argument>(
            [&] { t.discard(0, {"2h"}); }, "cannot draw another seat's card");
        expect_throws<std::invalid_argument>(
            [&] { t.discard(0, {"As", "As"}); }, "duplicate discard");
        expect_throws<std::invalid_argument>(
            [&] {
                t.discard(0, {"As", "Ks", "Qs", "Js"});
            },
            "over the cap");
        // seat0's royal stands pat; betting resumes on the flop slot.
        t.discard(0, {});
        check(t.draws_pending().empty(), "draw round done");
        t.deal_next_street();
        check(t.street() == Street::Flop, "post-draw betting");
        drive_betting(t);
        t.deal_next_street();
        drive_betting(t);
        t.deal_next_street();
        drive_betting(t);
        check(t.hand_complete(), "draw hand completes");
        t.settle();
        check(t.went_to_showdown(), "draw showdown after exchange");
        check(t.stack(0) == 10100 && t.stack(1) == 9900,
              "royal still wins");
    }

    // Draw events count cards, never name them; the log round-trips.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DrawFive;
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        t.act(0, {ActionType::Call, 0});
        t.act(1, {ActionType::Check, 0});
        t.deal_next_street();
        t.discard(1, {"9c"});
        t.discard(0, {});
        bool saw_street = false;
        int draws = 0;
        for (const Event& e : t.events()) {
            if (const auto* s = std::get_if<StreetDealtEvent>(&e)) {
                if (s->street == Street::Draw) {
                    saw_street = true;
                    check(s->cards.empty(), "draw street deals no board");
                }
            }
            if (const auto* d = std::get_if<DrawEvent>(&e)) {
                ++draws;
                const std::string text = format_event(e);
                const Event back = parse_event(text, c);
                check(format_event(back) == text, "draw line round-trips");
            }
        }
        check(saw_street, "draw street logged");
        check(draws == 2, "two draws logged");
        check(format_event(DrawEvent{1, 1}) == "draw 1 drew 1",
              "draw text");
        check(format_event(DrawEvent{0, 0}) == "draw 0 drew 0",
              "pat text");
        expect_throws<std::invalid_argument>(
            [&] { parse_event("draw 1", c); }, "short draw line");
        expect_throws<std::invalid_argument>(
            [&] { parse_event("draw 1 kept 2", c); }, "bad draw verb");
    }

    // Folded (and all-in) seats skip the exchange; everyone else must draw.
    {
        GameConfig c;
        c.num_players = 3;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DeuceSeven;
        Table t(c);
        // 15 deal, no stub needed (this hand ends at the draw street —
        // nobody draws, so no replacements are dealt).
        t.start_hand_from_deck(cards(
            {"2h", "3c", "Ah", "3d", "4d", "7c", "4c", "5h", "2c", "5s",
             "6c", "6h", "9c", "3s", "Ts", "7s", "8s", "4s", "5c", "6d",
             "7d", "8d", "9d", "Td"}));
        // seat2 folds preflop (acting order: 2, 0, 1 in a 3-handed game
        // with the button on 0... SB 1, BB 2, UTG 0 — first to act is 0).
        t.act(0, {ActionType::Fold, 0});
        t.act(1, {ActionType::Call, 0});
        t.act(2, {ActionType::Check, 0});
        t.deal_next_street();
        check(t.street() == Street::Draw, "3-handed draw street");
        for (int s : t.draws_pending()) {
            check(s != 0, "folder skips the draw");
        }
        check(t.draws_pending().size() == 2, "two drawers left");
    }

    // Bots: made hands stand pat, junk draws (capped at the house max).
    // (Heuristic coverage lives here — not in test_bot — because each
    // Table here owns its draw shoe in one block.)
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DrawFive;
        std::istringstream bot_src(
            "format_version = 1\nname = H\nstyle = heuristic\n"
            "mistake_rate = 0.0\naggression = 0.5\nlooseness = 0.3\nseed = "
            "11\n");
        auto bot = make_bot(parse_bot(bot_src));
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        // Royal stands pat.
        check(bot->choose_discards(make_view(t, 0)).empty(),
              "royal stands pat");
    }

    // Junk draws up to the house cap (keeps ace/king only; this 9-high
    // holds neither, so it draws the max 3).
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DrawFive;
        std::istringstream bot_src(
            "format_version = 1\nname = H\nstyle = heuristic\n"
            "mistake_rate = 0.0\naggression = 0.5\nlooseness = 0.3\nseed = "
            "11\n");
        auto bot = make_bot(parse_bot(bot_src));
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        const std::vector<std::string> junk =
            bot->choose_discards(make_view(t, 1));
        check(!junk.empty() && junk.size() <= 3, "junk draws, capped");
    }

    // Betting judgment on pat hands: the royal raises.
    {
        GameConfig c;
        c.num_players = 2;
        c.hole_cards = 5;
        c.board_cards = 0;
        c.max_draw = 3;
        c.showdown = HandConstruction::DrawFive;
        std::istringstream bot_src(
            "format_version = 1\nname = H\nstyle = heuristic\n"
            "mistake_rate = 0.0\naggression = 0.5\nlooseness = 0.3\nseed = "
            "11\n");
        auto bot = make_bot(parse_bot(bot_src));
        Table t(c);
        t.start_hand_from_deck(draw_deck());
        const Action open = bot->decide(make_view(t, 0));
        check(open.type == ActionType::Raise, "draw royal raises");
    }

    std::cout << "test_draw ok\n";
    return 0;
}
