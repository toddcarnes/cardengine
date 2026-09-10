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
// StudSeven is seven-card stud: no shared board, each seat's own 7 cards
// (3 down, 4 up) play best-five; upcards are public, downcards private.
// DrawFive is five-card draw: 5 private cards, one discard/draw round,
// then best-five showdown. DeuceSeven is 2-7 lowball draw: same deal with
// the worst hand winning (straights/flushes count against, aces high).
enum class HandConstruction {
    BestFiveOfAll,
    OmahaTwoAndThree,
    OmahaHiLo,
    StudSeven,
    DrawFive,
    DeuceSeven
};

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
                             // cuts variance without changing equity). The
                             // shoe must cover every spare board up front
                             // (num_players*hole + board*runouts <= 52); a
                             // short shoe falls back to one board at settle.
    int hole_cards = 2;      // Cards dealt to each seat (7 for stud: 3 down,
                             // 4 up — see upcards below).
    int board_cards = 5;     // Community cards, dealt 3-1-1 across streets
                             // (scaled down when fewer are configured).
                             // 0 for stud (no shared board).
    int upcards = 0;         // Stud only: face-up cards per seat (4 for
                             // seven-card stud: 2 down + 1 up on third street,
                             // then 1 up each street, 1 down on the river).
    int bring_in = 0;        // Stud only: forced bet by the lowest upcard on
                             // third street (0 = no bring-in, high hand opens).
                             // Must be below the small blind when set.
    int max_draw = 5;        // Draw only: most cards a seat may exchange
                             // (5 = any number, 3 = classic 3-card limit).
    BettingStructure betting = BettingStructure::NoLimit;
    HandConstruction showdown = HandConstruction::BestFiveOfAll;
    int max_raises_per_round = 4;  // Limit betting only (bet + raises cap).
};

// Throws std::invalid_argument unless the config is sane AND implemented.
void validate(const GameConfig& config);

}  // namespace cardengine
