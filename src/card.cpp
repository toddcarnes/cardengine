#include "cardengine/card.h"

#include <stdexcept>

namespace cardengine {

std::string to_string(Card card) {
    std::string text;
    text.push_back(to_char(card.rank));
    text.push_back(to_char(card.suit));
    return text;
}

namespace {

Rank parse_rank(char c) {
    switch (c) {
        case '2': return Rank::Two;
        case '3': return Rank::Three;
        case '4': return Rank::Four;
        case '5': return Rank::Five;
        case '6': return Rank::Six;
        case '7': return Rank::Seven;
        case '8': return Rank::Eight;
        case '9': return Rank::Nine;
        case 'T':
        case 't': return Rank::Ten;
        case 'J':
        case 'j': return Rank::Jack;
        case 'Q':
        case 'q': return Rank::Queen;
        case 'K':
        case 'k': return Rank::King;
        case 'A':
        case 'a': return Rank::Ace;
        default: throw std::invalid_argument("bad rank in card text");
    }
}

Suit parse_suit(char c) {
    switch (c) {
        case 'C':
        case 'c': return Suit::Clubs;
        case 'D':
        case 'd': return Suit::Diamonds;
        case 'H':
        case 'h': return Suit::Hearts;
        case 'S':
        case 's': return Suit::Spades;
        default: throw std::invalid_argument("bad suit in card text");
    }
}

}  // namespace

Card parse_card(std::string_view text) {
    if (text.size() == 3 && (text[0] == '1' && (text[1] == '0'))) {
        return Card{Rank::Ten, parse_suit(text[2])};
    }
    if (text.size() != 2) {
        throw std::invalid_argument("card text must be like \"Qh\"");
    }
    return Card{parse_rank(text[0]), parse_suit(text[1])};
}

}  // namespace cardengine
