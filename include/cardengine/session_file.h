#pragma once

// A saved session: everything needed to resume after a crash or a move to
// another machine. Plain `key = value` lines (same conventions as game
// files: `#` comments, blank lines ignored, every failure names its line),
// followed by the raw event-log lines they describe:
//
//   format_version = 2
//   mode = tournament            # cash | tournament
//   num_players = 6              # any game-file key overrides defaults
//   starting_stack = 10000
//   ...
//   stacks = 10000,9500,...      # per seat, in order
//   sitting_out = 0,1,0,...      # optional, defaults to all seated
//   button = 2
//   kill_pending = 0             # full kill armed for the next hand (v2+)
//   buy_in = 10000               # tournament books below (tournament only)
//   prizes = 50, 30, 20
//   level = 50, 100, 0, 10       # repeatable (or 5 numbers with minutes)
//   level_index = 1
//   hands_into_level = 3
//   level_elapsed = 61           # seconds banked in this level
//   prize_pool = 60000
//   prize_awarded = 0
//   eliminated = 0,0,1,...       # 0 seated / 1 out, per seat
//   places = 0,0,3,...
//   prizes_earned = 0,0,6000,...
//   events = 14                  # the next 14 lines are raw log lines
//   begin_hand button 0 seed 7 stacks ...
//   ...
//
// Seated bots are deliberately NOT saved: `addbot` reseats them after a
// restore (adaptive reads rebuild from the restored log as new hands are
// observed). A snapshot is always between hands — saving or restoring
// mid-hand is refused, never half-done.
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "cardengine/config.h"
#include "cardengine/event.h"
#include "cardengine/tournament.h"

namespace cardengine {

struct SessionFile {
    int format_version = 1;
    bool tournament = false;
    GameConfig game;
    // Tournament books (tournament mode only).
    std::vector<BlindLevel> levels;
    std::vector<int> prizes;  // Percentages by place (pool shares).
    int buy_in = 0;
    int level_index = 0;
    int hands_into_level = 0;
    int level_elapsed = 0;  // Seconds banked in this level (timed levels).
    int prize_pool = 0;
    int prize_awarded = 0;
    std::vector<bool> eliminated;
    std::vector<int> places;
    std::vector<int> prizes_earned;  // Per seat, booked so far.
    // Felt state (both modes).
    std::vector<int> stacks;
    std::vector<bool> sitting_out;
    int button = 0;
    bool kill_pending = false;  // Full kill armed for the next hand.
    // Audit trail: raw log lines, oldest first.
    std::vector<Event> events;
};

// Throws std::invalid_argument with line numbers ("line N: ..." or
// "<path>: ..." via load_session_file).
SessionFile parse_session(std::istream& in);
SessionFile load_session_file(const std::string& path);
void save_session_file(const SessionFile& session, std::ostream& out);

}  // namespace cardengine
