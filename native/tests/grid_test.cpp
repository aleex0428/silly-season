// Checks the real-grid reader on the host: a fake career entity holds entrant rows laid out as the SDK's
// game headers say, and the market must take the game's grid from them.
//
//   grid_test.exe <data/baseline.json>
#define SILLY_SEASON_PREVIEW 1
#include "../src/plugin.cpp"

#include <cstdarg>

namespace fake {

std::vector<uint8_t> entity(0x1100), rows;

void put32(std::vector<uint8_t> &b, size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); }
void put16(std::vector<uint8_t> &b, size_t off, uint16_t v) { std::memcpy(b.data() + off, &v, 2); }

// one entrant row: driver and team by stat id (team -1: none; Mercedes is team 0), seat 0/1 (-1: no seat)
void add_row(uint32_t driver, int64_t team, int seat, bool human)
{
    size_t off = rows.size();
    rows.resize(off + 0x88);
    put32(rows, off + 0x48, human ? 0x10000000u : 7u);
    put32(rows, off + 0x50, (driver << 16) | 0x00);
    put32(rows, off + 0x58, team < 0 ? 0x3fff0000u : static_cast<uint32_t>(team) << 16);
    put32(rows, off + 0x5c, seat < 0 ? 0x3fff0000u : 0x00120000u);
    put16(rows, off + 0x80, seat < 0 ? 0 : static_cast<uint16_t>(0x100 | seat));
}

void publish()
{
    uintptr_t b = reinterpret_cast<uintptr_t>(rows.data()), e = b + rows.size();
    std::memcpy(entity.data() + 0x2e0, &b, 8);
    std::memcpy(entity.data() + 0x2e8, &e, 8);
    entity[0x2f8] = 0;
}

int available() { return 1; }
int state(SwoftyCareerState *out)
{
    *out = SwoftyCareerState{};
    return SWOFTY_ASSET_UNAVAILABLE;
}
void *get_entity() { return entity.data(); }

void putp(std::vector<uint8_t> &b, size_t off, const void *p)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(b.data() + off, &v, 8);
}

// the season's standings: a championship whose season 0 holds a weekend and results rounds
std::vector<uint8_t> champ(0x5b0), season_list(0x10), season(0x550), weekends(8), results_rounds;
std::vector<std::vector<uint8_t>> sessions, records;

// a round whose race classified these drivers (stat ids) in this order
void add_round(const std::vector<uint32_t> &order)
{
    records.emplace_back(order.size() * 0x10);
    std::vector<uint8_t> &rec = records.back();
    for (size_t i = 0; i < order.size(); ++i) {
        put32(rec, i * 0x10, order[i] << 16);
        put32(rec, i * 0x10 + 4, static_cast<uint32_t>(i)); // the game counts from 0
    }
    sessions.emplace_back(0x28);
    std::vector<uint8_t> &ses = sessions.back();
    putp(ses, 8, rec.data());
    putp(ses, 16, rec.data() + rec.size());
}

void publish_standings()
{
    results_rounds.assign(sessions.size() * 0x20, 0);
    for (size_t r = 0; r < sessions.size(); ++r) {
        putp(results_rounds, r * 0x20 + 8, sessions[r].data());
        putp(results_rounds, r * 0x20 + 16, sessions[r].data() + sessions[r].size());
    }
    putp(season, 0x288, weekends.data());
    putp(season, 0x290, weekends.data() + weekends.size());
    putp(season, 0x2a8 + 0xd0, results_rounds.data());
    putp(season, 0x2a8 + 0xd8, results_rounds.data() + results_rounds.size());
    putp(season_list, 8, season.data());
    putp(champ, 0x548, season_list.data());
    putp(champ, 0x550, season_list.data() + season_list.size());
    put32(champ, 0x560, 0);
    putp(entity, 0x1058, champ.data());
}

// the contracts manager: one driver's secret meetings, the current one with a team
std::vector<uint8_t> manager(0x400), meeting_list(0x10), meetings_of(0x50), meeting(0x24);
void set_meeting(uint32_t driver, uint32_t id, uint32_t team)
{
    put32(meeting, 0, id);
    meeting[0x14] = 2;
    put32(meeting, 0x20, team << 16);
    putp(meetings_of, 0x18, meeting.data());
    put32(meetings_of, 0x40, driver << 16);
    putp(meeting_list, 8, meetings_of.data());
    putp(manager, 0x3a8, meeting_list.data());
    putp(manager, 0x3b0, meeting_list.data() + meeting_list.size());
}

void *component(const char *name)
{
    return std::string_view(name) == "DriverCareerContractsManagerComponent" ? manager.data() : nullptr;
}

const SwoftyCareer career = {available, state, nullptr, nullptr, get_entity, component, nullptr, nullptr, nullptr, nullptr};

void log_(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}

} // namespace fake

static int failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    failures += ok ? 0 : 1;
}

// the baseline 2025 grid as rows, with an edit applied: a -> b's seat and b -> a's seat
static void grid_rows(const std::string &swap_a, const std::string &swap_b, const std::string &human)
{
    fake::rows.clear();
    for (const ss::Team &t : g_state.teams)
        for (int k = 0; k < 2; ++k) {
            std::string who = t.seats[static_cast<size_t>(k)];
            if (who == swap_a)
                who = swap_b;
            else if (who == swap_b)
                who = swap_a;
            const ss::Driver *d = g_state.driver(who);
            if (d)
                fake::add_row(static_cast<uint32_t>(d->stat), static_cast<uint32_t>(t.stat), k, who == human);
            else
                std::printf("  (no driver for %s seat %d: '%s')\n", t.id.c_str(), k, who.c_str());
        }
    fake::add_row(71, -1, -1, false); // a reserve without a team
    fake::publish();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("grid_test <baseline.json>\n");
        return 2;
    }
    static SwoftyApi api{};
    api.version = SWOFTY_API_VERSION;
    api.log = fake::log_;
    api.career = &fake::career;
    swofty::api = &api;

    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    json::Error err;
    json::Value baseline = json::parse(text.str(), &err);
    if (!ss::init(g_state, baseline, "f1_career_2025", 42, nullptr)) {
        std::printf("init failed\n");
        return 1;
    }
    g_loaded = true;
    g_baseline = baseline;
    g_baseline_ok = true;

    std::printf("an edited grid: Verstappen and Russell swapped, Hamilton the human\n");
    grid_rows("max_verstappen", "george_russell", "lewis_hamilton");
    bool ok = sync_real_grid();
    check(ok, "the rows are read");
    check(g_state.driver("max_verstappen") && g_state.driver("max_verstappen")->team == "mercedes", "Verstappen is at Mercedes");
    check(g_state.driver("george_russell") && g_state.driver("george_russell")->team == "redbull", "Russell is at Red Bull");
    check(g_state.player == "lewis_hamilton" && g_state.player_team == "ferrari", "the player is Hamilton at Ferrari");
    check(g_state.protected_ids.empty(), "nobody is protected");

    std::printf("Konnersport stays out of a career that doesn't have it\n");
    {
        const ss::Team *kon = g_state.team("konnersport");
        check(kon && kon->optional && !kon->active, "Konnersport waits off the grid");
        check(g_state.shielded("aiden_jackson") && g_state.shielded("callie_mayer"), "its drivers can't be signed");
        ss::Settings s;
        update_overlay(s);
        bool listed = false;
        for (const UiDriver &u : g_ov.free_agents)
            listed = listed || u.name == "Aiden Jackson";
        for (const UiTeam &t : g_ov.grid)
            listed = listed || t.name == "Konnersport";
        check(!listed, "neither the team nor its drivers show in the hub");
    }

    std::printf("the eleventh team (Konnersport, in the custom team's slot): the market plans it\n");
    grid_rows("", "", "lewis_hamilton");
    fake::add_row(102, 104, 0, false);
    fake::add_row(127, 104, 1, false);
    fake::publish();
    ok = sync_real_grid();
    check(ok, "the rows are read");
    {
        const ss::Team *kon = g_state.team("konnersport");
        check(kon && kon->active, "Konnersport is in this career");
        check(kon && kon->seats[0] == "aiden_jackson" && kon->seats[1] == "callie_mayer", "Jackson and Mayer race for it");
        check(!g_state.shielded("aiden_jackson") && g_state.protected_ids.empty(), "its drivers are part of the market");
        check(g_row_team_stat["konnersport"] == 104, "its seats are written to the slot the game uses (104)");
        ss::Settings s;
        update_overlay(s);
        bool shown = false;
        for (const UiTeam &t : g_ov.grid)
            shown = shown || t.name == "Konnersport";
        check(shown, "the hub shows it");
    }

    std::printf("My Team in the custom team's slot: the player's own team, not Konnersport\n");
    fake::rows.clear();
    for (const ss::Team &t : g_state.teams)
        for (int k = 0; k < 2; ++k)
            if (const ss::Driver *d = g_state.driver(t.seats[static_cast<size_t>(k)]))
                if (d->id != "lewis_hamilton" && d->id != "liam_lawson" && t.id != "konnersport")
                    fake::add_row(static_cast<uint32_t>(d->stat), static_cast<uint32_t>(t.stat), k, false);
    fake::add_row(9000, 104, 0, true);
    fake::add_row(static_cast<uint32_t>(g_state.driver("liam_lawson")->stat), 104, 1, false);
    fake::publish();
    ok = sync_real_grid();
    check(ok, "the rows are read");
    check(g_state.player_team.empty() && !g_own_team_name.empty(), "the player drives for his own team");
    check(g_state.shielded("liam_lawson"), "his team-mate Lawson is protected");
    check(!g_state.team("konnersport")->active, "Konnersport isn't in this career");
    {
        ss::Settings s;
        update_overlay(s);
        const UiTeam *own = nullptr;
        for (const UiTeam &t : g_ov.grid)
            if (t.id == "#own")
                own = &t;
        check(own && own->name == "My Team" && own->seat[1].name == "Liam Lawson", "My Team is the twelfth team of the grid page, with Lawson");
        bool lawson = false;
        for (const UiDriver &u : g_ov.drivers)
            lawson = lawson || (u.name == "Liam Lawson" && u.team == "My Team");
        check(lawson, "Lawson is in the drivers' table as a My Team driver");
        check(g_ov.team_card("#own") != nullptr, "My Team has a team card");
        int ranks = 0;
        for (const UiTeam &t : g_ov.teams)
            ranks += t.rank >= 1 && t.rank <= static_cast<int>(g_ov.teams.size());
        check(ranks == static_cast<int>(g_ov.teams.size()), "every team, My Team included, has a place in the order");
    }
    {
        ss::Settings s;
        auto picks = ss::scout(g_state, s, "", 5);
        check(!picks.empty(), "the scout has a shortlist for the second seat");
        bool sane = true;
        for (const ss::ScoutPick &p : picks)
            sane = sane && p.driver != "liam_lawson" && p.driver != g_state.player && !g_state.shielded(p.driver);
        check(sane, "the shortlist leaves out the team-mate, the player and protected drivers");
    }

    std::printf("My Team: the human and his team-mate race for a team the baseline doesn't have\n");
    fake::rows.clear();
    for (const ss::Team &t : g_state.teams)
        for (int k = 0; k < 2; ++k)
            if (const ss::Driver *d = g_state.driver(t.seats[static_cast<size_t>(k)]))
                if (d->id != "lewis_hamilton" && d->id != "liam_lawson")
                    fake::add_row(static_cast<uint32_t>(d->stat), static_cast<uint32_t>(t.stat), k, false);
    fake::add_row(9000, 900, 0, true); // a created driver in his own team
    fake::add_row(static_cast<uint32_t>(g_state.driver("liam_lawson")->stat), 900, 1, false);
    fake::publish();
    ok = sync_real_grid();
    check(ok, "the rows are read");
    check(g_state.player_team.empty() && !g_own_team_name.empty(), "the player drives for his own team");
    check(g_state.shielded("liam_lawson"), "his team-mate Lawson is protected");
    check(g_state.driver("liam_lawson")->team.empty(), "Lawson is on no team the market plans");
    {
        ss::Settings s;
        update_overlay(s);
        check(g_ov.has_player && g_ov.player_team == "My Team" && g_ov.teammate == "Liam Lawson", "the hub shows the player's own team");
    }

    std::printf("a career met in its fifth season (2029 in a 2025 career)\n");
    {
        g_state.group = "f1_career_2025";
        int before = g_state.driver("max_verstappen")->contract;
        check(realign_year(4), "the market moves on");
        check(g_state.year == 2029, "the market's year is 2029");
        const ss::Driver *v = g_state.driver("max_verstappen");
        check(v->contract == before + 4 && v->contract >= 2029, "Verstappen's contract moves on four years");
        check(!realign_year(4), "a second visit changes nothing");
        bool veterans_off = true;
        for (const auto &[id, d] : g_state.drivers)
            if (d.team.empty() && d.kind == "f1" && d.age(2029) >= 36 && !d.retired && !g_state.shielded(id) && id != g_state.player)
                veterans_off = false;
        check(veterans_off, "veterans off the grid have retired");
    }

    std::printf("the season's results come from the standings, each round once\n");
    {
        // back to the 2025 grid with Hamilton the human
        g_state.protected_ids.clear();
        grid_rows("", "", "lewis_hamilton");
        sync_real_grid();
        std::vector<uint32_t> order;
        for (const char *id : {"max_verstappen", "lando_norris", "oscar_piastri", "charles_leclerc", "lewis_hamilton", "george_russell",
                               "andrea-kimi_antonelli", "alex_albon", "carlos_sainz-jr", "fernando_alonso", "lance_stroll", "pierre_gasly",
                               "franco_colapinto", "esteban_ocon", "oliver_bearman", "nico_hulkenberg", "gabriel_bortoleto", "yuki_tsunoda",
                               "liam_lawson", "isack_hadjar"})
            if (const ss::Driver *d = g_state.driver(id))
                order.push_back(static_cast<uint32_t>(d->stat));
        check(order.size() == 20, "twenty drivers in the race");
        fake::add_round(order);
        std::vector<uint32_t> reversed(order.rbegin(), order.rend());
        fake::add_round(reversed);
        fake::publish_standings();
        g_state.results_read = 0;
        int v_before = g_state.driver("max_verstappen")->points, h_before = g_state.driver("isack_hadjar")->points;
        take_season_data();
        check(g_results_rounds == 2, "two rounds run");
        check(g_state.results_read == 2, "both rounds taken");
        check(g_state.driver("max_verstappen")->points == v_before + 25 + 0, "Verstappen: a win and a last place");
        check(g_state.driver("isack_hadjar")->points == h_before + 0 + 25, "Hadjar: a last place and a win");
        take_season_data();
        check(g_state.driver("max_verstappen")->points == v_before + 25, "a second visit takes nothing twice");
    }

    std::printf("the player's secret meeting is news once\n");
    {
        size_t news = g_state.news.size();
        g_player_stat = static_cast<uint32_t>(g_state.driver("lewis_hamilton")->stat);
        fake::set_meeting(g_player_stat, 77, static_cast<uint32_t>(g_state.team("mclaren")->stat));
        take_season_data();
        check(g_state.news.size() == news + 1 && g_state.news.back().title.find("MCLAREN") != std::string::npos,
              "the paddock hears of a meeting with McLaren");
        take_season_data();
        check(g_state.news.size() == news + 1, "the same meeting isn't news twice");
    }

    std::printf("drivers of the game the baseline doesn't have (More Driver Models)\n");
    {
        float now = 0, peak = 0;
        check(game_pool::curve("@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.85,0:0.85,1:0.89,1:0.89", now, peak) &&
                  std::fabs(now - 85) < 0.01f && std::fabs(peak - 89) < 0.01f,
              "a statistics curve gives the rating now and at its peak");
        check(game_pool::words("lng_andrea_kimi") == "Andrea Kimi", "a name key gives the name");
        std::vector<ss::GameDriver> pool;
        ss::GameDriver hartley;
        hartley.id = "brendon_hartley";
        hartley.name = "Brendon Hartley";
        hartley.stat = 253;
        hartley.born = 2000;
        hartley.rated = true;
        hartley.pace = 80, hartley.racecraft = 82, hartley.awareness = 84, hartley.experience = 88;
        hartley.pace_peak = 80, hartley.racecraft_peak = 82, hartley.awareness_peak = 84, hartley.experience_peak = 88;
        pool.push_back(hartley);
        ss::GameDriver prost;
        prost.id = "alain_prost";
        prost.name = "Alain Prost";
        prost.born = 1900;
        prost.legend = true;
        pool.push_back(prost);
        ss::GameDriver known;
        known.id = "max_verstappen";
        pool.push_back(known);
        int added = ss::adopt_game_drivers(g_state, pool);
        check(added == 2, "two drivers new to the market");
        const ss::Driver *h = g_state.driver("brendon_hartley");
        check(h && h->pace == 80 && h->kind == "pool" && h->team.empty() && !g_state.shielded("brendon_hartley"),
              "Hartley is a free agent with the game's ratings");
        const ss::Driver *p = g_state.driver("alain_prost");
        check(p && p->kind == "legend" && p->age(g_state.year) >= 26 && p->age(g_state.year) <= 33,
              "Prost is a legend with an age that fits (the game says 1900)");
        check(ss::adopt_game_drivers(g_state, pool) == 0, "a second read adds nobody");
    }

    std::printf("1.1.0 phase 5: who the game takes for a seat (race drivers, free agents; not reserves)\n");
    {
        // a driver off the grid with a stat id: as a reserve (a team, no seat), then as a free agent
        std::string spare;
        for (const auto &[id, d] : g_state.drivers)
            if (d.team.empty() && d.stat > 0 && d.stat != 71 && !d.retired && d.kind != "player" && g_state.driver(id) &&
                id.find("extra") == std::string::npos && d.stat < 3000) {
                spare = id;
                break;
            }
        check(!spare.empty(), "a driver off the grid to test with");
        uint32_t spare_stat = spare.empty() ? 0 : static_cast<uint32_t>(g_state.driver(spare)->stat);
        grid_rows("", "", "lewis_hamilton");
        fake::add_row(spare_stat, 0, -1, false); // Mercedes' reserve: a team, no race seat
        fake::publish();
        check(sync_real_grid() && g_state.game_rows_known, "the rows say who can take a seat");
        check(g_state.seatable("max_verstappen"), "a race driver can take a seat");
        check(!g_state.seatable(spare), "a reserve can't (the core refuses him)");
        grid_rows("", "", "lewis_hamilton");
        fake::add_row(spare_stat, -1, -1, false); // now a free agent
        fake::publish();
        check(sync_real_grid() && g_state.seatable(spare), "a free agent can");
        // a sacking the game turns down: the market undoes it and its news goes
        ss::Settings s;
        ss::Team *t = g_state.team("williams");
        std::string out = t ? t->seats[0] : std::string();
        ss::State::Change ch;
        ch.kind = "sacking";
        ch.team = "williams";
        ch.seat = 0;
        ch.out = out;
        ch.in = spare;
        ch.out_contract = g_state.driver(out) ? g_state.driver(out)->contract : 0;
        ch.news = 999999;
        ch.round = g_state.round;
        g_state.changes.push_back(ch);
        if (t)
            t->seats[0] = spare; // as midseason() leaves the model
        g_state.driver(spare)->team = "williams";
        g_state.driver(spare)->seat = 0;
        if (ss::Driver *o = g_state.driver(out)) {
            o->team.clear();
            o->seat = -1;
            o->released = true;
        }
        g_state.pending.push_back({"williams", 0, spare, "midseason"});
        apply_changes(ss::take_pending(g_state), "mid-season"); // no career ops on the host: the write fails
        check(t && t->seats[0] == out && g_state.driver(out)->team == "williams" && !g_state.driver(out)->released,
              "a refused sacking is undone: the driver keeps his seat");
        check(g_state.changes.empty(), "and the change is gone");
    }

    std::printf("F2 Revamp: silly_season.market's sign() takes a promotion as a signing\n");
    {
        // an F2 driver (a stat id the market doesn't know) takes Alpine's second seat, then F2 Revamp calls sign()
        grid_rows("", "", "lewis_hamilton");
        const ss::Team *alpine = g_state.team("renault");
        uint32_t f2_stat = 2999;
        for (size_t off = 0; alpine && off < fake::rows.size(); off += 0x88) {
            uint32_t team = 0, driver = 0;
            uint16_t pos = 0;
            std::memcpy(&team, fake::rows.data() + off + 0x58, 4);
            std::memcpy(&driver, fake::rows.data() + off + 0x50, 4);
            std::memcpy(&pos, fake::rows.data() + off + 0x80, 2);
            if ((team >> 16) == static_cast<uint32_t>(alpine->stat) && (pos & 0xff) == 1)
                fake::put32(fake::rows, off + 0x50, f2_stat << 16); // his row now
        }
        fake::publish();
        int until = g_state.year + 1;
        int r = kMarketService.sign(f2_stat, alpine ? static_cast<uint32_t>(alpine->stat) : 0, until);
        std::string id = known_driver(f2_stat);
        const ss::Driver *d = g_state.driver(id);
        check(r == 0, "sign() takes it");
        check(d && d->team == "renault" && d->contract == until, "the driver races for Alpine with the contract F2 Revamp gave");
        size_t news = g_state.news.size();
        check(kMarketService.sign(f2_stat, static_cast<uint32_t>(alpine->stat), until) == 0 && g_state.news.size() == news,
              "a repeat is the same contract, with no new news");
        check(kMarketService.sign(f2_stat, 0x3ff0, until) == 1, "an unknown team is refused");
        bool was = g_loaded;
        g_loaded = false;
        check(kMarketService.sign(f2_stat, static_cast<uint32_t>(alpine->stat), until) == 2, "no market loaded: refused");
        g_loaded = was;
    }

    std::printf("rows that don't look like a grid are left alone\n");
    fake::rows.clear();
    fake::add_row(1, 1, 0, false);
    fake::add_row(2, 1, 0, false); // two on one seat
    fake::publish();
    check(!sync_real_grid(), "two drivers on one seat: not synced");
    fake::rows.assign(10, 0);
    fake::publish();
    check(!sync_real_grid(), "a row list of the wrong size: not synced");

    std::printf(failures ? "%d check(s) failed\n" : "all grid checks passed\n", failures);
    return failures ? 1 : 0;
}
