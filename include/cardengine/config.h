#pragma once

namespace cardengine {

// How bets may grow. Only NoLimit is implemented; Limit and PotLimit are
// declared so variant configs can name them — validate() rejects them with
// a clear error until their rules modules land.
enum class BettingStructure { NoLimit, Limit, PotLimit };

// Everything a variant needs to change about a game, as data.
// Defaults describe standard no-limit Texas Hold'em, which is the test case
// the config design is proven against. Future variants (Omaha-style hole
// counts, stud-style boards, limit betting) plug in here, not in Table.
struct GameConfig {
    int num_players = 6;     // Seats at the table, 2..10.
    int starting_stack = 10000;
    int small_blind = 50;
    int big_blind = 100;
    int ante = 0;            // Dead money from every participant each hand.
    int hole_cards = 2;      // Cards dealt to each seat.
    int board_cards = 5;     // Community cards, dealt 3-1-1 across streets
                             // (scaled down when fewer are configured).
    BettingStructure betting = BettingStructure::NoLimit;
};

// Throws std::invalid_argument unless the config is sane AND implemented.
void validate(const GameConfig& config);

}  // namespace cardengine
