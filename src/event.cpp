#include "cardengine/event.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace cardengine {

const char* event_name(const Event& event) {
    if (std::holds_alternative<HandStartedEvent>(event)) return "begin_hand";
    if (std::holds_alternative<ActionTakenEvent>(event)) return "action";
    if (std::holds_alternative<StreetDealtEvent>(event)) return "street";
    if (std::holds_alternative<StudDealtEvent>(event)) return "stud";
    if (std::holds_alternative<DrawEvent>(event)) return "draw";
    if (std::holds_alternative<RunoutDealtEvent>(event)) return "runout";
    if (std::holds_alternative<TimeoutEvent>(event)) return "timeout";
    return "settle";
}

namespace {

std::string action_name(ActionType type) {
    switch (type) {
        case ActionType::Fold: return "fold";
        case ActionType::Check: return "check";
        case ActionType::Call: return "call";
        case ActionType::Raise: return "raise";
    }
    return "?";  // Unreachable; keeps /W4 happy.
}

std::string street_name(Street street) {
    switch (street) {
        case Street::None: return "none";
        case Street::Preflop: return "preflop";
        case Street::Draw: return "draw";
        case Street::Flop: return "flop";
        case Street::Turn: return "turn";
        case Street::River: return "river";
        case Street::Complete: return "complete";
        case Street::Third: return "third";
        case Street::Fourth: return "fourth";
        case Street::Fifth: return "fifth";
        case Street::Sixth: return "sixth";
        case Street::Seventh: return "seventh";
    }
    return "?";  // Unreachable; keeps /W4 happy.
}

}  // namespace

std::string format_event(const Event& event) {
    std::ostringstream out;
    if (const auto* e = std::get_if<HandStartedEvent>(&event)) {
        out << "begin_hand button " << e->button << " seed ";
        if (e->seeded) {
            out << e->seed;
        } else {
            out << "-";
        }
        out << " stacks ";
        for (std::size_t i = 0; i < e->stacks.size(); ++i) {
            if (i > 0) out << ",";
            out << e->stacks[i];
        }
        out << " hole ";
        for (std::size_t i = 0; i < e->hole.size(); ++i) {
            if (i > 0) out << "|";
            for (std::size_t k = 0; k < e->hole[i].size(); ++k) {
                if (k > 0) out << ",";
                out << to_string(e->hole[i][k]);
            }
        }
        return out.str();
    }
    if (const auto* e = std::get_if<ActionTakenEvent>(&event)) {
        out << "action " << e->seat << " " << action_name(e->action.type);
        if (e->action.type == ActionType::Raise) {
            out << " " << e->action.amount;
        }
        out << " pot " << e->pot_after;
        return out.str();
    }
    if (const auto* e = std::get_if<StreetDealtEvent>(&event)) {
        out << "street " << street_name(e->street);
        for (const Card& c : e->cards) out << " " << to_string(c);
        return out.str();
    }
    if (const auto* e = std::get_if<StudDealtEvent>(&event)) {
        // Up cards are public: per-seat tags keep the seat mapping (only
        // live seats are dealt, so bare seat order turns ambiguous after
        // folds). The shared river logs bare; down seventh streets log no
        // cards at all (down cards stay private, like folded hands).
        out << "stud " << street_name(e->street);
        if (e->community) {
            out << " community";
            // Tagged community lines round-trip with their seats; the
            // engine only ever emits the bare shared river (per_seat is
            // empty there), so no engine line changes shape here.
            if (!e->per_seat.empty()) {
                for (const auto& sc : e->per_seat) {
                    out << " " << sc.seat << ":" << to_string(sc.card);
                }
                return out.str();
            }
            for (const Card& c : e->cards) out << " " << to_string(c);
            return out.str();
        }
        if (!e->face_up) return out.str();
        if (!e->per_seat.empty()) {
            for (const auto& sc : e->per_seat) {
                out << " " << sc.seat << ":" << to_string(sc.card);
            }
            return out.str();
        }
        for (const Card& c : e->cards) out << " " << to_string(c);
        return out.str();
    }
    if (const auto* e = std::get_if<DrawEvent>(&event)) {
        // Counts only — the cards stay private, like folded hands.
        out << "draw " << e->seat << " drew " << e->drew;
        return out.str();
    }
    if (const auto* e = std::get_if<RunoutDealtEvent>(&event)) {
        out << "runout " << e->board;
        for (const Card& c : e->cards) out << " " << to_string(c);
        return out.str();
    }
    if (const auto* e = std::get_if<TimeoutEvent>(&event)) {
        out << "timeout " << e->seat << " pot " << e->pot_after;
        return out.str();
    }
    const auto& e = std::get<HandSettledEvent>(event);
    out << "settle showdown " << (e.showdown ? "yes" : "no") << " payouts ";
    if (e.payouts.empty()) {
        out << "-";
    } else {
        for (std::size_t i = 0; i < e.payouts.size(); ++i) {
            if (i > 0) out << ",";
            out << e.payouts[i].seat << ":" << e.payouts[i].amount;
        }
    }
    out << " committed ";
    for (std::size_t i = 0; i < e.committed.size(); ++i) {
        if (i > 0) out << ",";
        out << e.committed[i];
    }
    if (e.boards != 1) out << " boards " << e.boards;
    return out.str();
}

namespace {

std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word) out.push_back(word);
    return out;
}

std::vector<std::string> split_on(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream in(text);
    while (std::getline(in, item, sep)) out.push_back(item);
    return out;
}

int parse_count(const std::string& text, const char* what) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size() || value < 0) {
            throw std::invalid_argument("bad count");
        }
        return value;
    } catch (const std::invalid_argument& ex) {
        // Bad values surface as invalid_argument (the shape the tests
        // match); only a genuinely missing key escapes as out_of_range.
        if (std::string(ex.what()).rfind("event is missing", 0) == 0) throw;
        throw std::invalid_argument(std::string("bad ") + what + " '" +
                                    text + "'");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument(std::string("bad ") + what + " '" +
                                    text + "'");
    }
}

ActionType parse_action_name(const std::string& text) {
    if (text == "fold") return ActionType::Fold;
    if (text == "check") return ActionType::Check;
    if (text == "call") return ActionType::Call;
    if (text == "raise") return ActionType::Raise;
    throw std::invalid_argument("bad action '" + text + "'");
}

Street parse_street_name(const std::string& text) {
    if (text == "flop") return Street::Flop;
    if (text == "turn") return Street::Turn;
    if (text == "river") return Street::River;
    if (text == "draw") return Street::Draw;
    if (text == "third") return Street::Third;
    if (text == "fourth") return Street::Fourth;
    if (text == "fifth") return Street::Fifth;
    if (text == "sixth") return Street::Sixth;
    if (text == "seventh") return Street::Seventh;
    throw std::invalid_argument("bad street '" + text + "'");
}

// Value after `key` in a token list; throws std::out_of_range if missing
// (callers let it propagate — a missing key is a malformed line, and
// std::invalid_argument stays reserved for bad values the tests match on).
std::string after(const std::vector<std::string>& toks, const std::string& key) {
    for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i] == key) return toks[i + 1];
    }
    throw std::out_of_range("event is missing '" + key + "'");
}

}  // namespace

Event parse_event(const std::string& line, const GameConfig& config) {
    const std::vector<std::string> toks = tokens(line);
    if (toks.empty()) throw std::invalid_argument("empty event line");
    if (toks[0] == "begin_hand") {
        HandStartedEvent e;
        e.config = config;
        e.button = parse_count(after(toks, "button"), "button");
        const std::string seed_text = after(toks, "seed");
        if (seed_text == "-") {
            e.seeded = false;
        } else {
            try {
                std::size_t used = 0;
                e.seed = std::stoull(seed_text, &used);
                if (used != seed_text.size()) {
                    throw std::invalid_argument("bad seed");
                }
            } catch (const std::invalid_argument& ex) {
                throw std::invalid_argument(std::string(ex.what()) +
                                            " in seed '" + seed_text + "'");
            } catch (const std::out_of_range&) {
                throw std::invalid_argument("bad seed '" + seed_text + "'");
            }
            e.seeded = true;
        }
        for (const std::string& item : split_on(after(toks, "stacks"), ',')) {
            e.stacks.push_back(parse_count(item, "stack"));
        }
        for (const std::string& seat : split_on(after(toks, "hole"), '|')) {
            std::vector<Card> hole;
            for (const std::string& text : split_on(seat, ',')) {
                try {
                    hole.push_back(parse_card(text));
                } catch (const std::exception&) {
                    throw std::invalid_argument("bad card '" + text + "'");
                }
            }
            e.hole.push_back(std::move(hole));
        }
        if (e.stacks.size() != e.hole.size()) {
            throw std::invalid_argument("stacks and hole disagree");
        }
        return e;
    }
    if (toks[0] == "action") {
        if (toks.size() < 4) throw std::invalid_argument("short action event");
        ActionTakenEvent e;
        e.seat = parse_count(toks[1], "seat");
        e.action.type = parse_action_name(toks[2]);
        std::size_t rest = 3;
        if (e.action.type == ActionType::Raise) {
            if (toks.size() < 5) throw std::invalid_argument("short raise event");
            e.action.amount = parse_count(toks[3], "raise amount");
            rest = 4;
        }
        if (rest + 1 >= toks.size() || toks[rest] != "pot") {
            throw std::invalid_argument("action event is missing 'pot'");
        }
        e.pot_after = parse_count(toks[rest + 1], "pot");
        return e;
    }
    if (toks[0] == "street") {
        if (toks.size() < 2) throw std::invalid_argument("short street event");
        StreetDealtEvent e;
        e.street = parse_street_name(toks[1]);
        for (std::size_t i = 2; i < toks.size(); ++i) {
            try {
                e.cards.push_back(parse_card(toks[i]));
            } catch (const std::exception&) {
                throw std::invalid_argument(std::string("bad card '") +
                                            toks[i] + "'");
            }
        }
        return e;
    }
    if (toks[0] == "draw") {
        if (toks.size() != 4 || toks[2] != "drew") {
            throw std::invalid_argument("draw needs 'draw <seat> drew <n>'");
        }
        DrawEvent e;
        e.seat = parse_count(toks[1], "seat");
        e.drew = parse_count(toks[3], "drew");
        return e;
    }
    if (toks[0] == "runout") {
        if (toks.size() < 3) throw std::invalid_argument("short runout event");
        RunoutDealtEvent e;
        e.board = parse_count(toks[1], "board");
        if (e.board < 2) throw std::invalid_argument("bad board 'runout'");
        for (std::size_t i = 2; i < toks.size(); ++i) {
            try {
                e.cards.push_back(parse_card(toks[i]));
            } catch (const std::exception&) {
                throw std::invalid_argument(std::string("bad card '") +
                                            toks[i] + "'");
            }
        }
        return e;
    }
    if (toks[0] == "stud") {
        // `stud <street> [community] <seat:card ...>`: face-up cards with
        // their seats, or the one shared river card. Old bare-cards lines
        // (`stud <street> <ups...>`, seat order) still parse for existing
        // saved logs. Down seventh streets log bare (`stud seventh`).
        if (toks.size() < 2) throw std::invalid_argument("short stud event");
        StudDealtEvent e;
        std::size_t rest = 2;
        e.street = parse_street_name(toks[1]);
        if (rest < toks.size() && toks[rest] == "community") {
            e.community = true;
            ++rest;
        }
        bool tagged = false;
        bool bare = false;
        std::vector<int> seen_seats;
        for (std::size_t i = rest; i < toks.size(); ++i) {
            const std::string& tok = toks[i];
            const std::size_t colon = tok.find(':');
            if (colon == std::string::npos) {
                bare = true;
                try {
                    e.cards.push_back(parse_card(tok));
                } catch (const std::exception&) {
                    throw std::invalid_argument(std::string("bad card '") +
                                                tok + "'");
                }
                continue;
            }
            tagged = true;
            const int seat =
                parse_count(tok.substr(0, colon), "stud seat");
            if (seat >= config.num_players) {
                throw std::invalid_argument("bad stud seat '" + tok + "'");
            }
            for (const int seen : seen_seats) {
                if (seen == seat) {
                    throw std::invalid_argument("bad stud seat '" + tok +
                                                "'");
                }
            }
            seen_seats.push_back(seat);
            try {
                const Card c = parse_card(tok.substr(colon + 1));
                e.per_seat.push_back({seat, c});
                e.cards.push_back(c);
            } catch (const std::exception&) {
                throw std::invalid_argument(std::string("bad card '") +
                                            tok + "'");
            }
        }
        if (tagged && bare) {
            throw std::invalid_argument("mixed stud cards in '" + line + "'");
        }
        if (tagged && e.cards.size() != e.per_seat.size()) {
            throw std::invalid_argument("stud seats and cards disagree in '" +
                                        line + "'");
        }
        e.face_up = !e.cards.empty() || e.community;
        return e;
    }
    if (toks[0] == "timeout") {
        if (toks.size() != 4 || toks[2] != "pot") {
            throw std::invalid_argument("timeout needs 'timeout <seat> pot <pot>'");
        }
        TimeoutEvent e;
        e.seat = parse_count(toks[1], "seat");
        e.pot_after = parse_count(toks[3], "pot");
        return e;
    }
    if (toks[0] == "settle") {
        HandSettledEvent e;
        const std::string showdown = after(toks, "showdown");
        if (showdown == "yes") {
            e.showdown = true;
        } else if (showdown == "no") {
            e.showdown = false;
        } else {
            throw std::invalid_argument("bad showdown '" + showdown + "'");
        }
        const std::string payouts = after(toks, "payouts");
        if (payouts != "-") {
            for (const std::string& item : split_on(payouts, ',')) {
                const std::size_t colon = item.find(':');
                if (colon == std::string::npos) {
                    throw std::invalid_argument("bad payout '" + item + "'");
                }
                e.payouts.push_back(
                    {parse_count(item.substr(0, colon), "payout seat"),
                     parse_count(item.substr(colon + 1), "payout amount")});
            }
        }
        for (const std::string& item : split_on(after(toks, "committed"), ',')) {
            e.committed.push_back(parse_count(item, "committed"));
        }
        // Optional trailing `boards N` (run-it-twice); absent means 1.
        for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
            if (toks[i] == "boards") {
                e.boards = parse_count(toks[i + 1], "boards");
                if (e.boards < 1) {
                    throw std::invalid_argument("bad boards count");
                }
                break;
            }
        }
        return e;
    }
    throw std::invalid_argument("unknown event '" + toks[0] + "'");
}

}  // namespace cardengine
