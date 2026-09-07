#include "cardengine/protocol.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cardengine/detail/kv.h"
#include "cardengine/game_file.h"
#include "cardengine/session_file.h"

#include <fstream>

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

Table& Session::active_table() {
    if (tournament_) return tournament_->table();
    return table_;
}

const Table& Session::active_table() const {
    if (tournament_) return tournament_->table();
    return table_;
}

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
            return "ok commands: help load tload tstatus tlevel trebuy tchop sitout resume save restore start state options "
                   "act deal settle log addbot bots step quit";
        }
        if (command == "quit") return "bye";
        if (command == "save") {
            if (rest.empty()) return "error usage: save <session-file>";
            if (active_table().street() != Street::None &&
                active_table().street() != Street::Complete) {
                return "error cannot save mid-hand";
            }
            SessionFile file;
            file.tournament = (tournament_ != nullptr);
            if (tournament_) {
                const Tournament::Snapshot books = tournament_->snapshot();
                file.game = books.config.game;
                file.levels = books.config.levels;
                file.prizes = books.config.prizes;
                file.buy_in = books.config.buy_in;
                file.level_index = books.level_index;
                file.hands_into_level = books.hands_into_level;
                file.prize_pool = books.prize_pool;
                file.prize_awarded = books.prize_awarded;
                file.eliminated = books.eliminated;
                file.places = books.places;
                file.prizes_earned = books.prizes;
                file.stacks = books.stacks;
                file.sitting_out = books.sitting_out;
                file.button = books.button;
            } else {
                const Table::Snapshot felt = table_.snapshot();
                file.game = felt.config;
                file.stacks = felt.stacks;
                file.sitting_out = felt.sitting_out;
                file.button = felt.button;
            }
            for (const Event& e : active_table().events()) {
                file.events.push_back(e);
            }
            std::ofstream out(detail::unquote(rest));
            if (!out) {
                return "error cannot open '" + rest + "'";
            }
            save_session_file(file, out);
            out.flush();
            if (!out) {
                return "error cannot write '" + rest + "'";
            }
            return "ok";
        }
        if (command == "restore") {
            if (rest.empty()) return "error usage: restore <session-file>";
            if (active_table().street() != Street::None &&
                active_table().street() != Street::Complete) {
                return "error cannot restore mid-hand";
            }
            const SessionFile file =
                load_session_file(detail::unquote(rest));
            config_ = file.game;
            if (file.tournament) {
                Tournament::Snapshot books;
                books.config.game = file.game;
                books.config.levels = file.levels;
                books.config.prizes = file.prizes;
                books.config.buy_in = file.buy_in;
                books.level_index = file.level_index;
                books.hands_into_level = file.hands_into_level;
                books.prize_pool = file.prize_pool;
                books.prize_awarded = file.prize_awarded;
                books.eliminated = file.eliminated;
                books.places = file.places;
                books.prizes = file.prizes_earned;
                books.stacks = file.stacks;
                books.sitting_out = file.sitting_out;
                books.button = file.button;
                auto fresh = std::make_unique<Tournament>(books.config);
                fresh->restore(books);
                tournament_ = std::move(fresh);
            } else {
                Table::Snapshot felt;
                felt.config = file.game;
                felt.stacks = file.stacks;
                felt.sitting_out = file.sitting_out;
                felt.button = file.button;
                table_ = Table(file.game);
                table_.restore(felt);
                tournament_.reset();
            }
            active_table().clear_events();
            active_table().append_events(file.events);
            bots_.clear();
            hand_events_begin_ = active_table().events().size();
            return "ok";
        }
        if (command == "load") {
            if (rest.empty()) return "error usage: load <game-file>";
            const GameFile game = load_game_file(detail::unquote(rest));
            config_ = game.config;
            table_ = Table(config_);
            tournament_.reset();
            bots_.clear();
            hand_events_begin_ = 0;
            return "ok";
        }
        if (command == "tload") {
            if (rest.empty()) return "error usage: tload <tournament-file>";
            TournamentFile file = load_tournament_file(detail::unquote(rest));
            config_ = file.config.game;
            tournament_ = std::make_unique<Tournament>(file.config);
            bots_.clear();
            hand_events_begin_ = 0;
            return "ok";
        }
        if (command == "tstatus") {
            if (!tournament_) return "error no tournament loaded";
            const Tournament& tournament = *tournament_;
            const BlindLevel level = tournament.level();
            const int levels = static_cast<int>(
                tournament.config().levels.size());
            std::ostringstream out;
            out << "tournament level " << tournament.level_index() << "/"
                << levels << " hands " << tournament.hands_into_level() << "/"
                << level.hands << " blinds " << level.small_blind << "/"
                << level.big_blind << " ante " << level.ante << " pool "
                << tournament.prize_pool() << "\n";
            for (const Tournament::Standing& standing :
                 tournament.standings()) {
                out << "standing " << standing.seat << " stack "
                    << standing.stack << " "
                    << (standing.eliminated ? "out" : "alive") << " place ";
                if (standing.finish_place == 0) {
                    out << "-";
                } else {
                    out << standing.finish_place;
                }
                out << " prize " << standing.prize << "\n";
            }
            out << "ok";
            return out.str();
        }
        if (command == "tlevel") {
            if (!tournament_) return "error no tournament loaded";
            if (!rest.empty()) return "error usage: tlevel";
            tournament_->advance_level();
            return "ok";
        }
        if (command == "trebuy") {
            if (!tournament_) return "error no tournament loaded";
            if (rest.empty()) return "error usage: trebuy <seat>";
            std::size_t used = 0;
            int seat = -1;
            try {
                seat = std::stoi(rest, &used);
            } catch (const std::exception&) {
                return "error bad seat '" + rest + "'";
            }
            if (used != rest.size() || seat < 0 ||
                seat >= active_table().num_seats()) {
                return "error bad seat '" + rest + "'";
            }
            tournament_->rebuy(seat);
            return "ok";
        }
        if (command == "tchop") {
            if (!tournament_) return "error no tournament loaded";
            // tchop <seat:amount> ... — one pair per surviving seat.
            const std::vector<std::string> args = words(rest);
            if (args.empty()) return "error usage: tchop <seat:amount> ...";
            std::vector<Payout> deal;
            for (const std::string& arg : args) {
                const std::size_t colon = arg.find(':');
                if (colon == std::string::npos) {
                    return "error bad deal '" + arg + "'";
                }
                int seat = -1;
                int amount = -1;
                try {
                    std::size_t used = 0;
                    seat = std::stoi(arg.substr(0, colon), &used);
                    if (used != colon) throw std::invalid_argument("seat");
                    used = 0;
                    amount = std::stoi(arg.substr(colon + 1), &used);
                    if (used != arg.size() - colon - 1) {
                        throw std::invalid_argument("amount");
                    }
                } catch (const std::exception&) {
                    return "error bad deal '" + arg + "'";
                }
                if (seat < 0 || seat >= active_table().num_seats() ||
                    amount < 0) {
                    return "error bad deal '" + arg + "'";
                }
                deal.push_back({seat, amount});
            }
            tournament_->chop(deal);
            return "ok";
        }
        if (command == "sitout" || command == "resume") {
            if (rest.empty()) {
                return "error usage: " + command + " <seat>";
            }
            std::size_t used = 0;
            int seat = -1;
            try {
                seat = std::stoi(rest, &used);
            } catch (const std::exception&) {
                return "error bad seat '" + rest + "'";
            }
            if (used != rest.size() || seat < 0 ||
                seat >= active_table().num_seats()) {
                return "error bad seat '" + rest + "'";
            }
            active_table().set_sitting_out(seat, command == "sitout");
            return "ok";
        }
        if (command == "start") {
            if (rest.empty()) return "error usage: start <seed>";
            if (rest[0] == '-') return "error bad seed '" + rest + "'";
            std::size_t used = 0;
            unsigned long long seed = 0;
            try {
                seed = std::stoull(rest, &used);
            } catch (const std::exception&) {
                return "error bad seed '" + rest + "'";
            }
            if (used != rest.size()) return "error bad seed '" + rest + "'";
            // Baseline first: a rejected start must not move it.
            const std::size_t baseline = active_table().events().size();
            if (tournament_) {
                tournament_->begin_hand(static_cast<std::uint64_t>(seed));
            } else {
                active_table().start_hand(static_cast<std::uint64_t>(seed));
            }
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
                seat >= active_table().num_seats()) {
                return "error bad seat '" + rest + "'";
            }
            return do_state(seat);
        }
        if (command == "log") {
            std::ostringstream out;
            for (const Event& e : active_table().events()) {
                out << format_event(e) << "\n";
            }
            out << "end";
            return out.str();
        }
        if (command == "options") {
            const int seat = active_table().acting();
            if (seat == -1) return "error no action pending";
            const ActionOptions o = active_table().options(seat);
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
            const int seat = active_table().acting();
            if (seat == -1) return "error no action pending";
            if (bots_.count(seat) != 0) {
                return "error seat " + std::to_string(seat) + " is automated";
            }
            const std::vector<std::string> args = words(rest);
            if (args.empty()) {
                return "error usage: act fold|check|call|raise [amount]";
            }
            if (args[0] == "fold") {
                active_table().act(seat, {ActionType::Fold, 0});
            } else if (args[0] == "check") {
                active_table().act(seat, {ActionType::Check, 0});
            } else if (args[0] == "call") {
                active_table().act(seat, {ActionType::Call, 0});
            } else if (args[0] == "raise") {
                if (args.size() < 2) return "error usage: act raise <amount>";
                active_table().act(seat, {ActionType::Raise, parse_amount(args[1])});
            } else {
                return "error unknown action '" + args[0] + "'";
            }
            return "ok";
        }
        if (command == "deal") {
            active_table().deal_next_street();
            return "ok";
        }
        if (command == "settle") {
            const std::vector<Payout> payouts = active_table().settle();
            std::ostringstream out;
            out << "showdown " << (active_table().went_to_showdown() ? "yes" : "no")
                << "\n";
            for (const Payout& p : payouts) {
                out << "payout " << p.seat << " " << p.amount << "\n";
            }
            out << "ok";
            // Seated bots study the finished hand before the next deal.
            const HandSummary summary = summarize_hand(
                active_table().events(), hand_events_begin_, active_table().events().size());
            for (auto& [seat, bot] : bots_) {
                bot->observe(seat, summary);
            }
            // Tournaments book eliminations, prizes, and levels at settle.
            if (tournament_) {
                tournament_->finish_hand();
            }
            return out.str();
        }
        if (command == "addbot") {
            const auto [seat_text, raw_path] = split_first(rest);
            const std::string path = detail::unquote(raw_path);
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
                seat >= active_table().num_seats()) {
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
            const int seat = active_table().acting();
            if (seat == -1) return "error no action pending";
            const auto it = bots_.find(seat);
            if (it == bots_.end()) {
                return "error seat " + std::to_string(seat) + " is manual";
            }
            const Action action = it->second->decide(make_view(table_, seat));
            active_table().act(seat, action);
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
    out << "street " << street_name(active_table().street()) << "\n";
    out << "showdown "
        << (active_table().config().showdown ==
                    HandConstruction::OmahaTwoAndThree
                ? "omaha"
                : "holdem")
        << "\n";
    out << "button " << active_table().button() << "\n";
    out << "acting " << active_table().acting() << "\n";
    out << "pot " << active_table().pot_total() << "\n";
    out << "current " << active_table().current_bet() << "\n";
    out << "board";
    if (active_table().board().empty()) {
        out << " -";
    } else {
        for (const Card& c : active_table().board()) out << " " << to_string(c);
    }
    out << "\n";
    for (int i = 0; i < active_table().num_seats(); ++i) {
        out << "seat " << i << " stack " << active_table().stack(i) << " bet "
            << active_table().bet(i) << " committed " << active_table().committed(i) << " "
            << (active_table().in_hand(i) ? "in" : "out") << " "
            << (active_table().has_folded(i) ? "folded" : "live")
            << (active_table().sitting_out(i) ? " out" : "") << " hole";
        // Filtered views hide every other seat's cards; bare `state` is the
        // local-trust full dump. Folded and out seats always show `--`.
        const bool show =
            active_table().in_hand(i) && !active_table().has_folded(i) &&
            (view_seat < 0 || view_seat == i);
        if (show) {
            for (const Card& c : active_table().hole_cards(i)) {
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
