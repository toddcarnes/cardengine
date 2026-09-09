#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/table.h"

namespace cardengine {

// One blind level: fixed bets for `hands` completed hands (when minutes
// is 0), or for `minutes` wall-clock minutes (when positive), then the
// next. A level with both set advances on whichever hits first; the final
// level repeats forever either way.
struct BlindLevel {
    int small_blind = 50;
    int big_blind = 100;
    int ante = 0;
    int hands = 10;
    int minutes = 0;  // 0 = hands-based (classic); positive = timed.
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
// the books at settle time. Hands-based level changes are automatic;
// timed levels advance on the host's clock (it hands the engine stamps via
// begin_hand_at/finish_hand_at, or plain begin_hand/finish_hand to read
// the wall clock itself). Manual tlevel jumps still work underneath.
class Tournament {
public:
    explicit Tournament(const TournamentConfig& config);

    const TournamentConfig& config() const { return config_; }
    Table& table() { return table_; }
    const Table& table() const { return table_; }

    // Starts a hand at the current level's blinds (Table rejects mid-hand
    // starts as usual). Throws std::logic_error when one player remains.
    // Stamps the level clock at `now` (seconds, see clock.h); the plain
    // form reads the wall clock.
    void begin_hand(std::uint64_t seed);
    void begin_hand_at(std::uint64_t seed, std::int64_t now);
    void begin_hand_from_deck(std::vector<Card> top_first);
    void begin_hand_from_deck_at(std::vector<Card> top_first, std::int64_t now);

    // Books a settled hand: eliminations, prizes, level progress.
    // Throws std::logic_error unless the table hand is settled and unbooked.
    // Stamps `now` for timed-level accounting; the plain form reads the
    // wall clock.
    void finish_hand();
    void finish_hand_at(std::int64_t now);

    // Manual level advance for GUI clocks. Sticks at the final level.
    // The plain form stamps the wall clock; _at takes an injected stamp
    // (tests, hosts with their own clock).
    void advance_level();
    void advance_level_at(std::int64_t now);

    // Timed-level clock. Seconds until the current level ends (-1 when the
    // level is hands-based or already final). Hosts poll this to ring the
    // bell; the engine advances the level at the next begin/finish (never
    // mid-hand). The plain form reads the wall clock.
    std::int64_t level_seconds_left(std::int64_t now) const;
    std::int64_t level_seconds_left() const;
    // Applies any due timed advance now (between hands only). Returns true
    // when the level moved. Hosts call this on their timer tick; dealing
    // paths call it internally, so a sleeping host still advances on time.
    bool advance_level_if_due(std::int64_t now);
    bool advance_level_if_due();

    // Adds starting_stack chips for another buy_in into the pool.
    // Between hands only; clears an elimination (with its recorded prize).
    void rebuy(int seat);

    struct Snapshot {
        TournamentConfig config;
        std::vector<int> stacks;
        std::vector<bool> sitting_out;
        int button = 0;
        int level_index = 0;
        int hands_into_level = 0;
        std::int64_t level_elapsed = 0;  // Seconds banked in this level.
        int prize_pool = 0;
        int prize_awarded = 0;
        std::vector<bool> eliminated;
        std::vector<int> places;
        std::vector<int> prizes;
    };
    // Between-hands books: levels, pool, busts, places. Throws
    // std::logic_error when a hand is open.
    Snapshot snapshot() const;
    // Inverse of snapshot. Throws std::invalid_argument on a mismatched
    // snapshot, std::logic_error when a hand is open.
    void restore(const Snapshot& saved);

    // Final-table deal: the remaining players agree to split the rest of
    // the pool (listed amounts must sum to exactly pool minus prizes
    // already awarded) and the tournament ends at once. Places go by stack,
    // chip leader first; ties break in seat order; earlier busts slide
    // below in bust order. Between hands only.
    // Throws std::invalid_argument on a bad split, std::logic_error when
    // a hand is open or the tournament is already over.
    void chop(const std::vector<Payout>& deal);

    bool complete() const;
    int winner() const;  // Throws std::logic_error unless complete().
    int level_index() const { return level_index_; }
    BlindLevel level() const { return config_.levels[static_cast<std::size_t>(level_index_)]; }
    int hands_into_level() const { return hands_into_level_; }
    // Seconds banked in the current level (timed levels). Resets on entry.
    std::int64_t level_elapsed() const { return level_elapsed_; }
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
    std::int64_t level_started_at_ = 0;  // Stamp of the current level's start.
    bool clock_live_ = false;  // True once the first hand sets the stamp.
    std::int64_t level_elapsed_ = 0;     // Banked seconds (across save/load).
    int prize_pool_ = 0;
    int prize_awarded_ = 0;
    bool hand_open_ = false;  // A dealt hand awaits finish_hand().
    std::vector<bool> eliminated_;
    std::vector<int> places_;
    std::vector<int> prizes_;
};

// Seats in finishing order, champion first. Throws std::logic_error unless
// the tournament is complete. The stress CSV's `placements` column is this
// order; the ratings tool reads it for pairwise Elo.
std::vector<int> finishing_order(const Tournament& event);

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
//   level = 200, 400, 50, 4, 20 # ...or add minutes: 4 hands or 20 minutes,
//                               # whichever hits first (0 = hands only)
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
