#pragma once

// Shared test utilities. Every suite links stdlib only, so the helpers are
// header-inline. Failure output stays uniform: `FAIL: ...` plus exit 1.
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "cardengine/card.h"

namespace testutil {

inline void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

inline void check(bool condition, const std::string& message) {
    check(condition, message.c_str());
}

inline bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Expects body() to throw Expected (and only Expected).
template <typename Expected, typename F>
void expect_throws(F&& run, const char* message) {
    // The second catch is dead code when Expected is std::exception itself,
    // which warnings-as-errors rejects: hence the split.
    if constexpr (std::is_same_v<Expected, std::exception>) {
        try {
            run();
        } catch (const std::exception&) {
            return;
        }
    } else {
        try {
            run();
        } catch (const Expected&) {
            return;
        } catch (const std::exception& e) {
            std::cerr << "FAIL (wrong exception type: " << e.what()
                      << "): " << message << "\n";
            std::exit(1);
        }
    }
    std::cerr << "FAIL (expected exception): " << message << "\n";
    std::exit(1);
}

inline std::vector<cardengine::Card> cards(
    std::initializer_list<const char*> texts) {
    std::vector<cardengine::Card> out;
    for (const char* text : texts) out.push_back(cardengine::parse_card(text));
    return out;
}

// Portable full-deck deal: Fisher-Yates driven by raw mt19937_64 output.
// Only the engine itself is exactly specified; the standard
// distributions may map it differently per stdlib, so portable test code
// must never go through them (use rng() % bound; modulo bias is
// irrelevant for fixed regression decks).
inline std::vector<cardengine::Card> shuffled_deck(std::uint64_t seed) {
    std::vector<cardengine::Card> deck;
    for (int s = 0; s < 4; ++s) {
        for (int r = 2; r <= 14; ++r) {
            deck.push_back(cardengine::Card{
                static_cast<cardengine::Rank>(r),
                static_cast<cardengine::Suit>(s)});
        }
    }
    std::mt19937_64 rng(seed);
    for (std::size_t i = deck.size() - 1; i > 0; --i) {
        const std::size_t j =
            static_cast<std::size_t>(rng() % (i + 1));
        const cardengine::Card tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
    return deck;
}

// Portable index shuffle with the same construction (for draw discards).
inline void shuffle_indices(std::vector<int>& idx, std::mt19937_64& rng) {
    for (std::size_t i = idx.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(rng() % i);
        const int tmp = idx[i - 1];
        idx[i - 1] = idx[j];
        idx[j] = tmp;
    }
}

}  // namespace testutil
