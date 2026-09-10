// Bot runner contract: text in, action out, opponents' cards ignored.
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cardengine/bot.h"
#include "cardengine/bot_runner.h"
#include "cardengine/protocol.h"
#include "cardengine/table.h"
#include "helpers.h"

namespace {

using testutil::check;
using testutil::expect_throws;

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

cardengine::BotFile heuristic_file() {
    // bluff_rate = 0: the free-card check below must not become a bluff
    // (default 0.35 would raise trash here on some RNG draws).
    std::istringstream in("format_version = 1\nname = H\nstyle = heuristic\n"
                          "mistake_rate = 0.0\naggression = 0.5\n"
                          "looseness = 0.3\nbluff_rate = 0.0\nseed = 3\n");
    return cardengine::parse_bot(in);
}

}  // namespace

int main() {
    using namespace cardengine;

    auto bot = make_bot(heuristic_file());

    // Aces preflop raise through the text contract.
    {
        const std::string state =
            "street preflop\nbutton 0\nacting 0\npot 150\ncurrent 100\n"
            "board -\n"
            "seat 0 stack 9950 bet 50 committed 50 in live hole As Ad\n"
            "seat 1 stack 9900 bet 100 committed 100 in live hole --\n"
            "end\n";
        const std::string options =
            "options seat 0 check no call 50 raise yes min 200 max 10000";
        check(starts_with(decide_from_text(*bot, 0, state, options),
                          "act raise"),
              "aces raise via text");
    }

    // Weak pair folds a big bet even with the opponent's aces visible in
    // the text: the runner must only ever read its own seat's hole cards.
    {
        const std::string state =
            "street river\nbutton 0\nacting 0\npot 1200\ncurrent 500\n"
            "board Ks Qd 2h 9c 3s\n"
            "seat 0 stack 8000 bet 0 committed 700 in live hole 7c 2d\n"
            "seat 1 stack 9000 bet 500 committed 1200 in live hole As Ad\n"
            "end\n";
        const std::string options =
            "options seat 0 check no call 500 raise yes min 1000 max 8500";
        check(decide_from_text(*bot, 0, state, options) == "act fold",
              "weak folds, opponents ignored");
    }

    // Free option with trash checks.
    {
        const std::string state =
            "street flop\nbutton 0\nacting 1\npot 200\ncurrent 0\n"
            "board Ah 7d 2c\n"
            "seat 0 stack 9900 bet 0 committed 100 in live hole --\n"
            "seat 1 stack 9900 bet 0 committed 100 in live hole 9c 3d\n"
            "end\n";
        const std::string options =
            "options seat 1 check yes call 0 raise yes min 100 max 10000";
        check(decide_from_text(*bot, 1, state, options) == "act check",
              "trash checks free card");
    }

    // Position and table size ride the state block: the runner rebuilds
    // the same view an in-process bot would get (button distance + live
    // seats), and live rivals' stud up-cards come along.
    {
        cardengine::GameConfig config;
        config.num_players = 6;
        cardengine::Table table(config);
        table.start_hand(7);
        // Mirror make_view through the exact text the protocol emits.
        cardengine::Session session;
        check(session.execute("start 7") == "ok", "protocol hand");
        const std::string block = session.execute("state 3");
        const std::string opts = session.execute("options");
        // Seat 3 holds the action here (UTG in 6-max off button 0).
        check(starts_with(opts, "options seat 3"), "seat 3 to act");
        const cardengine::SeatView direct =
            cardengine::make_view(table, table.acting());
        // Rebuild the runner's view from the same hand: position and size
        // must match the authoritative view (not defaults -1/0).
        const std::string state_for_runner = session.execute("state 3");
        // Parse the runner view indirectly: a position-aware probe bot
        // reports what it saw via its decision path.
        struct Probe : public cardengine::Bot {
            cardengine::SeatView seen;
            const std::string& name() const override {
                static const std::string n = "probe";
                return n;
            }
            cardengine::Action decide(const cardengine::SeatView& view) override {
                seen = view;
                return {cardengine::ActionType::Fold, 0};
            }
        };
        Probe probe;
        (void)decide_from_text(probe, 3, state_for_runner,
                               "options seat 3 check no call 100 raise yes "
                               "min 200 max 10000");
        check(probe.seen.position == 3, "position is button distance");
        check(probe.seen.position == direct.position, "position matches");
        check(probe.seen.table_size == 6, "table size is live seats");
        check(probe.seen.num_seats == direct.num_seats,
              "seat count matches");
        (void)block;
    }

    // Stud rival up-cards survive the text contract (public, like a board).
    {
        const std::string state =
            "street fourth\nshowdown stud\nbutton 0\nacting 1\npot 60\n"
            "current 0\nboard -\n"
            "seat 0 stack 9900 bet 0 committed 20 in live hole -- up Kh\n"
            "seat 1 stack 9900 bet 0 committed 20 in live hole 2c 3d up Ac\n"
            "end\n";
        struct UpProbe : public cardengine::Bot {
            cardengine::SeatView seen;
            const std::string& name() const override {
                static const std::string n = "up-probe";
                return n;
            }
            cardengine::Action decide(const cardengine::SeatView& view) override {
                seen = view;
                return {cardengine::ActionType::Check, 0};
            }
        };
        UpProbe probe;
        (void)decide_from_text(
            probe, 1, state,
            "options seat 1 check yes call 0 raise yes min 100 max 9900");
        check(probe.seen.rival_up.size() == 1 &&
                  probe.seen.rival_up[0].size() == 1 &&
                  cardengine::to_string(probe.seen.rival_up[0][0]) == "Kh",
              "rival up-cards parsed");
    }

    // Malformed input fails loudly, never silently.
    {
        const std::string state =
            "street preflop\nbutton 0\nacting 0\npot 150\ncurrent 100\n"
            "board -\n"
            "seat 0 stack 9950 bet 50 committed 50 in live hole As Ad\n"
            "end\n";
        const std::string options =
            "options seat 0 check no call 50 raise yes min 200 max 10000";
        expect_throws<std::invalid_argument>(
            [&] { decide_from_text(*bot, 0, state, "nonsense"); },
            "bad options line");
        expect_throws<std::invalid_argument>(
            [&] {
                decide_from_text(*bot, 0, state,
                                 "options seat 1 check no call 50 raise no");
            },
            "options for another seat");
        expect_throws<std::invalid_argument>([&] { decide_from_text(*bot, 0, "street preflop\nend\n",
                                            options); },
                     "incomplete state");
        const std::string hidden =
            "street preflop\nbutton 0\nacting 0\npot 150\ncurrent 100\n"
            "board -\n"
            "seat 0 stack 9950 bet 50 committed 50 in live hole --\n"
            "end\n";
        expect_throws<std::invalid_argument>([&] { decide_from_text(*bot, 0, hidden, options); },
                     "hidden own hole");
    }

    std::cout << "test_runner ok\n";
    return 0;
}
