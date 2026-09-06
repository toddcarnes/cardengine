// Bot runner contract: text in, action out, opponents' cards ignored.
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cardengine/bot.h"
#include "cardengine/bot_runner.h"
#include "helpers.h"

namespace {

using testutil::check;
using testutil::expect_throws;

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

cardengine::BotFile heuristic_file() {
    std::istringstream in("format_version = 1\nname = H\nstyle = heuristic\n"
                          "mistake_rate = 0.0\naggression = 0.5\n"
                          "looseness = 0.3\nseed = 3\n");
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
