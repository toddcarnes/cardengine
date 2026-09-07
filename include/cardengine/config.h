#pragma once

namespace cardengine {

// How bets may grow. NoLimit: any amount from the minimum to all-in.
// Limit: fixed sizes (big blind preflop/flop, twice after) with a per-round
// raise cap. PotLimit: raises capped at the pot-sized raise.
enum class BettingStructure { NoLimit, Limit, PotLimit };

// How the showdown winner is found. BestFiveOfAll is Hold'em-style (any
// five). OmahaTwoAndThree is Omaha-style: exactly 2 from hand + 3 from board.
// OmahaHiLo splits every pot: best high hand takes half, best 8-or-better
// low takes half (exact 2+3 both ways); no qualifying low means high scoops.
enum class HandConstruction { BestFiveOfAll, OmahaTwoAndThree, OmahaHiLo };

// Everything a variant needs to change about a game, as data.
// Defaults describe standard no-limit Texas Hold'em, which is the test case
// the config design is proven against. Future variants (Omaha-style hole
// counts, stud-style boards, limit betting) plug in here, not in Table.
// Where antes come from. EverySeat: each participant posts `ante`
// (classic). ButtonOnly: the button posts the whole table's ante at once
// (one `ante × seats` payment — faster live dealing, same dead money).
enum class AnteSource { EverySeat, ButtonOnly };

struct GameConfig {
    int num_players = 6;     // Seats at the table, 2..10.
    int starting_stack = 10000;
    int small_blind = 50;
    int big_blind = 100;
    int ante = 0;            // Dead money from every participant each hand.
    AnteSource ante_from = AnteSource::EverySeat;
    int straddle = 0;        // Optional blind 2× the big blind, posted by the
                             // seat after the big blind (UTG). 0 = no straddle.
                             // The straddler acts last preflop (live straddle:
                             // they may raise their own blind when it returns).
    bool kill = false;       // Double the blinds for the next hand after any
                             // pot over 10× the big blind (full kill: the
                             // trigger hand's winner posts the extra blind).
    int runouts = 1;         // Boards run at showdown, 1..3. 1 is classic
                             // poker; 2+ deals that many boards from the
                             // remaining shoe and splits each pot across them
                             // (run-it-twice: all-in cash-game practice that
                             // cuts variance without changing equity).
    int hole_cards = 2;      // Cards dealt to each seat.
    int board_cards = 5;     // Community cards, dealt 3-1-1 across streets
                             // (scaled down when fewer are configured).
    BettingStructure betting = BettingStructure::NoLimit;
    HandConstruction showdown = HandConstruction::BestFiveOfAll;
    int max_raises_per_round = 4;  // Limit betting only (bet + raises cap).
};

// Throws std::invalid_argument unless the config is sane AND implemented.
void validate(const GameConfig& config);

}  // namespace cardengine
