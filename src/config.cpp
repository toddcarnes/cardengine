#include "cardengine/config.h"

#include <stdexcept>

namespace cardengine {

void validate(const GameConfig& config) {
    // Seat count first: stud caps at 8 (the standard full table), every
    // other variant at 10. (Stud's 8 × 7 = 56 exceeds one deck, but stud
    // deals street-by-street with a community-river fallback — validated
    // in the stud branch below, not by the generic deck math.)
    if (config.showdown == HandConstruction::StudSeven) {
        if (config.num_players < 2 || config.num_players > 8) {
            throw std::invalid_argument("stud needs 2..8 players");
        }
    } else if (config.num_players < 2 || config.num_players > 10) {
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
    if (config.straddle < 0) {
        throw std::invalid_argument("straddle cannot be negative");
    }
    if (config.straddle > 0 && config.straddle != 2 * config.big_blind) {
        throw std::invalid_argument("straddle must be twice the big blind");
    }
    if (config.runouts < 1 || config.runouts > 3) {
        throw std::invalid_argument("runouts must be 1..3");
    }
    if (config.upcards < 0 || config.upcards > config.hole_cards) {
        throw std::invalid_argument("upcards cannot exceed hole_cards");
    }
    if (config.bring_in < 0) {
        throw std::invalid_argument("bring_in cannot be negative");
    }
    if (config.max_draw < 1 || config.max_draw > 5) {
        throw std::invalid_argument("max_draw must be 1..5");
    }
    if (config.showdown != HandConstruction::StudSeven) {
        if (config.upcards != 0) {
            throw std::invalid_argument("upcards need stud showdown");
        }
        if (config.bring_in != 0) {
            throw std::invalid_argument("bring_in needs stud showdown");
        }
    }
    if (config.showdown != HandConstruction::DrawFive &&
        config.showdown != HandConstruction::DeuceSeven &&
        config.max_draw != 5) {
        throw std::invalid_argument("max_draw needs draw showdown");
    }
    if (config.hole_cards < 1 || config.hole_cards > 7) {
        throw std::invalid_argument("hole_cards must be 1..7");
    }
    if (config.board_cards < 0 || config.board_cards > 5) {
        throw std::invalid_argument("board_cards must be 0..5");
    }
    if (config.showdown == HandConstruction::OmahaTwoAndThree ||
        config.showdown == HandConstruction::OmahaHiLo) {
        // Omaha deals 4 hole + 5 board and constructs exactly 2+3.
        if (config.hole_cards != 4 || config.board_cards != 5) {
            throw std::invalid_argument(
                "omaha showdown needs hole_cards 4 and board_cards 5");
        }
    } else if (config.showdown == HandConstruction::StudSeven) {
        // Seven-card stud: dealt street-by-street (2 down + 1 up, 3 more
        // up, river down), so the deck only needs third street up front.
        // Full-table worst case (8 × 7 = 56) exceeds one deck — but folds
        // shrink demand each street, and a short seventh street plays one
        // shared up card instead of one per seat, so 8-max is legal.
        if (config.hole_cards != 7 || config.board_cards != 0) {
            throw std::invalid_argument(
                "stud showdown needs hole_cards 7 and board_cards 0");
        }
        if (config.upcards != 4) {
            throw std::invalid_argument("stud needs upcards 4");
        }
        if (config.runouts != 1) {
            throw std::invalid_argument("stud cannot run it twice");
        }
        if (config.straddle != 0) {
            throw std::invalid_argument("stud has no straddle");
        }
        if (config.kill) {
            throw std::invalid_argument("stud has no kill");
        }
        if (config.bring_in < 0) {
            throw std::invalid_argument("bring_in cannot be negative");
        }
        if (config.bring_in >= config.small_blind && config.bring_in > 0) {
            throw std::invalid_argument("bring_in must be below small_blind");
        }
    } else if (config.showdown == HandConstruction::DrawFive ||
               config.showdown == HandConstruction::DeuceSeven) {
        // Five-card draw / 2-7 lowball: 5 private cards, no board, one
        // discard round (up to max_draw each).
        if (config.hole_cards != 5 || config.board_cards != 0) {
            throw std::invalid_argument(
                "draw showdown needs hole_cards 5 and board_cards 0");
        }
        if (config.max_draw < 1 || config.max_draw > 5) {
            throw std::invalid_argument("max_draw must be 1..5");
        }
        // Replacements come off the same shoe, so the worst case (every
        // seat drawing the max) must fit one deck. Six draw seats at the
        // classic 3-card cap deal 48 total (6×(5+3)); anything bigger needs
        // fewer seats or a smaller cap. The testing seam asks for the same
        // stub (see start_hand_from_deck).
        if (config.num_players * (config.hole_cards + config.max_draw) > 52) {
            throw std::invalid_argument(
                "not enough cards in the deck for full draws");
        }
    } else {
        // Best-five showdown needs a total that fits evaluate_best.
        const int total = config.hole_cards + config.board_cards;
        if (total < 5 || total > 7) {
            throw std::invalid_argument(
                "hole_cards + board_cards must be 5..7 for best-5 showdown");
        }
    }
    if (config.showdown != HandConstruction::StudSeven &&
        config.num_players * config.hole_cards + config.board_cards > 52) {
        throw std::invalid_argument("not enough cards in the deck");
    }
    if (config.board_cards == 0 && config.runouts != 1) {
        throw std::invalid_argument("runouts need board cards");
    }
    if (config.max_raises_per_round < 1) {
        throw std::invalid_argument("max_raises_per_round must be positive");
    }
}

}  // namespace cardengine
