// Runs whole careers through the market engine on the host and checks the grid stays valid.
//
//   sim_test.exe <data/baseline.json> [seasons] [intensity] [--quiet]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "../src/market.hpp"

using namespace ss;

static int failures = 0;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                           \
            std::printf(__VA_ARGS__);                                                                                  \
            std::printf("\n");                                                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

struct Lcg {
    uint64_t s;
    double next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
};

static void check_grid(const State &st, const std::string &where)
{
    std::set<std::string> seen;
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            if (!t.active) {
                CHECK(t.seats[static_cast<size_t>(k)].empty(), "%s: %s isn't in the career but has a driver", where.c_str(), t.id.c_str());
                continue;
            }
            const std::string &who = t.seats[static_cast<size_t>(k)];
            CHECK(!who.empty(), "%s: %s seat %d is empty", where.c_str(), t.id.c_str(), k);
            if (who.empty())
                continue;
            CHECK(seen.insert(who).second, "%s: %s is on two seats", where.c_str(), who.c_str());
            const Driver *d = st.driver(who);
            CHECK(d != nullptr, "%s: unknown driver %s", where.c_str(), who.c_str());
            if (!d)
                continue;
            CHECK(!d->retired, "%s: retired %s races for %s", where.c_str(), who.c_str(), t.id.c_str());
            CHECK(d->team == t.id && d->seat == k, "%s: %s thinks he is at %s/%d, grid says %s/%d", where.c_str(),
                  who.c_str(), d->team.c_str(), d->seat, t.id.c_str(), k);
            CHECK(d->contract >= st.year, "%s: %s races with a contract ending %d in %d", where.c_str(), who.c_str(),
                  d->contract, st.year);
        }
    for (const auto &[id, d] : st.drivers)
        if (!d.team.empty())
            CHECK(seen.count(id), "%s: %s says %s but is not on the grid", where.c_str(), id.c_str(), d.team.c_str());
}

// 1.1.0 phase 3, gathered over every run
static int g_loyal_renewed = 0, g_loyal_expiring = 0, g_ambitious_renewed = 0, g_ambitious_expiring = 0;
static double g_title_value = 0, g_young_value = 0;
static int g_title_signings = 0, g_young_signings = 0;
static int g_dominoes = 0, g_deadline_items = 0, g_awards = 0, g_media = 0, g_refusals = 0, g_swoops = 0;
// 1.1.0 phase 5
static int g_swaps = 0, g_sponsors = 0, g_principals = 0, g_farewells = 0, g_undone = 0, g_new_topics = 0, g_duels = 0;
// 1.1.0 phase 6
static int g_academy_news = 0, g_reserve_news = 0, g_rookies_feeder = 0, g_rookies_senior = 0, g_standins_family = 0,
           g_standins_all = 0;

// a deal made official mid-season had a seat to go to: a free one, his own, or one whose driver is leaving
static bool seat_ready(const State &st, const Deal &d)
{
    const Team *t = st.team(d.team);
    if (!t)
        return true;
    for (const std::string &who : t->seats) {
        const Driver *o = st.driver(who);
        if (!o || who == d.driver || o->retiring || o->released || o->contract <= st.year)
            return true;
        for (const Deal &x : st.deals)
            if (x.driver == who && x.stage == Stage::official && !x.fake && x.team != t->id)
                return true;
    }
    return false;
}

// a race: order by car + driver with noise, report the top finishers
static void race(State &st, Lcg &rng)
{
    std::vector<std::pair<double, std::string>> order;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                order.push_back({t.performance * 0.7 + d->overall() * 0.5 + rng.next() * 18.0 - (rng.next() < 0.06 ? 100 : 0),
                                 who});
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); ++i)
        record_result(st, order[i].second, order[i].first < 0 ? 0 : static_cast<int>(i + 1));
}

// refuse: the game takes only some drivers for a seat (race drivers and the pool's free agents, as the plugin reads the
// career's rows), and turns down half the mid-season changes anyway: the market must undo them
static int run(const Value &baseline, int seasons, Intensity in, bool legends, bool results, bool quiet, uint64_t seed,
               const char *group = "f1_career_2026", bool konnersport = false, bool blocks_off = false, bool refuse = false)
{
    Settings s;
    if (blocks_off) { // the options that turn phase 3's and phase 5's blocks off
        s.personality = false;
        s.strategies = false;
        s.deadline = false;
        s.living = false;
        s.academies = false;
    }
    s.intensity = in;
    s.legends = legends;
    s.rumours = Rumours::many;
    s.progression = true;
    State st;
    std::string err;
    if (!init(st, baseline, group, seed, &err)) {
        std::printf("FAIL init: %s\n", err.c_str());
        return 1;
    }
    // the player takes Fernando Alonso's seat at Aston Martin
    std::string aston = st.driver("fernando_alonso") ? st.driver("fernando_alonso")->team : std::string();
    std::vector<GridCar> cars;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            cars.push_back({who == "fernando_alonso" ? std::string("player_custom") : who, t.id, who == "fernando_alonso"});
    if (konnersport) // the game shows the eleventh team
        for (const char *who : {"aiden_jackson", "callie_mayer"})
            cars.push_back({who, "konnersport", false});
    sync_grid(st, cars);
    // another addon's drivers in the career's lists (More Driver Models): free agents of the market
    std::vector<GameDriver> extra;
    if (seed % 5 == 0)
        for (int i = 0; i < 40; ++i) {
            GameDriver g;
            g.id = "extra_driver_" + std::to_string(i);
            g.name = "Extra Driver " + std::to_string(i);
            g.stat = 300 + i;
            g.born = i % 3 ? 1900 : 1998;
            g.kind = i % 4 ? "pool" : "f2";
            g.rated = i % 2 == 0;
            float lvl = 70.0f + static_cast<float>(i % 20);
            g.pace = g.racecraft = g.awareness = lvl;
            g.experience = 60;
            g.pace_peak = g.racecraft_peak = g.awareness_peak = lvl + 3;
            g.experience_peak = 85;
            g.legend = i == 7;
            extra.push_back(g);
        }
    if (!extra.empty())
        CHECK(adopt_game_drivers(st, extra) == 40, "the extra drivers didn't all join the market");
    st.player_name = "Tú";
    CHECK(st.player == "player_custom" && st.player_team == aston, "player not seen");
    if (const Team *kon = st.team("konnersport"))
        CHECK(kon->active == konnersport, "Konnersport is %s but the game %s it", kon->active ? "in" : "out", konnersport ? "shows" : "doesn't show");
    check_grid(st, "start");

    Lcg rng{seed * 7 + 3};
    int moves_total = 0;
    season_preview(st, s);
    CHECK(!st.news.empty() && st.news.back().kind == "preseason", "no season preview");
    size_t breaking = 0, player_items = 0, pressure_items = 0, standins_seen = 0, returns_seen = 0, press_answered = 0;
    for (int season = 0; season < seasons; ++season) {
        int rounds = 24;
        for (int r = 1; r <= rounds; ++r) {
            if (results)
                race(st, rng);
            std::set<int> official_before;
            for (const Deal &d : st.deals)
                if (d.stage == Stage::official)
                    official_before.insert(d.id);
            // the career's rows, as the plugin reads them at Team HQ: race drivers, and the pool's free agents
            if (refuse) {
                st.game_rows_known = true;
                st.game_seatable.clear();
                for (const auto &[id, d] : st.drivers)
                    if (!d.team.empty() || d.kind == "pool" || d.kind == "f1")
                        st.game_seatable.insert(id);
            }
            std::set<std::string> seatable = st.game_seatable;
            complete_round(st, s, r, rounds);
            // only a driver the game takes came in mid-season (a sacking's replacement, a stand-in)
            if (refuse) {
                for (const Move &m : st.history)
                    if (m.year == st.year && m.round == r && m.kind == "replaced")
                        CHECK(seatable.count(m.driver), "%d round %d: %s replaced a driver but the game wouldn't seat him", st.year, r,
                              m.driver.c_str());
                for (const State::Standin &sb : st.standins)
                    CHECK(seatable.count(sb.in), "%d round %d: stand-in %s is a reserve in the game", st.year, r, sb.in.c_str());
            }
            // the dominoes fall in order: a signing made official mid-season had a seat to take
            if (r < rounds)
                for (const Deal &d : st.deals)
                    if (d.stage == Stage::official && !d.fake && d.kind != "renewal" && !official_before.count(d.id))
                        CHECK(seat_ready(st, d), "%d round %d: %s signed for %s before a seat there was free", st.year, r,
                              d.driver.c_str(), d.team.c_str());
            // every chain points at a deal of this season, and deal ids are unique
            std::set<int> ids;
            for (const Deal &d : st.deals) {
                CHECK(d.id > 0 && ids.insert(d.id).second, "deal id %d missing or used twice", d.id);
                if (d.chain > 0)
                    CHECK(std::any_of(st.deals.begin(), st.deals.end(), [&](const Deal &x) { return x.id == d.chain; }),
                          "deal %d chains to unknown deal %d", d.id, d.chain);
            }
            CHECK(st.outlets.size() == 5, "the outlets' record is missing");
            // the value chart: every driver on the grid has a point for this round
            // (a driver brought in this round, a mid-season replacement, starts his chart at the next one)
            for (const Team &t : st.teams)
                for (const std::string &who : t.seats)
                    if (const Driver *d = st.driver(who)) {
                        bool joined = std::any_of(st.history.begin(), st.history.end(), [&](const Move &m) {
                            return m.year == st.year && m.round == r && m.driver == who && m.kind == "replaced";
                        });
                        CHECK(joined || (d->values.size() >= static_cast<size_t>(r) && d->values[static_cast<size_t>(r - 1)] > 0),
                              "%d round %d: %s has no value point", st.year, r, who.c_str());
                    }
            // the chains: each link opened by the one before, no deal twice
            {
                std::set<int> used;
                for (const std::vector<int> &chain : transfer_chains(st)) {
                    CHECK(chain.size() >= 2, "a chain of one deal");
                    for (size_t i = 0; i < chain.size(); ++i) {
                        CHECK(used.insert(chain[i]).second, "deal %d in two chains", chain[i]);
                        const Deal *d = deal_with_id(st, chain[i]);
                        CHECK(d != nullptr, "a chain names an unknown deal");
                        if (d && i > 0)
                            CHECK(d->chain == chain[i - 1], "deal %d doesn't follow %d", chain[i], chain[i - 1]);
                    }
                }
            }
            CHECK(deadline_races_left(st) == ((r == rounds - 2) ? 2 : (r == rounds - 1) ? 1 : 0), "deadline day at round %d is %d", r,
                  deadline_races_left(st));
            if (const State::Press *p = press_pending(st)) {
                CHECK(p->answers.size() == 3, "a press question without three answers");
                g_new_topics += p->asked == r && (p->topic == "deadline" || p->topic == "rival" || p->topic == "renewal" || p->topic == "awards");
                if (rng.next() < 0.7) {
                    CHECK(answer_press(st, s, static_cast<int>(rng.next() * 3) % 3), "answer refused");
                    ++press_answered;
                }
            }
            refresh_mails(st, s);
            CHECK(!st.mail_preview.subject.empty() && !st.mail_preview.body.empty() && !st.mail_radar.body.empty() &&
                      !st.mail_grid.body.empty(),
                  "empty mail text in %d round %d", st.year, r);
            breaking += take_breaking(st, 3).size();
            if (r % 5 == 0) {
                mark_seen(st);
                CHECK(unread(st) == 0, "unread after mark_seen");
            }
            // the changes apply in order; the last one on each seat must leave the seat as the model has it
            std::map<std::pair<std::string, int>, SeatChange> last;
            for (const SeatChange &c : take_pending(st)) {
                const Team *t = st.team(c.team);
                CHECK(c.driver != st.player, "the player was moved mid-season");
                // the game turns the change down: the market takes it back, and its news and moves go
                if (refuse && (c.reason == "midseason" || c.reason == "swap") && rng.next() < 0.5) {
                    const State::Change *ch = nullptr;
                    for (const State::Change &x : st.changes)
                        if (x.team == c.team && x.seat == c.seat && x.in == c.driver)
                            ch = &x;
                    CHECK(ch != nullptr, "a %s change without its record", c.reason.c_str());
                    if (!ch)
                        continue;
                    std::string out = ch->out, in_id = ch->in, reserve_of = ch->kind == "sacking" ? ch->other : std::string();
                    int news = ch->news, moves_before = st.midseason_moves;
                    CHECK(cancel_change(st, c.team, c.seat, c.driver), "cancel_change refused a change it made");
                    if (const Team *rt = st.team(reserve_of))
                        CHECK(rt->reserve == in_id, "an undone sacking didn't give %s back his reserve role", in_id.c_str());
                    ++g_undone;
                    const Team *tt = st.team(c.team);
                    CHECK(tt && tt->seats[static_cast<size_t>(c.seat)] == out, "an undone change didn't give %s his seat back", out.c_str());
                    CHECK(!st.driver(out)->released, "an undone sacking left %s released", out.c_str());
                    CHECK(std::none_of(st.news.begin(), st.news.end(), [&](const NewsItem &n) { return n.id == news; }),
                          "an undone change's news is still there");
                    CHECK(std::none_of(st.history.begin(), st.history.end(), [&](const Move &m) {
                              return m.year == st.year && m.round == r && ((m.driver == out && (m.kind == "sacked" || m.kind == "demoted")) ||
                                                                           (m.driver == in_id && (m.kind == "replaced" || m.kind == "promoted")));
                          }),
                          "an undone change's moves are still in the history");
                    CHECK(st.midseason_moves == moves_before - 1, "an undone change still counts");
                    last.erase({c.team, c.seat});
                    continue;
                }
                if (c.reason == "standin") {
                    ++standins_seen;
                    // phase 6: the reserve or an academy driver, when the team has one the game would seat
                    if (t && !blocks_off) {
                        const Driver *in = st.driver(c.driver);
                        ++g_standins_all;
                        g_standins_family += in && (reserve_for(st, *t) == c.driver || in_academy(*t, *in) || in->reserve_of == t->id);
                    }
                    CHECK(!st.standins.empty() && st.standins.back().in == c.driver, "a stand-in change without a record");
                    CHECK(t && t->seats[static_cast<size_t>(c.seat)] != c.driver, "the model seated the stand-in");
                    last.erase({c.team, c.seat});
                    continue;
                }
                if (c.reason == "return")
                    ++returns_seen;
                last[{c.team, c.seat}] = c;
            }
            for (auto &[seat, c] : last) {
                const Team *t = st.team(c.team);
                CHECK(t && t->seats[static_cast<size_t>(c.seat)] == c.driver, "pending change not in the model (%s)",
                      c.reason.c_str());
            }
            CHECK(st.standins.size() <= 1, "more than one stand-in at once");
            check_grid(st, "season " + std::to_string(st.year) + " round " + std::to_string(r));
            // 1.1.0 phase 5: the duel is the same race count seen from either side, the odds are percentages,
            // the player's suitors are every other team with his own first
            for (const Team &t : st.teams) {
                const Driver *a = st.driver(t.seats[0]), *b = st.driver(t.seats[1]);
                if (!a || !b)
                    continue;
                HeadToHead ha = head_to_head(st, *a), hb = head_to_head(st, *b);
                CHECK(ha.mate == b->id && hb.mate == a->id && ha.ahead == hb.behind && ha.behind == hb.ahead,
                      "%d round %d: %s's duel doesn't match (%d-%d vs %d-%d)", st.year, r, t.id.c_str(), ha.ahead, ha.behind, hb.ahead,
                      hb.behind);
                CHECK(ha.ahead + ha.behind <= r, "%d round %d: %s's duel counts more races than were run", st.year, r, t.id.c_str());
                g_duels += ha.ahead + ha.behind > 0;
            }
            for (const Deal &d : st.deals) {
                int o = deal_odds(st, s, d);
                CHECK(o >= 0 && o <= 100 && (d.stage != Stage::official || o == 100) && (d.stage != Stage::collapsed || o == 0),
                      "odds %d for a %s deal", o, stage_name(d.stage));
            }
            {
                std::vector<Suitor> suitors = player_suitors(st, s);
                CHECK(!suitors.empty() && suitors.front().status == "own" && suitors.front().team == st.player_team,
                      "the player's suitors don't start with his own team");
                for (const Suitor &x : suitors)
                    CHECK(x.interest >= 2 && x.interest <= 98, "a suitor's interest is %d", x.interest);
            }
        }
        // the team principals and the budgets stay sane
        for (const Team &t : st.teams) {
            CHECK(!t.tp_name.empty() && t.tp_since <= st.year, "%s has no team principal", t.id.c_str());
            CHECK(t.budget >= 50 && t.budget <= 160, "%s's budget is %.1f", t.id.c_str(), t.budget);
        }
        CHECK(st.finalised, "season %d not finalised after the last round", st.year);
        // personality: who renewed among the drivers whose contracts ran out
        for (const Team &t : blocks_off ? std::vector<Team>{} : st.teams)
            for (const std::string &who : t.seats) {
                const Driver *d = st.driver(who);
                if (!d || d->id == st.player || d->contract > st.year || d->retiring || d->kind == "player")
                    continue;
                auto &g = st.next_grid[t.id];
                bool stayed = g[0] == who || g[1] == who;
                if (d->loyalty >= 0.65f && d->ambition < 0.6f) {
                    ++g_loyal_expiring;
                    g_loyal_renewed += stayed;
                } else if (d->ambition >= 0.65f && d->loyalty < 0.45f) {
                    ++g_ambitious_expiring;
                    g_ambitious_renewed += stayed;
                }
            }
        // strategy: the level of the drivers each kind of team signed
        for (const Move &m : blocks_off ? std::vector<Move>{} : st.history)
            if (m.year == st.year && (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback"))
                if (const Team *t = st.team(m.to))
                    if (const Driver *d = st.driver(m.driver)) {
                        if (t->strategy == "title") {
                            g_title_value += d->overall();
                            ++g_title_signings;
                        } else if (t->strategy == "rebuild" || t->strategy == "youth") {
                            g_young_value += d->overall();
                            ++g_young_signings;
                        }
                    }
        CHECK(std::any_of(st.awards.begin(), st.awards.end(), [&](const State::Award &a) { return a.year == st.year && a.key == "champion"; }),
              "no champion named for %d", st.year);
        for (const NewsItem &n : st.news)
            if (n.year == st.year && blocks_off)
                CHECK(n.kind != "refusal" && n.kind != "wants_out" && n.kind != "deadline" && n.kind != "strategy" &&
                          n.kind != "swap" && n.kind != "sponsor" && n.kind != "principal" && n.kind != "academy" && n.kind != "reserve" &&
                          n.title.rfind("DEADLINE-DAY SWOOP", 0) != 0,
                      "a story of a block that is off: %s", n.title.c_str());
        for (const NewsItem &n : st.news)
            if (n.year == st.year) {
                g_swaps += n.kind == "swap";
                g_sponsors += n.kind == "sponsor";
                g_principals += n.kind == "principal";
                g_farewells += n.kind == "farewell";
                g_academy_news += n.kind == "academy";
                g_reserve_news += n.kind == "reserve";
            }
        // phase 6: an academy's rookies come in through the second or partner team rather than the senior one
        if (!blocks_off)
            for (const Move &m : st.history)
                if (m.year == st.year && m.kind == "promotion")
                    if (const Driver *d = st.driver(m.driver); d && !d->academy.empty())
                        if (const Team *to = st.team(m.to); to && in_academy(*to, *d)) {
                            bool feeder = to->junior || !partner_family(to->family).empty();
                            bool has_feeder = std::any_of(st.teams.begin(), st.teams.end(), [&](const Team &x) {
                                return x.active && ((x.junior && x.family == to->family) || partner_family(x.family) == to->family);
                            });
                            if (feeder)
                                ++g_rookies_feeder;
                            else if (has_feeder)
                                ++g_rookies_senior;
                        }
        // next season's reserves: one team each, none racing next season, shared by the second and partner teams
        if (!blocks_off && st.reserves_year == st.year) {
            std::set<std::string> reserves, racing;
            for (const auto &[team, pair] : st.next_grid)
                for (const std::string &who : pair)
                    racing.insert(who);
            for (const Team &t : st.teams) {
                if (t.reserve.empty())
                    continue;
                CHECK(reserves.insert(t.reserve).second, "%s is the reserve of two teams", t.reserve.c_str());
                CHECK(!racing.count(t.reserve), "reserve %s also races in %d", t.reserve.c_str(), st.year + 1);
                CHECK(lead_team(st, t) == &t, "%s has a reserve of its own but shares its senior team's", t.id.c_str());
            }
        }
        // every driver retiring this winter who raced got his farewell
        for (const auto &[id, d] : st.drivers)
            if (d.retiring && d.starts > 0 && d.kind != "legend" && id != st.player)
                CHECK(std::any_of(st.news.begin(), st.news.end(), [&](const NewsItem &n) { return n.kind == "farewell" && n.driver == id; }),
                      "%s retires without a farewell", id.c_str());
        for (const NewsItem &n : st.news)
            if (n.year == st.year && !blocks_off) {
                g_dominoes += n.kind == "domino";
                g_deadline_items += n.kind == "deadline";
                g_awards += n.kind == "awards";
                g_media += n.kind == "media";
                g_refusals += n.kind == "refusal";
                g_swoops += n.title.rfind("DEADLINE-DAY SWOOP", 0) == 0;
            }
        // next grid is complete and has no duplicates
        std::set<std::string> next;
        for (auto &[team, pair] : st.next_grid)
            for (auto &who : pair) {
                if (const Team *t = st.team(team); t && !t->active)
                    continue; // a team this career doesn't have
                CHECK(!who.empty(), "next grid %d: %s has an open seat", st.year + 1, team.c_str());
                CHECK(next.insert(who).second, "next grid %d: %s twice", st.year + 1, who.c_str());
                // the game seats race drivers and free agents only: a reserve on the next grid would be refused
                CHECK(st.seatable(who), "next grid %d: %s is a reserve the game won't seat", st.year + 1, who.c_str());
            }
        // persistence round trip
        Value saved = to_json(st);
        State back;
        CHECK(from_json(back, saved), "from_json failed");
        // a save from before 1.1.0's market loads: characters, plans, deal ids and the outlets come back
        {
            Value old = saved;
            for (const char *k : {"outlets", "awards", "refusals_told", "next_deal_id", "strategy_reviewed"})
                old.erase(k);
            if (swofty::json::Object *ds = old["drivers"].if_object())
                for (auto &[id, o] : *ds)
                    for (const char *k : {"ambition", "loyalty", "money", "wins", "podiums", "number", "home"})
                        o.erase(k);
            if (swofty::json::Array *ts = old["teams"].if_array())
                for (Value &o : *ts)
                    for (const char *k : {"strategy", "optional", "active"})
                        o.erase(k);
            if (swofty::json::Array *ds = old["deals"].if_array())
                for (Value &o : *ds)
                    for (const char *k : {"id", "chain", "waits", "scored"})
                        o.erase(k);
            // ... and from before phase 5: no principals, budgets' start, career totals, race records or changes
            for (const char *k : {"changes", "team_comfort", "push_signing", "sponsor_news", "principals_out", "farewells_year"})
                old.erase(k);
            if (swofty::json::Object *ds = old["drivers"].if_object())
                for (auto &[id, o] : *ds)
                    for (const char *k : {"c_starts", "c_wins", "c_podiums", "c_points", "titles", "race_pos", "race_team"})
                        o.erase(k);
            if (swofty::json::Array *ts = old["teams"].if_array())
                for (Value &o : *ts)
                    for (const char *k : {"tp_name", "tp_since", "budget_start"})
                        o.erase(k);
            State legacy;
            CHECK(from_json(legacy, old), "an old save doesn't load");
            bool traits = true, plans = true;
            for (const auto &[id, d] : legacy.drivers)
                traits = traits && d.ambition >= 0 && d.loyalty >= 0 && d.money >= 0;
            for (const Team &t : legacy.teams)
                plans = plans && !t.strategy.empty() && t.active && !t.tp_name.empty() && t.budget_start > 0;
            std::set<int> ids;
            for (const Deal &d : legacy.deals)
                ids.insert(d.id);
            CHECK(traits && plans && legacy.outlets.size() == 5 && ids.size() == legacy.deals.size() && !ids.count(0),
                  "an old save lacks characters, plans, the outlets or deal ids");
        }
        {
            std::string a = swofty::json::dump(to_json(back), 0), b = swofty::json::dump(saved, 0);
            size_t at = 0;
            while (at < a.size() && at < b.size() && a[at] == b[at])
                ++at;
            auto near = [&](const std::string &x) {
                std::string out = x.substr(at > 90 ? at - 90 : 0, 140);
                std::replace(out.begin(), out.end(), '\n', ' ');
                return out;
            };
            CHECK(a == b, "JSON round trip differs at %zu: saved ...%s... / reloaded ...%s...", at, near(b).c_str(), near(a).c_str());
        }

        std::string player_team = st.player_team;
        int player_seat = st.driver(st.player) ? st.driver(st.player)->seat : -1;
        // the season's starts go into the careers; the champions' titles add up to the champions named
        long long starts_all = 0;
        int titles = 0;
        for (const auto &[id, d] : st.drivers) {
            starts_all += d.c_starts + d.starts;
            titles += d.titles;
        }
        int champions = static_cast<int>(std::count_if(st.awards.begin(), st.awards.end(), [](const State::Award &a) { return a.key == "champion"; }));
        CHECK(titles == champions, "%d titles but %d champions", titles, champions);
        std::vector<RatingChange> ratings;
        auto changes = start_new_season(st, s, &ratings);
        {
            long long banked = 0;
            for (const auto &[id, d] : st.drivers)
                banked += d.c_starts;
            CHECK(banked == starts_all, "the season's starts weren't banked (%lld of %lld)", banked, starts_all);
        }
        moves_total += static_cast<int>(changes.size());
        // progression writes only our talents' ratings: the game develops its own drivers
        for (const RatingChange &rc : ratings)
            CHECK(st.driver(rc.driver) && st.driver(rc.driver)->regen, "progression wrote %s's ratings over the game's", rc.driver.c_str());
        for (const SeatChange &c : changes)
            CHECK(c.driver != st.player && !(c.team == player_team && c.seat == player_seat),
                  "season change touches the player's car: %s %s", c.team.c_str(), c.driver.c_str());
        // the player stays where he is
        const Team *pt = st.team(player_team);
        CHECK(pt && (pt->seats[0] == st.player || pt->seats[1] == st.player), "the player lost his seat");
        {
            size_t charted = 0;
            for (const auto &[id, d] : st.drivers)
                charted += !d.values_last.empty() && d.values.empty();
            CHECK(charted >= 20, "last season's value chart wasn't kept (%zu drivers)", charted);
        }
        check_grid(st, "start of " + std::to_string(st.year));
        if (!quiet) {
            std::printf("\n=== %d (%zu seat changes, %zu rating updates) ===\n", st.year, changes.size(), ratings.size());
            std::printf("%s", report_contracts(st, s).c_str());
            std::printf("%s", report_history(st, s).c_str());
        }
    }
    for (size_t i = 1; i < st.news.size(); ++i)
        CHECK(st.news[i].id > st.news[i - 1].id, "news ids are not increasing");
    if (!extra.empty()) {
        // the extra drivers took part in the market: some raced in F1 over the seasons
        int raced = 0;
        for (const Move &m : st.history)
            if (m.driver.rfind("extra_driver_", 0) == 0 && !m.to.empty())
                ++raced;
        CHECK(raced > 0, "no extra driver ever got a seat (seed %llu)", static_cast<unsigned long long>(seed));
        const Driver *legend = st.driver("extra_driver_7");
        bool legend_signed = std::any_of(st.history.begin(), st.history.end(),
                                         [](const Move &m) { return m.driver == "extra_driver_7" && !m.to.empty(); });
        CHECK(legend && legend->kind == "legend" && (legends || !legend_signed), "the extra legend raced without the Legends option");
    }
    // new talents: each renamed driver keeps unique game name keys and a fresh name
    std::set<std::string> talent_names;
    for (const auto &[id, d] : st.drivers)
        if (d.regen) {
            CHECK(d.name_keys.size() == 2 && !d.alias_first.empty() && !d.alias_last.empty(), "talent %s lacks a name", id.c_str());
            CHECK(talent_names.insert(d.name).second, "two talents named %s", d.name.c_str());
        }
    CHECK(name_overrides(st).size() == talent_names.size() * 2, "name overrides don't match the talents");
    if (!quiet) {
        Settings es = s;
        std::printf("\n%s\n%s\n", report_f2(st, es).c_str(), report_staff(st, es).c_str());
        std::printf("talents: ");
        for (const auto &[id, d] : st.drivers)
            if (d.regen)
                std::printf("%s [%s, %s, %d] team=%s; ", d.name.c_str(), d.initials.c_str(), d.nat.c_str(), d.age(st.year),
                            d.team.c_str());
        std::printf("\npress answered: %zu\n", press_answered);
    }
    for (const NewsItem &n : st.news) {
        player_items += n.kind == "player";
        pressure_items += n.kind == "pressure";
    }
    if (!quiet) {
        Settings es = s;
        std::printf("\n--- mails at the end ---\n[%s]\n%s\n\n[%s]\n%s\n\n[%s]\n%s\n", st.mail_preview.subject.c_str(),
                    st.mail_preview.body.c_str(), st.mail_radar.subject.c_str(), st.mail_radar.body.c_str(),
                    st.mail_grid.subject.c_str(), st.mail_grid.body.c_str());
        std::printf("\nbreaking banners: %zu, player items (last 200 news): %zu, pressure items: %zu, stand-ins %zu, "
                    "returns %zu\n",
                    breaking, player_items, pressure_items, standins_seen, returns_seen);
        std::printf("\n--- last news ---\n");
        int shown = 0;
        for (auto it = st.news.rbegin(); it != st.news.rend() && shown < 12; ++it, ++shown)
            std::printf("[%d R%d] %s: %s\n", it->year, it->round, it->title.c_str(), it->body.c_str());
        std::printf("\n%s\n%s\n", report_driver_market(st, es).c_str(), report_grid(st, es).c_str());
    }
    std::printf("run seed %llu intensity %d legends %d results %d: %d seat changes over %d seasons, %zu news, %zu moves\n",
                static_cast<unsigned long long>(seed), static_cast<int>(in), legends, results, moves_total, seasons,
                st.news.size(), st.history.size());
    return 0;
}

// 1.1.0 phase 5: Red Bull's second driver out of form, Racing Bulls' driver flying: they swap seats, and the swap can
// be undone when the game turns it down
static void swap_test(const Value &baseline)
{
    Settings s;
    s.intensity = Intensity::chaos;
    State st;
    CHECK(init(st, baseline, "f1_career_2026", 77, nullptr), "init failed");
    Team *rb = st.team("redbull_26"), *vc = st.team("tororosso_26");
    CHECK(rb && vc && rb->family == vc->family && vc->junior, "no sister teams in the 2026 baseline");
    if (!rb || !vc)
        return;
    s.midseason = true;
    auto swapped = [&] {
        return std::any_of(st.changes.begin(), st.changes.end(), [](const State::Change &x) { return x.kind == "swap"; });
    };
    int r = 1;
    std::string down, up;
    for (; r <= 24; ++r) {
        // force the case before every round: Red Bull's second driver struggling, Racing Bulls' first in form
        Driver *a = st.driver(rb->seats[0]), *b = st.driver(rb->seats[1]), *c = st.driver(vc->seats[0]);
        if (a && b && c && !swapped()) {
            Driver *weak = a->overall() < b->overall() ? a : b;
            weak->form = -0.5f;
            c->form = 0.5f;
            c->pace = weak->pace;
            c->racecraft = weak->racecraft;
            c->awareness = weak->awareness;
            c->experience = weak->experience;
            down = weak->id;
            up = c->id;
        }
        complete_round(st, s, r, 24);
        if (swapped())
            break;
    }
    CHECK(swapped(), "no sister-team swap in a season that called for one");
    if (!swapped())
        return;
    State::Change ch;
    for (const State::Change &x : st.changes)
        if (x.kind == "swap")
            ch = x;
    CHECK(ch.out == down && ch.other == vc->id, "the swap moved %s out, not %s", ch.out.c_str(), down.c_str());
    up = ch.in; // the junior team's driver who fits best (either of its two)
    CHECK(st.driver(up)->team == rb->id && st.driver(down)->team == vc->id, "the model didn't swap the seats");
    std::vector<SeatChange> pending = take_pending(st);
    bool asked = std::any_of(pending.begin(), pending.end(), [&](const SeatChange &c) { return c.reason == "swap" && c.driver == up && c.team == rb->id; });
    CHECK(asked, "the swap wasn't sent to the game");
    CHECK(std::any_of(st.news.begin(), st.news.end(), [](const NewsItem &n) { return n.kind == "swap"; }), "the swap made no news");
    CHECK(cancel_change(st, rb->id, ch.seat, up), "the swap can't be undone");
    CHECK(st.driver(down)->team == rb->id && st.driver(up)->team == vc->id, "the undone swap didn't put them back");
    CHECK(std::none_of(st.news.begin(), st.news.end(), [](const NewsItem &n) { return n.kind == "swap"; }), "the undone swap's news stayed");
    check_grid(st, "after an undone swap");
    std::printf("sister swap at round %d: %s up, %s down, undone\n", r, up.c_str(), down.c_str());
}

// F2 Revamp's promotions: a signing another addon made reaches the market once the grid shows the driver, once
static void outside_test(const Value &baseline)
{
    State st;
    CHECK(init(st, baseline, "f1_career_2026", 91, nullptr), "init failed");
    // a free agent of the pool takes Alpine's second seat in the game
    std::string kid, alpine = "renault_26", out;
    for (const auto &[id, d] : st.drivers)
        if (d.team.empty() && !d.retired && d.kind != "legend") {
            kid = id;
            break;
        }
    const Team *t = st.team(alpine);
    CHECK(!kid.empty() && t, "no free agent or no Alpine");
    if (kid.empty() || !t)
        return;
    out = t->seats[1];
    int until = st.year + 1;
    CHECK(!outside_signing(st, "nobody", alpine, until, "F2 Revamp"), "an unknown driver was taken");
    CHECK(outside_signing(st, kid, alpine, until, "F2 Revamp"), "the signing was refused");
    CHECK(apply_outside(st) == 0, "told before the grid shows him");
    std::vector<GridCar> cars;
    for (const Team &x : st.teams)
        for (int k = 0; k < 2; ++k)
            cars.push_back({x.id == alpine && k == 1 ? kid : x.seats[static_cast<size_t>(k)], x.id, false});
    sync_grid(st, cars);
    size_t news = st.news.size();
    CHECK(apply_outside(st) == 1, "not told once the grid shows him");
    const Driver *d = st.driver(kid);
    CHECK(d && d->team == alpine && d->contract == until, "the promoted driver has no contract to %d", until);
    CHECK(st.news.size() == news + 1 && st.news.back().driver == kid && st.news.back().kind == "official", "no news of the promotion");
    CHECK(std::any_of(st.history.begin(), st.history.end(), [&](const Move &m) { return m.driver == kid && m.to == alpine && m.kind == "promoted"; }),
          "no move for the promotion");
    // told again (every Team HQ): the same contract, no new news
    CHECK(outside_signing(st, kid, alpine, until, "F2 Revamp") && apply_outside(st) == 0 && st.news.size() == news + 1,
          "a repeat changed something");
    CHECK(st.outside.size() == 1, "a repeat added a second signing");
    State back;
    CHECK(from_json(back, to_json(st)) && back.outside.size() == 1 && back.outside[0].told && back.outside[0].until == until,
          "the signing isn't saved");
    check_grid(st, "after an outside signing");
    std::printf("outside signing: %s to %s until %d (in for %s)\n", kid.c_str(), alpine.c_str(), until, out.c_str());
}

// a season of the market alone (results raced, the plugin's grid read before each round)
static void play_season(State &st, const Settings &s, Lcg &rng)
{
    for (int r = 1; r <= 24; ++r) {
        race(st, rng);
        complete_round(st, s, r, 24);
    }
}

// the cars of a grid, as the game would show it
static std::vector<GridCar> cars_of(const std::map<std::string, std::array<std::string, 2>> &grid, const State &st)
{
    std::vector<GridCar> cars;
    for (const Team &t : st.teams) {
        auto it = grid.find(t.id);
        if (t.active && it != grid.end())
            for (const std::string &who : it->second)
                cars.push_back({who, t.id, who == st.player});
    }
    return cars;
}

// 1.1.0 (a 1.0.1 report): the game refuses every seat of the new grid. The market takes the game's grid back; the
// signings it told go (never seen) or turn into "deal off" stories (seen), and no move of them stays in the history.
static void season_refusal_test(const Value &baseline, bool seen)
{
    Settings s;
    s.intensity = Intensity::chaos;
    State st;
    CHECK(init(st, baseline, "f1_career_2026", seen ? 515 : 414, nullptr), "init failed");
    Lcg rng{seen ? 515u : 414u};
    play_season(st, s, rng);
    CHECK(st.finalised, "the season didn't end");
    std::map<std::string, std::array<std::string, 2>> old;
    for (const Team &t : st.teams)
        old[t.id] = t.seats;
    if (seen)
        for (NewsItem &n : st.news)
            n.shown = true; // the winter's signings were in a popup already
    size_t deal_off_before = std::count_if(st.news.begin(), st.news.end(), [](const NewsItem &n) { return n.title_en.rfind("DEAL OFF", 0) == 0; });
    auto changes = start_new_season(st, s);
    CHECK(!changes.empty(), "a chaotic winter without seat changes");
    sync_grid(st, cars_of(old, st)); // the game kept last season's grid
    int told = 0;
    for (const SeatChange &c : changes)
        told += season_change_refused(st, c.team, c.driver) ? 1 : 0;
    size_t deal_off = std::count_if(st.news.begin(), st.news.end(), [](const NewsItem &n) { return n.title_en.rfind("DEAL OFF", 0) == 0; });
    for (const SeatChange &c : changes) {
        const Driver *d = st.driver(c.driver);
        if (d && d->team == c.team)
            continue; // he was in that car already (another seat of the same team)
        CHECK(std::none_of(st.history.begin(), st.history.end(), [&](const Move &m) {
                  return m.driver == c.driver && m.to == c.team && m.year >= st.year - 1 &&
                         (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback");
              }),
              "the refused move of %s to %s stayed in the history", c.driver.c_str(), c.team.c_str());
        if (!seen)
            CHECK(std::none_of(st.news.begin(), st.news.end(), [&](const NewsItem &n) {
                      return n.kind == "official" && n.driver == c.driver && n.to == c.team && n.year >= st.year - 1;
                  }),
                  "the unseen signing of %s to %s is still in the news", c.driver.c_str(), c.team.c_str());
    }
    CHECK(told > 0, "no refused seat had a signing to take back");
    CHECK(seen ? deal_off == deal_off_before + static_cast<size_t>(told) : deal_off == deal_off_before,
          "%zu deal-off stories for %d signings taken back (%s)", deal_off - deal_off_before, told, seen ? "seen" : "unseen");
    auto same_pair = [](const std::array<std::string, 2> &p, const std::array<std::string, 2> &q) {
        return p == q || (p[0] == q[1] && p[1] == q[0]);
    };
    for (const Team &t : st.teams)
        CHECK(!t.active || same_pair(t.seats, old[t.id]), "%s's seats aren't the game's", t.id.c_str());
    // the opening item names no newcomer who didn't come (unseen: told again)
    if (!seen)
        for (const NewsItem &n : st.news)
            if (n.kind == "summary" && n.year == st.year && n.round == 0)
                for (const SeatChange &c : changes)
                    if (const Driver *d = st.driver(c.driver); d && d->team != c.team)
                        CHECK(n.body_en.find(d->name + " (" + st.team(c.team)->name + ")") == std::string::npos,
                              "the season's opening item still names %s at %s", d->name.c_str(), c.team.c_str());
    check_grid(st, "after a refused winter");
    std::printf("refused winter (%s): %zu seat changes, %d signings taken back\n", seen ? "seen" : "unseen", changes.size(), told);
}

// 1.1.0 (a 1.0.1 report): My Team, where only the game changes seats. The market signs, sacks and retires nobody,
// sends the game nothing, and tells the game's moves as official once the grid shows them.
static void observer_test(const Value &baseline)
{
    Settings s;
    s.intensity = Intensity::chaos;
    s.midseason = true;
    State st;
    CHECK(init(st, baseline, "f1_career_2026", 616, nullptr), "init failed");
    become_observer(st);
    CHECK(st.observer && st.news.back().title_en == "MY TEAM: THE TEAMS MAKE THE MOVES", "no word that the market observes");
    size_t intro = st.news.size();
    become_observer(st);
    CHECK(st.news.size() == intro, "the observer item told twice");
    Lcg rng{616};
    play_season(st, s, rng);
    CHECK(st.finalised, "the season didn't end");
    CHECK(take_pending(st).empty() && st.standins.empty() && st.changes.empty(), "an observer market sent the game a seat change");
    for (const Move &m : st.history)
        CHECK(m.kind != "sacked" && m.kind != "retirement" && m.kind != "transfer" && m.kind != "promotion" && m.kind != "comeback",
              "an observer market made a %s move (%s)", m.kind.c_str(), m.driver.c_str());
    for (const Deal &d : st.deals)
        CHECK(d.stage != Stage::official, "an observer market made %s's deal official", d.driver.c_str());
    for (const NewsItem &n : st.news)
        CHECK(n.kind != "official" && n.kind != "sacking" && n.kind != "swap", "an observer market told \"%s\"", n.title_en.c_str());
    // the game's winter: two drivers swap teams, a free agent takes a third team's seat
    std::map<std::string, std::array<std::string, 2>> grid, before_grid;
    std::map<std::string, std::string> before;
    for (const Team &t : st.teams) {
        grid[t.id] = t.seats;
        for (const std::string &who : t.seats)
            if (!who.empty())
                before[who] = t.id;
    }
    before_grid = grid;
    const Team &a = st.teams[0], &b = st.teams[3], &c = st.teams[6];
    std::string x = a.seats[1], y = b.seats[1], out = c.seats[0], kid;
    for (const auto &[id, d] : st.drivers)
        if (d.team.empty() && !d.retired && d.kind != "legend" && d.kind != "player") {
            kid = id;
            break;
        }
    grid[a.id][1] = y;
    grid[b.id][1] = x;
    grid[c.id][0] = kid;
    // the paddock had one of them right: an agreed deal of x's with b's team, from an outlet
    Deal tip{x, b.id, "transfer", st.year + 1, st.year + 2, Stage::agreed, false, 20};
    tip.source = 0;
    tip.id = st.next_deal_id++;
    st.deals.push_back(tip);
    int hits = st.outlets[0].hits;
    // a read that misses most cars says nothing
    std::vector<GridCar> partial = cars_of(grid, st);
    partial.resize(6);
    {
        State probe = st;
        sync_grid(probe, partial);
        CHECK(observe_grid(probe, s, before, true) == 0, "a partial read was told as moves");
    }
    size_t news = st.news.size();
    sync_grid(st, cars_of(grid, st));
    int told = observe_grid(st, s, before, true);
    CHECK(told == 3, "%d of the game's 3 moves told", told);
    size_t official = std::count_if(st.news.begin() + static_cast<long>(news), st.news.end(), [](const NewsItem &n) { return n.kind == "official"; });
    CHECK(official == 3, "%zu official stories for the game's 3 moves", official);
    CHECK(st.outlets[0].hits == hits + 1, "the outlet that had it right got no credit");
    CHECK(std::any_of(st.history.begin(), st.history.end(), [&](const Move &m) { return m.driver == kid && m.to == c.id && m.kind != "transfer"; }),
          "the free agent's move isn't a promotion or a comeback");
    CHECK(observe_grid(st, s, [&] {
              std::map<std::string, std::string> now;
              for (const Team &t : st.teams)
                  for (const std::string &who : t.seats)
                      if (!who.empty())
                          now[who] = t.id;
              return now;
          }(), true) == 0,
          "the same grid read again was told again");
    auto changes = start_new_season(st, s);
    CHECK(changes.empty(), "an observer market sent %zu seat change(s) at the new season", changes.size());
    for (const Team &t : st.teams)
        CHECK(!t.active || t.seats == grid[t.id] || (t.seats[0] == grid[t.id][1] && t.seats[1] == grid[t.id][0]),
              "%s's seats aren't the game's after the winter", t.id.c_str());
    for (const Deal &d : st.deals)
        CHECK(false, "a deal (%s) outlived the winter", d.driver.c_str());
    bool named = false;
    for (const NewsItem &n : st.news)
        if (n.kind == "summary" && n.year == st.year && n.round == 0)
            named = n.body_en.find(st.driver(kid)->name) != std::string::npos;
    CHECK(named, "the season's opening item doesn't name the newcomer");
    State back;
    CHECK(from_json(back, to_json(st)) && back.observer, "the observer flag isn't saved");
    check_grid(st, "an observer's new season");
    std::printf("observer: %d of the game's moves told, outlet credited, no seat sent\n", told);
}

// 1.1.0 (a 2026-11-10 report: the hub said 88 where the game said 91): the market takes the game's ratings screen
static void game_ratings_test(const Value &baseline)
{
    State st;
    CHECK(init(st, baseline, "f1_career_2025", 808, nullptr), "init failed");
    Driver *n = st.driver("lando_norris");
    CHECK(n != nullptr, "no Norris in the 2025 baseline");
    if (!n)
        return;
    float pot_before = n->potential() - n->overall();
    CHECK(observe_game_ratings(st, "lando_norris", 91, 81, 89, 73, 81, 94), "the game's ratings were refused");
    CHECK(n->overall() == 91 && n->pace == 94 && n->racecraft == 89 && n->awareness == 81 && n->experience == 81 && n->focus == 73,
          "the market doesn't show the game's ratings (overall %.1f)", n->overall());
    CHECK(n->potential() >= 91 && n->potential() - 91 <= pot_before + 0.01f, "potential %.1f doesn't grow from the game's 91",
          n->potential());
    CHECK(!observe_game_ratings(st, "lando_norris", 140, 81, 89, 73, 81, 94) && n->overall() == 91, "an impossible reading was taken");
    CHECK(!observe_game_ratings(st, "nobody", 80, 80, 80, 80, 80, 80), "an unknown driver was taken");
    // a new talent keeps the market's numbers: his game row holds the old reserve's until the plugin writes his
    for (auto &[id, d] : st.drivers)
        if (d.regen) {
            float pace = d.pace;
            CHECK(!observe_game_ratings(st, id, 60, 50, 50, 50, 50, 50) && d.pace == pace && d.game_overall == 0,
                  "talent %s took his row's old ratings", id.c_str());
            break;
        }
    State back;
    CHECK(from_json(back, to_json(st)) && back.driver("lando_norris")->game_overall == 91 && back.driver("lando_norris")->focus == 73,
          "the game's ratings aren't saved");
    std::printf("game ratings: Norris 91 (pace 94, focus 73), potential %.0f\n", n->potential());
}

// 1.1.2 (a 1.1.1 report: "You" in both seats of a team): a read naming one driver twice seats him once, in the
// human's car when one of the two is the player's
static void duplicate_driver_test(const Value &baseline)
{
    State st;
    CHECK(init(st, baseline, "f1_career_2025", 303, nullptr), "init failed");
    std::vector<GridCar> cars;
    std::string twice;
    std::string team;
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            std::string who = t.seats[static_cast<size_t>(k)];
            if (twice.empty() && k == 1) {
                twice = t.seats[0];
                team = t.id;
                who = twice; // the second seat's row names the first seat's driver, and that one is the human
            }
            cars.push_back({who, t.id, k == 1 && t.id == team});
        }
    sync_grid(st, cars);
    const Team *t = st.team(team);
    CHECK(t && (t->seats[0] == twice) != (t->seats[1] == twice), "%s sits in both seats of %s", twice.c_str(), team.c_str());
    CHECK(st.player == twice, "the human's car wasn't kept");
    std::printf("duplicate driver: %s seated once in %s\n", twice.c_str(), team.c_str());
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: sim_test <baseline.json> [seasons] [--quiet]\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    swofty::json::Error err;
    Value baseline = swofty::json::parse(ss.str(), &err);
    if (!baseline.is_object()) {
        std::printf("FAIL: cannot read %s\n", argv[1]);
        return 2;
    }
    int seasons = argc > 2 ? std::atoi(argv[2]) : 10;
    bool quiet = argc > 3 && std::string(argv[3]) == "--quiet";

    run(baseline, seasons, Intensity::realistic, false, true, quiet, 2026);
    swap_test(baseline);
    outside_test(baseline);
    duplicate_driver_test(baseline);
    season_refusal_test(baseline, false);
    season_refusal_test(baseline, true);
    observer_test(baseline);
    game_ratings_test(baseline);
    // the phase 3 blocks turned off
    for (uint64_t seed = 1; seed <= 4; ++seed)
        run(baseline, seasons, seed % 2 ? Intensity::chaos : Intensity::lively, false, true, true, seed * 71, "f1_career_2026", false, true);
    // 2025 careers: with the eleventh team (Konnersport) and without
    for (uint64_t seed = 1; seed <= 6; ++seed)
        run(baseline, seasons, seed % 3 == 0 ? Intensity::chaos : Intensity::lively, false, true, true, seed * 13, "f1_career_2025", seed % 2 == 1);
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        run(baseline, seasons, Intensity::realistic, false, seed % 2 == 0, true, seed);
        run(baseline, seasons, Intensity::lively, false, true, true, seed * 31);
        run(baseline, seasons, Intensity::chaos, seed % 3 == 0, seed % 2 == 1, true, seed * 97);
    }
    // 1.1.0 phase 5: a game that seats only some drivers and turns half the mid-season changes down
    for (uint64_t seed = 1; seed <= 8; ++seed)
        run(baseline, seasons, seed % 2 ? Intensity::chaos : Intensity::lively, false, true, true, seed * 53,
            seed % 3 ? "f1_career_2026" : "f1_career_2025", seed % 3 == 0, false, true);
    // personality and strategy show over many careers
    double loyal = g_loyal_expiring ? 1.0 * g_loyal_renewed / g_loyal_expiring : 0;
    double ambitious = g_ambitious_expiring ? 1.0 * g_ambitious_renewed / g_ambitious_expiring : 0;
    double title = g_title_signings ? g_title_value / g_title_signings : 0, young = g_young_signings ? g_young_value / g_young_signings : 0;
    std::printf("\nloyal drivers renewed %d/%d (%.0f%%), ambitious %d/%d (%.0f%%)\n", g_loyal_renewed, g_loyal_expiring, loyal * 100,
                g_ambitious_renewed, g_ambitious_expiring, ambitious * 100);
    std::printf("signings' level: title teams %.1f (%d), rebuild/youth teams %.1f (%d)\n", title, g_title_signings, young, g_young_signings);
    std::printf("dominoes %d, deadline items %d, swoops %d, refusals %d, awards %d, media scorecards %d\n", g_dominoes, g_deadline_items,
                g_swoops, g_refusals, g_awards, g_media);
    CHECK(g_loyal_expiring > 20 && g_ambitious_expiring > 20 && loyal > ambitious + 0.05, "loyal drivers don't renew more than ambitious ones");
    CHECK(g_title_signings > 20 && g_young_signings > 20 && title > young + 2.0, "title teams don't sign better drivers than rebuilding ones");
    CHECK(g_dominoes > 0 && g_deadline_items > 0 && g_swoops > 0 && g_refusals > 0 && g_awards > 0 && g_media > 0,
          "a phase 3 story never happens");
    std::printf("phase 5: sister swaps %d, sponsors %d, principals sacked %d, farewells %d, changes undone %d, new press topics %d, "
                "duels %d\n",
                g_swaps, g_sponsors, g_principals, g_farewells, g_undone, g_new_topics, g_duels);
    // (sister swaps need a junior team's driver near the senior's level: rare with the baseline's rookies; swap_test forces one)
    CHECK(g_sponsors > 0 && g_principals > 0 && g_farewells > 0 && g_undone > 0 && g_new_topics > 0 && g_duels > 0,
          "a phase 5 story never happens");
    std::printf("phase 6: academy signings %d, reserve lists %d, academy rookies via the second/partner team %d vs the senior team %d, "
                "stand-ins from the reserve or academy %d of %d\n",
                g_academy_news, g_reserve_news, g_rookies_feeder, g_rookies_senior, g_standins_family, g_standins_all);
    CHECK(g_academy_news > 0 && g_reserve_news > 0, "academies or reserves never sign anyone");
    CHECK(g_rookies_feeder > g_rookies_senior, "academy rookies don't come in through the second or partner team");
    CHECK(g_standins_all == 0 || g_standins_family * 2 >= g_standins_all, "stand-ins rarely come from the reserve or the academy");
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
