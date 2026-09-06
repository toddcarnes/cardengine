#include "cardengine/deck.h"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace cardengine {

Deck::Deck() {
    std::size_t i = 0;
    for (int s = 0; s < 4; ++s) {
        for (int r = 2; r <= 14; ++r) {
            cards_[i++] = Card{static_cast<Rank>(r), static_cast<Suit>(s)};
        }
    }
}

void Deck::shuffle(std::uint64_t seed) {
    next_ = 0;
    std::mt19937_64 rng{seed};
    std::shuffle(cards_.begin(), cards_.end(), rng);
}

Card Deck::deal() {
    if (empty()) {
        throw std::out_of_range("deal from empty deck");
    }
    return cards_[next_++];
}

}  // namespace cardengine
