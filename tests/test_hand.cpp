// Hand evaluator: every category, tiebreaks, edge cases, best-of-7.
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/hand.h"

namespace {

using cardengine::Card;
using cardengine::HandCategory;
using cardengine::HandValue;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::array<Card, 5> five(std::initializer_list<const char*> texts) {
    std::array<Card, 5> cards{};
    std::size_t i = 0;
    for (const char* t : texts) cards[i++] = cardengine::parse_card(t);
    return cards;
}

std::vector<Card> cards(std::initializer_list<const char*> texts) {
    std::vector<Card> out;
    for (const char* t : texts) out.push_back(cardengine::parse_card(t));
    return out;
}

HandValue best(std::initializer_list<const char*> texts) {
    return cardengine::evaluate_best(cards(texts));
}

}  // namespace

int main() {
    using namespace cardengine;

    // Full category ladder, weakest to strongest.
    const std::vector<std::pair<HandCategory, std::array<Card, 5>>> ladder = {
        {HandCategory::HighCard, five({"As", "Kd", "Qh", "Jc", "9s"})},
        {HandCategory::OnePair, five({"As", "Ad", "Qh", "Jc", "9s"})},
        {HandCategory::TwoPair, five({"As", "Ad", "Kh", "Kc", "9s"})},
        {HandCategory::ThreeOfAKind, five({"As", "Ad", "Ah", "Kc", "9s"})},
        {HandCategory::Straight, five({"9s", "Td", "Jh", "Qc", "Ks"})},
        {HandCategory::Flush, five({"As", "Js", "9s", "7s", "5s"})},
        {HandCategory::FullHouse, five({"As", "Ad", "Ah", "Kc", "Ks"})},
        {HandCategory::FourOfAKind, five({"As", "Ad", "Ah", "Ac", "Ks"})},
        {HandCategory::StraightFlush, five({"Ts", "Js", "Qs", "Ks", "As"})},
    };
    for (const auto& [category, hand] : ladder) {
        check(evaluate_five(hand).category == category, "ladder category");
    }
    for (std::size_t i = 1; i < ladder.size(); ++i) {
        const HandValue lo = evaluate_five(ladder[i - 1].second);
        const HandValue hi = evaluate_five(ladder[i].second);
        check(hi > lo && lo < hi, "ladder ordering");
        check(!(hi == lo), "ladder inequality");
    }

    // Wheel straight plays five-high and loses to any higher straight.
    const HandValue wheel = evaluate_five(five({"Ah", "2c", "3d", "4h", "5s"}));
    check(wheel.category == HandCategory::Straight, "wheel is a straight");
    check(wheel < evaluate_five(five({"2h", "3c", "4d", "5h", "6s"})),
          "wheel loses to six-high");

    // Kickers decide pairs.
    check(evaluate_five(five({"Ts", "Td", "Ah", "Kc", "Qs"})) >
              evaluate_five(five({"Th", "Tc", "Ad", "Kh", "Js"})),
          "pair kickers");

    // Second pair decides two pair; then the kicker.
    check(evaluate_five(five({"As", "Ad", "Kh", "Kc", "Qs"})) >
              evaluate_five(five({"Ah", "Ac", "Qd", "Qh", "Ks"})),
          "two pair second pair");
    check(evaluate_five(five({"As", "Ad", "Kh", "Kc", "Qs"})) >
              evaluate_five(five({"Ah", "Ac", "Kd", "Kh", "Js"})),
          "two pair kicker");

    // Trips decide full houses, not the pair.
    check(evaluate_five(five({"Qs", "Qd", "Qh", "Ac", "As"})) >
              evaluate_five(five({"Js", "Jd", "Jh", "Ac", "As"})),
          "full house trips first");
    // Quads kicker.
    check(evaluate_five(five({"9s", "9h", "9d", "9c", "Kd"})) >
              evaluate_five(five({"9s", "9h", "9d", "9c", "Qd"})),
          "quads kicker");

    // Same ranks, different suits tie (suits don't break ties in Hold'em).
    check(evaluate_five(five({"As", "Kd", "Qh", "Jc", "9s"})) ==
              evaluate_five(five({"Ah", "Ks", "Qd", "Jh", "9c"})),
          "high card suits tie");
    check(evaluate_five(five({"As", "Ks", "Qs", "Js", "9s"})) ==
              evaluate_five(five({"Ah", "Kh", "Qh", "Jh", "9h"})),
          "flush suits tie");

    // Best-of-7 finds the flush hiding in 7 cards.
    const HandValue nut_flush =
        best({"Ad", "Kd", "Qd", "Jd", "9d", "2c", "3h"});
    check(nut_flush.category == HandCategory::Flush, "best finds flush");
    check(nut_flush.tiebreak[0] == Rank::Ace, "nut flush is ace-high");

    // Double trips: higher trips stay trips, lower trips play as the pair.
    const HandValue dbl =
        best({"Ks", "Kd", "Kh", "Qs", "Qd", "Qh", "2c"});
    check(dbl.category == HandCategory::FullHouse, "double trips full house");
    check(dbl.tiebreak[0] == Rank::King && dbl.tiebreak[1] == Rank::Queen,
          "double trips order");

    // Three pairs in 7 cards: lowest pair drops out.
    const HandValue three_pair =
        best({"As", "Ad", "Ks", "Kd", "Qs", "Qd", "2c"});
    check(three_pair.category == HandCategory::TwoPair, "three pairs");
    check(three_pair.tiebreak[0] == Rank::Ace &&
              three_pair.tiebreak[1] == Rank::King &&
              three_pair.tiebreak[2] == Rank::Queen,
          "three pairs keep top two");

    // Board plays: identical best-five from different hole cards is a tie.
    const HandValue p1 =
        best({"2c", "3c", "As", "Kd", "Qh", "Jc", "Ts"});
    const HandValue p2 =
        best({"9h", "9d", "As", "Kd", "Qh", "Jc", "Ts"});
    check(p1 == p2 && p1.category == HandCategory::Straight, "board plays");

    // 5-card input to evaluate_best agrees with evaluate_five.
    check(best({"Ts", "Js", "Qs", "Ks", "As"}) ==
              evaluate_five(five({"Ts", "Js", "Qs", "Ks", "As"})),
          "best-of-5 equals five");

    bool threw = false;
    try {
        evaluate_best(cards({"As", "Ks", "Qs", "Js"}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "four cards throws");

    std::cout << "test_hand ok\n";
    return 0;
}
