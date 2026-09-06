#include "cardengine/protocol.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cardengine/game_file.h"

namespace cardengine {

namespace {

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Splits "command rest of line" into {"command", "rest of line"}.
std::pair<std::string, std::string> split_first(const std::string& line) {
    const std::size_t space = line.find_first_of(" \t");
    if (space == std::string::npos) return {line, ""};
    return {line.substr(0, space), trim(line.substr(space + 1))};
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string word;
    while (in >> word) out.push_back(word);
    return out;
}

int parse_amount(const std::string& text) {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used != text.size() || value < 0) {
        throw std::invalid_argument("bad amount '" + text + "'");
    }
    return value;
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

Session::Session() : table_(config_) {}

std::string Session::execute(const std::string& raw_line) {
    std::string line = trim(raw_line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
        line = trim(line);
    }
    if (line.empty()) return "error empty command";
    try {
        const auto [command, rest] = split_first(line);
        if (command == "help") {
            return "ok commands: help load start state options act deal "
                   "settle log addbot bots step quit";
        }
        if (command == "quit") return "bye";
        if (command == "load") {
            if (rest.empty()) return "error usage: load <game-file>";
            const GameFile game = load_game_file(rest);
            config_ = game.config;
            table_ = Table(config_);
            bots_.clear();
            hand_events_begin_ = 0;
            return "ok";
        }
        if (command == "start") {
            if (rest.empty()) return "error usage: start <seed>";
            std::size_t used = 0;
            unsigned long long seed = 0;
            try {
                seed = std::stoull(rest, &used);
            } catch (const std::exception&) {
                return "error bad seed '" + rest + "'";
            }
            if (used != rest.size()) return "error bad seed '" + rest + "'";
            // Baseline first: a rejected start must not move it.
            const std::size_t baseline = table_.events().size();
            table_.start_hand(static_cast<std::uint64_t>(seed));
            hand_events_begin_ = baseline;
            return "ok";
        }
        if (command == "state") {
            if (rest.empty()) return do_state(-1);
            std::size_t used = 0;
            int seat = -1;
            try {
                seat = std::stoi(rest, &used);
            } catch (const std::exception&) {
                return "error bad seat '" + rest + "'";
            }
            if (used != rest.size() || seat < 0 ||
                seat >= table_.num_seats()) {
                return "error bad seat '" + rest + "'";
            }
            return do_state(seat);
        }
        if (command == "log") {
            std::ostringstream out;
            for (const Event& e : table_.events()) {
                out << format_event(e) << "\n";
            }
            out << "end";
            return out.str();
        }
        if (command == "options") {
            const int seat = table_.acting();
            if (seat == -1) return "error no action pending";
            const ActionOptions o = table_.options(seat);
            std::ostringstream out;
            out << "options seat " << seat << " check "
                << (o.can_check ? "yes" : "no") << " call " << o.call_amount
                << " raise " << (o.can_raise ? "yes" : "no");
            if (o.can_raise) {
                out << " min " << o.min_raise_to << " max " << o.max_raise_to;
            }
            return out.str();
        }
        if (command == "act") {
            const int seat = table_.acting();
            if (seat == -1) return "error no action pending";
            if (bots_.count(seat) != 0) {
                return "error seat " + std::to_string(seat) + " is automated";
            }
            const std::vector<std::string> args = words(rest);
            if (args.empty()) {
                return "error usage: act fold|check|call|raise [amount]";
            }
            if (args[0] == "fold") {
                table_.act(seat, {ActionType::Fold, 0});
            } else if (args[0] == "check") {
                table_.act(seat, {ActionType::Check, 0});
            } else if (args[0] == "call") {
                table_.act(seat, {ActionType::Call, 0});
            } else if (args[0] == "raise") {
                if (args.size() < 2) return "error usage: act raise <amount>";
                table_.act(seat, {ActionType::Raise, parse_amount(args[1])});
            } else {
                return "error unknown action '" + args[0] + "'";
            }
            return "ok";
        }
        if (command == "deal") {
            table_.deal_next_street();
            return "ok";
        }
        if (command == "settle") {
            const std::vector<Payout> payouts = table_.settle();
            std::ostringstream out;
            out << "showdown " << (table_.went_to_showdown() ? "yes" : "no")
                << "\n";
            for (const Payout& p : payouts) {
                out << "payout " << p.seat << " " << p.amount << "\n";
            }
            out << "ok";
            // Seated bots study the finished hand before the next deal.
            const HandSummary summary = summarize_hand(
                table_.events(), hand_events_begin_, table_.events().size());
            for (auto& [seat, bot] : bots_) {
                bot->observe(seat, summary);
            }
            return out.str();
        }
        if (command == "addbot") {
            const auto [seat_text, path] = split_first(rest);
            if (seat_text.empty() || path.empty()) {
                return "error usage: addbot <seat> <bot-file>";
            }
            std::size_t used = 0;
            int seat = -1;
            try {
                seat = std::stoi(seat_text, &used);
            } catch (const std::exception&) {
                return "error bad seat '" + seat_text + "'";
            }
            if (used != seat_text.size() || seat < 0 ||
                seat >= table_.num_seats()) {
                return "error bad seat '" + seat_text + "'";
            }
            bots_[seat] = make_bot(load_bot_file(path));
            return "ok";
        }
        if (command == "bots") {
            std::ostringstream out;
            out << "bots";
            if (bots_.empty()) {
                out << " -";
            } else {
                for (const auto& [seat, bot] : bots_) {
                    (void)bot;
                    out << " " << seat;
                }
            }
            return out.str();
        }
        if (command == "step") {
            const int seat = table_.acting();
            if (seat == -1) return "error no action pending";
            const auto it = bots_.find(seat);
            if (it == bots_.end()) {
                return "error seat " + std::to_string(seat) + " is manual";
            }
            const Action action = it->second->decide(make_view(table_, seat));
            table_.act(seat, action);
            std::ostringstream out;
            out << "ok " << seat << " ";
            switch (action.type) {
                case ActionType::Fold: out << "fold"; break;
                case ActionType::Check: out << "check"; break;
                case ActionType::Call: out << "call"; break;
                case ActionType::Raise: out << "raise " << action.amount; break;
            }
            return out.str();
        }
        return "error unknown command '" + command + "'";
    } catch (const std::exception& e) {
        return std::string("error ") + e.what();
    }
}

std::string Session::do_state(int view_seat) const {
    std::ostringstream out;
    out << "street " << street_name(table_.street()) << "\n";
    out << "button " << table_.button() << "\n";
    out << "acting " << table_.acting() << "\n";
    out << "pot " << table_.pot_total() << "\n";
    out << "current " << table_.current_bet() << "\n";
    out << "board";
    if (table_.board().empty()) {
        out << " -";
    } else {
        for (const Card& c : table_.board()) out << " " << to_string(c);
    }
    out << "\n";
    for (int i = 0; i < table_.num_seats(); ++i) {
        out << "seat " << i << " stack " << table_.stack(i) << " bet "
            << table_.bet(i) << " committed " << table_.committed(i) << " "
            << (table_.in_hand(i) ? "in" : "out") << " "
            << (table_.has_folded(i) ? "folded" : "live") << " hole";
        // Filtered views hide every other seat's cards; bare `state` is the
        // local-trust full dump. Folded and out seats always show `--`.
        const bool show =
            table_.in_hand(i) && !table_.has_folded(i) &&
            (view_seat < 0 || view_seat == i);
        if (show) {
            for (const Card& c : table_.hole_cards(i)) {
                out << " " << to_string(c);
            }
        } else {
            out << " --";
        }
        out << "\n";
    }
    out << "end";
    return out.str();
}

int run_protocol(std::istream& in, std::ostream& out) {
    Session session;
    std::string line;
    while (std::getline(in, line)) {
        const std::string reply = session.execute(line);
        out << reply << "\n" << std::flush;
        if (trim(line) == "quit") return 0;
    }
    return 0;
}

}  // namespace cardengine
