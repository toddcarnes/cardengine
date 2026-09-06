#include "cardengine/bot_runner.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cardengine {

namespace {

std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word) out.push_back(word);
    return out;
}

std::vector<std::string> lines_of(std::string text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

int to_int(const std::string& text, const char* what) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("bad ") + what + " '" +
                                    text + "'");
    }
}

// Value after `key` in a token list; throws if the key is missing.
const std::string& after(const std::vector<std::string>& toks,
                         const std::string& key) {
    for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i] == key) return toks[i + 1];
    }
    throw std::invalid_argument("state is missing '" + key + "'");
}

Street parse_street(const std::string& text) {
    if (text == "preflop") return Street::Preflop;
    if (text == "flop") return Street::Flop;
    if (text == "turn") return Street::Turn;
    if (text == "river") return Street::River;
    if (text == "complete") return Street::Complete;
    if (text == "none") return Street::None;
    throw std::invalid_argument("bad street '" + text + "'");
}

}  // namespace

std::string decide_from_text(Bot& bot, int seat,
                             const std::string& state_text,
                             const std::string& options_text) {
    SeatView view;
    view.seat = seat;
    bool saw_board = false;
    bool saw_pot = false;
    bool saw_current = false;
    bool saw_street = false;
    bool saw_seat = false;
    int bet = 0;
    for (const std::string& line : lines_of(state_text)) {
        const std::vector<std::string> toks = tokens(line);
        if (toks.empty() || toks[0] == "end") continue;
        if (toks[0] == "street" && toks.size() == 2) {
            view.street = parse_street(toks[1]);
            saw_street = true;
        } else if (toks[0] == "board" && toks.size() >= 2) {
            for (std::size_t i = 1; i < toks.size(); ++i) {
                if (toks[i] == "-") continue;
                view.board.push_back(parse_card(toks[i]));
            }
            saw_board = true;
        } else if (toks[0] == "pot" && toks.size() == 2) {
            view.pot = to_int(toks[1], "pot");
            saw_pot = true;
        } else if (toks[0] == "current" && toks.size() == 2) {
            view.current_bet = to_int(toks[1], "current bet");
            saw_current = true;
        } else if (toks[0] == "seat" && toks.size() >= 11 && toks[1] == std::to_string(seat)) {
            view.stack = to_int(after(toks, "stack"), "stack");
            bet = to_int(after(toks, "bet"), "bet");
            bool alive = false;
            for (std::size_t i = 0; i < toks.size(); ++i) {
                if (toks[i] == "live") alive = true;
            }
            if (!alive) {
                throw std::invalid_argument("seat is not live");
            }
            bool hole_seen = false;
            for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
                if (toks[i] == "hole") {
                    hole_seen = true;
                    for (std::size_t k = i + 1; k < toks.size(); ++k) {
                        if (toks[k] == "--") {
                            throw std::invalid_argument(
                                "own hole cards are hidden");
                        }
                        view.hole.push_back(parse_card(toks[k]));
                    }
                }
            }
            if (!hole_seen || view.hole.empty()) {
                throw std::invalid_argument("own hole cards are missing");
            }
            saw_seat = true;
        }
    }
    if (!saw_street || !saw_board || !saw_pot || !saw_current || !saw_seat) {
        throw std::invalid_argument("incomplete state block");
    }
    view.to_call = view.current_bet > bet ? view.current_bet - bet : 0;

    const std::vector<std::string> opts = tokens(options_text);
    if (opts.empty() || opts[0] != "options") {
        throw std::invalid_argument("expected an options line");
    }
    if (to_int(after(opts, "seat"), "options seat") != seat) {
        throw std::invalid_argument("options are for another seat");
    }
    view.can_check = after(opts, "check") == "yes";
    view.call_amount = to_int(after(opts, "call"), "call amount");
    view.can_raise = after(opts, "raise") == "yes";
    if (view.can_raise) {
        view.min_raise_to = to_int(after(opts, "min"), "min raise");
        view.max_raise_to = to_int(after(opts, "max"), "max raise");
    }

    const Action action = bot.decide(view);
    std::ostringstream out;
    switch (action.type) {
        case ActionType::Fold: out << "act fold"; break;
        case ActionType::Check: out << "act check"; break;
        case ActionType::Call: out << "act call"; break;
        case ActionType::Raise: out << "act raise " << action.amount; break;
    }
    return out.str();
}

}  // namespace cardengine
