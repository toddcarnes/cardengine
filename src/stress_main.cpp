// Championship stress driver: plays whole brackets with rotating bots,
// logging every table result for later analysis (edge-case hunting, bot
// strength ranging). In-process bots: same brains as match.py, none of the
// pipe overhead across tens of thousands of hands.
//
// Exit codes: 0 bracket complete, 1 usage/file error, 2 hand cap reached,
// 3 estimate gate (re-run with --yes).
#include <algorithm>
#include <cstdint>
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

// Minimal CSV escaping: quote fields containing , " or newline.
std::string csv_field(const std::string& text) {
    if (text.find_first_of(",\"\n") == std::string::npos) return text;
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
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
                return order[static_cast<std::size_t>(
                    s % static_cast<int>(order.size()))];
            };
            // Fresh bot per seat: adaptive personalities learn within each
            // table without leaking reads across unrelated tables.
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
                event.begin_hand(table_seed + hands);
                Table& felt = event.table();
                while (!felt.hand_complete()) {
                    if (felt.acting() != -1) {
                        const int seat = felt.acting();
                        felt.act(seat,
                                 table_bots[static_cast<std::size_t>(seat)]
                                     ->decide(make_view(felt, seat)));
                    } else {
                        felt.deal_next_street();
                    }
                }
                felt.settle();
                event.finish_hand();
                const HandSummary summary = summarize_hand(
                    felt.events(), baseline, felt.events().size());
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
                << "," << winner << "," << csv_field(winner_bot) << ","
                << csv_field(lineup) << "," << placements << "\n";
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
