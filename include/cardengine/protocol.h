#pragma once

#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <string>

#include "cardengine/bot.h"
#include "cardengine/config.h"
#include "cardengine/table.h"
#include "cardengine/tournament.h"

namespace cardengine {

// Text protocol for outside programs (spec: docs/PROTOCOL.md). One line in, one reply
// out: `ok ...`, `error ...`, or `bye` on one line; `state` and `log` reply
// with blocks terminated by `end`; `settle` and `tstatus` reply with prelude
// lines and a final `ok`. Every reply is exactly one of those shapes.
//
// Session is the testable unit: drive it line by line with execute().
// run_protocol() wraps it around streams for the real engine process and
// flushes after every reply (required: GUIs read us through a pipe).
class Session {
public:
    Session();

    // Executes one command line (without the trailing newline) and returns
    // the reply. Never throws: engine bugs surface as `error ...`, never
    // as a dead process.
    std::string execute(const std::string& line);

private:
    // view_seat < 0 is the full local-trust dump; otherwise that seat's
    // hole cards are shown and every other seat is hidden.
    std::string do_state(int view_seat) const;
    // Cash table, or the tournament's table when a tournament is loaded.
    Table& active_table();
    const Table& active_table() const;

    GameConfig config_;
    Table table_;
    // Tournament mode replaces the cash table (null = cash game).
    std::unique_ptr<Tournament> tournament_;
    // Automated seats for same-machine bots (in-process convenience;
    // out-of-process runners use filtered views instead).
    std::map<int, std::unique_ptr<Bot>> bots_;
    // Event-log index where the current hand began (for post-hand observe).
    std::size_t hand_events_begin_ = 0;
};

int run_protocol(std::istream& in, std::ostream& out);

}  // namespace cardengine
