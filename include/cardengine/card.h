#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cardengine {

// Ranks carry poker values Two=2 .. Ace=14 so high-card compares are plain
// integer compares. Enum class on purpose: no implicit int conversions.
enum class Rank : std::uint8_t {
    Two = 2,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace
};

enum class Suit : std::uint8_t { Clubs, Diamonds, Hearts, Spades };

struct Card {
    Rank rank{Rank::Two};
    Suit suit{Suit::Clubs};

    constexpr bool operator==(const Card&) const = default;
};

constexpr bool operator<(Card a, Card b) {
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.suit < b.suit;
}
constexpr bool operator>(Card a, Card b) { return b < a; }

constexpr char to_char(Rank rank) {
    switch (rank) {
        case Rank::Two: return '2';
        case Rank::Three: return '3';
        case Rank::Four: return '4';
        case Rank::Five: return '5';
        case Rank::Six: return '6';
        case Rank::Seven: return '7';
        case Rank::Eight: return '8';
        case Rank::Nine: return '9';
        case Rank::Ten: return 'T';
        case Rank::Jack: return 'J';
        case Rank::Queen: return 'Q';
        case Rank::King: return 'K';
        case Rank::Ace: return 'A';
    }
    return '?';  // Unreachable; keeps MSVC /W4 happy without a default label.
}

constexpr char to_char(Suit suit) {
    switch (suit) {
        case Suit::Clubs: return 'c';
        case Suit::Diamonds: return 'd';
        case Suit::Hearts: return 'h';
        case Suit::Spades: return 's';
    }
    return '?';  // Unreachable; see above.
}

// Compact notation, e.g. "Qh", "Ts", "7c".
std::string to_string(Card card);

// Inverse of to_string; also accepts "10h" for "Th". Case-insensitive suit.
// Throws std::invalid_argument on bad input.
Card parse_card(std::string_view text);

}  // namespace cardengine
