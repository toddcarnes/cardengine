#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cardengine/card.h"

namespace cardengine {

// Standard 52-card deck. Shuffle is deterministic per seed on every platform:
// Fisher-Yates driven by raw std::mt19937_64 output (both fully specified
// by the C++ standard), so seed N produces the same order on Windows,
// macOS, and Linux. (std::shuffle goes through uniform_int_distribution,
// whose mapping may differ per stdlib — never use it here.)
class Deck {
public:
    Deck();

    void shuffle(std::uint64_t seed);

    // Removes and returns the top card. Throws std::out_of_range if empty.
    Card deal();

    [[nodiscard]] bool empty() const { return next_ >= cards_.size(); }
    [[nodiscard]] std::size_t size() const { return cards_.size() - next_; }

private:
    std::array<Card, 52> cards_{};
    std::size_t next_{0};
};

}  // namespace cardengine
