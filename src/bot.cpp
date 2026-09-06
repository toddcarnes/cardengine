#include "cardengine/bot.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

#include "cardengine/detail/kv.h"
#include "cardengine/hand.h"

namespace cardengine {

using detail::lower;
using detail::parse_double;
using detail::parse_int;
using detail::trim;
using detail::unquote;

void validate_bot(const BotFile& file) {
    if (file.format_version != 1) {
        throw std::invalid_argument("unsupported bot format_version");
    }
    if (file.mistake_rate < 0.0 || file.mistake_rate > 1.0) {
        throw std::invalid_argument("mistake_rate must be 0..1");
    }
    if (file.aggression < 0.0 || file.aggression > 1.0) {
        throw std::invalid_argument("aggression must be 0..1");
    }
    if (file.looseness < 0.0 || file.looseness > 1.0) {
        throw std::invalid_argument("looseness must be 0..1");
    }
}

BotFile parse_bot(std::istream& in) {
    BotFile bot;
    bool saw_version = false;
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#') continue;
        const std::size_t eq = text.find('=');
        if (eq == std::string::npos) {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": expected 'key = value'");
        }
        const std::string key = lower(trim(text.substr(0, eq)));
        const std::string value = trim(text.substr(eq + 1));
        if (value.empty()) {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": missing value");
        }
        if (key == "format_version") {
            bot.format_version = parse_int(value, lineno);
            if (bot.format_version != 1) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": unsupported format_version (want 1)");
            }
            saw_version = true;
        } else if (key == "name") {
            bot.name = unquote(value);
        } else if (key == "description") {
            bot.description = unquote(value);
        } else if (key == "style") {
            const std::string s = lower(value);
            if (s == "random") {
                bot.style = BotStyle::Random;
            } else if (s == "heuristic") {
                bot.style = BotStyle::Heuristic;
            } else {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": style must be random or heuristic");
            }
        } else if (key == "mistake_rate") {
            bot.mistake_rate = parse_double(value, lineno);
        } else if (key == "aggression") {
            bot.aggression = parse_double(value, lineno);
        } else if (key == "looseness") {
            bot.looseness = parse_double(value, lineno);
        } else if (key == "seed") {
            try {
                std::size_t used = 0;
                bot.seed = std::stoull(value, &used);
                if (used != value.size()) {
                    throw std::invalid_argument("trailing characters");
                }
            } catch (const std::exception&) {
                throw std::invalid_argument("line " + std::to_string(lineno) +
                                            ": bad seed '" + value + "'");
            }
        } else {
            throw std::invalid_argument("line " + std::to_string(lineno) +
                                        ": unknown key '" + key + "'");
        }
    }
    if (!saw_version) {
        throw std::invalid_argument("missing required key 'format_version'");
    }
    try {
        validate_bot(bot);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string("invalid bot: ") + e.what());
    }
    return bot;
}

BotFile load_bot_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::invalid_argument("cannot open bot file '" + path + "'");
    }
    try {
        return parse_bot(file);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

SeatView make_view(const Table& table, int seat) {
    SeatView view;
    view.seat = seat;
    view.hole = table.hole_cards(seat);
    view.board = table.board();
    view.street = table.street();
    view.stack = table.stack(seat);
    view.pot = table.pot_total();
    view.to_call = table.to_call(seat);
    view.current_bet = table.current_bet();
    const ActionOptions opts = table.options(seat);
    view.can_check = opts.can_check;
    view.call_amount = opts.call_amount;
    view.can_raise = opts.can_raise;
    view.min_raise_to = opts.min_raise_to;
    view.max_raise_to = opts.max_raise_to;
    return view;
}

namespace {

// Uniform random legal action. The baseline every real bot must beat.
class RandomBot : public Bot {
public:
    explicit RandomBot(const BotFile& file)
        : name_(file.name), rng_(file.seed) {}

    Action decide(const SeatView& view) override {
        std::vector<Action> legal{{ActionType::Fold, 0}};
        if (view.can_check) {
            legal.push_back({ActionType::Check, 0});
        } else if (view.call_amount > 0 || view.to_call == 0) {
            legal.push_back({ActionType::Call, 0});
        }
        if (view.can_raise) {
            std::uniform_int_distribution<int> sizing(view.min_raise_to,
                                                      view.max_raise_to);
            legal.push_back({ActionType::Raise, sizing(rng_)});
        }
        std::uniform_int_distribution<std::size_t> pick(0, legal.size() - 1);
        return legal[pick(rng_)];
    }

    const std::string& name() const override { return name_; }

private:
    std::string name_;
    std::mt19937_64 rng_;
};

double card_points(Rank r) {
    switch (r) {
        case Rank::Ace: return 0.55;
        case Rank::King: return 0.50;
        case Rank::Queen: return 0.45;
        case Rank::Jack: return 0.40;
        case Rank::Ten: return 0.35;
        default:
            return static_cast<double>(static_cast<int>(r) - 2) * 0.02;
    }
}

// Rough 0..1 hand strength: Chen-style points preflop, category-based after.
double strength(const SeatView& view) {
    if (view.board.empty()) {
        std::vector<Rank> ranks;
        for (const Card& c : view.hole) ranks.push_back(c.rank);
        std::sort(ranks.begin(), ranks.end());
        for (std::size_t i = 1; i < ranks.size(); ++i) {
            if (ranks[i] == ranks[i - 1]) {
                return 0.70 +
                       static_cast<double>(static_cast<int>(ranks[i]) - 2) *
                           0.02;
            }
        }
        const Rank hi = ranks.back();
        const Rank lo = ranks.size() > 1 ? ranks[ranks.size() - 2] : hi;
        double value = card_points(hi) + card_points(lo) * 0.5;
        if (view.hole.size() == 2 && view.hole[0].suit == view.hole[1].suit) {
            value += 0.08;
        }
        if (static_cast<int>(hi) - static_cast<int>(lo) <= 2) value += 0.05;
        if (value > 0.95) value = 0.95;
        return value;
    }
    std::vector<Card> all = view.board;
    all.insert(all.end(), view.hole.begin(), view.hole.end());
    const HandValue value = evaluate_best(all);
    double base = 0.15;
    switch (value.category) {
        case HandCategory::HighCard: base = 0.15; break;
        case HandCategory::OnePair: base = 0.35; break;
        case HandCategory::TwoPair: base = 0.50; break;
        case HandCategory::ThreeOfAKind: base = 0.62; break;
        case HandCategory::Straight: base = 0.72; break;
        case HandCategory::Flush: base = 0.80; break;
        case HandCategory::FullHouse: base = 0.88; break;
        case HandCategory::FourOfAKind: base = 0.95; break;
        case HandCategory::StraightFlush: base = 1.00; break;
    }
    const double kick =
        static_cast<double>(static_cast<int>(value.tiebreak[0]) - 2) / 12.0;
    double total = base + kick * 0.08;
    if (total > 1.0) total = 1.0;
    return total;
}

class HeuristicBot : public Bot {
public:
    explicit HeuristicBot(const BotFile& file)
        : file_(file), rng_(file.seed), random_(file) {}

    Action decide(const SeatView& view) override {
        // Mistakes keep bots beatable and distinct: a random legal action.
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (unit(rng_) < file_.mistake_rate) {
            return random_.decide(view);
        }
        const double s = strength(view);
        if (view.to_call == 0) {
            if (view.can_raise &&
                s > 0.60 - file_.aggression * 0.15) {
                return {ActionType::Raise, size_bet(view)};
            }
            if (view.can_check) return {ActionType::Check, 0};
            return {ActionType::Call, 0};  // Zero-cost call, same as check.
        }
        if (s >= 0.62 && view.can_raise) return {ActionType::Raise, size_bet(view)};
        const double pot_odds =
            static_cast<double>(view.to_call) /
            static_cast<double>(view.pot + view.to_call);
        if (view.can_raise && s + file_.looseness * 0.25 >= pot_odds * 2.0) {
            return {ActionType::Call, 0};
        }
        if (!view.can_raise && s + file_.looseness * 0.25 >= pot_odds) {
            return {ActionType::Call, 0};
        }
        return {ActionType::Fold, 0};
    }

    const std::string& name() const override { return file_.name; }

private:
    int size_bet(const SeatView& view) const {
        const double frac = 0.5 + 0.5 * file_.aggression;
        const int target =
            view.current_bet +
            static_cast<int>(static_cast<double>(view.pot) * frac);
        if (target < view.min_raise_to) return view.min_raise_to;
        if (target > view.max_raise_to) return view.max_raise_to;
        return target;
    }

    BotFile file_;
    std::mt19937_64 rng_;
    RandomBot random_;
};

}  // namespace

std::unique_ptr<Bot> make_bot(const BotFile& file) {
    validate_bot(file);
    if (file.style == BotStyle::Random) {
        return std::make_unique<RandomBot>(file);
    }
    return std::make_unique<HeuristicBot>(file);
}

}  // namespace cardengine
