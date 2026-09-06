#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/tournament.h"

namespace cardengine {

// One bracket slot: which stage table feeds a seat. Stage -1 means an
// original entrant (no prior table to win).
struct BracketSlot {
    int stage = -1;
    int table = -1;
};

// One stage: identical tournament tables played in parallel (in turn).
struct BracketStage {
    std::string file;  // Stage file as written (for save round-trips).
    TournamentConfig tournament;
    int tables = 1;
};

struct ChampionshipConfig {
    std::vector<BracketStage> stages;
    int champion_prize = 0;  // Flat award recorded for the champion.
};

void validate_championship(const ChampionshipConfig& config);

// A fixed bracket of ordinary tournaments. Each stage's winners fill the
// next stage's seats in order (table 0's winner takes the first seat, and
// so on); stacks reset every stage. The last stage is a single table whose
// winner is the champion.
//
// The host drives hands through the normal Table API. Touching a table
// whose prior stage is incomplete throws: winners must exist before they
// can advance. Player identity stays host-side (seats are numbers here);
// source() maps every seat to its bracket slot for display.
class Championship {
public:
    explicit Championship(const ChampionshipConfig& config);

    int num_stages() const;
    int num_tables(int stage) const;
    // Throws std::invalid_argument on bad indices, std::logic_error while
    // an earlier stage is still playing.
    Tournament& tournament(int stage, int table);
    const Tournament& tournament(int stage, int table) const;

    bool stage_complete(int stage) const;
    bool complete() const;
    int champion() const;  // Final winner; throws unless complete().
    int champion_prize() const { return config_.champion_prize; }

    // Bracket slot feeding a seat: original entrants report {-1, -1}.
    // `seat` is the stage-global seat index (table 0's seats first).
    // Pure bracket math (no state); throws once winners are needed but
    // the prior stage is incomplete.
    BracketSlot source(int stage, int seat) const;

private:
    void check_position(int stage, int table) const;

    ChampionshipConfig config_;
    std::vector<std::vector<std::unique_ptr<Tournament>>> tournaments_;
};

// Upper bound on the work a bracket needs. Tournaments/tables/entrants are
// exact; max_hands bounds total hands (every hand costs at least its
// blinds, so chips divided by blinds caps the run).
struct ChampionshipEstimate {
    int stages = 0;
    int tournaments = 0;
    int entrants = 0;
    long long max_hands = 0;
};

ChampionshipEstimate estimate_championship(const ChampionshipConfig& config);

// Shortcut: a level-X championship all using one game. Depth 0 is a single
// tournament; depth d opens P^d tables of P seats, halving (by seats) each
// round down to one final. `level`/`prizes`/`buy_in`/game keys in `spec`
// apply to every generated table; anything unsaid stays at file defaults
// (one long level, no stage prizes).
struct GeneratedBracket {
    GameConfig game;
    int depth = 1;
    BlindLevel level = {50, 100, 0, 1000000};
    std::vector<int> prizes;
    int buy_in = 0;
    int champion_prize = 0;
};

ChampionshipConfig generate_championship(const GeneratedBracket& spec);

// Shareable championship file. Either explicit stages or the shortcut
// (never both):
//
//   format_version = 1
//   name = "Winter Classic"
//   champion_prize = 50000
//   stage = semifinal.txt, 2
//   stage = final-headsup.txt, 1
//
//   format_version = 1
//   name = "Heads-up Cup"
//   game = ../games/holdem-headsup.txt
//   depth = 2
//   buy_in = 1000
//   prizes = 70, 30
struct ChampionshipFile {
    int format_version = 1;
    std::string name;
    std::string description;
    ChampionshipConfig config;
    // Shortcut echo for save round-trips (empty unless parsed from one).
    std::string shortcut_game;
    int shortcut_depth = -1;
};

ChampionshipFile parse_championship(std::istream& in,
                                    const std::string& base_dir = "");
ChampionshipFile load_championship_file(const std::string& path);
void save_championship_file(const ChampionshipFile& championship,
                            std::ostream& out);

}  // namespace cardengine
