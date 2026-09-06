// Card basics: values, notation, parsing, ordering.
#include <cassert>
#include <iostream>
#include <stdexcept>

#include "cardengine/card.h"

namespace {

using cardengine::Card;
using cardengine::Rank;
using cardengine::Suit;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace cardengine;

    static_assert(static_cast<int>(Rank::Two) == 2);
    static_assert(static_cast<int>(Rank::Ace) == 14);

    check(to_string(Card{Rank::Ace, Suit::Spades}) == "As", "ace of spades");
    check(to_string(Card{Rank::Ten, Suit::Diamonds}) == "Td", "ten of diamonds");
    check(to_string(Card{Rank::Seven, Suit::Clubs}) == "7c", "seven of clubs");

    check(parse_card("Qh") == Card{Rank::Queen, Suit::Hearts}, "parse Qh");
    check(parse_card("10h") == parse_card("Th"), "10h equals Th");
    check(parse_card("AS") == parse_card("As"), "suit case-insensitive");

    bool threw = false;
    try {
        parse_card("Zz");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "bad card text throws");

    threw = false;
    try {
        parse_card("Q");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "short card text throws");

    // Round trip through every rank and suit.
    for (int r = 2; r <= 14; ++r) {
        for (int s = 0; s < 4; ++s) {
            const Card c{static_cast<Rank>(r), static_cast<Suit>(s)};
            check(parse_card(to_string(c)) == c, "round trip");
        }
    }

    check(Card{Rank::Two, Suit::Clubs} < Card{Rank::Three, Suit::Clubs},
          "rank ordering");
    check(Card{Rank::Ace, Suit::Clubs} > Card{Rank::King, Suit::Spades},
          "ace high");

    std::cout << "test_card ok\n";
    return 0;
}
