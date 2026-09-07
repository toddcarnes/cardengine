// Hand evaluator: every category, tiebreaks, edge cases, best-of-7.
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/hand.h"
#include "helpers.h"

namespace {

using cardengine::Card;
using cardengine::HandCategory;
using cardengine::HandValue;
using cardengine::LowValue;
using cardengine::OmahaHiLoValue;
using testutil::check;

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

    // Omaha: exactly 2 from 4 hole + exactly 3 from 5 board.
    {
        // Finds the royal among all 60 combos.
        const HandValue royal = evaluate_omaha(
            cards({"Ah", "Kh", "Qs", "Qd"}), cards({"Qh", "Jh", "Th", "2c", "3d"}));
        check(royal.category == HandCategory::StraightFlush, "omaha royal");

        // Four spades on board plus one suited hole card is NOT a flush:
        // exactly two hole cards must play. (Hold'em would call this a
        // royal flush; Omaha calls it ace-high.)
        const HandValue noflush = evaluate_omaha(
            cards({"Ts", "8c", "4h", "3d"}),
            cards({"As", "Ks", "Qs", "Js", "2d"}));
        check(noflush.category == HandCategory::HighCard, "no four-flush");
        check(noflush.tiebreak[0] == Rank::Ace &&
                  noflush.tiebreak[3] == Rank::Ten,
              "ace-high kickers");

        // Trips on board don't give everyone quads: only Qc plays with them.
        const HandValue quads = evaluate_omaha(
            cards({"Qc", "Jc", "4d", "6h"}),
            cards({"Qs", "Qd", "Qh", "2c", "5s"}));
        const HandValue trips = evaluate_omaha(
            cards({"Ac", "Kd", "7h", "8c"}),
            cards({"Qs", "Qd", "Qh", "2c", "5s"}));
        check(quads.category == HandCategory::FourOfAKind, "pocket pair quads");
        check(quads > trips, "quads beat board-trip trips");

        // A straight on board counterfeits to trips-vs-pair in Omaha.
        const HandValue pair_aces = evaluate_omaha(
            cards({"Ac", "Ad", "2h", "3h"}),
            cards({"Ks", "Qs", "Js", "Ts", "9c"}));
        const HandValue trip_kings = evaluate_omaha(
            cards({"Kc", "Kd", "4h", "5h"}),
            cards({"Ks", "Qs", "Js", "Ts", "9c"}));
        check(trip_kings > pair_aces, "trips beat pair on straight board");

        bool bad = false;
        try {
            evaluate_omaha(cards({"As", "Ks"}),
                           cards({"Qs", "Js", "Ts", "9c", "8d"}));
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        check(bad, "omaha needs 4 hole");
        bad = false;
        try {
            evaluate_omaha(cards({"As", "Ks", "Qd", "Jd"}),
                           cards({"Qs", "Js", "Ts", "9c"}));
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        check(bad, "omaha needs 5 board");
    }

    // Omaha Hi-Lo: the wheel is the nut low; pairs and 9s don't qualify.
    {
        // A2 + 3-4-5 board: high is the wheel straight, low is the nut low.
        const OmahaHiLoValue scoop = evaluate_omaha_hilo(
            cards({"Ah", "2c", "Ks", "Qd"}), cards({"3h", "4d", "5s", "Jc", "Td"}));
        check(scoop.high.category == HandCategory::Straight, "wheel high");
        check(scoop.low.qualifies, "wheel qualifies");
        check((scoop.low.descending == std::array<int, 5>{5, 4, 3, 2, 1}),
              "wheel is the nut low");

        // 7-6 low beats 8-7 low (compare the highest card first).
        const OmahaHiLoValue seven = evaluate_omaha_hilo(
            cards({"Ah", "7c", "Ks", "Qd"}), cards({"2h", "3d", "6s", "Jc", "Td"}));
        const OmahaHiLoValue eight = evaluate_omaha_hilo(
            cards({"Ah", "8c", "Ks", "Qd"}), cards({"2h", "3d", "7s", "Jc", "Td"}));
        check(seven.low.qualifies && eight.low.qualifies, "both qualify");
        check(seven.low < eight.low, "7-low beats 8-low");
        check(!(eight.low < seven.low), "8-low loses");

        // Aces play both ways: A4 + 2-3-5 board makes a 5-high low.
        const OmahaHiLoValue aces = evaluate_omaha_hilo(
            cards({"Ah", "4d", "Ks", "Qd"}), cards({"2h", "3d", "5s", "Jc", "Td"}));
        check(aces.low.qualifies, "aces low qualifies");
        check((aces.low.descending == std::array<int, 5>{5, 4, 3, 2, 1}),
              "ace-four makes the wheel");

        // Counterfeit: A2 with two aces on board pairs the ace — the low
        // must come from the remaining 2+3 combos (or vanish entirely).
        const OmahaHiLoValue counterfeit = evaluate_omaha_hilo(
            cards({"Ah", "2c", "Ks", "Qd"}),
            cards({"Ad", "As", "7h", "8d", "9s"}));
        check(!counterfeit.low.qualifies, "paired low cards kill the low");

        // No low possible: K-Q-J board with high hole cards.
        const OmahaHiLoValue high_only = evaluate_omaha_hilo(
            cards({"Ah", "Kd", "Qs", "Js"}),
            cards({"Kc", "Qh", "Jd", "9s", "Ts"}));
        check(!high_only.low.qualifies, "high cards make no low");

        // Flushes don't spoil lows: four to a low flush still qualifies.
        const OmahaHiLoValue flush_low = evaluate_omaha_hilo(
            cards({"Ah", "3h", "Ks", "Qd"}),
            cards({"2h", "4h", "6h", "Jc", "Td"}));
        check(flush_low.low.qualifies, "flush cards still make a low");

        // Unqualified sorts after any qualifier; nothing beats itself.
        LowValue none;
        check(!(none < seven.low), "no low loses to a qualifier");
        check(seven.low < none, "qualifier beats no low");
        check(!(seven.low < seven.low), "low ties itself");

        bool bad = false;
        try {
            evaluate_omaha_hilo(cards({"As", "Ks"}),
                                cards({"Qs", "Js", "Ts", "9c", "8d"}));
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        check(bad, "hilo needs 4 hole");
        bad = false;
        try {
            evaluate_omaha_hilo(cards({"As", "Ks", "Qd", "Jd"}),
                                cards({"Qs", "Js", "Ts", "9c"}));
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        check(bad, "hilo needs 5 board");
    }

    std::cout << "test_hand ok\n";
    return 0;
}
