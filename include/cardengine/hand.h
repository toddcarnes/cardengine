#pragma once

#include <array>
#include <vector>

#include "cardengine/card.h"

namespace cardengine {

// Ordered low to high; matches standard poker hand rankings.
enum class HandCategory : std::uint8_t {
    HighCard,
    OnePair,
    TwoPair,
    ThreeOfAKind,
    Straight,
    Flush,
    FullHouse,
    FourOfAKind,
    StraightFlush
};

struct HandValue {
    HandCategory category{HandCategory::HighCard};
    // Ranks in order of significance (e.g. pair rank, then kickers high to
    // low). Always 5 entries; unused tail slots are Rank::Two padding.
    std::array<Rank, 5> tiebreak{Rank::Two, Rank::Two, Rank::Two, Rank::Two,
                                 Rank::Two};

    bool operator==(const HandValue&) const = default;
};

bool operator<(const HandValue& a, const HandValue& b);
inline bool operator>(const HandValue& a, const HandValue& b) { return b < a; }
inline bool operator<=(const HandValue& a, const HandValue& b) { return !(b < a); }
inline bool operator>=(const HandValue& a, const HandValue& b) { return !(a < b); }

// Ranks exactly 5 cards. Same ranks in different suits tie (correct for
// Hold'em: suits never break ties except to make a flush).
HandValue evaluate_five(const std::array<Card, 5>& cards);

// Best 5-card value out of 5, 6, or 7 cards (hole + board). Enumerates all
// C(n,5) combos, so it is obviously correct; n <= 7 keeps it trivially fast.
// Throws std::invalid_argument for any other size.
HandValue evaluate_best(const std::vector<Card>& cards);

// Omaha construction: exactly 2 cards from a 4-card hand plus exactly 3
// from a 5-card board (6 * 10 = 60 combos, best wins).
// Throws std::invalid_argument for any other counts.
HandValue evaluate_omaha(const std::vector<Card>& hole,
                         const std::vector<Card>& board);

// Omaha Hi-Lo low hand (8-or-better): five unpaired ranks, all 8 or lower,
// ace counting as 1, straights and flushes ignored. `descending` holds the
// low-converted values (ace = 1) sorted high-first, so the better low is
// the lexicographically smaller array (wheel {5,4,3,2,1} is the nuts).
// operator< means "better low" (smaller) — the reverse sense of HandValue,
// documented here so call sites read plainly. Unqualified sorts after any
// qualifier.
struct LowValue {
    bool qualifies = false;
    // Low values high-first (ace = 1, 1..8); Rank::Two padding when empty.
    std::array<int, 5> descending{9, 9, 9, 9, 9};

    bool operator==(const LowValue&) const = default;
};

bool operator<(const LowValue& a, const LowValue& b);

// Both halves of an Omaha Hi-Lo showdown under exact 2+3 construction.
// `low` is unqualified when no 2+3 combo makes 8-or-better.
struct OmahaHiLoValue {
    HandValue high;
    LowValue low;
};

// 2-7 lowball value: the worse the poker hand, the better the lowball.
// Straights and flushes count against (a flush is a strong hand, hence a
// terrible low), aces always high. operator< means "better low": 7-5-4-3-2
// (the nuts) sorts first; any pair sorts after every broken hand.
//
// The tiebreak keeps the high-hand order (top card down): 7-5-4-3-2 beats
// 8-5-4-3-2 on the first card, deuces up beats aces up the same way.
// Suits never break ties (identical lows split).
struct DeuceValue {
    // Penalty bucket: 0 for a broken hand (no pair, no straight, no
    // flush), then 1..8 up the poker ladder (pairs ... straight flushes).
    // Lower bucket always wins; within a bucket the smaller high-hand
    // tiebreak wins the lowball.
    int penalty = 0;
    std::array<Rank, 5> tiebreak{Rank::Ace, Rank::Ace, Rank::Ace,
                                 Rank::Ace, Rank::Ace};

    bool operator==(const DeuceValue&) const = default;
};

bool operator<(const DeuceValue& a, const DeuceValue& b);

// Best (lowest) 2-7 value out of exactly 5 cards. Same cards, same winner
// as evaluate_five reversed — except straights and flushes, which
// evaluate_five rewards and lowball punishes, so this ranks directly.
DeuceValue evaluate_deuce(const std::array<Card, 5>& cards);

// High plus best qualifying low (if any) over the same 60 combos.
// Throws std::invalid_argument for any counts but 4 hole + 5 board.
OmahaHiLoValue evaluate_omaha_hilo(const std::vector<Card>& hole,
                                   const std::vector<Card>& board);

}  // namespace cardengine
