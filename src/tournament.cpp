#include "cardengine/tournament.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "cardengine/detail/kv.h"
#include "cardengine/game_file.h"

namespace cardengine {

using detail::lower;
using detail::parse_int;
using detail::parse_int_list;
using detail::trim;
using detail::unquote;

namespace {

namespace fs = std::filesystem;

BlindLevel parse_level(const std::string& value, int lineno) {
    const std::vector<int> parts = parse_int_list(value, lineno);
    if (parts.size() != 4) {
        throw std::invalid_argument(
            "line " + std::to_string(lineno) +
            ": level needs 4 numbers: small, big, ante, hands");
    }
    return BlindLevel{parts[0], parts[1], parts[2], parts[3]};
}

}  // namespace

void validate_tournament(const TournamentConfig& config) {
    validate(config.game);
    if (config.levels.empty()) {
        throw std::invalid_argument("tournament needs at least one level");
    }
    for (const BlindLevel& level : config.levels) {
        if (level.small_blind < 1 || level.big_blind < 1) {
            throw std::invalid_argument("level blinds must be positive");
        }
        if (level.small_blind > level.big_blind) {
            throw std::invalid_argument("level small blind exceeds big blind");
        }
        if (level.ante < 0) {
            throw std::invalid_argument("level ante cannot be negative");
        }
        if (level.hands < 1) {
            throw std::invalid_argument("level must last at least 1 hand");
        }
    }
    int total = 0;
    for (int prize : config.prizes) {
        if (prize < 0 || prize > 100) {
            throw std::invalid_argument("prizes must be 0..100 each");
        }
        total += prize;
    }
    if (total > 100) {
        throw std::invalid_argument("prizes must sum to at most 100");
    }
    if (config.buy_in < 0) {
        throw std::invalid_argument("buy_in cannot be negative");
    }
}

Tournament::Tournament(const TournamentConfig& config)
    : config_(config), table_(config_.game) {
    validate_tournament(config_);
    const int n = config_.game.num_players;
    eliminated_.assign(static_cast<std::size_t>(n), false);
    places_.assign(static_cast<std::size_t>(n), 0);
    prizes_.assign(static_cast<std::size_t>(n), 0);
    prize_pool_ = config_.buy_in * n;
}

void Tournament::begin_hand(std::uint64_t seed) {
    if (complete()) throw std::logic_error("tournament is over");
    const BlindLevel current = level();
    table_.set_blinds(current.small_blind, current.big_blind);
    table_.set_ante(current.ante);
    table_.start_hand(seed);
    hand_open_ = true;
}

void Tournament::begin_hand_from_deck(std::vector<Card> top_first) {
    if (complete()) throw std::logic_error("tournament is over");
    const BlindLevel current = level();
    table_.set_blinds(current.small_blind, current.big_blind);
    table_.set_ante(current.ante);
    table_.start_hand_from_deck(std::move(top_first));
    hand_open_ = true;
}

void Tournament::finish_hand() {
    if (!hand_open_) {
        throw std::logic_error("no open hand to finish");
    }
    if (table_.street() != Street::Complete) {
        throw std::logic_error("settle the hand first");
    }
    const int n = config_.game.num_players;
    for (int seat = 0; seat < n; ++seat) {
        if (eliminated_[static_cast<std::size_t>(seat)]) continue;
        if (!table_.in_hand(seat) || table_.stack(seat) > 0) continue;
        // Broke: finishing place is everyone still standing, self included.
        int place = 0;
        for (int i = 0; i < n; ++i) {
            if (!eliminated_[static_cast<std::size_t>(i)]) ++place;
        }
        eliminated_[static_cast<std::size_t>(seat)] = true;
        places_[static_cast<std::size_t>(seat)] = place;
        // Simultaneous busts take places in seat order (documented).
        if (place >= 1 &&
            static_cast<std::size_t>(place) <= config_.prizes.size()) {
            const int award = prize_pool_ *
                              config_.prizes[static_cast<std::size_t>(place - 1)] /
                              100;
            prizes_[static_cast<std::size_t>(seat)] = award;
            prize_awarded_ += award;
        }
    }
    ++hands_into_level_;
    const BlindLevel& current = level();
    if (hands_into_level_ >= current.hands &&
        level_index_ + 1 < static_cast<int>(config_.levels.size())) {
        ++level_index_;
        hands_into_level_ = 0;
    }
    hand_open_ = false;
}

void Tournament::advance_level() {
    if (level_index_ + 1 < static_cast<int>(config_.levels.size())) {
        ++level_index_;
        hands_into_level_ = 0;
    }
}

void Tournament::rebuy(int seat) {
    if (seat < 0 || seat >= config_.game.num_players) {
        throw std::invalid_argument("seat out of range");
    }
    if (complete()) throw std::logic_error("tournament is over");
    if (table_.street() != Street::None &&
        table_.street() != Street::Complete) {
        throw std::logic_error("no rebuys mid-hand");
    }
    if (table_.sitting_out(seat)) {
        throw std::logic_error("seat is sitting out");
    }
    table_.set_stack(seat, table_.stack(seat) + config_.game.starting_stack);
    prize_pool_ += config_.buy_in;
    // A returning player vacates their recorded finish (and prize).
    if (eliminated_[static_cast<std::size_t>(seat)]) {
        prize_awarded_ -= prizes_[static_cast<std::size_t>(seat)];
        prizes_[static_cast<std::size_t>(seat)] = 0;
        places_[static_cast<std::size_t>(seat)] = 0;
        eliminated_[static_cast<std::size_t>(seat)] = false;
    }
}

void Tournament::chop(const std::vector<Payout>& deal) {
    if (complete()) throw std::logic_error("tournament is over");
    if (hand_open_ || (table_.street() != Street::None &&
                       table_.street() != Street::Complete)) {
        throw std::logic_error("no deals mid-hand");
    }
    const int n = config_.game.num_players;
    // The survivors: everyone not already eliminated.
    std::vector<int> survivors;
    for (int seat = 0; seat < n; ++seat) {
        if (!eliminated_[static_cast<std::size_t>(seat)]) survivors.push_back(seat);
    }
    if (survivors.size() < 2) throw std::logic_error("nobody left to deal with");
    if (deal.size() != survivors.size()) {
        throw std::invalid_argument("deal must cover every surviving seat");
    }
    std::vector<bool> seen(static_cast<std::size_t>(n), false);
    int total = 0;
    for (const Payout& p : deal) {
        if (p.seat < 0 || p.seat >= n) {
            throw std::invalid_argument("deal seat out of range");
        }
        if (eliminated_[static_cast<std::size_t>(p.seat)]) {
            throw std::invalid_argument("deal covers an eliminated seat");
        }
        if (seen[static_cast<std::size_t>(p.seat)]) {
            throw std::invalid_argument("deal lists a seat twice");
        }
        seen[static_cast<std::size_t>(p.seat)] = true;
        if (p.amount < 0) throw std::invalid_argument("deal amounts cannot be negative");
        total += p.amount;
    }
    if (total != prize_pool_ - prize_awarded_) {
        throw std::invalid_argument("deal must split the remaining pool exactly");
    }
    // Places by stack, leader first; seat order breaks ties (documented).
    // Bust-order places are recomputed: the dealers take 1..S by stack,
    // earlier busts slide below in their bust order.
    std::vector<int> order = survivors;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (table_.stack(a) != table_.stack(b)) return table_.stack(a) > table_.stack(b);
        return a < b;
    });
    std::vector<int> busts;
    for (int seat = 0; seat < n; ++seat) {
        if (eliminated_[static_cast<std::size_t>(seat)]) busts.push_back(seat);
    }
    std::sort(busts.begin(), busts.end(), [&](int a, int b) {
        return places_[static_cast<std::size_t>(a)] < places_[static_cast<std::size_t>(b)];
    });
    int place = 1;
    for (int seat : order) {
        eliminated_[static_cast<std::size_t>(seat)] = true;
        places_[static_cast<std::size_t>(seat)] = place++;
    }
    for (int seat : busts) {
        places_[static_cast<std::size_t>(seat)] = place++;
    }
    for (const Payout& p : deal) {
        prizes_[static_cast<std::size_t>(p.seat)] = p.amount;
        prize_awarded_ += p.amount;
    }
}

bool Tournament::complete() const {
    int alive = 0;
    for (bool out : eliminated_) {
        if (!out) ++alive;
    }
    return alive <= 1;
}

int Tournament::winner() const {
    if (!complete()) throw std::logic_error("tournament is not complete");
    for (int i = 0; i < config_.game.num_players; ++i) {
        if (!eliminated_[static_cast<std::size_t>(i)]) return i;
    }
    throw std::logic_error("no winner without a tournament");
}

std::vector<Tournament::Standing> Tournament::standings() const {
    std::vector<Standing> out;
    const bool done = complete();
    for (int i = 0; i < config_.game.num_players; ++i) {
        Standing standing;
        standing.seat = i;
        standing.stack = table_.stack(i);
        standing.eliminated = eliminated_[static_cast<std::size_t>(i)];
        standing.finish_place = places_[static_cast<std::size_t>(i)];
        standing.prize = prizes_[static_cast<std::size_t>(i)];
        if (done && !standing.eliminated) {
            standing.finish_place = 1;
            standing.prize = prize_pool_ - prize_awarded_;
        }
        out.push_back(standing);
    }
    return out;
}

std::vector<int> finishing_order(const Tournament& event) {
    if (!event.complete()) {
        throw std::logic_error("tournament is not complete");
    }
    std::vector<Tournament::Standing> table = event.standings();
    std::sort(table.begin(), table.end(), [](const Tournament::Standing& a,
                                             const Tournament::Standing& b) {
        return a.finish_place < b.finish_place;
    });
    std::vector<int> order;
    order.reserve(table.size());
    for (const Tournament::Standing& standing : table) {
        order.push_back(standing.seat);
    }
    return order;
}

TournamentFile parse_tournament(std::istream& in, const std::string& base_dir) {
    TournamentFile file;
    file.config.levels.clear();  // Levels come only from `level` lines.
    bool saw_version = false;
    std::string game_ref;
    std::vector<std::pair<std::string, std::string>> game_keys;
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
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
            file.format_version = parse_int(value, lineno);
            if (file.format_version != 1) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": unsupported format_version (want 1)");
            }
            saw_version = true;
        } else if (key == "name") {
            file.name = unquote(value);
        } else if (key == "description") {
            file.description = unquote(value);
        } else if (key == "game") {
            game_ref = unquote(value);
        } else if (key == "level") {
            file.config.levels.push_back(parse_level(value, lineno));
        } else if (key == "prizes") {
            file.config.prizes = parse_int_list(value, lineno);
        } else if (key == "buy_in") {
            file.config.buy_in = parse_int(value, lineno);
        } else {
            // A game rule: applied after the base game file loads, so
            // tournament files override cleanly. Validated on apply.
            try {
                apply_game_key(file.config.game, key, value, lineno);
            } catch (const std::invalid_argument& e) {
                throw std::invalid_argument(std::string(e.what()));
            }
            game_keys.emplace_back(key, value);
        }
    }
    if (!saw_version) {
        throw std::invalid_argument("missing required key 'format_version'");
    }
    if (!game_ref.empty()) {
        fs::path game_path = game_ref;
        if (game_path.is_relative() && !base_dir.empty()) {
            game_path = fs::path(base_dir) / game_path;
        }
        const GameFile base = load_game_file(game_path.string());
        file.config.game = base.config;
        // Re-apply overrides in file order on top of the base game.
        int override_line = 0;
        for (const auto& [key, value] : game_keys) {
            ++override_line;
            apply_game_key(file.config.game, key, value, override_line);
        }
    }
    // A tournament file without levels keeps the default single level.
    if (file.config.levels.empty()) {
        file.config.levels.push_back(BlindLevel{50, 100, 0, 10});
    }
    try {
        validate_tournament(file.config);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string("invalid tournament: ") +
                                    e.what());
    }
    return file;
}

TournamentFile load_tournament_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::invalid_argument("cannot open tournament file '" + path +
                                    "'");
    }
    try {
        const std::string base_dir = fs::path(path).parent_path().string();
        return parse_tournament(file, base_dir);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

void save_tournament_file(const TournamentFile& tournament,
                          std::ostream& out) {
    out << "# CardEngine tournament file (format "
        << tournament.format_version << ")\n";
    out << "format_version = " << tournament.format_version << "\n";
    out << "name = \"" << tournament.name << "\"\n";
    out << "description = \"" << tournament.description << "\"\n";
    write_game_config(tournament.config.game, out);
    out << "buy_in = " << tournament.config.buy_in << "\n";
    out << "prizes = ";
    for (std::size_t i = 0; i < tournament.config.prizes.size(); ++i) {
        if (i > 0) out << ", ";
        out << tournament.config.prizes[i];
    }
    out << "\n";
    for (const BlindLevel& level : tournament.config.levels) {
        out << "level = " << level.small_blind << ", " << level.big_blind
            << ", " << level.ante << ", " << level.hands << "\n";
    }
}

}  // namespace cardengine
