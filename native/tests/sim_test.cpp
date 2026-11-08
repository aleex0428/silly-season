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

static int run(const Value &baseline, int seasons, Intensity in, bool legends, bool results, bool quiet, uint64_t seed)
{
    Settings s;
    s.intensity = in;
    s.legends = legends;
    s.rumours = Rumours::many;
    s.progression = true;
    State st;
    std::string err;
    if (!init(st, baseline, "f1_career_2026", seed, &err)) {
        std::printf("FAIL init: %s\n", err.c_str());
        return 1;
    }
    // the player takes Fernando Alonso's seat at Aston Martin
    std::vector<GridCar> cars;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            cars.push_back({who == "fernando_alonso" ? std::string("player_custom") : who, t.id, who == "fernando_alonso"});
    sync_grid(st, cars);
    st.player_name = "Tú";
    CHECK(st.player == "player_custom" && st.player_team == "forceindia_26", "player not seen");
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
            complete_round(st, s, r, rounds);
            if (const State::Press *p = press_pending(st)) {
                CHECK(p->answers.size() == 3, "a press question without three answers");
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
                if (c.reason == "standin") {
                    ++standins_seen;
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
        }
        CHECK(st.finalised, "season %d not finalised after the last round", st.year);
        // next grid is complete and has no duplicates
        std::set<std::string> next;
        for (auto &[team, pair] : st.next_grid)
            for (auto &who : pair) {
                CHECK(!who.empty(), "next grid %d: %s has an open seat", st.year + 1, team.c_str());
                CHECK(next.insert(who).second, "next grid %d: %s twice", st.year + 1, who.c_str());
            }
        // persistence round trip
        Value saved = to_json(st);
        State back;
        CHECK(from_json(back, saved), "from_json failed");
        CHECK(swofty::json::dump(to_json(back), 0) == swofty::json::dump(saved, 0), "JSON round trip differs");

        std::string player_team = st.player_team;
        int player_seat = st.driver(st.player) ? st.driver(st.player)->seat : -1;
        std::vector<RatingChange> ratings;
        auto changes = start_new_season(st, s, &ratings);
        moves_total += static_cast<int>(changes.size());
        for (const SeatChange &c : changes)
            CHECK(c.driver != st.player && !(c.team == player_team && c.seat == player_seat),
                  "season change touches the player's car: %s %s", c.team.c_str(), c.driver.c_str());
        // the player stays where he is
        const Team *pt = st.team(player_team);
        CHECK(pt && (pt->seats[0] == st.player || pt->seats[1] == st.player), "the player lost his seat");
        check_grid(st, "start of " + std::to_string(st.year));
        if (!quiet) {
            std::printf("\n=== %d (%zu seat changes, %zu rating updates) ===\n", st.year, changes.size(), ratings.size());
            std::printf("%s", report_contracts(st, s).c_str());
            std::printf("%s", report_history(st, s).c_str());
        }
    }
    for (size_t i = 1; i < st.news.size(); ++i)
        CHECK(st.news[i].id > st.news[i - 1].id, "news ids are not increasing");
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
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        run(baseline, seasons, Intensity::realistic, false, seed % 2 == 0, true, seed);
        run(baseline, seasons, Intensity::lively, false, true, true, seed * 31);
        run(baseline, seasons, Intensity::chaos, seed % 3 == 0, seed % 2 == 1, true, seed * 97);
    }
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
