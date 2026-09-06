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
        } else if (key == "num_players") {
            game.config.num_players = parse_int(value, lineno);
        } else if (key == "starting_stack") {
            game.config.starting_stack = parse_int(value, lineno);
        } else if (key == "small_blind") {
            game.config.small_blind = parse_int(value, lineno);
        } else if (key == "big_blind") {
            game.config.big_blind = parse_int(value, lineno);
        } else if (key == "ante") {
            game.config.ante = parse_int(value, lineno);
        } else if (key == "hole_cards") {
            game.config.hole_cards = parse_int(value, lineno);
        } else if (key == "board_cards") {
            game.config.board_cards = parse_int(value, lineno);
        } else if (key == "betting") {
            const std::string b = lower(value);
            if (b == "nolimit") {
                game.config.betting = BettingStructure::NoLimit;
            } else if (b == "limit") {
                game.config.betting = BettingStructure::Limit;
            } else if (b == "potlimit") {
                game.config.betting = BettingStructure::PotLimit;
            } else {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": betting must be nolimit, limit, or potlimit");
            }
        } else if (key == "showdown") {
            const std::string s = lower(value);
            if (s == "holdem") {
                game.config.showdown = HandConstruction::BestFiveOfAll;
            } else if (s == "omaha") {
                game.config.showdown = HandConstruction::OmahaTwoAndThree;
            } else {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": showdown must be holdem or omaha");
            }
        } else if (key == "max_raises") {
            game.config.max_raises_per_round = parse_int(value, lineno);
        } else {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": unknown key '" + key + "'");
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
    out << "num_players = " << game.config.num_players << "\n";
    out << "starting_stack = " << game.config.starting_stack << "\n";
    out << "small_blind = " << game.config.small_blind << "\n";
    out << "big_blind = " << game.config.big_blind << "\n";
    out << "ante = " << game.config.ante << "\n";
    out << "hole_cards = " << game.config.hole_cards << "\n";
    out << "board_cards = " << game.config.board_cards << "\n";
    out << "betting = ";
    switch (game.config.betting) {
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
    switch (game.config.showdown) {
        case HandConstruction::BestFiveOfAll:
            out << "holdem\n";
            break;
        case HandConstruction::OmahaTwoAndThree:
            out << "omaha\n";
            break;
    }
    out << "max_raises = " << game.config.max_raises_per_round << "\n";
}

}  // namespace cardengine
