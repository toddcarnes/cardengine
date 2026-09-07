#include "cardengine/game_file.h"

#include <fstream>
#include <stdexcept>

#include "cardengine/detail/kv.h"

namespace cardengine {

using detail::lower;
using detail::parse_int;
using detail::trim;
using detail::unquote;

GameFile parse_game(std::istream& in) {
    GameFile game;
    bool saw_version = false;
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
            game.format_version = parse_int(value, lineno);
            if (game.format_version != 1) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": unsupported format_version (want 1)");
            }
            saw_version = true;
        } else if (key == "name") {
            game.name = unquote(value);
        } else if (key == "description") {
            game.description = unquote(value);
        } else {
            apply_game_key(game.config, key, value, lineno);
        }
    }
    if (!saw_version) {
        throw std::invalid_argument("missing required key 'format_version'");
    }
    try {
        validate(game.config);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string("invalid config: ") + e.what());
    }
    return game;
}

void apply_game_key(GameConfig& config, const std::string& key,
                    const std::string& value, int lineno) {
    if (key == "num_players") {
        config.num_players = parse_int(value, lineno);
    } else if (key == "starting_stack") {
        config.starting_stack = parse_int(value, lineno);
    } else if (key == "small_blind") {
        config.small_blind = parse_int(value, lineno);
    } else if (key == "big_blind") {
        config.big_blind = parse_int(value, lineno);
    } else if (key == "ante") {
        config.ante = parse_int(value, lineno);
    } else if (key == "ante_from") {
        const std::string a = lower(value);
        if (a == "seats" || a == "every_seat" || a == "everyseat") {
            config.ante_from = AnteSource::EverySeat;
        } else if (a == "button" || a == "button_only" || a == "buttononly") {
            config.ante_from = AnteSource::ButtonOnly;
        } else {
            throw std::invalid_argument(
                "line " + std::to_string(lineno) +
                ": ante_from must be seats or button");
        }
    } else if (key == "straddle") {
        config.straddle = parse_int(value, lineno);
    } else if (key == "kill") {
        const std::string k = lower(value);
        if (k == "on" || k == "yes" || k == "true" || k == "1") {
            config.kill = true;
        } else if (k == "off" || k == "no" || k == "false" || k == "0") {
            config.kill = false;
        } else {
            throw std::invalid_argument(
                "line " + std::to_string(lineno) +
                ": kill must be on or off");
        }
    } else if (key == "hole_cards") {
        config.hole_cards = parse_int(value, lineno);
    } else if (key == "board_cards") {
        config.board_cards = parse_int(value, lineno);
    } else if (key == "betting") {
        const std::string b = lower(value);
        if (b == "nolimit") {
            config.betting = BettingStructure::NoLimit;
        } else if (b == "limit") {
            config.betting = BettingStructure::Limit;
        } else if (b == "potlimit") {
            config.betting = BettingStructure::PotLimit;
        } else {
            throw std::invalid_argument(
                "line " + std::to_string(lineno) +
                ": betting must be nolimit, limit, or potlimit");
        }
    } else if (key == "showdown") {
        const std::string s = lower(value);
        if (s == "holdem") {
            config.showdown = HandConstruction::BestFiveOfAll;
        } else if (s == "omaha") {
            config.showdown = HandConstruction::OmahaTwoAndThree;
        } else if (s == "omaha_hilo" || s == "omahahilo" || s == "omaha-hilo") {
            config.showdown = HandConstruction::OmahaHiLo;
        } else {
            throw std::invalid_argument(
                "line " + std::to_string(lineno) +
                ": showdown must be holdem, omaha, or omaha_hilo");
        }
    } else if (key == "max_raises") {
        config.max_raises_per_round = parse_int(value, lineno);
    } else {
        throw std::invalid_argument("line " + std::to_string(lineno) +
                                    ": unknown key '" + key + "'");
    }
}

void write_game_config(const GameConfig& config, std::ostream& out) {
    out << "num_players = " << config.num_players << "\n";
    out << "starting_stack = " << config.starting_stack << "\n";
    out << "small_blind = " << config.small_blind << "\n";
    out << "big_blind = " << config.big_blind << "\n";
    out << "ante = " << config.ante << "\n";
    out << "ante_from = "
        << (config.ante_from == AnteSource::ButtonOnly ? "button" : "seats")
        << "\n";
    out << "straddle = " << config.straddle << "\n";
    out << "kill = " << (config.kill ? "on" : "off") << "\n";
    out << "hole_cards = " << config.hole_cards << "\n";
    out << "board_cards = " << config.board_cards << "\n";
    out << "betting = ";
    switch (config.betting) {
        case BettingStructure::NoLimit:
            out << "nolimit\n";
            break;
        case BettingStructure::Limit:
            out << "limit\n";
            break;
        case BettingStructure::PotLimit:
            out << "potlimit\n";
            break;
    }
    out << "showdown = ";
    switch (config.showdown) {
        case HandConstruction::BestFiveOfAll:
            out << "holdem\n";
            break;
        case HandConstruction::OmahaTwoAndThree:
            out << "omaha\n";
            break;
        case HandConstruction::OmahaHiLo:
            out << "omaha_hilo\n";
            break;
    }
    out << "max_raises = " << config.max_raises_per_round << "\n";
}

GameFile load_game_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::invalid_argument("cannot open game file '" + path + "'");
    }
    try {
        return parse_game(file);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

void save_game_file(const GameFile& game, std::ostream& out) {
    out << "# CardEngine game file (format " << game.format_version << ")\n";
    out << "format_version = " << game.format_version << "\n";
    out << "name = \"" << game.name << "\"\n";
    out << "description = \"" << game.description << "\"\n";
    write_game_config(game.config, out);
}

}  // namespace cardengine
