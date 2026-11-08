// Checks the real-grid reader on the host: a fake career entity holds entrant rows laid out as the SDK's
// game headers say, and the market must take the game's grid from them.
//
//   grid_test.exe <data/baseline.json>
#define SILLY_SEASON_PREVIEW 1
#include "../src/plugin.cpp"

#include <cstdarg>

namespace fake {

std::vector<uint8_t> entity(0x400), rows;

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
void *component(const char *) { return nullptr; }

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

    std::printf("an eleventh team the baseline doesn't have (Konnersport): its drivers stay where they are\n");
    grid_rows("", "", "lewis_hamilton");
    fake::add_row(102, 104, 0, false);
    fake::add_row(127, 104, 1, false);
    fake::publish();
    ok = sync_real_grid();
    check(ok, "the rows are read");
    {
        std::string jackson = driver_for_stat(102);
        check(g_state.shielded(jackson) && g_state.shielded(driver_for_stat(127)), "both Konnersport drivers are protected");
        ss::Settings s;
        update_overlay(s);
        bool listed = false;
        for (const UiDriver &u : g_ov.free_agents)
            listed = listed || u.name == g_state.driver(jackson)->name;
        check(!listed, "they aren't listed as free agents");
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
