#pragma once

#include <istream>
#include <ostream>
#include <string>

#include "cardengine/config.h"

namespace cardengine {

// A shareable game variant: metadata for listing plus the rules config.
// Text format is plain `key = value` lines (`#` comments, blank lines
// ignored) so users can write and email them without tooling:
//
//   format_version = 1
//   name = "No-Limit Hold'em 6-max"
//   description = "Standard cash game"
//   num_players = 6
//   starting_stack = 10000
//   small_blind = 50
//   big_blind = 100
//   ante = 0
//   ante_from = seats        # seats | button (button posts the whole ante)
//   straddle = 0             # 0 = off, else twice the big blind (UTG, live)
//   kill = off               # on = double blinds after a 10xBB pot
//   hole_cards = 2
//   board_cards = 5
//   betting = nolimit          # nolimit | limit | potlimit
//   showdown = holdem           # holdem | omaha (exactly 2+3) | omaha_hilo
//   max_raises = 4              # limit betting: bets-per-round cap
//
// Omitted rules keys fall back to GameConfig defaults. Shared files are
// untrusted input: every failure reports its line number and no bad file
// ever produces a half-parsed config.
struct GameFile {
    int format_version = 1;
    std::string name;
    std::string description;
    GameConfig config;
};

// Throws std::invalid_argument ("line N: ..." or "invalid config: ...").
GameFile parse_game(std::istream& in);
GameFile load_game_file(const std::string& path);

void save_game_file(const GameFile& game, std::ostream& out);

// Shared with tournament files: applies one lowered `key` + trimmed `value`
// to a GameConfig, or throws a line-numbered error (including "unknown key").
void apply_game_key(GameConfig& config, const std::string& key,
                    const std::string& value, int lineno);

// Shared writer for the game-rule lines (used by both file kinds).
void write_game_config(const GameConfig& config, std::ostream& out);

}  // namespace cardengine
