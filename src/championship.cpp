#include "cardengine/championship.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
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

// Generous but finite: generated brackets never escalate, so one long level
// covers any realistic run while keeping every count exact.
constexpr int kGeneratedLevelHands = 1000000;
// Bracket sizes explode (P seats ^ depth tables); refuse absurdity early.
constexpr int kMaxBracketTables = 4096;

int int_pow(int base, int exp) {
    long long result = 1;
    for (int i = 0; i < exp; ++i) {
        result *= base;
        if (result > kMaxBracketTables) return kMaxBracketTables + 1;
    }
    return static_cast<int>(result);
}

}  // namespace

void validate_championship(const ChampionshipConfig& config) {
    if (config.stages.empty()) {
        throw std::invalid_argument("championship needs at least one stage");
    }
    if (config.champion_prize < 0) {
        throw std::invalid_argument("champion_prize cannot be negative");
    }
    for (const BracketStage& stage : config.stages) {
        if (stage.tables < 1) {
            throw std::invalid_argument("each stage needs at least 1 table");
        }
        try {
            validate_tournament(stage.tournament);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(std::string("bad stage game: ") +
                                        e.what());
        }
    }
    // Winners of each stage must exactly fill the next stage's seats.
    for (std::size_t k = 0; k + 1 < config.stages.size(); ++k) {
        int seats_next = 0;
        for (int t = 0; t < config.stages[k + 1].tables; ++t) {
            seats_next +=
                config.stages[k + 1].tournament.game.num_players;
        }
        if (config.stages[k].tables != seats_next) {
            throw std::invalid_argument(
                "stage " + std::to_string(k) + " produces " +
                std::to_string(config.stages[k].tables) +
                " winners for " + std::to_string(seats_next) + " seats");
        }
    }
    // One final table, or there is no single champion.
    if (config.stages.back().tables != 1) {
        throw std::invalid_argument("the final stage must be 1 table");
    }
}

Championship::Championship(const ChampionshipConfig& config)
    : config_(config) {
    validate_championship(config_);
    for (const BracketStage& stage : config_.stages) {
        std::vector<std::unique_ptr<Tournament>> tables;
        for (int t = 0; t < stage.tables; ++t) {
            tables.push_back(
                std::make_unique<Tournament>(stage.tournament));
        }
        tournaments_.push_back(std::move(tables));
    }
}

int Championship::num_stages() const {
    return static_cast<int>(config_.stages.size());
}

int Championship::num_tables(int stage) const {
    if (stage < 0 || stage >= num_stages()) {
        throw std::invalid_argument("stage out of range");
    }
    return config_.stages[static_cast<std::size_t>(stage)].tables;
}

void Championship::check_position(int stage, int table) const {
    if (stage < 0 || stage >= num_stages()) {
        throw std::invalid_argument("stage out of range");
    }
    if (table < 0 || table >= num_tables(stage)) {
        throw std::invalid_argument("table out of range");
    }
    for (int k = 0; k < stage; ++k) {
        if (!stage_complete(k)) {
            throw std::logic_error("earlier stage still playing");
        }
    }
}

Tournament& Championship::tournament(int stage, int table) {
    check_position(stage, table);
    return *tournaments_[static_cast<std::size_t>(stage)]
                        [static_cast<std::size_t>(table)];
}

const Tournament& Championship::tournament(int stage, int table) const {
    check_position(stage, table);
    return *tournaments_[static_cast<std::size_t>(stage)]
                        [static_cast<std::size_t>(table)];
}

bool Championship::stage_complete(int stage) const {
    if (stage < 0 || stage >= num_stages()) {
        throw std::invalid_argument("stage out of range");
    }
    for (const auto& table :
         tournaments_[static_cast<std::size_t>(stage)]) {
        if (!table->complete()) return false;
    }
    return true;
}

bool Championship::complete() const {
    return stage_complete(num_stages() - 1);
}

int Championship::champion() const {
    if (!complete()) throw std::logic_error("championship is not complete");
    return tournaments_.back()[0]->winner();
}

BracketSlot Championship::source(int stage, int seat) const {
    if (stage < 0 || stage >= num_stages()) {
        throw std::invalid_argument("stage out of range");
    }
    if (stage == 0) {
        if (seat < 0 ||
            seat >= num_tables(0) *
                        config_.stages[0].tournament.game.num_players) {
            throw std::invalid_argument("seat out of range");
        }
        return BracketSlot{-1, -1};
    }
    // Feed math guarantees stage seats == prior tables, so seat g is filled
    // by the winner of prior table g, in order.
    if (seat < 0 || seat >= num_tables(stage - 1)) {
        throw std::invalid_argument("seat out of range");
    }
    if (!stage_complete(stage - 1)) {
        throw std::logic_error("prior stage still playing");
    }
    return BracketSlot{stage - 1, seat};
}

ChampionshipEstimate estimate_championship(const ChampionshipConfig& config) {
    validate_championship(config);
    ChampionshipEstimate estimate;
    estimate.stages = static_cast<int>(config.stages.size());
    if (!config.stages.empty()) {
        estimate.entrants = config.stages[0].tournament.game.num_players *
                            config.stages[0].tables;
    }
    long long max_hands = 0;
    for (const BracketStage& stage : config.stages) {
        const int seats = stage.tournament.game.num_players;
        const long long chips =
            static_cast<long long>(stage.tournament.game.starting_stack) *
            seats;
        const long long per_hand = static_cast<long long>(
                                       stage.tournament.levels[0].small_blind +
                                       stage.tournament.levels[0].big_blind);
        estimate.tournaments += stage.tables;
        // Every hand costs at least its blinds: chips/blinds bounds hands.
        max_hands += static_cast<long long>(stage.tables) *
                     (chips / (per_hand > 0 ? per_hand : 1) + 1);
    }
    if (max_hands > std::numeric_limits<int>::max()) {
        max_hands = std::numeric_limits<int>::max();
    }
    estimate.max_hands = max_hands;
    return estimate;
}

ChampionshipConfig generate_championship(const GeneratedBracket& spec) {
    if (spec.depth < 0) {
        throw std::invalid_argument("depth cannot be negative");
    }
    validate(spec.game);
    const int seats = spec.game.num_players;
    const int openers = int_pow(seats, spec.depth);
    if (openers > kMaxBracketTables) {
        throw std::invalid_argument(
            "bracket too large: lower depth (or fewer seats)");
    }
    ChampionshipConfig config;
    int tables = openers;
    while (tables >= 1) {
        BracketStage stage;
        stage.tables = tables;
        stage.tournament.game = spec.game;
        stage.tournament.levels = {spec.level};
        stage.tournament.prizes = spec.prizes;
        stage.tournament.buy_in = spec.buy_in;
        validate_tournament(stage.tournament);
        config.stages.push_back(std::move(stage));
        if (tables == 1) break;
        tables /= seats;
    }
    config.champion_prize = spec.champion_prize;
    validate_championship(config);
    return config;
}

namespace {

BlindLevel parse_champ_level(const std::string& value, int lineno) {
    const std::vector<int> parts = parse_int_list(value, lineno);
    if (parts.size() != 4) {
        throw std::invalid_argument(
            "line " + std::to_string(lineno) +
            ": level needs 4 numbers: small, big, ante, hands");
    }
    return BlindLevel{parts[0], parts[1], parts[2], parts[3]};
}

}  // namespace

ChampionshipFile parse_championship(std::istream& in,
                                    const std::string& base_dir) {
    ChampionshipFile file;
    bool saw_version = false;
    bool saw_stage = false;
    bool saw_shortcut = false;
    std::string shortcut_game;
    int shortcut_depth = -1;
    // Game-rule overrides, applied to the base game after it loads.
    std::vector<std::tuple<std::string, std::string, int>> game_overrides;
    std::vector<int> shortcut_prizes;
    bool has_prizes = false;
    BlindLevel shortcut_level{50, 100, 0, kGeneratedLevelHands};
    bool has_level = false;
    int shortcut_buy_in = 0;
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
        } else if (key == "champion_prize") {
            file.config.champion_prize = parse_int(value, lineno);
        } else if (key == "stage") {
            const std::size_t comma = value.find(',');
            if (comma == std::string::npos) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": stage needs 'file, tables'");
            }
            const std::string stage_path =
                unquote(trim(value.substr(0, comma)));
            const int tables =
                parse_int(trim(value.substr(comma + 1)), lineno);
            if (tables < 1) {
                throw std::invalid_argument("line " +
                                            std::to_string(lineno) +
                                            ": stage needs 1+ tables");
            }
            fs::path resolved = stage_path;
            if (resolved.is_relative() && !base_dir.empty()) {
                resolved = fs::path(base_dir) / resolved;
            }
            TournamentFile stage_file =
                load_tournament_file(resolved.string());
            BracketStage stage;
            stage.file = stage_path;
            stage.tournament = stage_file.config;
            stage.tables = tables;
            file.config.stages.push_back(std::move(stage));
            saw_stage = true;
        } else if (key == "game" || key == "depth" || key == "buy_in" ||
                   key == "prizes" || key == "level") {
            saw_shortcut = true;
            if (key == "game") {
                shortcut_game = unquote(value);
            } else if (key == "depth") {
                shortcut_depth = parse_int(value, lineno);
            } else if (key == "buy_in") {
                shortcut_buy_in = parse_int(value, lineno);
            } else if (key == "prizes") {
                shortcut_prizes = parse_int_list(value, lineno);
                has_prizes = true;
            } else {  // level
                shortcut_level = parse_champ_level(value, lineno);
                has_level = true;
            }
        } else {
            // A game rule: only meaningful with the shortcut (explicit
            // stages already carry complete tournament files). Validated
            // now for line numbers, applied to the base game below.
            GameConfig probe;
            try {
                apply_game_key(probe, key, value, lineno);
            } catch (const std::invalid_argument& e) {
                throw std::invalid_argument(std::string(e.what()));
            }
            game_overrides.emplace_back(key, value, lineno);
        }
    }
    if (!saw_version) {
        throw std::invalid_argument("missing required key 'format_version'");
    }
    if (saw_stage &&
        (saw_shortcut || !game_overrides.empty() || !shortcut_game.empty() ||
         shortcut_depth >= 0)) {
        throw std::invalid_argument(
            "use stage lines or the game+depth shortcut, not both");
    }
    if (!saw_stage) {
        if (shortcut_game.empty() || shortcut_depth < 0) {
            throw std::invalid_argument(
                "need stage lines or game + depth");
        }
        fs::path game_path = shortcut_game;
        if (game_path.is_relative() && !base_dir.empty()) {
            game_path = fs::path(base_dir) / game_path;
        }
        const GameFile base = load_game_file(game_path.string());
        GeneratedBracket spec;
        spec.game = base.config;
        // File-level game keys override the base game before generating.
        for (const auto& [key, value, key_line] : game_overrides) {
            apply_game_key(spec.game, key, value, key_line);
        }
        spec.depth = shortcut_depth;
        if (!has_level) {
            shortcut_level = BlindLevel{spec.game.small_blind,
                                        spec.game.big_blind, spec.game.ante,
                                        kGeneratedLevelHands};
        }
        spec.level = shortcut_level;
        if (has_prizes) spec.prizes = shortcut_prizes;
        spec.buy_in = shortcut_buy_in;
        spec.champion_prize = file.config.champion_prize;
        file.config = generate_championship(spec);
        file.shortcut_game = shortcut_game;
        file.shortcut_depth = shortcut_depth;
    }
    try {
        validate_championship(file.config);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string("invalid championship: ") +
                                    e.what());
    }
    return file;
}

ChampionshipFile load_championship_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::invalid_argument("cannot open championship file '" + path +
                                    "'");
    }
    try {
        const std::string base_dir = fs::path(path).parent_path().string();
        return parse_championship(file, base_dir);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

void save_championship_file(const ChampionshipFile& championship,
                            std::ostream& out) {
    if (championship.shortcut_depth >= 0) {
        throw std::invalid_argument(
            "generated brackets save as explicit stage lines (plus their "
            "tournament files), not as shortcuts");
    }
    out << "# CardEngine championship file (format "
        << championship.format_version << ")\n";
    out << "format_version = " << championship.format_version << "\n";
    out << "name = \"" << championship.name << "\"\n";
    out << "description = \"" << championship.description << "\"\n";
    out << "champion_prize = " << championship.config.champion_prize << "\n";
    for (const BracketStage& stage : championship.config.stages) {
        out << "stage = " << stage.file << ", " << stage.tables << "\n";
    }
}

}  // namespace cardengine
