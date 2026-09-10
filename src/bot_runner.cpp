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
    if (text == "draw") return Street::Draw;
    if (text == "flop") return Street::Flop;
    if (text == "turn") return Street::Turn;
    if (text == "river") return Street::River;
    if (text == "complete") return Street::Complete;
    if (text == "none") return Street::None;
    if (text == "third") return Street::Third;
    if (text == "fourth") return Street::Fourth;
    if (text == "fifth") return Street::Fifth;
    if (text == "sixth") return Street::Sixth;
    if (text == "seventh") return Street::Seventh;
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
    int button = 0;
    bool saw_button = false;
    int num_seats = 0;
    int live_seats = 0;
    std::vector<std::vector<Card>> other_up;
    for (const std::string& line : lines_of(state_text)) {
        const std::vector<std::string> toks = tokens(line);
        if (toks.empty() || toks[0] == "end") continue;
        if (toks[0] == "button" && toks.size() == 2) {
            button = to_int(toks[1], "button");
            saw_button = true;
        } else if (toks[0] == "street" && toks.size() == 2) {
            view.street = parse_street(toks[1]);
            saw_street = true;
        } else if (toks[0] == "showdown" && toks.size() == 2) {
            // Absent in older streams: holdem construction is the default.
            if (toks[1] == "omaha") {
                view.showdown = cardengine::HandConstruction::OmahaTwoAndThree;
            } else if (toks[1] == "omaha_hilo") {
                view.showdown = cardengine::HandConstruction::OmahaHiLo;
            } else if (toks[1] == "stud") {
                view.showdown = cardengine::HandConstruction::StudSeven;
            } else if (toks[1] == "draw") {
                view.showdown = cardengine::HandConstruction::DrawFive;
            } else if (toks[1] == "deuce") {
                view.showdown = cardengine::HandConstruction::DeuceSeven;
            } else if (toks[1] != "holdem") {
                throw std::invalid_argument("bad showdown '" + toks[1] + "'");
            }
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
        } else if (toks[0] == "seat" && toks.size() >= 11 &&
                   toks[1] == std::to_string(seat)) {
            ++num_seats;
            ++live_seats;  // Own line already proved live below (else throw).
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
                    // Down cards run to `up` (stud) or the line end; `--`
                    // means hidden (another seat's filtered view — but this
                    // branch only runs for our own seat line, so `--` here
                    // is malformed).
                    for (std::size_t k = i + 1; k < toks.size(); ++k) {
                        if (toks[k] == "up") break;
                        if (toks[k] == "--") {
                            throw std::invalid_argument(
                                "own hole cards are hidden");
                        }
                        view.hole.push_back(parse_card(toks[k]));
                    }
                } else if (toks[i] == "up") {
                    // Stud up cards are public; `--` means none yet (or a
                    // folded seat's hidden board — not ours, but tolerate).
                    for (std::size_t k = i + 1; k < toks.size(); ++k) {
                        if (toks[k] == "--") continue;
                        view.up.push_back(parse_card(toks[k]));
                    }
                }
            }
            if (!hole_seen || view.hole.empty()) {
                throw std::invalid_argument("own hole cards are missing");
            }
            saw_seat = true;
        } else if (toks[0] == "seat" && toks.size() >= 11) {
            // Every seat line counts toward table size; live rivals' `up`
            // cards are public (stud). `--` hole is the filtered norm here.
            ++num_seats;
            bool alive = false;
            for (std::size_t i = 0; i < toks.size(); ++i) {
                if (toks[i] == "live") alive = true;
            }
            if (alive) ++live_seats;
            for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
                if (toks[i] == "up") {
                    std::vector<Card> ups;
                    for (std::size_t k = i + 1; k < toks.size(); ++k) {
                        if (toks[k] == "--") continue;
                        ups.push_back(parse_card(toks[k]));
                    }
                    if (!ups.empty()) other_up.push_back(std::move(ups));
                    break;
                }
            }
        } else if (toks[0] == "community" && toks.size() >= 2) {
            for (std::size_t i = 1; i < toks.size(); ++i) {
                if (toks[i] == "-") continue;
                view.community.push_back(parse_card(toks[i]));
            }
        }
    }
    if (!saw_street || !saw_board || !saw_pot || !saw_current || !saw_seat) {
        throw std::invalid_argument("incomplete state block");
    }
    view.to_call = view.current_bet > bet ? view.current_bet - bet : 0;
    // Position and table size ride the state block (button + seat lines),
    // so out-of-process bots play the same game as in-process ones. Stud
    // opens by hand strength, so a missing button degrades to button (0).
    view.table_size = live_seats > 0 ? live_seats : num_seats;
    view.num_seats = view.table_size;
    view.position =
        (saw_button && num_seats > 0)
            ? ((seat - button) % num_seats + num_seats) % num_seats
            : 0;
    if (view.showdown == HandConstruction::StudSeven) {
        view.rival_up = std::move(other_up);
    }

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

namespace {

// Shared half of the out-of-process contract: the seat's own hole cards
// plus the house draw cap, out of a `state <seat>` block. Throws
// std::invalid_argument on malformed input.
struct DrawSeat {
    std::vector<Card> hole;
    int max_draw = 5;
};

DrawSeat parse_draw_seat(int seat, const std::string& state_text) {
    DrawSeat found;
    bool saw_seat = false;
    bool saw_showdown = false;
    HandConstruction showdown = HandConstruction::BestFiveOfAll;
    for (const std::string& line : lines_of(state_text)) {
        const std::vector<std::string> toks = tokens(line);
        if (toks.empty() || toks[0] == "end") continue;
        if (toks[0] == "showdown" && toks.size() == 2) {
            saw_showdown = true;
            if (toks[1] == "draw") {
                showdown = HandConstruction::DrawFive;
            } else if (toks[1] == "deuce") {
                showdown = HandConstruction::DeuceSeven;
            }
        } else if (toks[0] == "max_draw" && toks.size() == 2) {
            found.max_draw = to_int(toks[1], "max draw");
            if (found.max_draw < 1 || found.max_draw > 5) {
                throw std::invalid_argument("bad max_draw");
            }
        } else if (toks[0] == "seat" && toks.size() >= 11 &&
                   toks[1] == std::to_string(seat)) {
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
                        found.hole.push_back(parse_card(toks[k]));
                    }
                }
            }
            if (!hole_seen || found.hole.empty()) {
                throw std::invalid_argument("own hole cards are missing");
            }
            saw_seat = true;
        }
    }
    if (!saw_seat) throw std::invalid_argument("incomplete state block");
    if (saw_showdown && showdown != HandConstruction::DrawFive &&
        showdown != HandConstruction::DeuceSeven) {
        throw std::invalid_argument("not a draw game");
    }
    return found;
}

}  // namespace

std::string discard_from_text(Bot& bot, int seat,
                              const std::string& state_text) {
    const DrawSeat found = parse_draw_seat(seat, state_text);
    SeatView view;
    view.seat = seat;
    view.hole = found.hole;
    view.max_draw = found.max_draw;
    view.street = Street::Draw;
    const std::vector<std::string> discards = bot.choose_discards(view);
    if (static_cast<int>(discards.size()) > view.max_draw) {
        throw std::invalid_argument("too many discards");
    }
    std::ostringstream out;
    out << "discard";
    for (const std::string& c : discards) out << " " << c;
    return out.str();
}

}  // namespace cardengine
