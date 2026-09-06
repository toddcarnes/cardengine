// M9 bots: draws, position, adaptation flips, GTO balance, summaries.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cardengine/bot.h"

namespace {

using cardengine::ActionType;
using cardengine::HandSummary;
using cardengine::SeatSummary;
using cardengine::SeatView;

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

cardengine::BotFile heuristic_file() {
    return parse_text("format_version = 1\nname = H\nstyle = heuristic\n"
                      "mistake_rate = 0.0\naggression = 0.5\n"
                      "looseness = 0.3\nseed = 11\n");
}

cardengine::BotFile adaptive_file() {
    return parse_text("format_version = 1\nname = A\nstyle = adaptive\n"
                      "mistake_rate = 0.0\naggression = 0.5\n"
                      "looseness = 0.3\nseed = 2\n");
}

cardengine::BotFile gto_file() {
    return parse_text("format_version = 1\nname = G\nstyle = gto\n"
                      "mistake_rate = 0.0\naggression = 0.5\n"
                      "looseness = 0.3\nseed = 9\n");
}

cardengine::Card card(const char* text) {
    return cardengine::parse_card(text);
}

SeatView base_view() {
    SeatView view;
    view.seat = 0;
    view.stack = 8000;
    view.pot = 350;
    view.to_call = 150;
    view.current_bet = 150;
    view.can_check = false;
    view.call_amount = 150;
    view.can_raise = true;
    view.min_raise_to = 250;
    view.max_raise_to = 8150;
    view.street = cardengine::Street::Flop;
    return view;
}

HandSummary maniac_summary() {
    HandSummary summary;
    summary.big_blind = 100;
    summary.seats = std::vector<SeatSummary>(2);
    summary.seats[0].played = true;
    summary.seats[0].folded = false;
    summary.seats[1].played = true;
    summary.seats[1].folded = false;
    summary.seats[1].committed = 500;
    summary.seats[1].raises = 3;
    return summary;
}

}  // namespace

int main() {
    using namespace cardengine;

    // Position is the clockwise distance from the button (0 = button).
    {
        GameConfig config;
        config.num_players = 6;
        Table table(config);
        table.start_hand(3);
        check(make_view(table, 0).position == 0, "button is 0");
        check(make_view(table, 1).position == 1, "SB is 1");
        check(make_view(table, 5).position == 5, "cutoff is 5");
        check(make_view(table, 0).num_seats == 6, "seat count");
    }

    // A naked flush draw (9 outs ~ 36%) calls a small bet it would fold bare.
    {
        auto bot = make_bot(heuristic_file());
        SeatView view = base_view();
        view.hole = {card("9h"), card("7h")};
        view.board = {card("Ah"), card("5h"), card("2c")};
        view.pot = 400;
        view.to_call = 100;
        view.call_amount = 100;
        const Action decision = bot->decide(view);
        check(decision.type == ActionType::Call, "flush draw calls");
    }

    // Adaptation flips a marginal fold into a call against a maniac,
    // and stays folded against rocks. Pair of 7s, K kicker (s ~ 0.38)
    // facing 130 into 370: folds at base looseness, calls once loosened.
    {
        SeatView view = base_view();
        view.hole = {card("7h"), card("2d")};
        view.board = {card("7s"), card("Kd"), card("Qc")};
        view.pot = 370;
        view.to_call = 130;
        view.call_amount = 130;
        view.current_bet = 130;
        view.min_raise_to = 230;
        view.max_raise_to = 8000;

        auto versus_maniac = make_bot(adaptive_file());
        check(versus_maniac->decide(view).type == ActionType::Fold,
              "unread marginal folds");
        for (int i = 0; i < 10; ++i) {
            versus_maniac->observe(0, maniac_summary());
        }
        check(versus_maniac->decide(view).type == ActionType::Call,
              "maniac gets called lighter");

        auto versus_rock = make_bot(adaptive_file());
        HandSummary tight = maniac_summary();
        tight.seats[1].committed = 100;  // Never voluntary: a rock.
        tight.seats[1].raises = 0;
        for (int i = 0; i < 10; ++i) {
            versus_rock->observe(0, tight);
        }
        check(versus_rock->decide(view).type == ActionType::Fold,
              "rock still gets respect");
    }

    // Summaries read straight off a finished hand's events.
    {
        GameConfig config;
        config.num_players = 3;
        Table table(config);
        const std::size_t begin = table.events().size();
        table.start_hand_from_deck(
            {card("2c"), card("3d"), card("4h"), card("5s"), card("6c"),
             card("7d"), card("8h"), card("9s"), card("Tc"), card("Jd"),
             card("Qh")});
        table.act(0, {ActionType::Fold, 0});
        table.act(1, {ActionType::Fold, 0});
        table.settle();
        const HandSummary summary =
            summarize_hand(table.events(), begin, table.events().size());
        check(summary.big_blind == 100, "blind recorded");
        check(summary.seats.size() == 3, "three seats");
        check(summary.seats[0].played && summary.seats[0].folded &&
                  summary.seats[0].committed == 0,
              "folder summarized");
        check(!summary.seats[2].folded && summary.seats[2].won == 150 &&
                  summary.seats[2].committed == 100,
              "winner summarized");
    }

    // GTO: nuts always raise; defense tracks minimum-defense frequency.
    {
        auto bot = make_bot(gto_file());
        SeatView nuts = base_view();
        nuts.hole = {card("As"), card("Ad")};
        nuts.board = {};
        nuts.street = Street::Preflop;
        const Action value = bot->decide(nuts);
        check(value.type == ActionType::Raise, "gto raises the nuts");

        // Air facing a pot-sized bet: defend about half the time.
        SeatView air = base_view();
        air.hole = {card("7c"), card("2d")};
        air.board = {card("Ks"), card("Qd"), card("8h"), card("4c")};
        air.street = Street::Turn;
        air.pot = 200;
        air.to_call = 200;
        air.call_amount = 200;
        air.current_bet = 200;
        int defended = 0;
        const int trials = 300;
        for (int i = 0; i < trials; ++i) {
            const Action action = bot->decide(air);
            check(action.type == ActionType::Fold ||
                      action.type == ActionType::Call,
                  "gto air folds or calls");
            if (action.type != ActionType::Fold) ++defended;
        }
        const double rate = static_cast<double>(defended) / trials;
        check(rate > 0.3 && rate < 0.7, "mdf defense rate");
    }

    std::cout << "test_adaptive ok\n";
    return 0;
}
