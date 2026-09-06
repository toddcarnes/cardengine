// Protocol sessions: replies, errors, and a full driven hand.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cardengine/protocol.h"

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void check(bool condition, const std::string& message) {
    check(condition, message.c_str());
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string find_line(const std::string& text, const std::string& prefix) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) return line;
    }
    return "";
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word) out.push_back(word);
    return out;
}

}  // namespace

int main() {
    using namespace cardengine;

    // Unknown input and empty sessions never crash, only complain.
    {
        Session s;
        check(s.execute("frobnicate") == "error unknown command 'frobnicate'",
              "unknown command");
        check(s.execute("") == "error empty command", "empty line");
        check(s.execute("state").find("street none") != std::string::npos,
              "idle state");
        check(s.execute("options") == "error no action pending",
              "idle options");
        check(contains(s.execute("act fold"), "error"), "act with no hand");
        check(contains(s.execute("deal"), "error"), "deal with no hand");
        check(contains(s.execute("settle"), "error"), "settle with no hand");
        check(s.execute("start") == "error usage: start <seed>",
              "start usage");
        check(s.execute("start abc") == "error bad seed 'abc'", "bad seed");
        check(s.execute("load") == "error usage: load <game-file>",
              "load usage");
        check(contains(s.execute("load tmp_missing_xyz.txt"), "error"),
              "missing file");
    }

    // Driving a whole fold-out hand through the protocol.
    {
        Session s;
        check(s.execute("help").substr(0, 2) == "ok", "help");
        check(s.execute("start 7") == "ok", "start");
        check(s.execute("start 8").substr(0, 5) == "error",
              "double start rejected");
        const std::string opts = s.execute("options");
        check(opts == "options seat 3 check no call 100 raise yes min 200 "
                       "max 10000",
              "UTG options: " + opts);
        check(contains(s.execute("act check"), "error"), "check faces blind");
        check(contains(s.execute("act raise"), "amount"), "raise needs amount");
        check(contains(s.execute("act raise 150"), "error"), "short raise");
        check(contains(s.execute("act dance"), "error"), "bad action");
        for (int i = 0; i < 5; ++i) {
            const std::string reply = s.execute("act fold");
            check(reply == "ok", "fold accepted");
        }
        const std::string state = s.execute("state");
        check(contains(state, "acting -1"), "nobody left to act");
        check(contains(state, "pot 150"), "blinds in the pot");
        check(contains(state, "end"), "state terminated");
        check(contains(s.execute("deal"), "error"), "deal decided hand");
        const std::string done = s.execute("settle");
        check(contains(done, "showdown no"), "no showdown");
        check(contains(done, "payout 2 150"), "BB takes it");
        // Stacks persist: a second hand starts with winner's chips.
        check(s.execute("start 9") == "ok", "second hand");
        const std::string state2 = s.execute("state");
        check(contains(state2, "seat 1 stack 9950"), "chips carried over");
        check(contains(state2, "button 1"), "button advanced");
    }

    // Loading a game file re-tables the session.
    {
        const char* path = "tmp_test_heads_up.txt";
        {
            std::ofstream file(path);
            file << "format_version = 1\nname = HU\nnum_players = 2\n";
        }
        Session s;
        check(s.execute(std::string("load ") + path) == "ok", "load");
        check(s.execute("start 1") == "ok", "start heads-up");
        const std::string state = s.execute("state");
        check(contains(state, "seat 1 stack"), "two seats listed");
        check(!contains(state, "seat 2 "), "no third seat");
        std::remove(path);
    }

    // The stream loop relays replies and stops at quit.
    {
        Session unused;
        (void)unused;
        std::istringstream in("help\nquit\nstart 1\n");
        std::ostringstream out;
        check(run_protocol(in, out) == 0, "loop returns 0");
        const std::string text = out.str();
        check(contains(text, "ok commands"), "help relayed");
        check(contains(text, "bye"), "quit relayed");
        check(!contains(text, "street"), "stops at quit");
    }

    // Filtered views: your cards shown, opponents hidden, current posted.
    {
        Session s;
        check(s.execute("start 7") == "ok", "start");
        const std::string full = s.execute("state");
        check(contains(full, "current 100"), "current bet posted");
        const std::string seat3 = find_line(full, "seat 3 ");
        const std::vector<std::string> toks = split(seat3);
        check(toks.size() == 13, "full seat line has hole cards");
        const std::string hole = toks[11] + " " + toks[12];

        const std::string view = s.execute("state 3");
        check(contains(find_line(view, "seat 3 "), hole), "own cards shown");
        check(find_line(view, "seat 4 ").rfind("hole --") != std::string::npos,
              "opponents hidden");
        check(contains(s.execute("state 9"), "error"), "seat out of range");
        check(contains(s.execute("state x"), "error"), "bad seat text");
    }

    std::cout << "test_protocol ok\n";
    return 0;
}
