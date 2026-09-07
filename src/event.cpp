#include "cardengine/event.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace cardengine {

const char* event_name(const Event& event) {
    if (std::holds_alternative<HandStartedEvent>(event)) return "begin_hand";
    if (std::holds_alternative<ActionTakenEvent>(event)) return "action";
    if (std::holds_alternative<StreetDealtEvent>(event)) return "street";
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
        case Street::Flop: return "flop";
        case Street::Turn: return "turn";
        case Street::River: return "river";
        case Street::Complete: return "complete";
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
        return e;
    }
    throw std::invalid_argument("unknown event '" + toks[0] + "'");
}

}  // namespace cardengine
