// Out-of-process bot runner: one seat's brain on a pipe.
//
// The host (match script, GUI, future gateway) relays one `state <seat>`
// block plus one `options` line per decision; this process prints one
// `act ...` line back. Strict alternation, flushed replies, EOF exits.
// Any malfunction prints `error ...` and exits nonzero — fail loud, so the
// host can never mistake a crash for a check.
#include <iostream>
#include <string>

#include "cardengine/bot.h"
#include "cardengine/bot_runner.h"

namespace {

void usage() {
    std::cerr << "usage: cardengine_bot --seat N --bot <bot-file>\n";
}

}  // namespace

int main(int argc, char** argv) {
    int seat = -1;
    std::string bot_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seat" && i + 1 < argc) {
            try {
                seat = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                usage();
                return 2;
            }
        } else if (arg == "--bot" && i + 1 < argc) {
            bot_path = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    if (seat < 0 || bot_path.empty()) {
        usage();
        return 2;
    }

    cardengine::BotFile file;
    try {
        file = cardengine::load_bot_file(bot_path);
    } catch (const std::exception& e) {
        std::cout << "error " << e.what() << "\n" << std::flush;
        return 1;
    }
    const std::unique_ptr<cardengine::Bot> bot = cardengine::make_bot(file);

    std::string line;
    std::string state;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "end") {
            std::string options;
            if (!std::getline(std::cin, options)) break;
            try {
                std::cout << cardengine::decide_from_text(*bot, seat, state,
                                                          options)
                          << "\n"
                          << std::flush;
            } catch (const std::exception& e) {
                std::cout << "error " << e.what() << "\n" << std::flush;
                return 1;
            }
            state.clear();
        } else {
            state += line + "\n";
        }
    }
    return 0;
}
