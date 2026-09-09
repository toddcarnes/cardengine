#include "cardengine/session_file.h"

#include <fstream>
#include <stdexcept>

#include "cardengine/detail/kv.h"
#include "cardengine/game_file.h"

namespace cardengine {

using detail::lower;
using detail::parse_int;
using detail::parse_int_list;
using detail::trim;
using detail::unquote;

namespace {

std::vector<bool> parse_flags(const std::string& value, int line,
                              std::size_t want, const char* what) {
    std::vector<bool> out;
    for (int item : parse_int_list(value, line)) {
        if (item != 0 && item != 1) {
            throw std::invalid_argument("line " + std::to_string(line) +
                                        ": " + what + " must be 0 or 1");
        }
        out.push_back(item == 1);
    }
    if (out.size() != want) {
        throw std::invalid_argument("line " + std::to_string(line) + ": " +
                                    what + " needs one entry per seat");
    }
    return out;
}

void write_ints(std::ostream& out, const std::vector<int>& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ",";
        out << values[i];
    }
    out << "\n";
}

void write_flags(std::ostream& out, const std::vector<bool>& flags) {
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (i > 0) out << ",";
        out << (flags[i] ? 1 : 0);
    }
    out << "\n";
}

}  // namespace

SessionFile parse_session(std::istream& in) {
    SessionFile session;
    bool saw_version = false;
    bool saw_mode = false;
    bool saw_stacks = false;
    bool saw_button = false;
    bool saw_events = false;
    int want_events = -1;
    int kill_line = 0;  // Source line of kill_pending (0 when absent).
    std::vector<std::string> log_lines;
    session.levels.clear();  // Levels come only from `level` lines.
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Raw log lines trail the keys; they are not key=value lines.
        if (saw_events) {
            log_lines.push_back(line);
            continue;
        }
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#') continue;
        const std::size_t eq = text.find('=');
        if (eq == std::string::npos) {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": expected 'key = value'");
        }
        const std::string key = lower(trim(text.substr(0, eq)));
        const std::string value = trim(text.substr(eq + 1));
        if (value.empty()) {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": missing value");
        }
        if (key == "format_version") {
            session.format_version = parse_int(value, lineno);
            if (session.format_version != 1 && session.format_version != 2) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": unsupported format_version (want 1 or 2)");
            }
            saw_version = true;
        } else if (key == "mode") {
            const std::string mode = lower(value);
            if (mode == "cash") {
                session.tournament = false;
            } else if (mode == "tournament") {
                session.tournament = true;
            } else {
                throw std::invalid_argument("line " + std::to_string(lineno) +
                                            ": mode must be cash or tournament");
            }
            saw_mode = true;
        } else if (key == "stacks") {
            session.stacks = parse_int_list(value, lineno);
            saw_stacks = true;
        } else if (key == "sitting_out") {
            // Parsed after stacks (needs the seat count); stashed raw.
            session.sitting_out = parse_flags(value, lineno, session.stacks.size(),
                                              "sitting_out");
        } else if (key == "button") {
            session.button = parse_int(value, lineno);
            saw_button = true;
        } else if (key == "kill_pending") {
            const int flag = parse_int(value, lineno);
            if (flag != 0 && flag != 1) {
                throw std::invalid_argument("line " + std::to_string(lineno) +
                                            ": kill_pending must be 0 or 1");
            }
            session.kill_pending = (flag == 1);
            kill_line = lineno;
        } else if (key == "buy_in") {
            session.buy_in = parse_int(value, lineno);
        } else if (key == "prizes") {
            session.prizes = parse_int_list(value, lineno);
        } else if (key == "level") {
            const std::vector<int> parts = parse_int_list(value, lineno);
            if (parts.size() != 4 && parts.size() != 5) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": level needs 4 numbers: small, big, ante, hands (or 5 with minutes)");
            }
            BlindLevel level{parts[0], parts[1], parts[2], parts[3]};
            if (parts.size() == 5) level.minutes = parts[4];
            session.levels.push_back(level);
        } else if (key == "level_index") {
            session.level_index = parse_int(value, lineno);
        } else if (key == "hands_into_level") {
            session.hands_into_level = parse_int(value, lineno);
        } else if (key == "level_elapsed") {
            session.level_elapsed = parse_int(value, lineno);
            if (session.level_elapsed < 0) {
                throw std::invalid_argument("line " + std::to_string(lineno) +
                                            ": level_elapsed cannot be negative");
            }
        } else if (key == "prize_pool") {
            session.prize_pool = parse_int(value, lineno);
        } else if (key == "prize_awarded") {
            session.prize_awarded = parse_int(value, lineno);
        } else if (key == "eliminated") {
            session.eliminated =
                parse_flags(value, lineno, session.stacks.size(), "eliminated");
        } else if (key == "places") {
            session.places = parse_int_list(value, lineno);
        } else if (key == "prizes_earned") {
            session.prizes_earned = parse_int_list(value, lineno);
        } else if (key == "events") {
            want_events = parse_int(value, lineno);
            if (want_events < 0) {
                throw std::invalid_argument("line " + std::to_string(lineno) +
                                            ": events cannot be negative");
            }
            saw_events = true;
        } else {
            try {
                apply_game_key(session.game, key, value, lineno);
            } catch (const std::invalid_argument& e) {
                throw std::invalid_argument(std::string(e.what()));
            }
        }
    }
    if (!saw_version) {
        throw std::invalid_argument("missing required key 'format_version'");
    }
    if (!saw_mode) {
        throw std::invalid_argument("missing required key 'mode'");
    }
    if (!saw_stacks) {
        throw std::invalid_argument("missing required key 'stacks'");
    }
    if (!saw_button) {
        throw std::invalid_argument("missing required key 'button'");
    }
    if (!saw_events) {
        throw std::invalid_argument("missing required key 'events'");
    }
    // Version 1 predates the kill key: it defaults to no kill, and a v1
    // file carrying it is rejected rather than silently extended.
    if (session.format_version == 1 && kill_line > 0) {
        throw std::invalid_argument("line " + std::to_string(kill_line) +
                                    ": kill_pending needs format_version 2");
    }
    // Seat-count cross-checks (line-numbered where the count is known).
    const std::size_t n = static_cast<std::size_t>(session.game.num_players);
    if (session.stacks.size() != n) {
        throw std::invalid_argument("stacks needs one entry per seat");
    }
    if (session.sitting_out.empty()) {
        session.sitting_out.assign(n, false);
    }
    if (session.places.size() != n && session.tournament) {
        // Cash sessions omit books; tournaments must carry every ledger.
        if (!session.places.empty()) {
            throw std::invalid_argument("places needs one entry per seat");
        }
    }
    if (session.tournament) {
        if (session.eliminated.size() != n) {
            throw std::invalid_argument("missing required key 'eliminated'");
        }
        if (session.places.size() != n) {
            throw std::invalid_argument("missing required key 'places'");
        }
        if (session.prizes_earned.size() != n) {
            throw std::invalid_argument("missing required key 'prizes_earned'");
        }
        for (int chips : session.stacks) {
            if (chips < 0) {
                throw std::invalid_argument("stacks cannot be negative");
            }
        }
        TournamentConfig books;
        books.game = session.game;
        books.levels = session.levels;
        books.prizes = session.prizes;
        books.buy_in = session.buy_in;
        try {
            validate_tournament(books);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(std::string("invalid tournament: ") +
                                        e.what());
        }
        if (session.levels.empty()) {
            session.levels.push_back(BlindLevel{50, 100, 0, 10});
        }
        if (session.level_index < 0 ||
            session.level_index >= static_cast<int>(session.levels.size()) ||
            session.hands_into_level < 0 || session.prize_pool < 0 ||
            session.prize_awarded < 0) {
            throw std::invalid_argument("tournament books out of range");
        }
    } else {
        try {
            validate(session.game);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(std::string("invalid config: ") +
                                        e.what());
        }
    }
    if (session.button < 0 ||
        session.button >= static_cast<int>(n)) {
        throw std::invalid_argument("button out of range");
    }
    if (static_cast<int>(log_lines.size()) != want_events) {
        throw std::invalid_argument(
            "events says " + std::to_string(want_events) + " but found " +
            std::to_string(log_lines.size()));
    }
    for (const std::string& raw : log_lines) {
        try {
            session.events.push_back(parse_event(raw, session.game));
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(std::string("bad log line: ") +
                                        e.what());
        }
    }
    return session;
}

SessionFile load_session_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::invalid_argument("cannot open session file '" + path + "'");
    }
    try {
        return parse_session(file);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

void save_session_file(const SessionFile& session, std::ostream& out) {
    // Writers always emit the current version, upgrading v1 reads.
    out << "# CardEngine session file (format 2)\n";
    out << "format_version = 2\n";
    out << "mode = " << (session.tournament ? "tournament" : "cash") << "\n";
    write_game_config(session.game, out);
    out << "stacks = ";
    write_ints(out, session.stacks);
    out << "sitting_out = ";
    write_flags(out, session.sitting_out);
    out << "button = " << session.button << "\n";
    out << "kill_pending = " << (session.kill_pending ? 1 : 0) << "\n";
    if (session.tournament) {
        out << "buy_in = " << session.buy_in << "\n";
        out << "prizes = ";
        for (std::size_t i = 0; i < session.prizes.size(); ++i) {
            if (i > 0) out << ", ";
            out << session.prizes[i];
        }
        out << "\n";
        for (const BlindLevel& level : session.levels) {
            out << "level = " << level.small_blind << ", " << level.big_blind
                << ", " << level.ante << ", " << level.hands;
            if (level.minutes > 0) out << ", " << level.minutes;
            out << "\n";
        }
        out << "level_index = " << session.level_index << "\n";
        out << "hands_into_level = " << session.hands_into_level << "\n";
        out << "level_elapsed = " << session.level_elapsed << "\n";
        out << "prize_pool = " << session.prize_pool << "\n";
        out << "prize_awarded = " << session.prize_awarded << "\n";
        out << "eliminated = ";
        write_flags(out, session.eliminated);
        out << "places = ";
        write_ints(out, session.places);
        out << "prizes_earned = ";
        write_ints(out, session.prizes_earned);
    }
    out << "events = " << session.events.size() << "\n";
    for (const Event& e : session.events) out << format_event(e) << "\n";
}

}  // namespace cardengine
