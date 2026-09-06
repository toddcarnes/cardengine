// Deck: full 52, uniqueness, deterministic seeded shuffle, deal semantics.
#include <cassert>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/deck.h"
#include "helpers.h"

namespace {

using testutil::check;

std::vector<std::string> deal_all(cardengine::Deck& deck) {
    std::vector<std::string> seen;
    while (!deck.empty()) {
        seen.push_back(to_string(deck.deal()));
    }
    return seen;
}

}  // namespace

int main() {
    using namespace cardengine;

    Deck fresh;
    check(fresh.size() == 52, "fresh deck has 52 cards");

    auto all = deal_all(fresh);
    check(fresh.size() == 0 && fresh.empty(), "deck empties after 52 deals");
    check(std::set<std::string>(all.begin(), all.end()).size() == 52,
          "all 52 cards unique");

    bool threw = false;
    try {
        fresh.deal();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "deal from empty deck throws");

    // Same seed => same order (guaranteed identical on every platform).
    Deck a, b;
    a.shuffle(12345);
    b.shuffle(12345);
    check(deal_all(a) == deal_all(b), "seeded shuffle is deterministic");

    // Different seeds => different order (52! decks; a collision would be a
    // sign the shuffle is broken, not bad luck).
    Deck c, d;
    c.shuffle(1);
    d.shuffle(2);
    check(deal_all(c) != deal_all(d), "different seeds differ");

    // Shuffled deck still holds all 52 unique cards.
    Deck e;
    e.shuffle(999);
    auto shuffled = deal_all(e);
    check(std::set<std::string>(shuffled.begin(), shuffled.end()).size() == 52,
          "shuffled deck complete");

    std::cout << "test_deck ok\n";
    return 0;
}
