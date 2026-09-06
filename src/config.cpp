#include "cardengine/config.h"

#include <stdexcept>

namespace cardengine {

void validate(const GameConfig& config) {
    if (config.num_players < 2 || config.num_players > 10) {
        throw std::invalid_argument("num_players must be 2..10");
    }
    if (config.starting_stack < 1) {
        throw std::invalid_argument("starting_stack must be positive");
    }
    if (config.small_blind < 1 || config.big_blind < 1) {
        throw std::invalid_argument("blinds must be positive");
    }
    if (config.small_blind > config.big_blind) {
        throw std::invalid_argument("small_blind cannot exceed big_blind");
    }
    if (config.starting_stack < config.big_blind) {
        throw std::invalid_argument("starting_stack must cover the big blind");
    }
    if (config.ante < 0) {
        throw std::invalid_argument("ante cannot be negative");
    }
    if (config.hole_cards < 1 || config.hole_cards > 7) {
        throw std::invalid_argument("hole_cards must be 1..7");
    }
    if (config.board_cards < 0 || config.board_cards > 5) {
        throw std::invalid_argument("board_cards must be 0..5");
    }
    // Showdown is best-5-of-all-cards, so the total must fit evaluate_best.
    const int total = config.hole_cards + config.board_cards;
    if (total < 5 || total > 7) {
        throw std::invalid_argument(
            "hole_cards + board_cards must be 5..7 for best-5 showdown");
    }
    if (config.num_players * config.hole_cards + config.board_cards > 52) {
        throw std::invalid_argument("not enough cards in the deck");
    }
    if (config.betting != BettingStructure::NoLimit) {
        throw std::invalid_argument(
            "only NoLimit betting is implemented so far");
    }
}

}  // namespace cardengine
