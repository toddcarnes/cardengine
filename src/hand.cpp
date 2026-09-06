#include "cardengine/hand.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cardengine {

bool operator<(const HandValue& a, const HandValue& b) {
    if (a.category != b.category) return a.category < b.category;
    for (std::size_t i = 0; i < a.tiebreak.size(); ++i) {
        if (a.tiebreak[i] != b.tiebreak[i]) return a.tiebreak[i] < b.tiebreak[i];
    }
    return false;
}

namespace {

int rank_int(Rank r) { return static_cast<int>(r); }

HandValue make_value(HandCategory category, std::initializer_list<Rank> ranks) {
    HandValue value;
    value.category = category;
    std::size_t i = 0;
    for (Rank r : ranks) {
        value.tiebreak[i++] = r;
    }
    return value;
}

}  // namespace

HandValue evaluate_five(const std::array<Card, 5>& cards) {
    // Rank histogram, indexable by rank value 2..14.
    int counts[15] = {};
    for (const Card& c : cards) {
        ++counts[rank_int(c.rank)];
    }

    const bool flush = std::all_of(cards.begin(), cards.end(), [&](const Card& c) {
        return c.suit == cards[0].suit;
    });

    // Distinct ranks, high to low.
    std::vector<int> distinct;
    for (int r = 14; r >= 2; --r) {
        if (counts[r] > 0) distinct.push_back(r);
    }

    bool straight = false;
    int straight_high = 0;
    if (distinct.size() == 5) {
        if (distinct[0] - distinct[4] == 4) {
            straight = true;
            straight_high = distinct[0];
        } else if (distinct[0] == 14 && distinct[1] == 5) {
            straight = true;  // Wheel: A-5-4-3-2, plays as five-high.
            straight_high = 5;
        }
    }

    if (straight && flush) {
        return make_value(HandCategory::StraightFlush,
                          {static_cast<Rank>(straight_high)});
    }

    // Groups sorted by (count desc, rank desc).
    std::vector<std::pair<int, int>> groups;  // (count, rank)
    for (int r = 14; r >= 2; --r) {
        if (counts[r] > 0) groups.emplace_back(counts[r], r);
    }
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second > b.second;
    });
    auto rank = [&](std::size_t i) { return static_cast<Rank>(groups[i].second); };

    if (groups[0].first == 4) {
        return make_value(HandCategory::FourOfAKind, {rank(0), rank(1)});
    }
    // Full house includes double trips (7-card boards can deal two of them);
    // the higher trips stay trips, the lower plays as the pair.
    if (groups[0].first == 3 && groups.size() > 1 && groups[1].first >= 2) {
        return make_value(HandCategory::FullHouse, {rank(0), rank(1)});
    }
    if (flush) {
        std::array<Card, 5> sorted = cards;
        std::sort(sorted.begin(), sorted.end(), std::greater<Card>());
        return make_value(HandCategory::Flush,
                          {sorted[0].rank, sorted[1].rank, sorted[2].rank,
                           sorted[3].rank, sorted[4].rank});
    }
    if (straight) {
        return make_value(HandCategory::Straight,
                          {static_cast<Rank>(straight_high)});
    }
    if (groups[0].first == 3) {
        // Kickers are groups[1], groups[2], both count 1, already rank-sorted.
        return make_value(HandCategory::ThreeOfAKind,
                          {rank(0), rank(1), rank(2)});
    }
    if (groups[0].first == 2 && groups.size() > 2 && groups[1].first == 2) {
        // Three pairs are possible with 7 cards; the lowest pair is dropped.
        return make_value(HandCategory::TwoPair, {rank(0), rank(1), rank(2)});
    }
    if (groups[0].first == 2) {
        return make_value(HandCategory::OnePair,
                          {rank(0), rank(1), rank(2), rank(3)});
    }

    std::array<Card, 5> sorted = cards;
    std::sort(sorted.begin(), sorted.end(), std::greater<Card>());
    return make_value(HandCategory::HighCard,
                      {sorted[0].rank, sorted[1].rank, sorted[2].rank,
                       sorted[3].rank, sorted[4].rank});
}

HandValue evaluate_omaha(const std::vector<Card>& hole,
                         const std::vector<Card>& board) {
    if (hole.size() != 4 || board.size() != 5) {
        throw std::invalid_argument("evaluate_omaha needs 4 hole + 5 board");
    }
    bool best_set = false;
    HandValue best;
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = a + 1; b < 4; ++b) {
            for (std::size_t c = 0; c < 5; ++c) {
                for (std::size_t d = c + 1; d < 5; ++d) {
                    for (std::size_t e = d + 1; e < 5; ++e) {
                        const std::array<Card, 5> five{
                            hole[a], hole[b], board[c], board[d], board[e]};
                        const HandValue value = evaluate_five(five);
                        if (!best_set || best < value) {
                            best = value;
                            best_set = true;
                        }
                    }
                }
            }
        }
    }
    return best;
}

HandValue evaluate_best(const std::vector<Card>& cards) {
    if (cards.size() < 5 || cards.size() > 7) {
        throw std::invalid_argument("evaluate_best needs 5 to 7 cards");
    }
    // Enumerate all C(n,5) index combinations, lowest indices first.
    const std::size_t n = cards.size();
    std::vector<std::size_t> idx = {0, 1, 2, 3, 4};
    bool best_set = false;
    HandValue best;
    for (;;) {
        std::array<Card, 5> five{};
        for (std::size_t i = 0; i < 5; ++i) five[i] = cards[idx[i]];
        const HandValue value = evaluate_five(five);
        if (!best_set || best < value) {
            best = value;
            best_set = true;
        }
        std::size_t pos = 5;
        while (pos > 0 && idx[pos - 1] == n - 5 + (pos - 1)) --pos;
        if (pos == 0) break;
        ++idx[pos - 1];
        for (std::size_t i = pos; i < 5; ++i) idx[i] = idx[i - 1] + 1;
    }
    return best;
}

}  // namespace cardengine
