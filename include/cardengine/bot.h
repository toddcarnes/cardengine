#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "cardengine/card.h"
#include "cardengine/table.h"

namespace cardengine {

enum class BotStyle { Random, Heuristic };

// A tunable bot personality, shareable as a text file exactly like game
// files (same key=value conventions). Parameters, not code: strength and
// style sliders the GUI's future bot designer can author.
//
//   format_version = 1
//   name = "Tight Ted"
//   description = "..."
//   style = heuristic        # heuristic | random
//   mistake_rate = 0.05      # 0..1: decisions replaced by a random action
//   aggression = 0.6         # 0..1: bet sizing and thin-value frequency
//   looseness = 0.3          # 0..1: how weak a hand still continues
//   seed = 7                 # mistake/random RNG seed (deterministic)
struct BotFile {
    int format_version = 1;
    std::string name;
    std::string description;
    BotStyle style = BotStyle::Heuristic;
    double mistake_rate = 0.0;
    double aggression = 0.5;
    double looseness = 0.3;
    std::uint64_t seed = 0;
};

void validate_bot(const BotFile& file);

// Throws std::invalid_argument with line numbers, like parse_game.
BotFile parse_bot(std::istream& in);
BotFile load_bot_file(const std::string& path);

// Everything a bot may see: its own hole cards plus public table state.
// Built from the authoritative Table, so bots cannot peek by construction.
struct SeatView {
    int seat = -1;
    std::vector<Card> hole;
    std::vector<Card> board;
    Street street = Street::None;
    int stack = 0;
    int pot = 0;
    int to_call = 0;
    int current_bet = 0;
    bool can_check = false;
    int call_amount = 0;
    bool can_raise = false;
    int min_raise_to = 0;
    int max_raise_to = 0;
};

SeatView make_view(const Table& table, int seat);

class Bot {
public:
    virtual ~Bot() = default;
    virtual Action decide(const SeatView& view) = 0;
    virtual const std::string& name() const = 0;
};

std::unique_ptr<Bot> make_bot(const BotFile& file);

}  // namespace cardengine
