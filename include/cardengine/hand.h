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

}  // namespace cardengine
