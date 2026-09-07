// Session persistence: snapshots, files, save/restore round-trips.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardengine/protocol.h"
#include "cardengine/session_file.h"
#include "cardengine/tournament.h"
#include "helpers.h"

namespace {

using cardengine::Session;
using cardengine::Table;
using cardengine::Tournament;
using cardengine::TournamentConfig;
using testutil::check;
using testutil::contains;
using testutil::expect_throws;

void play_one_hand(Session& session, int seed) {
    check(session.execute("start " + std::to_string(seed)) == "ok", "start");
    while (true) {
        const std::string state = session.execute("state");
        // acting -1 with no streets left means the hand is decided.
        if (contains(state, "acting -1")) {
            if (session.execute("deal") == "ok") continue;
            break;
        }
        // The acting seat folds (parsed from the options line's seat).
        const std::string options = session.execute("options");
        check(contains(options, "options seat"), "options pending");
        check(session.execute("act fold") == "ok", "fold");
    }
    const std::string done = session.execute("settle");
    check(contains(done, "ok"), "settle");
}

void play_out_table(Table& table) {
    using cardengine::ActionType;
    while (!table.hand_complete()) {
        if (table.acting() != -1) {
            const int seat = table.acting();
            if (table.options(seat).can_check) {
                table.act(seat, {ActionType::Check, 0});
            } else {
                table.act(seat, {ActionType::Call, 0});
            }
        } else {
            table.deal_next_street();
        }
    }
    table.settle();
}

}  // namespace

int main() {
    using namespace cardengine;

    // Table snapshots carry stacks, sit-outs, and the button; refuse mid-hand.
    {
        GameConfig config;
        config.num_players = 3;
        Table table(config);
        table.set_sitting_out(1, true);
        table.start_hand(7);
        while (!table.hand_complete()) {
            if (table.acting() != -1) {
                table.act(table.acting(), {ActionType::Fold, 0});
            } else {
                table.deal_next_street();
            }
        }
        table.settle();
        expect_throws<std::logic_error>(
            [&] {
                Table running(config);
                running.start_hand(7);
                (void)running.snapshot();
            },
            "snapshot mid-hand refused");
        const Table::Snapshot saved = table.snapshot();
        check(saved.stacks.size() == 3 && saved.sitting_out.size() == 3,
              "snapshot covers every seat");
        check(saved.sitting_out[1] && !saved.sitting_out[0],
              "snapshot keeps sit-outs");
        Table fresh(config);
        fresh.restore(saved);
        check(fresh.stack(0) == table.stack(0) &&
                  fresh.stack(1) == table.stack(1) &&
                  fresh.stack(2) == table.stack(2),
              "stacks carried");
        check(fresh.sitting_out(1) && fresh.button() == table.button(),
              "flags and button carried");
        fresh.start_hand(8);
        check(!fresh.in_hand(1), "sitter still skips");
        Table settled(config);
        settled.restore(saved);
        // Mismatched and negative snapshots never half-apply.
        Table::Snapshot bad = saved;
        bad.stacks.pop_back();
        expect_throws<std::invalid_argument>([&] { settled.restore(bad); },
                                "short stacks rejected");
        bad = saved;
        bad.stacks[0] = -5;
        expect_throws<std::invalid_argument>([&] { settled.restore(bad); },
                                "negative stack rejected");
        bad = saved;
        bad.button = 9;
        expect_throws<std::invalid_argument>([&] { settled.restore(bad); },
                                "bad button rejected");
        expect_throws<std::logic_error>(
            [&] {
                Table running(config);
                running.start_hand(7);
                running.restore(saved);
            },
            "restore mid-hand refused");
    }

    // Tournament snapshots carry the books; restore replays the level blinds.
    {
        TournamentConfig config;
        config.game.num_players = 2;
        config.game.starting_stack = 10000;
        config.buy_in = 100;
        config.prizes = {100};
        Tournament tournament(config);
        tournament.begin_hand(3);
        play_out_table(tournament.table());
        tournament.finish_hand();
        expect_throws<std::logic_error>(
            [&] {
                TournamentConfig c;
                c.game.num_players = 2;
                Tournament running(c);
                running.begin_hand(1);
                (void)running.snapshot();
            },
            "tournament snapshot mid-hand refused");
        const Tournament::Snapshot saved = tournament.snapshot();
        check(saved.prize_pool == 200, "pool snapshotted");
        check(saved.hands_into_level == 1, "clock snapshotted");
        TournamentConfig other;
        other.game.num_players = 2;
        other.game.starting_stack = 10000;
        Tournament fresh(other);
        fresh.restore(saved);
        check(fresh.prize_pool() == 200 && fresh.hands_into_level() == 1,
              "books carried");
        check(fresh.table().stack(0) == tournament.table().stack(0),
              "felt carried");
        fresh.begin_hand(9);
        check(fresh.table().in_hand(0) && fresh.table().in_hand(1),
              "restored tournament deals");
    }

    // Cash save/restore: stacks, button, sit-outs, and the log survive.
    {
        Session session;
        check(session.execute("sitout 1") == "ok", "sit out a seat");
        play_one_hand(session, 11);
        check(session.execute("save tmp_session_cash.txt") == "ok",
              "cash saves");
        SessionFile on_disk = load_session_file("tmp_session_cash.txt");
        // Mutate past the save, then restore and compare against the past.
        check(session.execute("resume 1") == "ok", "mutate flags");
        play_one_hand(session, 12);
        check(session.execute("restore tmp_session_cash.txt") == "ok",
              "cash restores");
        const std::string felt = session.execute("state");
        for (int seat = 0; seat < 6; ++seat) {
            const int want_stack = on_disk.stacks[static_cast<std::size_t>(seat)];
            check(contains(felt, "seat " + std::to_string(seat) + " stack " +
                                    std::to_string(want_stack)),
                  "stack matches the save");
        }
        check(contains(felt, "seat 1 ") && contains(felt, " out hole"),
              "sitter flag matches the save");
        check(contains(felt, "button " + std::to_string(on_disk.button)),
              "button matches the save");
        check(session.execute("log") == session.execute("log"),
              "log stable");
        // The restored table deals on: stacks, button, and sit-outs held.
        check(session.execute("start 13") == "ok", "deals after restore");
        const std::string dealt = session.execute("state");
        check(contains(dealt, "seat 1 ") && contains(dealt, " out hole --"),
              "sitter still skips");
        // Mid-hand save/restore both refuse; bad files name their problem.
        check(contains(session.execute("save tmp_session_cash.txt"), "error"),
              "no saves mid-hand");
        check(contains(session.execute("restore tmp_session_cash.txt"), "error"),
              "no restores mid-hand");
        check(contains(session.execute("save"), "usage"), "save usage");
        check(contains(session.execute("restore"), "usage"), "restore usage");
        check(contains(session.execute("restore tmp_missing_xyz.txt"), "error"),
              "missing file");
        std::remove("tmp_session_cash.txt");
    }

    // Tournament save/restore: books, felt, and log survive the crash.
    {
        const char* path = "tmp_session_tourney.txt";
        {
            std::ofstream file(path);
            file << "format_version = 1\nname = S\nnum_players = 3\n"
                    "starting_stack = 10000\nbuy_in = 100\nprizes = 60, 40\n"
                    "level = 50, 100, 0, 2\n"
                    "level = 100, 200, 0, 2\n";
        }
        Session session;
        check(session.execute(std::string("tload ") + path) == "ok", "tload");
        play_one_hand(session, 21);
        check(session.execute("tlevel") == "ok", "clock advances");
        check(session.execute("trebuy 0") == "ok", "pool grows");
        const std::string before = session.execute("tstatus");
        const std::string felt_before = session.execute("state");
        check(session.execute("save tmp_session_save.txt") == "ok",
              "tournament saves");
        // A fresh session knows nothing until it restores.
        Session reboot;
        check(contains(reboot.execute("tstatus"), "error"), "reboot is empty");
        check(reboot.execute("restore tmp_session_save.txt") == "ok",
              "reboot restores");
        check(reboot.execute("tstatus") == before, "books match the save");
        const std::string felt_after = reboot.execute("state");
        check(contains(felt_after, "button ") && contains(felt_after, "seat 0 stack "),
              "felt restores with button and stacks");
        for (int seat = 0; seat < 3; ++seat) {
            const std::string needle =
                "seat " + std::to_string(seat) + " stack ";
            check(contains(felt_before, needle) && contains(felt_after, needle),
                  "seat lines present both sides");
        }
        // And play continues: levels, pool, and button all held.
        check(reboot.execute("start 22") == "ok", "deals after restore");
        check(contains(reboot.execute("tstatus"), "tournament level 1/2"),
              "level held");
        check(contains(reboot.execute("tstatus"), "pool 400"), "pool held");
        // Bots clear on restore (reseat them); files validate line by line.
        check(reboot.execute("bots") == "bots -", "bots cleared");
        {
            std::ofstream bad("tmp_session_bad.txt");
            bad << "format_version = 1\nmode = cash\n";
        }
        check(contains(reboot.execute("restore tmp_session_bad.txt"), "error"),
              "short file rejected");
        {
            std::ofstream bad("tmp_session_bad.txt");
            bad << "format_version = 1\nmode = cash\nnum_players = 6\n"
                   "stacks = 1,2\nbutton = 0\nevents = 0\n";
        }
        check(contains(reboot.execute("restore tmp_session_bad.txt"), "error"),
              "ragged stacks rejected");
        {
            std::ofstream bad("tmp_session_bad.txt");
            bad << "format_version = 1\nmode = cash\nnum_players = 2\n"
                   "stacks = 10000,10000\nbutton = 0\nevents = 2\n"
                   "begin_hand button 0 seed 1 stacks 10000,10000 hole Ac,Kd|Qh,Js\n";
        }
        check(contains(reboot.execute("restore tmp_session_bad.txt"), "error"),
              "ragged log rejected");
        std::remove("tmp_session_bad.txt");
        std::remove("tmp_session_save.txt");
        std::remove(path);
    }

    // Session files: keys, ledgers, and the log round-trip through text.
    {
        Session session;
        check(session.execute("sitout 2") == "ok", "sit out");
        play_one_hand(session, 31);
        check(session.execute("save tmp_session_file.txt") == "ok", "save");
        SessionFile file = load_session_file("tmp_session_file.txt");
        check(!file.tournament, "cash mode saved");
        check(file.stacks.size() == 6 && file.button >= 0, "felt saved");
        check(file.sitting_out.size() == 6 && file.sitting_out[2],
              "sit-outs saved");
        check(!file.events.empty(), "log saved");
        std::ostringstream out;
        save_session_file(file, out);
        std::istringstream in(out.str());
        const SessionFile back = parse_session(in);
        check(back.stacks == file.stacks && back.button == file.button &&
                  back.sitting_out == file.sitting_out &&
                  back.events.size() == file.events.size(),
              "file round-trips");
        for (std::size_t i = 0; i < file.events.size(); ++i) {
            check(format_event(back.events[i]) == format_event(file.events[i]),
                  "log lines round-trip");
        }
        // Every malformed shape names its problem, never half-parses.
        expect_throws<std::invalid_argument>(
            [] {
                std::istringstream bad("format_version = 1\nmode = cash\n");
                parse_session(bad);
            },
            "short file");
        expect_throws<std::invalid_argument>(
            [] {
                std::istringstream bad(
                    "format_version = 1\nmode = tournament\nnum_players = "
                    "2\nstacks = 1,2\nbutton = 0\nevents = 0\n");
                parse_session(bad);
            },
            "tournament without ledgers");
        expect_throws<std::invalid_argument>(
            [] {
                std::istringstream bad(
                    "format_version = 1\nmode = cash\nnum_players = 2\nstacks "
                    "= 10000,10000\nbutton = 0\nevents = 1\nfumble 1\n");
                parse_session(bad);
            },
            "bad log line");
        expect_throws<std::invalid_argument>(
            [] { load_session_file("tmp_missing_session_xyz.txt"); },
            "missing session file");
        std::remove("tmp_session_file.txt");
    }

    std::cout << "test_session ok\n";
    return 0;
}
