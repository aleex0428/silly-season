// Checks history mode with Progressive Career on the Swofty Seasons' own data (its data/years.json, read from
// the installed addon): a 2021 season told through to 2022's real grid, then the hand-over after 2026.
// Skips (passes) when the addon isn't installed on this PC.
//
//   history_test.exe <data/baseline.json>
#define SILLY_SEASON_PREVIEW 1
#include "../src/plugin.cpp"

#include <cstdarg>

static int failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    failures += ok ? 0 : 1;
}

static void log_(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}

static bool news_has(const char *kind, const char *text)
{
    for (const ss::NewsItem &n : g_state.news)
        if (n.kind == kind && (n.title + " " + n.body).find(text) != std::string::npos)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("history_test <baseline.json>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    static SwoftyApi api{};
    api.version = SWOFTY_API_VERSION;
    api.log = log_;
    swofty::api = &api;

    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    json::Error err;
    g_baseline = json::parse(text.str(), &err);
    json::Value stock = g_baseline["careers"]["f1_career_2026"];
    g_baseline["careers"]["f1_career_sw_progressive"] = std::move(stock);
    g_baseline_ok = true;
    g_real_career = true;
    if (!load_real_data()) {
        std::printf("Progressive Career on the Swofty Seasons isn't installed: history checks skipped\n");
        return 0;
    }
    if (!ss::init(g_state, g_baseline, kRealGroup, 7, nullptr)) {
        std::printf("init failed\n");
        return 1;
    }
    g_state.group = kRealGroup;
    g_loaded = true;
    real_people_in(g_state);
    check(g_state.driver("sw21_raikkonen") && g_state.driver("sw21_raikkonen")->name == "Kimi Räikkönen", "Räikkönen is in the market");

    std::printf("every driver of the real grids has his own name (2016 to 2026)\n");
    {
        std::string unnamed;
        for (int y = g_real_first; y <= g_real_last; ++y)
            for (const auto &[team, pair] : real_season_of(y).grid)
                for (const std::string &id : pair) {
                    const ss::Driver *d = g_state.driver(id);
                    if (!id.empty() && (!d || d->name.rfind("Sw", 0) == 0 || d->name.find('_') != std::string::npos))
                        unnamed += " " + std::to_string(y) + ":" + id;
                }
        if (!unnamed.empty())
            std::printf("  unnamed:%s\n", unnamed.c_str());
        check(unnamed.empty(), "no driver goes by his label");
        check(g_real_first <= 2016 || g_real_first == 2019, "the data starts in 2016 (or 2019 with an older Progressive)");
    }

    std::printf("the real year is told from the rows' looks (the driver at +0x50, his look at +0x54)\n");
    {
        // the game's layout: each driver keeps his own stat id (a season addon's driver one from the launcher), and
        // his look has another. 1.1.0-1.1.2 matched the drivers against the looks: no year, ever
        auto grid_of = [](int year, bool with_looks) {
            std::vector<RealSeat> seats;
            uint32_t own = 300; // drivers' and looks' stat ids overlap in number (2025's and 2026's looks are 261-342)
            for (const json::Value &t : *g_real["seasons"][std::to_string(year)]["teams"].if_array())
                for (const json::Value &d : *t["drivers"].if_array()) {
                    RealSeat s;
                    s.driver = own++;
                    s.team = static_cast<uint32_t>(t["slot"].as_int());
                    s.seat = static_cast<int>(d["position"].as_int());
                    uint32_t look = static_cast<uint32_t>(d["look"]["stat_id"].as_int());
                    if (with_looks)
                        s.look = look;
                    else
                        s.driver = look; // a layout with the look in the driver's field still counts
                    seats.push_back(s);
                }
            return seats;
        };
        for (int y : {g_real_first, 2016, 2021, 2025, g_real_last}) {
            g_last_seats = grid_of(y, true);
            check(detect_real_year() == y, ("the looks tell " + std::to_string(y)).c_str());
        }
        g_last_seats = grid_of(2019, false);
        check(detect_real_year() == 2019, "a row without a look counts by its driver field");
        for (RealSeat &s : g_last_seats)
            s.look = kNoLook, s.driver = 7;
        check(detect_real_year() == 0, "drivers alone tell no year");
        // 15 Nov: six looks of 2015, six of 2016 and eight cars nobody knows: no clear year, the market waits
        if (g_real_first <= 2015) {
            std::vector<RealSeat> a = grid_of(2015, true), b = grid_of(2016, true), mixed;
            for (size_t i = 0; i < 6; ++i) {
                mixed.push_back(a[i]);
                mixed.push_back(b[i]);
            }
            for (size_t i = 0; i < 8; ++i) {
                RealSeat s = a[6 + i];
                s.look = 60000 + static_cast<uint32_t>(i);
                mixed.push_back(s);
            }
            g_last_seats = mixed;
            check(detect_real_year() == 0, "6 cars of 2015, 6 of 2016 and 8 unknown: no year yet");
            g_last_seats = a;
            for (size_t i = 0; i < 4; ++i)
                g_last_seats[i].look = 60000 + static_cast<uint32_t>(i);
            check(detect_real_year() == 2015, "16 of 20 cars of 2015: the year is clear");
        }
        g_last_seats.clear();
    }

    // 15 Nov: Progressive 0.5.0 starts in 2014: its people have names, and 2014 is told through to 2015's real grid
    if (g_real_first <= 2014) {
        std::printf("2014, told through to 2015's real grid\n");
        start_real_season(2014, 19);
        check(g_state.real && g_state.year == 2014, "a real season, 2014");
        for (const char *id : {"sw14_bianchi", "sw14_chilton", "sw14_kobayashi", "sw14_sutil", "sw14_vergne"}) {
            const ss::Driver *d = g_state.driver(id);
            check(d && d->name.find('_') == std::string::npos && d->name.rfind("Sw", 0) != 0,
                  (std::string(id) + " has his own name: " + (d ? d->name : "?")).c_str());
        }
        ss::Settings s14;
        s14.standins = s14.talents = s14.midseason = false;
        for (int r = 1; r <= 19; ++r)
            ss::complete_round(g_state, s14, r, 19);
        check(g_state.finalised, "2014 closes");
        ss::start_new_season(g_state, s14);
        start_real_season(g_state.year, 19);
        check(g_state.year == 2015 && g_state.real, "2015 follows as a real season");
        std::string unseated;
        for (const auto &[team, pair] : real_season_of(2015).grid)
            for (const std::string &id : pair) {
                const ss::Team *t = g_state.team(team);
                if (!id.empty() && (!t || (t->seats[0] != id && t->seats[1] != id)))
                    unseated += " " + id;
            }
        check(unseated.empty(), ("every 2015 driver in his real seat" + unseated).c_str());
    }

    if (g_real_first <= 2016) {
        std::printf("2016, told through to 2017's real grid\n");
        start_real_season(2016, 21);
        check(g_state.real && g_state.year == 2016, "a real season, 2016");
        const ss::Team *m16 = g_state.team("mercedes_26");
        check(m16 && m16->seats[0] == "lewis_hamilton" && m16->seats[1] == "sw16_rosberg", "Mercedes 2016: Hamilton and Rosberg");
        check(g_state.driver("sw16_rosberg") && g_state.driver("sw16_rosberg")->name == "Nico Rosberg" &&
                  g_state.driver("sw16_rosberg")->age(2016) == 31,
              "Rosberg is Nico Rosberg, 31");
        bool verstappen = false;
        for (const ss::State::RealChange &c : g_state.real_changes)
            verstappen = verstappen || c.in == "max_verstappen";
        check(verstappen, "Verstappen's mid-season move to Red Bull is in the season");
        const ss::State::RealMove *nico = nullptr;
        for (const ss::State::RealMove &m : g_state.real_moves)
            if (m.driver == "sw16_rosberg")
                nico = &m;
        check(nico && nico->kind == "retirement", "Rosberg retires as champion");
        ss::Settings s16;
        s16.standins = s16.talents = s16.midseason = false;
        for (int r = 1; r <= 21; ++r)
            ss::complete_round(g_state, s16, r, 21);
        check(g_state.finalised, "2016 closes");
        check(news_has("retirement", "Rosberg") || news_has("retirement", "ROSBERG"), "Rosberg's retirement is announced");
        check(g_state.next_grid["mercedes_26"][0] == "lewis_hamilton" && g_state.next_grid["mercedes_26"][1] == "valtteri_bottas",
              "2017's Mercedes: Hamilton and Bottas");
        // the 2021 checks start from a fresh market
        ss::init(g_state, g_baseline, kRealGroup, 7, nullptr);
        g_state.group = kRealGroup;
        real_people_in(g_state);
    }

    std::printf("2021, told through to 2022's real grid\n");
    start_real_season(2021, 22);
    check(g_state.real && g_state.year == 2021, "a real season, 2021");
    const ss::Team *merc = g_state.team("mercedes_26");
    check(merc && merc->name == "Mercedes" && merc->seats[0] == "lewis_hamilton" && merc->seats[1] == "valtteri_bottas",
          "Mercedes 2021: Hamilton and Bottas");
    auto move = [](const char *driver) -> const ss::State::RealMove * {
        for (const ss::State::RealMove &m : g_state.real_moves)
            if (m.driver == driver)
                return &m;
        return nullptr;
    };
    const ss::State::RealMove *russell = move("george_russell"), *zhou = move("guanyu_zhou"), *kimi = move("sw21_raikkonen");
    check(russell && russell->to == "mercedes_26" && russell->kind == "transfer", "Russell moves to Mercedes");
    check(zhou && zhou->kind == "promotion", "Zhou is a rookie");
    check(kimi && kimi->kind == "retirement", "Räikkönen retires");
    check(move("valtteri_bottas") && move("valtteri_bottas")->to == "sauber_26", "Bottas moves to Alfa Romeo's seat");
    ss::Settings s;
    s.standins = s.talents = false;
    s.midseason = false;
    for (int r = 1; r <= 22; ++r)
        ss::complete_round(g_state, s, r, 22);
    check(g_state.finalised, "the season closes");
    check(news_has("official", "Russell") || news_has("official", "RUSSELL"), "Russell's signing is announced");
    check(news_has("retirement", "Räikkönen") || news_has("retirement", "RÄIKKÖNEN"), "Räikkönen's retirement is announced");
    check(g_state.next_grid["mercedes_26"][0] == "lewis_hamilton" && g_state.next_grid["mercedes_26"][1] == "george_russell",
          "next year's Mercedes: Hamilton and Russell");
    bool fake_only_story = true;
    for (const ss::Deal &d : g_state.deals)
        if (d.stage == ss::Stage::official && d.fake)
            fake_only_story = false;
    check(fake_only_story, "no tabloid story became a signing");
    size_t story = 0;
    for (const ss::NewsItem &n : g_state.news)
        story += n.kind == "rumour" || n.kind == "talks" || n.kind == "official" ? 1 : 0;
    std::printf("  (%zu rumours, talks and signings told)\n", story);
    if (argc > 2)
        for (const ss::NewsItem &n : g_state.news)
            std::printf("    R%-2d %-10s %s\n", n.round, n.kind.c_str(), n.title.c_str());

    std::printf("2022 begins\n");
    ss::start_new_season(g_state, s);
    start_real_season(g_state.year, 22);
    check(g_state.year == 2022 && g_state.real, "a real season, 2022");
    check(g_state.team("mercedes_26")->seats[1] == "george_russell", "Russell sits at Mercedes");

    std::printf("2026: the last real year\n");
    start_real_season(2026, 24);
    check(!g_state.real && g_state.real_moves.empty(), "the market is Silly Season's own from here on");
    check(g_state.team("gm_cadillac_26") && !g_state.team("gm_cadillac_26")->seats[0].empty(), "Cadillac races in 2026");

    std::printf(failures ? "%d check(s) failed\n" : "all history checks passed\n", failures);
    return failures ? 1 : 0;
}
