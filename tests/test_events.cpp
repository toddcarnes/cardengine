// Event log: exact sequences, formatting, no-leak rule, accumulation.
#include <iostream>
#include <string>
#include <vector>

#include "cardengine/event.h"
#include "cardengine/protocol.h"

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<cardengine::Card> shoe(std::initializer_list<const char*> texts) {
    std::vector<cardengine::Card> out;
    for (const char* t : texts) out.push_back(cardengine::parse_card(t));
    return out;
}

void fold(cardengine::Table& t, int s) {
    t.act(s, {cardengine::ActionType::Fold, 0});
}
void chk(cardengine::Table& t, int s) {
    t.act(s, {cardengine::ActionType::Check, 0});
}
void call(cardengine::Table& t, int s) {
    t.act(s, {cardengine::ActionType::Call, 0});
}

template <typename T>
const T* as_event(const cardengine::Event& e) {
    return std::get_if<T>(&e);
}

}  // namespace

int main() {
    using namespace cardengine;

    // A fold-out hand logs exactly four events with exact contents.
    {
        GameConfig config;
        config.num_players = 3;
        Table table(config);
        table.start_hand_from_deck(shoe({"2c", "3d", "4h", "5s", "6c", "7d",
                                         "8h", "9s", "Tc", "Jd", "Qh"}));
        fold(table, 0);
        fold(table, 1);
        table.settle();

        const std::vector<Event>& events = table.events();
        check(events.size() == 4, "four events");
        const auto* started = as_event<HandStartedEvent>(events[0]);
        check(started != nullptr, "first is begin");
        check(started->button == 0 && !started->seeded, "begin fields");
        check(started->stacks == std::vector<int>({10000, 10000, 10000}),
              "pre-hand stacks");
        check(started->hole.size() == 3 && started->hole[0].size() == 2,
              "hole recorded");
        const auto* fold0 = as_event<ActionTakenEvent>(events[1]);
        const auto* fold1 = as_event<ActionTakenEvent>(events[2]);
        check(fold0 != nullptr && fold0->seat == 0 &&
                  fold0->action.type == ActionType::Fold &&
                  fold0->pot_after == 150,
              "first fold");
        check(fold1 != nullptr && fold1->seat == 1 && fold1->pot_after == 150,
              "second fold");
        const auto* settled = as_event<HandSettledEvent>(events[3]);
        check(settled != nullptr && !settled->showdown &&
                  settled->payouts.size() == 1 &&
                  settled->payouts[0].seat == 2 &&
                  settled->payouts[0].amount == 150,
              "settle event");

        check(std::string(event_name(events[0])) == "begin_hand", "name 0");
        check(std::string(event_name(events[3])) == "settle", "name 3");
        const std::string first_line = format_event(events[0]);
        check(contains(first_line, "begin_hand button 0 seed -"), "text head");
        check(contains(first_line, "stacks 10000,10000,10000"), "text stacks");
        const std::string last_line = format_event(events[3]);
        check(last_line == "settle showdown no payouts 2:150", "text settle");

        // No-leak rule: the folders' hole cards appear in begin_hand and
        // nowhere else in the hand's log.
        const std::string folded_hole =
            to_string(started->hole[0][0]) + "," + to_string(started->hole[0][1]);
        check(contains(first_line, folded_hole), "hole in begin");
        check(!contains(format_event(events[1]), folded_hole), "not in action");
        check(!contains(last_line, folded_hole), "not in settle");
    }

    // Streets, pots, and payouts across a showdown hand.
    {
        GameConfig config;
        config.num_players = 2;
        Table table(config);
        table.start_hand_from_deck(shoe({"7c", "As", "2d", "Ad", "Ks", "Qh",
                                         "Jh", "9c", "3d"}));
        call(table, 0);
        chk(table, 1);
        table.deal_next_street();
        chk(table, 1);
        chk(table, 0);
        table.deal_next_street();
        chk(table, 1);
        chk(table, 0);
        table.deal_next_street();
        chk(table, 1);
        chk(table, 0);
        table.settle();

        const std::vector<Event>& events = table.events();
        check(events.size() == 13, "thirteen events");
        const auto* flop = as_event<StreetDealtEvent>(events[3]);
        check(flop != nullptr && flop->street == Street::Flop &&
                  flop->cards.size() == 3, "flop event");
        check(format_event(events[3]) == "street flop Ks Qh Jh", "flop text");
        check(format_event(events[6]) == "street turn 9c", "turn text");
        check(format_event(events[9]) == "street river 3d", "river text");
        const auto* settled = as_event<HandSettledEvent>(events[12]);
        check(settled != nullptr && settled->showdown, "showdown flag");
        check(format_event(events[12]) ==
                  "settle showdown yes payouts 0:200",
              "settle text");
    }

    // Seeded starts record the seed; the log accumulates until cleared.
    {
        GameConfig config;
        config.num_players = 2;
        Table table(config);
        table.start_hand(41);
        while (!table.hand_complete()) {
            if (table.acting() != -1) {
                const int seat = table.acting();
                if (table.options(seat).can_check) {
                    chk(table, seat);
                } else {
                    call(table, seat);
                }
            } else {
                table.deal_next_street();
            }
        }
        table.settle();
        const std::size_t after_one = table.events().size();
        check(after_one > 4, "a hand logs many events");
        const auto* started =
            as_event<HandStartedEvent>(table.events()[0]);
        check(started != nullptr && started->seeded && started->seed == 41,
              "seed recorded");

        table.start_hand(42);
        check(table.events().size() > after_one, "log accumulates");
        table.clear_events();
        check(table.events().empty(), "clear empties");
    }

    // The protocol relays the log with `end` framing.
    {
        Session session;
        check(session.execute("start 5") == "ok", "start");
        session.execute("act fold");
        session.execute("act fold");
        session.execute("act fold");
        session.execute("act fold");
        session.execute("act fold");
        const std::string log = session.execute("log");
        check(contains(log, "begin_hand button 0 seed 5"), "log has begin");
        check(contains(log, "settle showdown no") == false, "not settled yet");
        session.execute("settle");
        const std::string log2 = session.execute("log");
        check(contains(log2, "payouts 2:150\nend"), "log has framed settle");
    }

    std::cout << "test_events ok\n";
    return 0;
}
