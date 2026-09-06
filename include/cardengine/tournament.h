#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/table.h"

namespace cardengine {

// One blind level: fixed bets for `hands` completed hands, then the next.
struct BlindLevel {
    int small_blind = 50;
    int big_blind = 100;
    int ante = 0;
    int hands = 10;
};

struct TournamentConfig {
    GameConfig game;  // Base rules; level blinds/antes override per hand.
    std::vector<BlindLevel> levels = {{50, 100, 0, 10}};
    // Prize percentages by finishing place: prizes[0] is the champion's
    // share. Empty means play money (chips only). Must sum to <= 100.
    std::vector<int> prizes;
    int buy_in = 0;  // Per player, into the prize pool (0 = play money).
};

void validate_tournament(const TournamentConfig& config);

// A freezeout (or rebuy) tournament above Table: escalating levels,
// eliminations with finishing places, prize accounting. Table still plays
// the hands; this director only sets blinds, counts hands, and settles
// the books at settle time. Time-based level changes are the GUI's job
// (it calls advance_level); hands-based ones are automatic.
class Tournament {
public:
    explicit Tournament(const TournamentConfig& config);

    const TournamentConfig& config() const { return config_; }
    Table& table() { return table_; }
    const Table& table() const { return table_; }

    // Starts a hand at the current level's blinds (Table rejects mid-hand
    // starts as usual). Throws std::logic_error when one player remains.
    void begin_hand(std::uint64_t seed);
    void begin_hand_from_deck(std::vector<Card> top_first);

    // Books a settled hand: eliminations, prizes, level progress.
    // Throws std::logic_error unless the table hand is settled and unbooked.
    void finish_hand();

    // Manual level advance for GUI clocks. Sticks at the final level.
    void advance_level();

    // Adds starting_stack chips for another buy_in into the pool.
    // Between hands only; clears an elimination (with its recorded prize).
    void rebuy(int seat);

    bool complete() const;
    int winner() const;  // Throws std::logic_error unless complete().
    int level_index() const { return level_index_; }
    BlindLevel level() const { return config_.levels[static_cast<std::size_t>(level_index_)]; }
    int hands_into_level() const { return hands_into_level_; }
    int prize_pool() const { return prize_pool_; }

    struct Standing {
        int seat = -1;
        int stack = 0;
        bool eliminated = false;
        int finish_place = 0;  // 0 while still playing.
        int prize = 0;
    };
    std::vector<Standing> standings() const;

private:
    TournamentConfig config_;
    Table table_;
    int level_index_ = 0;
    int hands_into_level_ = 0;
    int prize_pool_ = 0;
    int prize_awarded_ = 0;
    bool hand_open_ = false;  // A dealt hand awaits finish_hand().
    std::vector<bool> eliminated_;
    std::vector<int> places_;
    std::vector<int> prizes_;
};

// Shareable tournament file. A `game` key composes a game file (path
// relative to the tournament file); any game keys alongside override it:
//
//   format_version = 1
//   name = "Friday Freezeout"
//   game = ../games/holdem-6max.txt
//   num_players = 6
//   buy_in = 10000
//   prizes = 50, 30, 20
//   level = 50, 100, 0, 10      # small, big, ante, hands (repeatable)
//   level = 100, 200, 25, 10
struct TournamentFile {
    int format_version = 1;
    std::string name;
    std::string description;
    TournamentConfig config;
};

TournamentFile parse_tournament(std::istream& in,
                                const std::string& base_dir = "");
TournamentFile load_tournament_file(const std::string& path);
void save_tournament_file(const TournamentFile& tournament, std::ostream& out);

}  // namespace cardengine
