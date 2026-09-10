#include "cardengine/deck.h"

#include <cstddef>
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
    // Portable Fisher-Yates on raw mt19937_64 output: std::shuffle is
    // specified via uniform_int_distribution, whose mapping may differ per
    // stdlib, so seed N must not go through it for cross-platform replay.
    std::mt19937_64 rng{seed};
    for (std::size_t i = cards_.size() - 1; i > 0; --i) {
        const std::size_t j = static_cast<std::size_t>(rng() % (i + 1));
        const Card tmp = cards_[i];
        cards_[i] = cards_[j];
        cards_[j] = tmp;
    }
}

Card Deck::deal() {
    if (empty()) {
        throw std::out_of_range("deal from empty deck");
    }
    return cards_[next_++];
}

}  // namespace cardengine
