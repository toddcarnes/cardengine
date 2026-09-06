// M9 bots: draws, position, adaptation flips, GTO balance, summaries.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cardengine/bot.h"
#include "helpers.h"

namespace {

using cardengine::ActionType;
using cardengine::HandSummary;
using cardengine::SeatSummary;
using cardengine::SeatView;
using testutil::check;

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

cardengine::BotFile slider_file(const char* text) { return parse_text(text); }

cardengine::Card card(const char* text) {
    return cardengine::parse_card(text);
}

cardengine::BotFile survival_file() {
    return parse_text("format_version = 1\nname = S\nstyle = heuristic\n"
                      "mistake_rate = 0.0\naggression = 0.5\n"
                      "looseness = 0.3\nsurvival = 1.0\nseed = 12\n");
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

    // Aggression reads: the same marginal hand is a bluff-catch against a
    // raising maniac but stays folded against a passive caller. adapt_rate
    // scales the swing (0 ignores the read entirely).
    {
        auto read_file = [](double adapt) {
            std::ostringstream text;
            text << "format_version = 1\nname = R\nstyle = adaptive\n"
                    "mistake_rate = 0.0\naggression = 0.5\n"
                    "looseness = 0.0\nadapt_rate = "
                 << adapt << "\nseed = 2\n";
            return parse_text(text.str());
        };
        // Weak pair (s ~ 0.35) facing 200 into 200 with no raise left:
        // pot odds demand 0.5, hand plus empty looseness gives ~0.35.
        // Maniac reads (3 raises/hand) flip it to a catch; passive reads
        // (no raises, low looseness signal) leave the fold standing.
        SeatView view = base_view();
        view.hole = {card("7h"), card("2d")};
        view.board = {card("7s"), card("Kd"), card("Qc")};
        view.pot = 200;
        view.to_call = 200;
        view.call_amount = 200;
        view.current_bet = 200;
        view.can_raise = false;

        auto catcher = make_bot(read_file(2.0));
        for (int i = 0; i < 10; ++i) {
            catcher->observe(0, maniac_summary());  // 3 raises/hand.
        }
        check(catcher->decide(view).type == ActionType::Call,
              "maniac bet is a bluff-catch");

        auto respect = make_bot(read_file(2.0));
        HandSummary passive = maniac_summary();
        passive.seats[1].committed = 100;  // Rock: no voluntary money.
        passive.seats[1].raises = 0;
        for (int i = 0; i < 10; ++i) {
            respect->observe(0, passive);
        }
        check(respect->decide(view).type == ActionType::Fold,
              "passive bet gets no discount");

        auto blind = make_bot(read_file(0.0));
        for (int i = 0; i < 10; ++i) {
            blind->observe(0, maniac_summary());
        }
        check(blind->decide(view).type == ActionType::Fold,
              "adapt zero ignores the read");
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

        // defense = 0 never defends air; defense = 2 always defends.
        auto never = make_bot(slider_file(
            "format_version = 1\nname = N\nstyle = gto\ndefense = 0.0\n"
            "seed = 9\n"));
        auto always = make_bot(slider_file(
            "format_version = 1\nname = A\nstyle = gto\ndefense = 2.0\n"
            "seed = 9\n"));
        for (int i = 0; i < 20; ++i) {
            check(never->decide(air).type == ActionType::Fold,
                  "defense zero folds air");
            check(always->decide(air).type == ActionType::Call,
                  "defense two calls air");
        }

        // bluff_rate = 0 never bluffs weak open hands; = 1 always does.
        SeatView weak_open = base_view();
        weak_open.hole = {card("7c"), card("2d")};
        weak_open.board = {};
        weak_open.street = Street::Preflop;
        weak_open.pot = 150;
        weak_open.to_call = 0;
        weak_open.can_check = false;
        weak_open.can_raise = true;
        weak_open.call_amount = 100;
        weak_open.current_bet = 100;
        weak_open.min_raise_to = 200;
        weak_open.max_raise_to = 8000;
        auto honest = make_bot(slider_file(
            "format_version = 1\nname = H\nstyle = gto\nbluff_rate = 0.0\n"
            "seed = 9\n"));
        auto bluffy = make_bot(slider_file(
            "format_version = 1\nname = B\nstyle = gto\nbluff_rate = 1.0\n"
            "seed = 9\n"));
        for (int i = 0; i < 20; ++i) {
            check(honest->decide(weak_open).type == ActionType::Call,
                  "bluff zero checks through");
            check(bluffy->decide(weak_open).type == ActionType::Raise,
                  "bluff one bets weak");
        }
    }

    // Omaha preflop: pairs and coordinated hands raise, bare high cards
    // with no coordination fold to pressure.
    {
        auto bot = make_bot(heuristic_file());
        SeatView omaha = base_view();
        omaha.showdown = HandConstruction::OmahaTwoAndThree;
        omaha.num_seats = 6;
        // Aces with connectors: a premium raising hand.
        omaha.hole = {card("As"), card("Ah"), card("Ks"), card("Qd")};
        omaha.pot = 150;
        omaha.to_call = 0;
        omaha.call_amount = 0;
        omaha.can_check = true;
        omaha.can_raise = true;
        omaha.min_raise_to = 200;
        omaha.max_raise_to = 8000;
        check(bot->decide(omaha).type == ActionType::Raise,
              "omaha aces raise");
        // Four bare high cards, rainbow, unconnected: folds to a big bet.
        SeatView junk = base_view();
        junk.showdown = HandConstruction::OmahaTwoAndThree;
        junk.num_seats = 6;
        junk.hole = {card("Ah"), card("Kd"), card("Qc"), card("7s")};
        junk.pot = 370;
        junk.to_call = 500;
        junk.call_amount = 500;
        junk.current_bet = 500;
        junk.min_raise_to = 600;
        junk.max_raise_to = 8000;
        check(bot->decide(junk).type == ActionType::Fold,
              "omaha junk folds to pressure");
        // Two small pair in four cards is bottom-two junk, not a premium:
        // folds to pressure where aces-up would continue.
        SeatView two_pair = base_view();
        two_pair.showdown = HandConstruction::OmahaTwoAndThree;
        two_pair.num_seats = 6;
        two_pair.hole = {card("7h"), card("7d"), card("3c"), card("3s")};
        two_pair.pot = 370;
        two_pair.to_call = 500;
        two_pair.call_amount = 500;
        two_pair.current_bet = 500;
        two_pair.min_raise_to = 600;
        two_pair.max_raise_to = 8000;
        check(bot->decide(two_pair).type == ActionType::Fold,
              "omaha bottom two folds to pressure");
    }

    // position_weight: 0 ignores position (same decision both seats),
    // 2 doubles the nudge (late raises wider, early folds harder).
    {
        auto flat = make_bot(slider_file(
            "format_version = 1\nname = F\nstyle = heuristic\n"
            "mistake_rate = 0.0\nposition_weight = 0.0\nseed = 11\n"));
        auto sharp = make_bot(slider_file(
            "format_version = 1\nname = P\nstyle = heuristic\n"
            "mistake_rate = 0.0\nposition_weight = 2.0\nseed = 11\n"));
        SeatView early = base_view();
        early.hole = {card("Ah"), card("7d")};
        early.board = {};
        early.street = Street::Preflop;
        early.num_seats = 6;
        early.position = 1;  // Small blind: worst nudge.
        early.pot = 150;
        early.to_call = 0;
        early.can_check = true;
        early.can_raise = true;
        early.call_amount = 100;
        early.current_bet = 100;
        early.min_raise_to = 200;
        early.max_raise_to = 8000;
        SeatView late = early;
        late.position = 0;  // Button: best nudge.
        // Weight 0: identical strength, identical decision.
        check(flat->decide(early).type == flat->decide(late).type,
              "position zero plays seats alike");
        // Weight 2: the nudge separates a borderline raiser.
        const Action early_act = sharp->decide(early);
        const Action late_act = sharp->decide(late);
        check(!(early_act.type == ActionType::Raise &&
                late_act.type != ActionType::Raise),
              "position two never raises early-only");
    }

    // Survival: a short stack folds a marginal continue a deep stack
    // takes, but still calls with a premium. Pair of 7s (s ~ 0.38) facing
    // 30 into 370 calls deep (heuristic baseline) and folds short.
    {
        SeatView view = base_view();
        view.hole = {card("7h"), card("2d")};
        view.board = {card("7s"), card("Kd"), card("Qc")};
        view.pot = 370;
        view.to_call = 30;
        view.call_amount = 30;
        view.current_bet = 30;
        view.min_raise_to = 130;
        view.max_raise_to = 8000;

        auto deep = make_bot(survival_file());
        view.stack = 8000;
        view.max_raise_to = 8030;
        check(deep->decide(view).type == ActionType::Call,
              "survival deep still calls");

        auto short_stack = make_bot(survival_file());
        view.stack = 60;
        view.call_amount = 30;
        view.max_raise_to = 90;
        check(short_stack->decide(view).type == ActionType::Fold,
              "survival short folds marginal");

        // Aces (s ~ 0.9) still continue short: premium beats the premium.
        auto premium = make_bot(survival_file());
        view.hole = {card("Ah"), card("Ad")};
        view.board = {card("7s"), card("Kd"), card("Qc")};
        view.stack = 60;
        check(premium->decide(view).type != ActionType::Fold,
              "survival short keeps premiums");

        // survival = 0 preserves the old call (round-trip default).
        auto classic = make_bot(heuristic_file());
        view.hole = {card("7h"), card("2d")};
        view.stack = 60;
        check(classic->decide(view).type == ActionType::Call,
              "survival zero preserves baseline");
    }

    // barrels: the prior aggressor keeps firing medium hands that would
    // check fresh; barrels = 0 never continues without a fresh reason.
    {
        auto story_file = [](double barrels) {
            std::ostringstream text;
            text << "format_version = 1\nname = C\nstyle = heuristic\n"
                    "mistake_rate = 0.0\naggression = 0.0\n"
                    "looseness = 0.3\nbluff_rate = 0.0\nbarrels = "
                 << barrels << "\nseed = 11\n";
            return parse_text(text.str());
        };
        SeatView flop = base_view();
        flop.hole = {card("Ah"), card("2d")};
        flop.board = {card("As"), card("Kd"), card("7c")};
        flop.street = Street::Flop;
        flop.num_seats = 6;
        flop.position = 0;
        flop.pot = 300;
        flop.to_call = 0;
        flop.can_check = true;
        flop.can_raise = true;
        flop.call_amount = 0;
        flop.current_bet = 0;
        flop.min_raise_to = 100;
        flop.max_raise_to = 8000;

        // Fresh (no prior aggression): medium pair checks at zero
        // aggression, no bluffs.
        auto fresh = make_bot(story_file(2.0));
        check(fresh->decide(flop).type == ActionType::Check,
              "fresh medium pair checks");

        // Same bot, but it raised preflop (seed the story by deciding an
        // open first): now the flop bet continues the story.
        auto story = make_bot(story_file(2.0));
        SeatView open = flop;
        open.board = {};
        open.street = Street::Preflop;
        open.hole = {card("Ah"), card("Kd")};
        open.pot = 150;
        check(story->decide(open).type == ActionType::Raise,
              "premiums open the story");
        check(story->decide(flop).type == ActionType::Raise,
              "barrels continue the story");

        // barrels = 0: the same story checks the flop.
        auto plain = make_bot(story_file(0.0));
        check(plain->decide(open).type == ActionType::Raise,
              "plain still opens premiums");
        check(plain->decide(flop).type == ActionType::Check,
              "barrels zero checks flop");
    }

    std::cout << "test_adaptive ok\n";
    return 0;
}
