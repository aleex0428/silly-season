// Times the market engine on the host: the work the plugin does on the game's thread at Team HQ (a round), at the
// season's turn and when the career saves or loads, and how the save grows season after season. Not shipped: a tool
// to find what to optimise and to check a change made things faster.
//
//   bench.exe <data/baseline.json> [careers] [seasons] [--json <out.json>] [--compare <old.json>]
//
// --json writes the numbers; --compare prints each one against an earlier run (build.bat bench keeps the last run in
// build\bench_last.json and compares with it on the next run).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../src/market.hpp"

using namespace ss;
using Clock = std::chrono::steady_clock;

namespace {

struct Stat {
    std::vector<double> ms;
    double pct(double p) const
    {
        if (ms.empty())
            return 0;
        std::vector<double> v = ms;
        std::sort(v.begin(), v.end());
        size_t i = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
        return v[std::min(i, v.size() - 1)];
    }
    double mean() const
    {
        double t = 0;
        for (double x : ms)
            t += x;
        return ms.empty() ? 0 : t / static_cast<double>(ms.size());
    }
    double max() const { return ms.empty() ? 0 : *std::max_element(ms.begin(), ms.end()); }
};

std::map<std::string, Stat> g_stats;
std::vector<std::string> g_order;

void record(const std::string &key, Clock::time_point t0)
{
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (!g_stats.count(key))
        g_order.push_back(key);
    g_stats[key].ms.push_back(ms);
}

struct Lcg {
    uint64_t s;
    double next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
};

void race(State &st, Lcg &rng)
{
    std::vector<std::pair<double, std::string>> order;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                order.push_back({t.performance * 0.7 + d->overall() * 0.5 + rng.next() * 18.0 - (rng.next() < 0.06 ? 100 : 0), who});
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); ++i)
        record_result(st, order[i].second, order[i].first < 0 ? 0 : static_cast<int>(i + 1));
}

Value read_json(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    swofty::json::Error err;
    return swofty::json::parse(ss.str(), &err);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: bench <baseline.json> [careers] [seasons] [--json out] [--compare old]\n");
        return 2;
    }
    int careers = 4, seasons = 6;
    const char *json_out = nullptr, *compare = nullptr;
    int positional = 0;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json" && i + 1 < argc)
            json_out = argv[++i];
        else if (a == "--compare" && i + 1 < argc)
            compare = argv[++i];
        else if (positional++ == 0)
            careers = std::max(1, std::atoi(argv[i]));
        else
            seasons = std::max(1, std::atoi(argv[i]));
    }
    Value baseline = read_json(argv[1]);
    if (!baseline.is_object()) {
        std::printf("cannot read %s\n", argv[1]);
        return 2;
    }

    std::vector<std::vector<double>> save_kb(static_cast<size_t>(seasons) + 1);
    Value last_save;
    auto all0 = Clock::now();
    for (int c = 0; c < careers; ++c) {
        const char *group = c % 2 ? "f1_career_2025" : "f1_career_2026";
        Settings s;
        s.intensity = c % 3 == 2 ? Intensity::chaos : Intensity::realistic;
        s.rumours = Rumours::many;
        s.progression = true;
        State st;
        std::string err;
        auto t0 = Clock::now();
        if (!init(st, baseline, group, 1000 + static_cast<uint64_t>(c), &err)) {
            std::printf("init failed: %s\n", err.c_str());
            return 1;
        }
        std::vector<GridCar> cars;
        for (const Team &t : st.teams)
            for (const std::string &who : t.seats)
                cars.push_back({who == "fernando_alonso" ? std::string("player_custom") : who, t.id, who == "fernando_alonso"});
        sync_grid(st, cars);
        season_preview(st, s);
        record("init + first grid + preview", t0);
        Lcg rng{static_cast<uint64_t>(c) * 7 + 3};
        for (int season = 0; season < seasons; ++season) {
            for (int r = 1; r <= 24; ++r) {
                race(st, rng);
                t0 = Clock::now();
                complete_round(st, s, r, 24);
                record(r == 24 ? "complete_round, last round" : "complete_round", t0);
                if (r % 6 == 0 || r == 24) { // a save and a load now and then, as the game saves
                    t0 = Clock::now();
                    Value v = to_json(st);
                    record("save: to_json", t0);
                    t0 = Clock::now();
#if defined(SS_HAS_DUMP_SAVE)
                    std::string text = dump_save(v); // as the plugin writes it (1.1.3)
#else
                    std::string text = swofty::json::dump(v); // an older commit (tools/bench_ab.py): the SDK's writer
#endif
                    record("save: dump", t0);
                    if (r == 24)
                        save_kb[static_cast<size_t>(season)].push_back(static_cast<double>(text.size()) / 1024.0);
                    t0 = Clock::now();
                    swofty::json::Error e;
                    Value p = swofty::json::parse(text, &e);
                    record("load: parse", t0);
                    t0 = Clock::now();
                    State back;
                    from_json(back, p);
                    record("load: from_json", t0);
                }
            }
            std::vector<RatingChange> ratings;
            t0 = Clock::now();
            start_new_season(st, s, &ratings);
            record("start_new_season", t0);
        }
        Value v = to_json(st);
        save_kb[static_cast<size_t>(seasons)].push_back(static_cast<double>(swofty::json::dump(v).size()) / 1024.0);
        if (c == careers - 1)
            last_save = std::move(v);
    }
    double total = std::chrono::duration<double>(Clock::now() - all0).count();

    Value old;
    if (compare) {
        old = read_json(compare);
        if (!old.is_object())
            std::printf("(no earlier run in %s to compare with)\n", compare);
    }
    std::printf("%d career(s) x %d season(s) x 24 rounds, %.1f s\n\n", careers, seasons, total);
    std::printf("%-30s %7s %9s %9s %9s %9s", "operation (ms)", "n", "mean", "p50", "p95", "max");
    if (old.is_object())
        std::printf("   mean before   change");
    std::printf("\n");
    Value out = Value::object();
    Value &ops = out["ops"];
    ops = Value::object();
    for (const std::string &k : g_order) {
        const Stat &st = g_stats[k];
        std::printf("%-30s %7zu %9.3f %9.3f %9.3f %9.3f", k.c_str(), st.ms.size(), st.mean(), st.pct(0.5), st.pct(0.95), st.max());
        if (old.is_object()) {
            const Value &o = old["ops"][k];
            if (o.is_object()) {
                double before = o["mean"].as_double();
                double change = before > 0 ? (st.mean() - before) / before * 100.0 : 0;
                std::printf("   %11.3f   %+6.1f%%%s", before, change, change > 15 ? "  slower" : change < -15 ? "  faster" : "");
            }
        }
        std::printf("\n");
        ops[k] = Value::object({{"n", static_cast<int64_t>(st.ms.size())}, {"mean", st.mean()}, {"p50", st.pct(0.5)},
                                {"p95", st.pct(0.95)}, {"max", st.max()}});
    }
    std::printf("\nsave size at the end of each season (KB, mean of the careers):\n ");
    Value &sizes = out["save_kb"];
    sizes = Value::array();
    for (size_t i = 0; i < save_kb.size(); ++i) {
        double m = 0;
        for (double x : save_kb[i])
            m += x;
        m = save_kb[i].empty() ? 0 : m / static_cast<double>(save_kb[i].size());
        sizes.push_back(m);
        std::printf(" %s%.0f", i == save_kb.size() - 1 ? "| after the last turn: " : "", m);
    }
    std::printf("\n\nwhere the last career's save goes (KB):\n");
    std::vector<std::pair<size_t, std::string>> parts;
    if (const swofty::json::Object *o = last_save.if_object())
        for (const auto &[key, val] : *o)
            parts.push_back({swofty::json::dump(val).size(), key});
    std::sort(parts.rbegin(), parts.rend());
    Value &where = out["save_parts_kb"];
    where = Value::object();
    for (size_t i = 0; i < parts.size() && i < 14; ++i) {
        std::printf("  %-24s %8.1f\n", parts[i].second.c_str(), static_cast<double>(parts[i].first) / 1024.0);
        where[parts[i].second] = static_cast<double>(parts[i].first) / 1024.0;
    }
    if (json_out) {
        std::ofstream f(json_out, std::ios::binary);
        f << swofty::json::dump(out, 1);
    }
    return 0;
}
