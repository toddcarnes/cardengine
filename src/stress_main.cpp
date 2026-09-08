// Championship stress driver: plays whole brackets with rotating bots,
// logging every table result for later analysis (edge-case hunting, bot
// strength ranging). In-process bots: same brains as match.py, none of the
// pipe overhead across tens of thousands of hands.
//
// Exit codes: 0 bracket complete, 1 usage/file error, 2 hand cap reached,
// 3 estimate gate (re-run with --yes).
#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cardengine/bot.h"
#include "cardengine/championship.h"
#include "cardengine/types.h"

namespace {

void usage() {
    std::cerr
        << "usage: cardengine_stress --championship FILE --bots F1,F2,...\n"
           "       [--seed N] [--max-hands N] [--yes] [--out results.csv]\n"
           "\n"
           "Bots rotate across every seat in bracket order (fair by design).\n"
           "Large brackets need --yes; --max-hands aborts cleanly instead\n"
           "of running forever.\n";
}

struct Args {
    std::string championship;
    std::vector<std::string> bot_files;
    unsigned long long seed = 1;
    unsigned long long max_hands = 100000;
    bool yes = false;
    std::string out = "stress-results.csv";
};

bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](std::string& slot) {
            if (i + 1 >= argc) return false;
            slot = argv[++i];
            return true;
        };
        if (arg == "--championship") {
            if (!need_value(args.championship)) return false;
        } else if (arg == "--bots") {
            std::string list;
            if (!need_value(list)) return false;
            std::string item;
            std::istringstream in(list);
            while (std::getline(in, item, ',')) {
                if (!item.empty()) args.bot_files.push_back(item);
            }
        } else if (arg == "--seed") {
            std::string text;
            if (!need_value(text)) return false;
            try {
                args.seed = std::stoull(text);
            } catch (const std::exception&) {
                return false;
            }
        } else if (arg == "--max-hands") {
            std::string text;
            if (!need_value(text)) return false;
            try {
                args.max_hands = std::stoull(text);
            } catch (const std::exception&) {
                return false;
            }
            if (args.max_hands < 1) return false;
        } else if (arg == "--yes") {
            args.yes = true;
        } else if (arg == "--out") {
            if (!need_value(args.out)) return false;
        } else if (arg == "--help") {
            return false;
        } else {
            return false;
        }
    }
    return !args.championship.empty() && !args.bot_files.empty();
}

}  // namespace

int main(int argc, char** argv) {
    using namespace cardengine;
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage();
        return 1;
    }

    ChampionshipFile file;
    try {
        file = load_championship_file(args.championship);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::vector<BotFile> bot_files;
    std::vector<std::string> bot_names;
    try {
        for (const std::string& path : args.bot_files) {
            bot_files.push_back(load_bot_file(path));
            bot_names.push_back(bot_files.back().name);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::ofstream csv(args.out);
    if (!csv) {
        std::cerr << "error: cannot open '" << args.out << "'\n";
        return 1;
    }

    const ChampionshipEstimate estimate =
        estimate_championship(file.config);
    std::cout << "championship '" << file.name << "': "
              << estimate.stages << " stages, " << estimate.tournaments
              << " tables, " << estimate.entrants << " entrants, up to ~"
              << estimate.max_hands << " hands\n";
    if (!args.yes &&
        (estimate.tournaments > 8 || estimate.max_hands > 5000)) {
        std::cout << "large run: re-run with --yes to proceed\n";
        return 3;
    }

    Championship cup(file.config);
    csv << "stage,table,seed,hands,winner_seat,winner_bot,lineup,placements\n";
    std::map<std::string, int> wins;
    unsigned long long hands_total = 0;
    unsigned long long seed = args.seed;
    int table_index = 0;
    const int bot_count = static_cast<int>(bot_names.size());
    for (int stage = 0; stage < cup.num_stages(); ++stage) {
        for (int table = 0; table < cup.num_tables(stage); ++table) {
            Tournament& event = cup.tournament(stage, table);
            const int seats = event.table().num_seats();
            // Seating shuffle: straight rotation resonates when table size
            // shares a divisor with the bot count (e.g. 2 seats, 8 files
            // locks seat parity to bot parity). A per-table shuffle of the
            // files is fair for every shape and still deterministic.
            const unsigned long long table_seed = seed + table_index++;
            std::vector<int> order(static_cast<std::size_t>(bot_count));
            std::iota(order.begin(), order.end(), 0);
            std::mt19937_64 seating(table_seed ^ 0x9E3779B97F4A7C15ULL);
            std::shuffle(order.begin(), order.end(), seating);
            auto file_for = [&](int s) {
                const std::size_t n = order.size();
                const int wrapped = s % static_cast<int>(n);
                return order[static_cast<std::size_t>(wrapped)];
            };
            // Fresh bot per seat: adaptive personalities learn within each
            // table without leaking reads across unrelated tables. Seats
            // wrap the shuffled files (a 1-file roster fills every seat).
            std::vector<std::unique_ptr<Bot>> table_bots;
            for (int s = 0; s < seats; ++s) {
                table_bots.push_back(make_bot(bot_files[static_cast<std::size_t>(
                    file_for(s))]));
            }
            int hands = 0;
            while (!event.complete()) {
                if (hands_total >= args.max_hands) {
                    std::cout << "hand cap reached (" << args.max_hands
                              << "): partial results in '" << args.out
                              << "'\n";
                    return 2;
                }
                const std::size_t baseline =
                    event.table().events().size();
                try {
                    event.begin_hand(table_seed + hands);
                    Table& hand = event.table();
                    while (!hand.hand_complete()) {
                        const std::vector<int> pending =
                            hand.draws_pending();
                        if (!pending.empty()) {
                            // Draw exchange: in turn order from the button,
                            // exactly like protocol.cpp's step path.
                            const int drawer = pending[0];
                            hand.discard(
                                drawer,
                                table_bots[static_cast<std::size_t>(drawer)]
                                    ->choose_discards(
                                        make_view(hand, drawer)));
                        } else if (hand.acting() != -1) {
                            const int seat = hand.acting();
                            hand.act(seat,
                                     table_bots[static_cast<std::size_t>(seat)]
                                         ->decide(make_view(hand, seat)));
                        } else {
                            hand.deal_next_street();
                        }
                    }
                    hand.settle();
                } catch (const std::exception& e) {
                    // A bad engine hand (bot or table bug) must not kill a
                    // 150k-hand bracket: fold the table's current hand into
                    // a walkover for the next live seat and keep going.
                    // The stderr line names the seed for repro.
                    std::cerr << "hand " << hands << " table " << table
                              << " seed " << (table_seed + hands)
                              << " aborted: " << e.what() << "\n";
                    Table& hand = event.table();
                    while (!hand.hand_complete()) {
                        if (hand.acting() != -1) {
                            try {
                                hand.act(hand.acting(),
                                         {ActionType::Fold, 0});
                            } catch (const std::exception&) {
                                break;  // Fold illegal: frozen, skip below.
                            }
                        } else if (!hand.draws_pending().empty()) {
                            try {
                                hand.discard(hand.draws_pending()[0], {});
                            } catch (const std::exception&) {
                                break;  // Exchange stuck: frozen, skip below.
                            }
                        } else {
                            try {
                                hand.deal_next_street();
                            } catch (const std::exception&) {
                                break;  // Street stuck: frozen, skip below.
                            }
                        }
                    }
                    if (hand.hand_complete()) {
                        try {
                            hand.settle();
                        } catch (const std::exception&) {
                            // Settle itself failed: skip the hand.
                            ++hands;
                            ++hands_total;
                            continue;
                        }
                    } else {
                        // Unfinishable (frozen street): skip the hand by
                        // dealing the next one; the walkover stands.
                        ++hands;
                        ++hands_total;
                        continue;
                    }
                }
                event.finish_hand();
                Table& felt = event.table();
                const HandSummary summary = summarize_hand(
                    felt.events(), baseline, felt.events().size());
                // Bound memory: the event log is append-only, and limit
                // tables play ~10k hands before busting. Only the current
                // hand's slice is ever read (via baseline above), so drop
                // history after each hand. (protocol.cpp does the same.)
                felt.clear_events();
                for (int s = 0; s < seats; ++s) {
                    if (summary.seats[static_cast<std::size_t>(s)].played) {
                        table_bots[static_cast<std::size_t>(s)]->observe(
                            s, summary);
                    }
                }
                ++hands;
                ++hands_total;
            }
            const int winner = event.winner();
            const std::string& winner_bot =
                bot_names[static_cast<std::size_t>(file_for(winner))];
            // Bot display names are safe: printable ASCII with no CSV
            // separators (load_bot_file rejects anything else), so the
            // lineup needs no quoting and no parser can split it wrong.
            std::string lineup;
            for (int s = 0; s < seats; ++s) {
                if (s > 0) lineup += ";";
                lineup += bot_names[static_cast<std::size_t>(file_for(s))];
            }
            // Finishing order (champion first) for multiplayer ratings.
            std::string placements;
            for (const int seat : finishing_order(event)) {
                if (!placements.empty()) placements += ";";
                placements += std::to_string(seat);
            }
    csv << stage << "," << table << "," << table_seed << "," << hands
        << "," << winner << "," << winner_bot << "," << lineup << ","
        << placements << "\n";
            ++wins[winner_bot];
        }
    }

    std::cout << "complete: " << hands_total << " hands, champion seat "
              << cup.champion() << "\nwins:\n";
    for (const auto& [name, count] : wins) {
        std::cout << "  " << name << ": " << count << "\n";
    }
    return 0;
}
