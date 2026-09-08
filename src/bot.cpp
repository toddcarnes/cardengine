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
    // Bot names land in CSV lineups/placements unquoted, so a present
    // name must be plain printable text with no field or name
    // separators. Empty stays legal (nameless test/default bots).
    for (char c : file.name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E || c == ',' || c == '"' || c == ';' ||
            c == '\'') {
            throw std::invalid_argument(
                "name must be printable ASCII without , \" ; or '");
        }
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
    view.position = (seat - table.button() + table.num_seats()) %
                    table.num_seats();
    view.table_size = 0;
    for (int i = 0; i < table.num_seats(); ++i) {
        if (table.in_hand(i)) ++view.table_size;
    }
    view.num_seats = view.table_size;
    view.showdown = table.config().showdown;
    view.max_draw = table.config().max_draw;
    view.drew = table.drew(seat);
    if (table.config().showdown == HandConstruction::StudSeven) {
        view.up = table.up_cards(seat);
        view.community = table.community();
        for (int i = 0; i < table.num_seats(); ++i) {
            if (i == seat) continue;
            if (table.in_hand(i) && !table.has_folded(i)) {
                view.rival_up.push_back(table.up_cards(i));
            }
        }
    }
    return view;
}

namespace {

// Discard choice shared by every style's five-card draw logic: keep made
// hands and strong draws, throw the rest. Returns card texts from the
// hole (empty = stand pat). Capped at max_draw by the caller.
std::vector<std::string> draw_keep(const std::vector<Card>& hole,
                                   bool deuce);
// Trim a wish list to the house cap (keep order: first cards matter most).
std::vector<std::string> cap_discards(std::vector<std::string> want,
                                      int max_draw);
// Discard contract guard (defined after the bot classes): every entry must
// name a hole card, no duplicates. Filters instead of throwing.
std::vector<std::string> checked_discards(std::vector<std::string> want,
                                          const SeatView& view);

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

    std::vector<std::string> choose_discards(
        const SeatView& view) override {
        const bool deuce = view.showdown == HandConstruction::DeuceSeven;
        return checked_discards(draw_keep(view.hole, deuce), view);
    }

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

// Omaha coordination: suited together or connected. Bare high cards with
// neither are PLO trash postflop too — a lone pair without a draw or a
// suit to grow into is a bluff-catcher in a game where everyone makes
// hands. (Mirrors the preflop trash gate in made_strength.)
bool omaha_coordinated(const std::vector<Card>& hole) {
    int suits[4] = {};
    int low = 15, high = 0;
    for (const Card& c : hole) {
        ++suits[static_cast<int>(c.suit)];
        const int v = static_cast<int>(c.rank);
        if (v < low) low = v;
        if (v > high) high = v;
    }
    for (int s = 0; s < 4; ++s) {
        if (suits[s] >= 2) return true;
    }
    return high - low <= 5;
}
HandValue stud_current(const std::vector<Card>& hole,
                       const std::vector<Card>& up,
                       const std::vector<Card>& community);
OmahaHiLoValue evaluate_partial_hilo(const std::vector<Card>& hole,
                                     const std::vector<Card>& board);
// Draw equity (flush/straight outs) — declared early: the Omaha honesty
// tax in made_strength keys off live draws, not just coordination.
double draw_equity(const SeatView& view);
double strength(const SeatView& view, double position_weight);
double made_strength(const SeatView& view) {
    // 2-7 lowball: the worst poker hand wins, so strength runs off the
    // DeuceValue directly — a pat 7-low is the nuts (~0.95), any broken
    // hand outranks any pair, and pairs-or-worse fold to pressure.
    // Straights and flushes are made hands here (penalty 4-5, strength
    // ~0.10): they beat pure air (which bluffs or folds) but pay off no
    // real bet — exactly the wheel's station in life.
    if (view.showdown == HandConstruction::DeuceSeven &&
        view.board.empty() && view.hole.size() == 5) {
        std::array<Card, 5> five{};
        for (std::size_t i = 0; i < 5; ++i) five[i] = view.hole[i];
        const DeuceValue low = evaluate_deuce(five);
        if (low.penalty == 0) {
            // High-first tiebreak: the top card decides (7-high is best).
            const double high = static_cast<double>(
                static_cast<int>(low.tiebreak[0]));
            // 7-low is the nuts (0.95); each pip worse costs ~0.06, down to
            // a 0.40 floor — smooth enough that J-low still opens but folds
            // to real pressure.
            double value = 0.95 - (high - 7.0) * 0.06;
            if (value < 0.40) value = 0.40;
            if (value > 0.95) value = 0.95;
            return value;
        }
        double base = 0.05;
        switch (low.penalty) {
            case 1: {
                // Smaller pairs lose less badly (deuces best of a bad lot).
                const double pair = static_cast<double>(
                    static_cast<int>(low.tiebreak[0]));
                base = 0.30 + (14.0 - pair) * 0.008;
                break;
            }
            case 2: base = 0.25; break;
            case 3: base = 0.20; break;
            case 4: base = 0.15; break;
            case 5: base = 0.12; break;
            default: base = 0.08; break;
        }
        return base;
    }
    // Five-card draw (high) is a pat hand: judge it the way postflop
    // judges five cards (category plus kicker), not the way preflop
    // judges two starting cards. Draw hands reach this path with an empty
    // board and five hole cards, so fall through to the category path.
    if (view.board.empty() &&
        !(view.showdown == HandConstruction::DrawFive &&
          view.hole.size() == 5)) {
        // Omaha deals four: pairs (6 combos, not 1) and rundowns/connectivity
        // dominate; raw high cards leak value without coordination. Hi-Lo
        // adds the other premium: two low cards (A-2 through A-5 wheel cards
        // best) that can scoop or split the low half.
        if (view.showdown == HandConstruction::OmahaTwoAndThree ||
            view.showdown == HandConstruction::OmahaHiLo) {
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
                // cards with neither are PLO trash (everyone makes hands) —
                // and multi-way pots are Randy's harvest, so single-suited
                // or loosely connected hands grade down too: only
                // double-suited or tight-connected hands play for a raise,
                // one-legged coordination calls one bet, trash folds.
                int suited_max = 0;
                int suits[4] = {};
                for (const Card& c : view.hole) {
                    ++suits[static_cast<int>(c.suit)];
                    if (suits[static_cast<int>(c.suit)] > suited_max) {
                        suited_max = suits[static_cast<int>(c.suit)];
                    }
                }
                const int span = static_cast<int>(ranks[3]) -
                                 static_cast<int>(ranks[0]);
                const bool tight = span <= 4;
                const bool connected = span <= 5;
                const bool double_suited =
                    suits[0] == 2 || suits[1] == 2 || suits[2] == 2 ||
                    suits[3] == 2;
                int legs = 0;
                if (suited_max >= 2) ++legs;
                if (connected) ++legs;
                if (view.showdown == HandConstruction::OmahaTwoAndThree) {
                    // PLO trash gate: zero legs is unplayable (0.0 folds to
                    // any pressure); one leg calls one small bet but never
                    // opens; two legs play poker.
                    if (legs == 0) {
                        return 0.0;
                    }
                    if (legs == 1) {
                        value = 0.30 + static_cast<double>(
                                            static_cast<int>(ranks[3]) - 2) *
                                            0.005;
                        if (value > 0.38) value = 0.38;
                        return value;
                    }
                    value += 0.04 + 0.06;
                    if (double_suited && tight) value += 0.04;
                } else {
                    if (suited_max >= 2) value += 0.04;
                    if (connected) value += 0.06;
                }
                // Hi-Lo low premium: two wheel cards (A + 2/3/4/5, pairs
                // excepted — a paired ace can't make the low) play for half
                // the pot on their own. Set AFTER the trash gate: A2 with
                // nothing else is a low draw, not PLO trash.
                bool hilo_low_draw = false;
                if (view.showdown == HandConstruction::OmahaHiLo &&
                    pairs == 0) {
                    int low_cards = 0;
                    bool has_ace = false;
                    for (Rank r : ranks) {
                        const int v = static_cast<int>(r);
                        if (v == 14) has_ace = true;
                        if (v == 14 || (v >= 2 && v <= 5)) ++low_cards;
                    }
                    hilo_low_draw = (low_cards >= 2);
                    if (hilo_low_draw) {
                        value = has_ace ? 0.34 : 0.22;
                    }
                }
                if (suited_max < 2 && !connected && !hilo_low_draw) value = 0.0;
                if (value > 0.75) value = 0.75;
            }
            return value;
        }
        std::vector<Rank> ranks;
        for (const Card& c : view.hole) ranks.push_back(c.rank);
        std::sort(ranks.begin(), ranks.end());
        // Five-card draw (high) holds a pat five: any pair is already a
        // made hand worth contesting, so score it on the category scale
        // (pair of aces ~0.70) rather than the two-card starting scale.
        if (view.showdown == HandConstruction::DrawFive) {
            for (std::size_t i = 1; i < ranks.size(); ++i) {
                if (ranks[i] == ranks[i - 1]) {
                    const double pair =
                        static_cast<double>(static_cast<int>(ranks[i]));
                    return 0.52 + (pair - 2.0) * 0.015;
                }
            }
        } else {
            for (std::size_t i = 1; i < ranks.size(); ++i) {
                if (ranks[i] == ranks[i - 1]) {
                    return 0.70 +
                           static_cast<double>(static_cast<int>(ranks[i]) - 2) *
                               0.02;
                }
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
    if (view.showdown == HandConstruction::StudSeven) {
        // Stud counts its own cards only (down + up, shared river when
        // dealt); fewer than five cards judges the door and the pair —
        // evaluate_best needs five, so pad short hands with nothing and
        // score pairs/high cards on the same scale below.
        value = stud_current(view.hole, view.up, view.community);
    } else if ((view.showdown == HandConstruction::OmahaTwoAndThree ||
                view.showdown == HandConstruction::OmahaHiLo) &&
               view.hole.size() == 4 && view.board.size() >= 3) {
        value = omaha_current(view.hole, view.board);
        // Hi-Lo postflop: a live low draw (or made low) is worth half the
        // pot on its own — play it like a strong made hand.
        if (view.showdown == HandConstruction::OmahaHiLo &&
            view.board.size() >= 3) {
            const OmahaHiLoValue hilo =
                evaluate_partial_hilo(view.hole, view.board);
            if (hilo.low.qualifies) {
                double low_strength = 0.55;
                if (hilo.low.descending[0] <= 6) low_strength = 0.65;
                if (hilo.low.descending[0] <= 5 &&
                    hilo.low.descending[1] <= 4) {
                    low_strength = 0.72;  // Nut-ish low: bet it.
                }
                double total = low_strength;
                // Both ways (good high + good low) is the scoop premium.
                if (value.category >= HandCategory::TwoPair) total = 0.85;
                return total > 1.0 ? 1.0 : total;
            }
        }
        // Omaha honesty tax: bare overpairs and naked draws read weaker
        // than their holdem twins. Everyone makes hands here, so a lone
        // pair without coordination or draws is a bluff-catcher, not a
        // value hand — discount it before the category scale below. A
        // coordinated pair with live equity keeps a middle grade (the
        // discount below, not the tax, decides those hands).
        if (value.category == HandCategory::OnePair &&
            draw_equity(view) <= 0.05) {
            HandValue taxed = value;
            taxed.category = HandCategory::HighCard;
            if (omaha_coordinated(view.hole)) taxed.tiebreak[0] = Rank::Ace;
            value = taxed;
        }
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

// Partial Hi-Lo on a 3-4 card board: high is the best exact-2+3 five so
// far; low qualifies only when the board already shows 3+ distinct
// 8-or-better ranks (a 1- or 2-low flop cannot make a low yet, however
// pretty the hole cards). At 5 board cards this is evaluate_omaha_hilo.
OmahaHiLoValue evaluate_partial_hilo(const std::vector<Card>& hole,
                                     const std::vector<Card>& board) {
    OmahaHiLoValue out;
    out.high = omaha_current(hole, board);
    int low_ranks[5] = {};
    int distinct = 0;
    bool seen[15] = {};
    for (const Card& c : board) {
        int v = static_cast<int>(c.rank);
        if (v == 14) v = 1;
        if (v > 8) continue;
        if (!seen[v]) {
            seen[v] = true;
            low_ranks[distinct++] = v;
        }
    }
    if (distinct < 3) return out;  // No low possible yet.
    bool low_set = false;
    for (std::size_t a = 0; a < hole.size(); ++a) {
        for (std::size_t b = a + 1; b < hole.size(); ++b) {
            int ha = static_cast<int>(hole[a].rank);
            int hb = static_cast<int>(hole[b].rank);
            if (ha == 14) ha = 1;
            if (hb == 14) hb = 1;
            if (ha > 8 || hb > 8 || ha == hb) continue;
            // Best 3 board lows to pair with these two hole lows.
            std::sort(low_ranks, low_ranks + distinct, std::greater<int>());
            for (int c = 0; c < distinct; ++c) {
                for (int d = c + 1; d < distinct; ++d) {
                    for (int e = d + 1; e < distinct; ++e) {
                        const int combo[5] = {ha, hb, low_ranks[c],
                                              low_ranks[d], low_ranks[e]};
                        bool paired = false;
                        for (int i = 0; i < 5 && !paired; ++i) {
                            for (int k = i + 1; k < 5; ++k) {
                                if (combo[i] == combo[k]) paired = true;
                            }
                        }
                        if (paired) continue;
                        LowValue low;
                        low.qualifies = true;
                        int sorted[5] = {combo[0], combo[1], combo[2],
                                         combo[3], combo[4]};
                        std::sort(sorted, sorted + 5, std::greater<int>());
                        for (int i = 0; i < 5; ++i) {
                            low.descending[i] = sorted[i];
                        }
                        if (!low_set || low < out.low) {
                            out.low = low;
                            low_set = true;
                        }
                    }
                }
            }
        }
    }
    return out;
}
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

// Best five of a stud hand so far: own down + up plus the shared river
// card when dealt. Fewer than five cards (third street deals three)
// scores the door and the pair on the made-hand scale: pairs play,
// three to a flush/straight draw at 0.30, high door cards linger.
HandValue stud_current(const std::vector<Card>& hole,
                       const std::vector<Card>& up,
                       const std::vector<Card>& community) {
    std::vector<Card> all = hole;
    all.insert(all.end(), up.begin(), up.end());
    all.insert(all.end(), community.begin(), community.end());
    if (all.size() >= 5) return evaluate_best(all);
    // Short hands: rank pairs first, then high cards. Three cards can't
    // make a flush or a straight, so trips-or-nothing never fires here —
    // a wired pair of aces is the whole game on third street.
    int rank_count[15] = {};
    for (const Card& c : all) ++rank_count[static_cast<int>(c.rank)];
    int best_pair = 0;
    for (int r = 14; r >= 2; --r) {
        if (rank_count[r] >= 2) {
            best_pair = r;
            break;
        }
    }
    HandValue out;
    if (best_pair > 0) {
        out.category = HandCategory::OnePair;
        out.tiebreak[0] = static_cast<Rank>(best_pair);
        std::size_t k = 1;
        for (int r = 14; r >= 2 && k < 5; --r) {
            if (r == best_pair) continue;
            for (int n = 0; n < rank_count[r] && k < 5; ++n) {
                out.tiebreak[k++] = static_cast<Rank>(r);
            }
        }
        return out;
    }
    out.category = HandCategory::HighCard;
    std::vector<int> ranks;
    for (const Card& c : all) ranks.push_back(static_cast<int>(c.rank));
    std::sort(ranks.begin(), ranks.end(), std::greater<int>());
    for (std::size_t i = 0; i < ranks.size() && i < 5; ++i) {
        out.tiebreak[i] = static_cast<Rank>(ranks[i]);
    }
    return out;
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
    const bool omaha = view.showdown == HandConstruction::OmahaTwoAndThree ||
                       view.showdown == HandConstruction::OmahaHiLo;
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

// Two unpaired wheel cards (A + 2/3/4/5) in four: the Hi-Lo low draw
// that contests half the pot preflop.
bool wheel_draw(const std::vector<Card>& hole) {
    if (hole.size() != 4) return false;
    int low_cards = 0;
    bool paired_ace = false;
    for (std::size_t i = 0; i < hole.size(); ++i) {
        const int v = static_cast<int>(hole[i].rank);
        if (v == 14 || (v >= 2 && v <= 5)) ++low_cards;
        for (std::size_t k = i + 1; k < hole.size(); ++k) {
            if (hole[i].rank == hole[k].rank && v == 14) paired_ace = true;
        }
    }
    return low_cards >= 2 && !paired_ace;
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

    // Five-card draw exchange: pat made hands stand, draws keep one,
    // deuce lows stand on 8-or-better, junk draws to the ace/king.
    // Mistakes (when they fire) randomize the count, not the cards —
    // a mistaken bot still throws plausible rags.
    std::vector<std::string> choose_discards(
        const SeatView& view) override {
        const bool deuce = view.showdown == HandConstruction::DeuceSeven;
        std::vector<std::string> want = draw_keep(view.hole, deuce);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (unit(rng_) < file_.mistake_rate && !want.empty() &&
            view.max_draw > 1) {
            std::uniform_int_distribution<int> count(
                0, view.max_draw);
            want.resize(static_cast<std::size_t>(count(rng_)));
        }
        return checked_discards(want, view);
    }

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
        // on real equity so air still gives up. Omaha strengthens the
        // gate (bare pairs and naked draws are bluff-catchers there, not
        // barreling hands) but coordinated draws earn a second street:
        // a four-flush or open-ender with a pair or a suit to grow into
        // keeps firing where one-and-done honesty would check.
        const bool omaha =
            view.showdown == HandConstruction::OmahaTwoAndThree ||
            view.showdown == HandConstruction::OmahaHiLo;
        double barrel_floor = 0.30;
        double barrel_cap = 0.60 - file_.aggression * 0.15;
        if (omaha) {
            barrel_floor = 0.45;
            // Live coordinated draw (flush/straight equity developing):
            // fire the second street. The bar is the draw itself, not
            // the made-hand grade — a naked taxed pair must not qualify,
            // but any real draw fires, even under a made pair.
            if (omaha_coordinated(view.hole) && draw_equity(view) > 0.15) {
                barrel_floor = 0.15;
                barrel_cap = 0.75;
            }
        }
        if (view.to_call == 0 && view.can_raise && prior_aggressor_ &&
            file_.barrels > 0.0 && s > barrel_floor && s <= barrel_cap &&
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
        // Hi-Lo low discount: with two wheel cards the call contests half
        // the pot on its own, so the pot-odds bar halves (a low draw at
        // 2x pot odds plays like a high draw at even money). PLO gets no
        // discount: the 0.6 experiment fed Randy (multi-way pots are its
        // harvest — every extra caller is split equity donated). Naked
        // pairs pay full price, and so does everything else.
        double bar_scale = 1.0;
        if (view.showdown == HandConstruction::OmahaHiLo &&
            view.board.empty() && wheel_draw(view.hole)) {
            bar_scale = 0.45;
        }
        if (equity >= 0.62 + premium && view.can_raise) {
            return {ActionType::Raise, size_bet(view)};
        }
        const double pot_odds =
            static_cast<double>(view.to_call) /
            static_cast<double>(view.pot + view.to_call);
        if (view.can_raise &&
            equity + file_.looseness * 0.25 >= pot_odds * 2.0 * bar_scale + premium) {
            return {ActionType::Call, 0};
        }
        if (!view.can_raise &&
            equity + file_.looseness * 0.25 >= pot_odds * bar_scale + premium) {
            return {ActionType::Call, 0};
        }
        return {ActionType::Fold, 0};
    }

private:
    // One-street lookahead: current strength blended with the hand's
    // draw trajectory. Draw-heavy hands gain (outs realize next street);
    // made hands with no redraws decay slightly (the board can only get
    // scarier). planning = 1 blends half, 2 blends three-quarters.
    // Short-handed the lookahead thins out: heads-up every hand is a
    // duel, so future-street geometry carries less weight than current
    // cards. Pure arithmetic over the existing evaluators: no search
    // tree, no opponent model — microseconds, not milliseconds.
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
        double table_blend = blend;
        if (view.table_size >= 2 && view.table_size < 6) {
            // Short tables thin the lookahead toward current strength.
            table_blend = blend * static_cast<double>(view.table_size - 2) /
                          4.0;
        }
        double equity = now + (next - now) * table_blend;
        if (equity < 0.0) equity = 0.0;
        if (equity > 1.0) equity = 1.0;
        return equity;
    }

    // Fraction of the stack at risk, scaled by shortness and table size:
    // deep stacks risk little per call, short stacks risk everything, and
    // full rings punish busts harder than short tables (more players share
    // the dead money, so survival matters more). Premium peaks when a call
    // costs a large share of a below-starting stack at a full table. The
    // 2x weight on shortness keeps deep-stack premiums negligible (a 1%
    // call must not fold) while letting half-stack calls demand real
    // hands. Heads-up (table_size 2) plays nearly survival-free: every
    // duel risks elimination, so there is no premium left to charge.
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
        double table_scale = 1.0;
        if (view.table_size >= 2) {
            // 0 at heads-up, ramping to full weight at 6-max.
            table_scale = static_cast<double>(view.table_size - 2) / 4.0;
            if (table_scale < 0.0) table_scale = 0.0;
            if (table_scale > 1.0) table_scale = 1.0;
        }
        return file_.survival * table_scale * at_risk *
               (2.0 * shortness + at_risk);
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
// Defense scales with table size: short-handed every duel is a flip, so
// the bot leans on its made-hand edge (equity floor) instead of MDF's
// break-even math, which assumes opponents bluff at equilibrium rates
// this field never reaches.
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
        // Deuce value + floor: pat lows are the made hands here. A pat
        // 7-low (s ~ 0.80+) raises for value like any nuts; lesser pat
        // lows (8-low and up, s ~ 0.40+) always continue through capped
        // bets instead of folding to MDF math built for high poker.
        // Pairs and worse still defend at (thinned) MDF — they are air.
        if (view.showdown == HandConstruction::DeuceSeven &&
            view.board.empty() && view.hole.size() == 5) {
            if (s >= 0.80 && view.can_raise) {
                return {ActionType::Raise, size_bet(view)};
            }
            if (s >= 0.40 && !view.can_raise) {
                return {ActionType::Call, 0};
            }
        }
        // Equity floor first: real made hands (top pair good kicker or
        // better) always continue — no paradox of folding winners to
        // satisfy a frequency. Air below the floor defends at MDF, scaled
        // by defense and thinned short-handed (heads-up MDF over-defends
        // against a value-heavy field; the floor carries the weight).
        if (s >= 0.50) return {ActionType::Call, 0};
        double defend_scale = file_.defense;
        if (view.table_size >= 2 && view.table_size < 6) {
            defend_scale *= static_cast<double>(view.table_size - 2) / 4.0;
        }
        const double mdf = static_cast<double>(view.pot) /
                           static_cast<double>(view.pot + view.to_call);
        if (unit(rng_) < mdf * defend_scale) return {ActionType::Call, 0};
        return {ActionType::Fold, 0};
    }

    const std::string& name() const override { return name_; }

    std::vector<std::string> choose_discards(
        const SeatView& view) override {
        // GTO lite: play the same sound discards as everyone else (there
        // is no balance edge in a 5-card exchange), capped at the house max.
        const bool deuce = view.showdown == HandConstruction::DeuceSeven;
        return checked_discards(draw_keep(view.hole, deuce), view);
    }

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

// Defined here (after the bot classes) so the helpers they need stay in
// one place.
std::vector<std::string> draw_keep(const std::vector<Card>& hole,
                                   bool deuce) {
    std::array<Card, 5> five{};
    for (std::size_t i = 0; i < 5 && i < hole.size(); ++i) five[i] = hole[i];
    // Count ranks and suits.
    int rank_count[15] = {};
    int suit_count[4] = {};
    for (const Card& c : hole) {
        ++rank_count[static_cast<int>(c.rank)];
        ++suit_count[static_cast<int>(c.suit)];
    }
    // Made hands stand pat — pairs included (drawing to two pair is a
    // classic leak; trips+ doubly so). Deuce made hands are trash, but
    // they still can't improve by drawing one to a pair... actually they
    // can: any pair draws three-plus. Only pat broken lows stand pat.
    const HandValue high = evaluate_five(five);
    if (!deuce) {
        if (high.category >= HandCategory::OnePair) return {};
    } else {
        const DeuceValue low = evaluate_deuce(five);
        if (low.penalty == 0 &&
            static_cast<int>(low.tiebreak[0]) <= 8) {
            return {};  // Pat 8-low or better: don't break it.
        }
    }
    // Four-flush and open-ender draws keep one card (draw one).
    for (int suit = 0; suit < 4; ++suit) {
        if (suit_count[suit] == 4) {
            for (const Card& c : hole) {
                if (static_cast<int>(c.suit) != suit) {
                    return {to_string(c)};
                }
            }
        }
    }
    bool present[15] = {};
    for (const Card& c : hole) {
        present[static_cast<int>(c.rank)] = true;
        if (c.rank == Rank::Ace) present[1] = true;
    }
    if (!deuce) {
        for (int start = 1; start <= 10; ++start) {
            int window = 0;
            for (int r = start; r < start + 5; ++r) {
                if (present[r]) ++window;
            }
            if (window == 4) {
                // Keep the four, throw the odd card out.
                for (const Card& c : hole) {
                    int v = static_cast<int>(c.rank);
                    if (v == 14) v = 1;
                    if (v < start || v >= start + 5) {
                        return {to_string(c)};
                    }
                }
            }
        }
    }
    // Deuce: keep the four lowest unpaired cards, throw the highest (or a
    // paired card first — pairs are the worst holding).
    if (deuce) {
        int drop = -1;
        for (std::size_t i = 0; i < hole.size(); ++i) {
            if (rank_count[static_cast<int>(hole[i].rank)] > 1) {
                drop = static_cast<int>(i);
                break;
            }
        }
        if (drop < 0) {
            drop = 0;
            for (std::size_t i = 1; i < hole.size(); ++i) {
                if (hole[i].rank > hole[static_cast<std::size_t>(drop)].rank) {
                    drop = static_cast<int>(i);
                }
            }
        }
        return {to_string(hole[static_cast<std::size_t>(drop)])};
    }
    // High draw: keep aces-up starting points (ace + kickers), else throw
    // the three lowest non-ace cards... classic: keep the ace, draw three.
    // Simplest sound rule: keep any ace or king, draw the rest (up to cap).
    std::vector<std::string> out;
    for (const Card& c : hole) {
        if (c.rank != Rank::Ace && c.rank != Rank::King) {
            out.push_back(to_string(c));
        }
    }
    if (out.empty()) {
        // All aces/kings but no pair (impossible with 5 cards... AA KK Q
        // is a pair) — fall through pat. Unreachable, but keep it total.
        return {};
    }
    return out;
}

// Trim a wish list to the house cap (keep order: first cards matter most).
std::vector<std::string> cap_discards(std::vector<std::string> want,
                                      int max_draw) {
    if (static_cast<int>(want.size()) > max_draw) {
        want.resize(static_cast<std::size_t>(max_draw));
    }
    return want;
}

// Discard contract guard: every entry must name a card currently in the
// hole (no duplicates). A bot bug here throws inside the engine's discard
// path — which once killed whole stress brackets (0xC0000409) — so filter
// before returning: keep hole cards in order, drop anything else. A short
// list just stands pat on the difference, never an exception.
std::vector<std::string> checked_discards(std::vector<std::string> want,
                                          const SeatView& view) {
    want = cap_discards(std::move(want), view.max_draw);
    std::vector<std::string> safe;
    std::vector<bool> used(view.hole.size(), false);
    for (const std::string& text : want) {
        Card card;
        try {
            card = parse_card(text);
        } catch (const std::exception&) {
            continue;
        }
        for (std::size_t k = 0; k < view.hole.size(); ++k) {
            if (!used[k] && view.hole[k] == card) {
                used[k] = true;
                safe.push_back(text);
                break;
            }
        }
    }
    return safe;
}

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
        } else if (const auto* timed = std::get_if<TimeoutEvent>(&event)) {
            if (timed->seat < 0 ||
                static_cast<std::size_t>(timed->seat) >= summary.seats.size()) {
                throw std::invalid_argument("timeout from unknown seat");
            }
            summary.seats[static_cast<std::size_t>(timed->seat)].folded = true;
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
