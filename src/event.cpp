#include "cardengine/event.h"

#include <sstream>

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

}  // namespace cardengine
