#include "cardengine/bot.h"

#include <algorithm>
#include <fstream>
#include <map>
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
    if (file.survival < 0.0 || file.survival > 1.0) {
        throw std::invalid_argument("survival must be 0..1");
    }
    if (file.bluff_rate < 0.0 || file.bluff_rate > 1.0) {
        throw std::invalid_argument("bluff_rate must be 0..1");
    }
    if (file.defense < 0.0 || file.defense > 2.0) {
        throw std::invalid_argument("defense must be 0..2");
    }
    if (file.position_weight < 0.0 || file.position_weight > 2.0) {
        throw std::invalid_argument("position_weight must be 0..2");
    }
    if (file.adapt_rate < 0.0 || file.adapt_rate > 2.0) {
        throw std::invalid_argument("adapt_rate must be 0..2");
    }
    if (file.barrels < 0.0 || file.barrels > 2.0) {
        throw std::invalid_argument("barrels must be 0..2");
    }
    if (file.planning < 0 || file.planning > 2) {
        throw std::invalid_argument("planning must be 0..2");
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
            } else if (s == "adaptive") {
                bot.style = BotStyle::Adaptive;
            } else if (s == "gto") {
                bot.style = BotStyle::Gto;
            } else {
                throw std::invalid_argument(
                    "line " + std::to_string(lineno) +
                    ": style must be random, heuristic, adaptive, or gto");
            }
        } else if (key == "mistake_rate") {
            bot.mistake_rate = parse_double(value, lineno);
        } else if (key == "aggression") {
            bot.aggression = parse_double(value, lineno);
        } else if (key == "looseness") {
            bot.looseness = parse_double(value, lineno);
        } else if (key == "survival") {
            bot.survival = parse_double(value, lineno);
        } else if (key == "bluff_rate") {
            bot.bluff_rate = parse_double(value, lineno);
        } else if (key == "defense") {
            bot.defense = parse_double(value, lineno);
        } else if (key == "position_weight") {
            bot.position_weight = parse_double(value, lineno);
        } else if (key == "adapt_rate") {
            bot.adapt_rate = parse_double(value, lineno);
        } else if (key == "barrels") {
            bot.barrels = parse_double(value, lineno);
        } else if (key == "planning") {
            bot.planning = parse_int(value, lineno);
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

void save_bot_file(const BotFile& bot, std::ostream& out) {
    out << "# CardEngine bot file (format " << bot.format_version << ")\n";
    out << "format_version = " << bot.format_version << "\n";
    out << "name = \"" << bot.name << "\"\n";
    out << "description = \"" << bot.description << "\"\n";
    out << "style = ";
    switch (bot.style) {
        case BotStyle::Random:
            out << "random\n";
            break;
        case BotStyle::Heuristic:
            out << "heuristic\n";
            break;
        case BotStyle::Adaptive:
            out << "adaptive\n";
            break;
        case BotStyle::Gto:
            out << "gto\n";
            break;
    }
    out << "mistake_rate = " << bot.mistake_rate << "\n";
    out << "aggression = " << bot.aggression << "\n";
    out << "looseness = " << bot.looseness << "\n";
    out << "survival = " << bot.survival << "\n";
    out << "bluff_rate = " << bot.bluff_rate << "\n";
    out << "defense = " << bot.defense << "\n";
    out << "position_weight = " << bot.position_weight << "\n";
    out << "adapt_rate = " << bot.adapt_rate << "\n";
    out << "barrels = " << bot.barrels << "\n";
    out << "planning = " << bot.planning << "\n";
    out << "seed = " << bot.seed << "\n";
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
    view.num_seats = table.num_seats();
    view.position = (seat - table.button() + table.num_seats()) %
                    table.num_seats();
    view.showdown = table.config().showdown;
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

// Rough 0..1 made-hand strength: Chen-style points preflop,
// category-based after.
HandValue omaha_current(const std::vector<Card>& hole,
                        const std::vector<Card>& board);
double made_strength(const SeatView& view) {
    if (view.board.empty()) {
        // Omaha deals four: pairs (6 combos, not 1) and rundowns/connectivity
        // dominate; raw high cards leak value without coordination.
        if (view.showdown == HandConstruction::OmahaTwoAndThree) {
            std::vector<Rank> ranks;
            for (const Card& c : view.hole) ranks.push_back(c.rank);
            std::sort(ranks.begin(), ranks.end());
            double value = 0.30;
            int pairs = 0;
            for (std::size_t i = 1; i < ranks.size(); ++i) {
                if (ranks[i] == ranks[i - 1]) ++pairs;
            }
            if (pairs > 0) {
                // Two pair in four cards is usually bottom-two junk, not a
                // premium: only aces-up+ or trips+ get the premium score.
                const bool premium =
                    ranks[3] == ranks[2] &&
                    (ranks[3] == Rank::Ace ||
                     (ranks[1] == ranks[0] &&
                      static_cast<int>(ranks[3]) >= 11));
                if (premium) {
                    value = 0.60 + static_cast<double>(
                                       static_cast<int>(ranks.back()) - 2) *
                                       0.02;
                } else {
                    value = 0.30 + static_cast<double>(
                                       static_cast<int>(ranks.back()) - 2) *
                                       0.01;
                }
            } else {
                value = card_points(ranks[3]) + card_points(ranks[2]) * 0.4;
                // Coordination: suits together, ranks connected. Bare high
                // cards with neither are PLO trash (everyone makes hands).
                // The penalty has to clear the open-raise bar (~0.45), not
                // just dent the number: -0.3 still calls 2x pot odds.
                int suited_max = 0;
                int suits[4] = {};
                for (const Card& c : view.hole) {
                    ++suits[static_cast<int>(c.suit)];
                    if (suits[static_cast<int>(c.suit)] > suited_max) {
                        suited_max = suits[static_cast<int>(c.suit)];
                    }
                }
                if (suited_max >= 2) value += 0.04;
                const int span = static_cast<int>(ranks[3]) -
                                 static_cast<int>(ranks[0]);
                const bool connected = span <= 5;
                if (connected) value += 0.06;
                if (suited_max < 2 && !connected) value = 0.0;
                if (value > 0.75) value = 0.75;
            }
            return value;
        }
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
    HandValue value;
    if (view.showdown == HandConstruction::OmahaTwoAndThree &&
        view.hole.size() == 4 && view.board.size() >= 3) {
        value = omaha_current(view.hole, view.board);
    } else {
        value = evaluate_best(all);
    }
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

// Best five right now under exact-2-from-hand rules, for a 3-5 card board.
// At 5 board cards this is exactly evaluate_omaha; earlier streets judge
// the made hand so far (draws are scored separately).
HandValue omaha_current(const std::vector<Card>& hole,
                        const std::vector<Card>& board) {
    bool best_set = false;
    HandValue best;
    for (std::size_t a = 0; a < hole.size(); ++a) {
        for (std::size_t b = a + 1; b < hole.size(); ++b) {
            for (std::size_t c = 0; c < board.size(); ++c) {
                for (std::size_t d = c + 1; d < board.size(); ++d) {
                    for (std::size_t e = d + 1; e < board.size(); ++e) {
                        const std::array<Card, 5> five{hole[a], hole[b],
                                                       board[c], board[d],
                                                       board[e]};
                        const HandValue value = evaluate_five(five);
                        if (!best_set || best < value) {
                            best = value;
                            best_set = true;
                        }
                    }
                }
            }
        }
    }
    return best;
}

// Drawing equity: 9 outs per four-flush, 8 per open-ender, times ~4% per
// street to come. Conservative on purpose; combined with made strength.
double draw_equity(const SeatView& view) {
    if (view.board.empty() || view.board.size() >= 5) return 0.0;
    int suited[4] = {};
    int hole_suited[4] = {};
    auto count_suit = [&](const Card& c) {
        ++suited[static_cast<int>(c.suit)];
    };
    for (const Card& c : view.hole) {
        count_suit(c);
        ++hole_suited[static_cast<int>(c.suit)];
    }
    for (const Card& c : view.board) count_suit(c);
    int outs = 0;
    const bool omaha = view.showdown == HandConstruction::OmahaTwoAndThree;
    for (int suit = 0; suit < 4; ++suit) {
        // Omaha needs exactly 2 from hand: a four-flush is only live with
        // 2+ hole cards of the suit.
        if (suited[suit] == 4 && (!omaha || hole_suited[suit] >= 2)) {
            outs += 9;
        }
    }
    bool present[15] = {};
    auto mark_rank = [&](Rank r) {
        present[static_cast<int>(r)] = true;
        if (r == Rank::Ace) present[1] = true;
    };
    for (const Card& c : view.hole) mark_rank(c.rank);
    for (const Card& c : view.board) mark_rank(c.rank);
    for (int start = 1; start <= 10; ++start) {
        int window = 0;
        for (int r = start; r < start + 5; ++r) {
            if (present[r]) ++window;
        }
        if (window == 4) {
            outs += 8;
            break;
        }
    }
    if (outs == 0) return 0.0;
    const double per_card = view.board.size() <= 3 ? 0.04 : 0.02;
    double equity = static_cast<double>(outs) * per_card;
    if (equity > 0.55) equity = 0.55;
    return equity;
}

double strength(const SeatView& view, double position_weight) {
    double total = made_strength(view);
    const double draw = draw_equity(view);
    if (draw > total) total = draw;
    // Position: late seats realize more equity and steal more often;
    // early seats pay for acting blind. Scaled by position_weight
    // (0 = ignore position, 1 = classic nudge, 2 = double).
    if (view.num_seats > 0 && position_weight > 0.0) {
        if (view.position == 0 || view.position == view.num_seats - 1) {
            total += 0.05 * position_weight;
        } else if (view.position == 1 || view.position == 2) {
            total -= 0.05 * position_weight;
        }
    }
    if (total < 0.0) total = 0.0;
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
        Action action = decide_inner(view, unit);
        // Track the story: our bets keep us the aggressor; anything else
        // hands the story to the table.
        prior_aggressor_ = (action.type == ActionType::Raise);
        return action;
    }

    const std::string& name() const override { return file_.name; }

protected:
    // AdaptiveBot retunes looseness as it profiles the table.
    BotFile file_;

    void observe(int, const HandSummary&) override {
        // A new hand starts a new story: last hand's line is over.
        prior_aggressor_ = false;
    }

    // The baseline decision without story tracking. Planning overrides
    // this, not decide, so lookahead shares one story update.
    Action decide_inner(const SeatView& view,
                        std::uniform_real_distribution<double>& unit) {
        const double s = strength(view, file_.position_weight);
        // Continuation: the prior street's aggressor keeps firing with
        // hands too weak to bet fresh — double-barrels, delayed c-bets,
        // and bluffs with a story. Scales with barrels (0 = off), gated
        // on real equity so air still gives up.
        if (view.to_call == 0 && view.can_raise && prior_aggressor_ &&
            file_.barrels > 0.0 && s > 0.30 &&
            s <= 0.60 - file_.aggression * 0.15 &&
            unit(rng_) < file_.barrels * 0.5) {
            return {ActionType::Raise, size_bet(view)};
        }
        // Planning: one-street lookahead reconsiders the baseline below.
        // (planning = 0 skips straight to it.)
        // Survival (ICM-lite): short stacks demand a risk premium on
        // elimination-risk calls — tournament chips lost are worth more
        // than chips gained, so marginal continues become folds. Zero when
        // deep or when survival = 0 (cash-game default).
        const double premium = risk_premium(view);
        if (view.to_call == 0) {
            // Bluffs: weak hands bet at bluff_rate so value bets get paid.
            if (view.can_raise &&
                (s > 0.60 - file_.aggression * 0.15 + premium * 0.5 ||
                 (s < 0.30 && unit(rng_) < file_.bluff_rate))) {
                return {ActionType::Raise, size_bet(view)};
            }
            if (view.can_check) return {ActionType::Check, 0};
            return {ActionType::Call, 0};  // Zero-cost call, same as check.
        }
        double equity = s;
        if (file_.planning > 0) {
            equity = planned_equity(view, s);
        }
        if (equity >= 0.62 + premium && view.can_raise) {
            return {ActionType::Raise, size_bet(view)};
        }
        const double pot_odds =
            static_cast<double>(view.to_call) /
            static_cast<double>(view.pot + view.to_call);
        if (view.can_raise &&
            equity + file_.looseness * 0.25 >= pot_odds * 2.0 + premium) {
            return {ActionType::Call, 0};
        }
        if (!view.can_raise &&
            equity + file_.looseness * 0.25 >= pot_odds + premium) {
            return {ActionType::Call, 0};
        }
        return {ActionType::Fold, 0};
    }

private:
    // One-street lookahead: current strength blended with the hand's
    // draw trajectory. Draw-heavy hands gain (outs realize next street);
    // made hands with no redraws decay slightly (the board can only get
    // scarier). planning = 1 blends half, 2 blends three-quarters.
    // Pure arithmetic over the existing evaluators: no search tree, no
    // opponent model — microseconds, not milliseconds.
    double planned_equity(const SeatView& view, double now) const {
        const double draw = draw_equity(view);
        const double made = made_strength(view);
        double next = now;
        if (draw > made && !view.board.empty() &&
            view.board.size() < 5) {
            // Drawing: expected next-street strength if one card hits.
            next = made + draw * 0.5;
        } else if (made >= 0.5 && draw <= 0.05 && !view.board.empty() &&
                   view.board.size() < 5) {
            // Vulnerable made hand, no redraws: discount for scary cards.
            next = made - 0.06;
        }
        const double blend =
            file_.planning >= 2 ? 0.75 : 0.5;
        double equity = now + (next - now) * blend;
        if (equity < 0.0) equity = 0.0;
        if (equity > 1.0) equity = 1.0;
        return equity;
    }

    // Fraction of the stack at risk, scaled by shortness: deep stacks risk
    // little per call, short stacks risk everything. Premium peaks when a
    // call costs a large share of a below-starting stack. The 2x weight on
    // shortness keeps deep-stack premiums negligible (a 1% call must not
    // fold) while letting half-stack calls demand real hands.
    double risk_premium(const SeatView& view) const {
        if (file_.survival <= 0.0 || view.stack <= 0 || view.to_call <= 0) {
            return 0.0;
        }
        const double at_risk =
            static_cast<double>(view.call_amount) /
            static_cast<double>(view.stack + view.call_amount);
        const double shortness =
            static_cast<double>(view.to_call) /
            static_cast<double>(view.stack + view.to_call);
        return file_.survival * at_risk * (2.0 * shortness + at_risk);
    }

    int size_bet(const SeatView& view) const {
        const double frac = 0.5 + 0.5 * file_.aggression;
        const int target =
            view.current_bet +
            static_cast<int>(static_cast<double>(view.pot) * frac);
        if (target < view.min_raise_to) return view.min_raise_to;
        if (target > view.max_raise_to) return view.max_raise_to;
        return target;
    }

    // This hand's story: true while our bets are the last aggression
    // (we bet/raise and face no bet since). Reset by observe each hand.
    bool prior_aggressor_ = false;
    std::mt19937_64 rng_;
    RandomBot random_;
};

// Heuristic core plus a table image: tracks each opponent's looseness
// (voluntary money per hand) and aggression (raises per hand) across
// observed hands. Shifts its own continuing range — looser against maniacs,
// tighter against rocks — and calls down aggressive bluffers lighter while
// giving tight raisers extra respect.
class AdaptiveBot : public HeuristicBot {
public:
    explicit AdaptiveBot(const BotFile& file)
        : HeuristicBot(file), base_looseness_(file.looseness) {}

    Action decide(const SeatView& view) override {
        Action action = HeuristicBot::decide(view);
        if (action.type != ActionType::Fold || view.to_call <= 0) {
            return action;
        }
        // Aggression read: the bettor's raise rate reconsiders a fold.
        // Against a maniac the same hand is a bluff-catch; against a rock
        // the fold stands. adapt_rate scales the swing (0 = ignore reads).
        const double aggression = table_aggression();
        const double swing = file_.adapt_rate * (aggression - 0.35) * 0.30;
        if (swing <= 0.0) return action;
        const double s = strength(view, file_.position_weight);
        const double pot_odds =
            static_cast<double>(view.to_call) /
            static_cast<double>(view.pot + view.to_call);
        const double bar = view.can_raise ? pot_odds * 2.0 : pot_odds;
        // The bluff discount: maniac bets are weaker than the pot claims,
        // so a near-miss fold becomes a catch.
        if (s + file_.looseness * 0.25 + swing * 2.0 >= bar) {
            return {ActionType::Call, 0};
        }
        return action;
    }

    void observe(int own_seat, const HandSummary& summary) override {
        HeuristicBot::observe(own_seat, summary);  // New hand: story resets.
        for (std::size_t i = 0; i < summary.seats.size(); ++i) {
            if (static_cast<int>(i) == own_seat) continue;
            const SeatSummary& seat = summary.seats[i];
            if (!seat.played) continue;
            Opponent& opp = opponents_[static_cast<int>(i)];
            ++opp.hands;
            // Voluntary money past one big blind means playing loose.
            if (seat.committed > summary.big_blind) ++opp.loose;
            opp.raises += seat.raises;
        }
        // Prior-weighted average: 3 imaginary neutral hands steady small
        // samples, real evidence dominates with volume.
        double total = 0.0;
        double weight = 0.0;
        for (const auto& [seat, opp] : opponents_) {
            (void)seat;
            total += static_cast<double>(opp.loose) + 0.4 * 3.0;
            weight += static_cast<double>(opp.hands) + 3.0;
        }
        double average = 0.4;
        if (weight > 0.0) average = total / weight;
        double tuned = base_looseness_ + file_.adapt_rate * (average - 0.4);
        if (tuned < 0.0) tuned = 0.0;
        if (tuned > 1.0) tuned = 1.0;
        file_.looseness = tuned;
    }

private:
    struct Opponent {
        int hands = 0;
        int loose = 0;
        int raises = 0;
    };

    // Table's raise rate per hand, prior-weighted toward a neutral 0.35
    // (about one raise every three hands each). Maniacs push it past 1.
    double table_aggression() const {
        double raises = 0.0;
        double hands = 0.0;
        for (const auto& [seat, opp] : opponents_) {
            (void)seat;
            raises += static_cast<double>(opp.raises);
            hands += static_cast<double>(opp.hands);
        }
        return (raises + 0.35 * 3.0) / (hands + 3.0);
    }

    double base_looseness_;
    std::map<int, Opponent> opponents_;
};

// Balanced-lite: fixed pot-fraction sizing, minimum-defense-frequency
// calls, value-heavy raises plus occasional bluffs at the same size.
class GtoBot : public Bot {
public:
    explicit GtoBot(const BotFile& file)
        : file_(file), name_(file.name), rng_(file.seed), random_(file) {}

    Action decide(const SeatView& view) override {
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (unit(rng_) < file_.mistake_rate) {
            return random_.decide(view);
        }
        const double s = strength(view, file_.position_weight);
        if (view.to_call == 0) {
            if (view.can_raise && (s > 0.62 ||
                                   (s < 0.30 && unit(rng_) < file_.bluff_rate))) {
                return {ActionType::Raise, size_bet(view)};
            }
            if (view.can_check) return {ActionType::Check, 0};
            return {ActionType::Call, 0};
        }
        if (s > 0.75 && view.can_raise) return {ActionType::Raise, size_bet(view)};
        // Minimum defense frequency, scaled by defense: call often enough
        // that bluffs break even at 1.0; under-defend below, over-defend
        // above. (An equity floor lost ~50 Elo here: this field bets
        // value-heavy, so MDF already over-defends.)
        const double mdf = static_cast<double>(view.pot) /
                           static_cast<double>(view.pot + view.to_call);
        if (unit(rng_) < mdf * file_.defense) return {ActionType::Call, 0};
        return {ActionType::Fold, 0};
    }

    const std::string& name() const override { return name_; }

private:
    int size_bet(const SeatView& view) const {
        const int target =
            view.current_bet +
            static_cast<int>(static_cast<double>(view.pot) * 0.6);
        if (target < view.min_raise_to) return view.min_raise_to;
        if (target > view.max_raise_to) return view.max_raise_to;
        return target;
    }

    BotFile file_;
    std::string name_;
    std::mt19937_64 rng_;
    RandomBot random_;
};

}  // namespace

HandSummary summarize_hand(const std::vector<Event>& events, std::size_t begin,
                           std::size_t end) {
    HandSummary summary;
    if (begin > events.size()) begin = events.size();
    if (end > events.size()) end = events.size();
    for (std::size_t k = begin; k < end; ++k) {
        const Event& event = events[k];
        if (const auto* started = std::get_if<HandStartedEvent>(&event)) {
            summary.big_blind = started->config.big_blind;
            summary.seats.assign(started->hole.size(), SeatSummary{});
            for (std::size_t i = 0; i < started->hole.size(); ++i) {
                summary.seats[i].played = !started->hole[i].empty();
                summary.seats[i].folded = false;
            }
        } else if (const auto* action = std::get_if<ActionTakenEvent>(&event)) {
            if (action->seat < 0 ||
                static_cast<std::size_t>(action->seat) >= summary.seats.size()) {
                throw std::invalid_argument("action from unknown seat");
            }
            SeatSummary& seat = summary.seats[static_cast<std::size_t>(action->seat)];
            if (action->action.type == ActionType::Fold) {
                seat.folded = true;
            } else {
                seat.folded = false;
                if (action->action.type == ActionType::Raise) ++seat.raises;
            }
        } else if (const auto* settled = std::get_if<HandSettledEvent>(&event)) {
            if (settled->committed.size() != summary.seats.size()) {
                throw std::invalid_argument("settle does not match its hand");
            }
            for (std::size_t i = 0; i < summary.seats.size(); ++i) {
                summary.seats[i].committed = settled->committed[i];
            }
            for (const Payout& payout : settled->payouts) {
                summary.seats[static_cast<std::size_t>(payout.seat)].won +=
                    payout.amount;
            }
        }
    }
    if (summary.seats.empty()) {
        throw std::invalid_argument("no hand in range");
    }
    return summary;
}

std::unique_ptr<Bot> make_bot(const BotFile& file) {
    validate_bot(file);
    if (file.style == BotStyle::Random) {
        return std::make_unique<RandomBot>(file);
    }
    if (file.style == BotStyle::Adaptive) {
        return std::make_unique<AdaptiveBot>(file);
    }
    if (file.style == BotStyle::Gto) {
        return std::make_unique<GtoBot>(file);
    }
    return std::make_unique<HeuristicBot>(file);
}

}  // namespace cardengine
