// plugin.cpp on the host, run through whole careers against a fake game (fake_game.hpp): the addon registers and
// loads as in the game, and the test plays the career: Team HQ visits, sessions, races with their telemetry, the
// game's saves, the season's turn, leaving and loading again. What the engine's tests (sim_test) can't see is checked
// here: that the market's grid follows the game's, that a game save skipped for "nothing changed" never loses a
// change, that the provisional list is read once, what counts on track, and how long each handler takes.
//
//   plugin_test.exe <data/baseline.json> <swofty.json> [verbose | bench]
//
// verbose prints the addon's log as it goes (otherwise only a failed scenario's); bench times the handlers.
#define SILLY_SEASON_PREVIEW 1
#define SILLY_SEASON_FIXED_SEED 1
#include "../src/plugin.cpp"

#include "fake_game.hpp"

#include <random>

namespace {

int failures = 0;
bool g_verbose = false;
size_t g_scenario_from = 0; // the log's size when the scenario began
int g_scenario_failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok) {
        ++failures;
        ++g_scenario_failures;
    }
}

void begin(const char *title)
{
    std::printf("\n%s\n", title);
    g_scenario_from = ::fake::log_lines.size();
    g_scenario_failures = 0;
}

// a failed scenario shows the addon's log (its last lines), which says what the market made of the game
void end()
{
    if (!g_scenario_failures || g_verbose)
        return;
    size_t from = std::max(g_scenario_from, ::fake::log_lines.size() > 60 ? ::fake::log_lines.size() - 60 : 0);
    std::printf("  ---- the addon's log (last %zu lines of the scenario)\n", ::fake::log_lines.size() - from);
    for (size_t i = from; i < ::fake::log_lines.size(); ++i)
        std::printf("  | %s\n", ::fake::log_lines[i].c_str());
}

std::string read_file(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double ms_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------------------------------------------
// The career the fake game runs: its rows, its round, its sessions

struct Career {
    ::fake::Game &g;
    const json::Value &baseline;
    std::string group, id, human;
    uint32_t mode = SWOFTY_CAREER_DRIVER;
    std::mt19937 rng{12345};
    std::map<uint32_t, std::string> by_stat; // the baseline's drivers
    bool loaded = false, at_hub = false;
    // cars a session adds to the career's race seats (another addon's entry list): driver stat ids, raced for this team
    std::vector<uint32_t> extra_cars;
    std::string extra_team = "sauber";
    bool udp = true; // the game's UDP output is on: the feed's participants carry driver ids
    std::string mate = "victor_martins"; // My Team: the second driver of the player's own team
    bool flipped = false; // the game holds each team's two drivers the other way round from the baseline (build_rows)

    // the game itself puts a driver in another's race seat (its own market in My Team, an editor): the rows swap drivers
    bool game_moves(const std::string &in, const std::string &out)
    {
        size_t a = SIZE_MAX, b = SIZE_MAX;
        for (size_t off = 0; off + 0x88 <= ::fake::rows.size(); off += 0x88) {
            uint32_t d = 0;
            std::memcpy(&d, ::fake::rows.data() + off + 0x50, 4);
            auto it = by_stat.find(d >> 16);
            if (it == by_stat.end())
                continue;
            if (it->second == in)
                a = off;
            if (it->second == out)
                b = off;
        }
        if (a == SIZE_MAX || b == SIZE_MAX)
            return false;
        uint32_t da = 0, db = 0;
        std::memcpy(&da, ::fake::rows.data() + a + 0x50, 4);
        std::memcpy(&db, ::fake::rows.data() + b + 0x50, 4);
        std::memcpy(::fake::rows.data() + a + 0x50, &db, 4);
        std::memcpy(::fake::rows.data() + b + 0x50, &da, 4);
        return true;
    }

    Career(::fake::Game &game, const json::Value &base, std::string data_group, std::string career_id, std::string human_driver,
           uint32_t career_mode = SWOFTY_CAREER_DRIVER)
        : g(game), baseline(base), group(std::move(data_group)), id(std::move(career_id)), human(std::move(human_driver)),
          mode(career_mode)
    {
        build_rows();
    }

    // the data group's grid as the game's career rows: two race drivers a team, the free agents, and the teams'
    // reserve drivers (a team, no race seat)
    void build_rows()
    {
        ss::State w;
        std::string err;
        if (!ss::init(w, baseline, group, 7, &err)) {
            std::printf("  (the fake career can't start: %s)\n", err.c_str());
            return;
        }
        ::fake::rows.clear();
        g.driver_stats.clear();
        g.team_stats.clear();
        by_stat.clear();
        std::set<std::string> placed;
        uint32_t human_stat = 0, human_team = 0;
        for (const ss::Team &t : w.teams) {
            if (!t.active)
                continue;
            g.team_stats[t.id] = static_cast<uint32_t>(t.stat);
            for (int k = 0; k < 2; ++k)
                if (const ss::Driver *d = w.driver(t.seats[static_cast<size_t>(k)])) {
                    ::fake::add_row(static_cast<uint32_t>(d->stat), t.stat, flipped ? 1 - k : k, d->id == human);
                    placed.insert(d->id);
                    if (d->id == human) {
                        human_stat = static_cast<uint32_t>(d->stat);
                        human_team = static_cast<uint32_t>(t.stat);
                    }
                }
            if (const ss::Driver *r = w.driver(t.reserve); r && placed.insert(r->id).second)
                ::fake::add_row(static_cast<uint32_t>(r->stat), t.stat, -1, false);
        }
        if (mode == SWOFTY_CAREER_MY_TEAM) {
            // the player's own team races in the custom team's slot (stat 104): a created driver and a team-mate
            human_stat = 9000;
            human_team = 104;
            ::fake::add_row(human_stat, human_team, 0, true);
            if (const ss::Driver *m = w.driver(mate)) {
                ::fake::add_row(static_cast<uint32_t>(m->stat), human_team, 1, false);
                placed.insert(m->id);
            }
        }
        for (const auto &[did, d] : w.drivers) {
            if (d.regen || d.kind == "player")
                continue;
            g.driver_stats[did] = static_cast<uint32_t>(d.stat);
            by_stat[static_cast<uint32_t>(d.stat)] = did;
            if (placed.count(did) || d.stat == 0) // stat 0 is Carlos Sainz, and what a driver without a stat id holds
                continue;
            if (d.kind == "reserve" && !d.reserve_of.empty()) {
                if (const ss::Team *t = w.team(d.reserve_of)) {
                    ::fake::add_row(static_cast<uint32_t>(d.stat), t->stat, -1, false);
                    continue;
                }
            }
            ::fake::add_row(static_cast<uint32_t>(d.stat), -1, -1, false); // a free agent
        }
        ::fake::publish();
        ::fake::sessions.clear();
        ::fake::records.clear();
        ::fake::publish_standings();
        ::fake::career_state = SwoftyCareerState{};
        ::fake::career_state.loaded = 1;
        ::fake::career_state.mode = mode;
        ::fake::career_state.has_player = human_stat || human_team ? 1 : 0;
        ::fake::career_state.player_driver = human_stat << 16;
        ::fake::career_state.player_team = human_team << 16;
        std::snprintf(::fake::career_state.key, sizeof ::fake::career_state.key, "dc-%08x", human_stat << 16);
        ::fake::have_state = true;
        g.season_index = 0;
        g.round = 1;
        g.rounds = 24;
        g.seats_writable = mode != SWOFTY_CAREER_MY_TEAM; // the core has no contracts manager to write to in My Team
        g.refuse_all = false;
        g.seats_ignored = false;
        g.busy_after_writes = 0;
        g.refuse.clear();
        g.seat_log.clear();
        g.seats_ok = g.seats_refused = 0;
        pause(false);
    }

    struct Seat {
        uint32_t driver = 0, team = 0;
        int seat = 0;
        bool human = false;
    };
    // the race seats the rows hold now
    std::vector<Seat> seats() const
    {
        std::vector<Seat> out;
        for (size_t off = 0; off + 0x88 <= ::fake::rows.size(); off += 0x88) {
            uint32_t ordinal = 0, d = 0, t = 0;
            uint16_t pos = 0;
            std::memcpy(&ordinal, ::fake::rows.data() + off + 0x48, 4);
            std::memcpy(&d, ::fake::rows.data() + off + 0x50, 4);
            std::memcpy(&t, ::fake::rows.data() + off + 0x58, 4);
            std::memcpy(&pos, ::fake::rows.data() + off + 0x80, 2);
            if ((t & 0xffff0000u) == 0x3fff0000u || !(pos >> 8))
                continue;
            out.push_back({d >> 16, t >> 16, pos & 0xff, (ordinal & 0x10000000u) != 0});
        }
        return out;
    }
    std::string team_of_stat(uint32_t stat) const
    {
        for (const auto &[tid, s] : g.team_stats)
            if (s == stat)
                return tid;
        return {};
    }
    // the session's cars: the race seats in row order (car 0 first), named as the game names them
    std::vector<events::Entrant> entrants() const
    {
        std::vector<events::Entrant> out;
        uint32_t car = 0;
        for (const Seat &s : seats()) {
            events::Entrant e;
            e.car = car++;
            auto it = by_stat.find(s.driver);
            e.driver = Ref<drivers::Identity>("game/" + (it == by_stat.end() ? "stat_" + std::to_string(s.driver) : it->second));
            e.team = Ref<teams::Identity>("game/" + team_of_stat(s.team));
            e.human = s.human;
            out.push_back(e);
        }
        for (uint32_t stat : extra_cars) {
            events::Entrant e;
            e.car = car++;
            auto it = by_stat.find(stat);
            e.driver = Ref<drivers::Identity>("game/" + (it == by_stat.end() ? "stat_" + std::to_string(stat) : it->second));
            e.team = Ref<teams::Identity>("game/" + extra_team);
            out.push_back(e);
        }
        return out;
    }
    // the session's car of a driver (its index in the feed and in the telemetry events)
    uint32_t car_of(const std::string &driver) const
    {
        uint32_t car = 0;
        for (const events::Entrant &e : entrants()) {
            if (e.driver.id() == "game/" + driver)
                return car;
            ++car;
        }
        return 99;
    }
    void pause(bool on)
    {
        using Fn = void (*)(uint32_t);
        if (auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"swofty_core.dll"), "StubSetPause")))
            fn(on ? 1u : 0u);
    }

    void context()
    {
        g.set_mode(mode == SWOFTY_CAREER_MY_TEAM ? events::Mode::my_team : events::Mode::driver_career);
        g.set_career(id, group);
        g.career_id = id;
    }
    void create()
    {
        context();
        g.hook("career.created");
        g.hook("career.loaded"); // the game loads the career it just made
        loaded = true;
    }
    void load()
    {
        context();
        g.hook("career.loaded");
        loaded = true;
    }
    void unload()
    {
        if (at_hub)
            leave_hub();
        g.hook("career.unloaded");
        loaded = false;
    }
    void enter_hub()
    {
        context();
        at_hub = true;
        g.fire("career.hub", 1, mode, &::fake::career_state);
    }
    void leave_hub()
    {
        at_hub = false;
        g.fire("career.hub", 0, mode, &::fake::career_state);
    }
    void game_save() { g.hook("career.saving"); }

    // a session of the weekend, as the core tells it: every session comes typed as practice in careers
    void session_start()
    {
        if (at_hub)
            leave_hub();
        context();
        g.set_session(events::SessionType::practice);
        g.set_participants(entrants());
        g.write_feed([&](SwoftyTelemetryFeed &f) {
            std::memset(&f.session, 0, sizeof f.session);
            std::memset(f.participants, 0, sizeof f.participants);
            uint32_t car = 0;
            std::vector<Seat> cars = seats();
            for (uint32_t stat : extra_cars)
                cars.push_back({stat, 0, 0, false});
            for (const Seat &s : cars) {
                if (car >= 24)
                    break;
                if (udp) { // the participants packet: without the game's UDP output the slots stay empty
                    SwoftyParticipant &p = f.participants[car];
                    p.version = SWOFTY_TELEMETRY_VERSION;
                    p.driver_id = s.driver;
                    p.ai_controlled = s.human ? 0 : 1;
                    p.race_number = static_cast<uint8_t>(car + 2);
                    if (s.human)
                        f.session.player_index = car;
                }
                ++car;
            }
            f.session.participant_count = car;
        });
        g.hook("session.start");
    }
    void session_end()
    {
        g.hook("session.end");
        g.hook("session.unload");
    }
    void lights_out() { g.fire("race.start"); }
    void clock(float t)
    {
        g.write_feed([&](SwoftyTelemetryFeed &f) { f.session.session_time = t; });
    }
    void overtake(uint32_t a, uint32_t b, float t)
    {
        clock(t);
        g.fire("telemetry.overtake", a, b);
    }
    void collision(uint32_t a, uint32_t b, float t)
    {
        clock(t);
        g.fire("telemetry.collision", a, b);
    }
    void retirement(uint32_t a, int reason, float t)
    {
        clock(t);
        g.fire("telemetry.retirement", a, reason);
    }

    // the race's classification goes into the game's standings and the game moves to the next round
    void finish_race()
    {
        std::vector<uint32_t> order;
        for (const Seat &s : seats())
            order.push_back(s.driver);
        std::shuffle(order.begin(), order.end(), rng);
        ::fake::add_round(order);
        ::fake::publish_standings();
        ++g.round;
    }
    // a whole weekend from Team HQ: practice, the race (some overtakes and a collision), back to Team HQ
    void weekend(bool with_track_events = true)
    {
        session_start();
        game_save();
        session_end();
        session_start();
        lights_out();
        if (with_track_events) {
            size_t n = std::min<size_t>(seats().size(), 24);
            std::uniform_int_distribution<uint32_t> car(0, static_cast<uint32_t>(n ? n - 1 : 0));
            float t = 5;
            for (int i = 0; i < 12 && n > 1; ++i) {
                uint32_t a = car(rng), b = car(rng);
                if (a != b)
                    overtake(a, b, t += 40);
            }
            uint32_t a = car(rng), b = car(rng);
            if (a != b)
                collision(a, b, t += 10);
        }
        game_save();
        session_end();
        finish_race();
        game_save();
        enter_hub();
        g.frames(2.5);
        game_save();
    }
    // the season ends: the game opens the next one at its first round, with empty standings
    void next_season()
    {
        g.hook("career.season.end");
        ++g.season_index;
        g.round = 1;
        ::fake::sessions.clear();
        ::fake::records.clear();
        ::fake::publish_standings();
    }
};

// the market's race seats and the game's rows tell the same grid; "" or the first difference
std::string grid_difference(const Career &c)
{
    for (const Career::Seat &s : c.seats()) {
        std::string team = c.team_of_stat(s.team);
        const ss::Team *t = g_state.team(team);
        if (!t)
            continue; // a team the market doesn't plan
        std::string who = t->seats[static_cast<size_t>(s.seat)];
        // a stand-in races the car for a round while the seat stays the regular driver's in the market
        for (const ss::State::Standin &sb : g_state.standins)
            if (sb.team == team && sb.out == who)
                who = sb.in;
        const ss::Driver *d = g_state.driver(who);
        uint32_t stat = s.human ? s.driver : d ? static_cast<uint32_t>(d->stat) : 0xffffffffu;
        if (s.human ? who != g_state.player : (!d || stat != s.driver))
            return team + " seat " + std::to_string(s.seat) + ": the game has driver " + std::to_string(s.driver) +
                   ", the market " + (who.empty() ? std::string("nobody") : who);
    }
    return {};
}

// what the storage holds for the loaded career is the market as it is now; "" or what differs
std::string save_difference(const ::fake::Game &g)
{
    if (!g_loaded)
        return {};
    auto it = g.files.find(storage_path(g_state.career));
    if (it == g.files.end())
        return "nothing is stored under " + storage_path(g_state.career);
    std::string now = ss::dump_save(ss::to_json(g_state));
    if (it->second == now)
        return {};
    size_t at = 0;
    while (at < now.size() && at < it->second.size() && now[at] == it->second[at])
        ++at;
    size_t from = at > 60 ? at - 60 : 0;
    return "stored and current differ at byte " + std::to_string(at) + ": stored ..." + it->second.substr(from, 120) +
           "... now ..." + now.substr(from, 120) + "...";
}

// the loaded career's save as the addon wrote it, kept next to the exe (build/saves/<name>.json) for the tools to
// read: tools/ss_save.py check tells whether each is a coherent market
std::string g_saves_dir;
void keep_save(const ::fake::Game &g, const char *name)
{
    auto it = g.files.find(storage_path(g_state.career));
    if (g_saves_dir.empty() || it == g.files.end())
        return;
    CreateDirectoryA(g_saves_dir.c_str(), nullptr);
    std::ofstream f(g_saves_dir + "/" + name + ".json", std::ios::binary);
    f << it->second;
}

// ---------------------------------------------------------------------------------------------------
// Scenarios

void registration(::fake::Game &g)
{
    begin("the addon registers and loads");
    check(g.registry.ids("hub.tab").size() == 3, "three Team HQ tabs (2025, Progressive, F2 Revamp): " +
                                                     std::to_string(g.registry.ids("hub.tab").size()));
    check(::fake::Game::logged("is not a") == 0, "every setting of the code is an option of swofty.json");
    check(g.handlers("career.loaded") == 1 && g.handlers("career.saving") == 1 && g.handlers("career.unloaded") == 1,
          "career loaded, saving and unloaded are handled");
    check(g.handlers("telemetry.overtake") == 1 && g.handlers("race.start") == 1 && g.handlers("career.hub") == 1,
          "the hub, lights out and the overtakes are subscribed");
    check(g.frame_fns.size() == 1, "one frame callback");
    check(!g.published.empty() && g.published[0] == "silly_season.market", "the market service is published");
    check(::fake::Game::logged("baseline: ok") == 1, "the baseline was read");
    end();
}

// a whole season of a new Driver Career and the turn into the next, with every game save checked
void whole_season(::fake::Game &g, const json::Value &baseline)
{
    begin("a new Driver Career (2025): a season, its turn, leaving and loading again");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:new1", "lando_norris");
    c.create();
    check(g_loaded, "the market is loaded for the new career");
    c.enter_hub();
    g.frames(2.5);
    check(g_state.player == "lando_norris" && g_state.player_team == "mclaren",
          "the player is Norris at McLaren: " + g_state.player + " / " + g_state.player_team);
    check(grid_difference(c).empty(), "first Team HQ: the market's grid is the game's " + grid_difference(c));
    c.game_save();
    check(save_difference(g).empty(), "the first save holds the market " + save_difference(g));
    check(g.files.count("careers/provisional.json") == 1, "a new career's market is noted in the provisional list");

    int bad_grid = 0, bad_save = 0;
    std::string first_grid, first_save;
    auto look = [&](const char *when) {
        if (std::string d = grid_difference(c); !d.empty() && !bad_grid++)
            first_grid = std::string(when) + ": " + d;
        if (std::string d = save_difference(g); !d.empty() && !bad_save++)
            first_save = std::string(when) + ": " + d;
    };
    for (int r = 1; r <= 24; ++r) {
        if (r == 3) // the game saved the new career's slot: it runs under its real id from here
            c.id = "f1_career_2025.driver_career:{0f6c7a55-21aa-4f0e-9c3b-5d2f1e8a7b10}";
        c.weekend();
        look(("round " + std::to_string(r)).c_str());
    }
    check(g_state.round == 24, "the market ran the 24 rounds: " + std::to_string(g_state.round));
    check(bad_grid == 0, "after every race the market's grid is the game's" + (bad_grid ? " (" + first_grid + ")" : std::string()));
    check(bad_save == 0, "after every game save the storage holds the market" + (bad_save ? " (" + first_save + ")" : std::string()));
    check(g_state.career == c.id, "the market moved to the career's real id");
    check(g.files.count(storage_path("f1_career_2025.driver_career:new1")) == 0, "the provisional id's file is gone");
    check(g.reads["careers/provisional.json"] <= 1,
          "the provisional list was read at most once: " + std::to_string(g.reads["careers/provisional.json"]));
    check(::fake::Game::logged("on track, round") >= 20, "the races' overtakes and collisions reached the market: " +
                                                           std::to_string(::fake::Game::logged("on track, round")) + " rounds");
    check(::fake::Game::logged("results: 24 round(s) run") >= 1, "the game's results were read to the last round");

    int year = g_state.year;
    size_t seats_before = static_cast<size_t>(g.seats_ok + g.seats_refused);
    c.next_season();
    c.enter_hub();
    g.frames(2.5);
    c.game_save();
    check(g_state.year == year + 1 && g_state.season_index == 1, "the season turned: " + std::to_string(g_state.year));
    check(static_cast<size_t>(g.seats_ok + g.seats_refused) > seats_before,
          "the new grid was written to the game: " + std::to_string(g.seats_ok) + " seat(s) taken, " +
              std::to_string(g.seats_refused) + " refused");
    check(grid_difference(c).empty(), "the new season's grid is the game's " + grid_difference(c));
    check(save_difference(g).empty(), "and it is saved " + save_difference(g));

    // leaving and loading again gives the same market
    c.weekend();
    std::string before = ss::dump_save(ss::to_json(g_state));
    int news = static_cast<int>(g_state.news.size());
    c.unload();
    check(!g_loaded, "the career unloads");
    check(g.files[storage_path(c.id)] == before, "leaving saved the market as it was");
    check(::fake::Game::logged("the game's saves for this career") >= 1, "the saves' count is logged on leaving");
    c.load();
    c.enter_hub();
    check(g_loaded && g_state.year == year + 1 && g_state.round == 1 && static_cast<int>(g_state.news.size()) >= news,
          "loading again continues it: " + std::to_string(g_state.year) + ", round " + std::to_string(g_state.round));
    check(grid_difference(c).empty(), "with the game's grid " + grid_difference(c));
    c.game_save();
    keep_save(g, "driver_career_two_seasons");
    c.unload();
    end();
}

// The game saves the profile all the time; 1.1.3 skips the market's JSON when nothing took its lock since the last
// write. Whatever happens between two saves, what is stored must be the market as it is.
void saves_never_lose(::fake::Game &g, const json::Value &baseline, unsigned seed, int steps)
{
    begin(("game saves between random events never lose a change (seed " + std::to_string(seed) + ")").c_str());
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{seed-" + std::to_string(seed) + "}", "george_russell");
    c.rng.seed(seed);
    c.load();
    c.enter_hub();
    g.frames(2.5);
    c.game_save();
    std::mt19937 rng(seed * 7919u + 1);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    int bad = 0, in_session = 0;
    std::string first, last_action;
    std::map<std::string, int> done;
    for (int i = 0; i < steps; ++i) {
        int a = pick(15);
        const char *what = "";
        size_t cars = std::min<size_t>(c.seats().size(), 24);
        switch (a) {
        case 0:
            what = "team hq";
            if (in_session) {
                c.session_end();
                in_session = 0;
            }
            c.enter_hub();
            break;
        case 1:
            what = "frames";
            g.frames(0.5 + pick(4));
            break;
        case 2:
            what = "session start";
            if (in_session)
                c.session_end();
            c.session_start();
            in_session = 1;
            break;
        case 3:
            what = "lights out";
            if (in_session)
                c.lights_out();
            break;
        case 4:
        case 5:
            what = "overtake";
            c.overtake(static_cast<uint32_t>(pick(int(cars))), static_cast<uint32_t>(pick(int(cars))), float(i) * 3);
            break;
        case 6:
            what = "collision";
            c.collision(static_cast<uint32_t>(pick(int(cars))), static_cast<uint32_t>(pick(int(cars))), float(i) * 3);
            break;
        case 7:
            what = "retirement";
            c.retirement(static_cast<uint32_t>(pick(int(cars))), pick(2) ? 8 : 3, float(i) * 3);
            break;
        case 8: {
            what = "race finished";
            if (in_session) {
                c.session_end();
                in_session = 0;
            }
            if (g.round <= g.rounds)
                c.finish_race();
            else
                c.next_season();
            break;
        }
        case 9: {
            what = "ratings read";
            drivers::events::RatingsRead r{};
            r.size = sizeof r;
            r.payload_version = 1;
            auto it = c.by_stat.begin();
            std::advance(it, pick(int(c.by_stat.size())));
            std::string ref = "game/" + it->second;
            r.driver_id = ref.c_str();
            r.stat = static_cast<drivers::Stat>(pick(4));
            r.value = float(60 + pick(35));
            g.hook("driver.ratings.read", &r);
            break;
        }
        case 10:
            what = "press answer";
            g.pick("press_answer", std::to_string(1 + pick(3)));
            g.frames(0.1);
            break;
        case 11:
            what = "menu report";
            g.pick("menu", pick(2) ? "noticias" : "bolsa");
            g.frames(0.1);
            break;
        case 12:
            what = "panel opened";
            g_seen_request = true;
            g.frames(0.1);
            break;
        case 13:
            what = "overlays watched";
            g_awards_done = g_state.year;
            g_cover_done = g_state.year;
            g_reveal_done = g_state.year;
            g.frames(0.1);
            break;
        case 14:
            what = "lap and weekend end";
            g.hook("session.lap");
            g.hook("career.weekend.end");
            g.hook("career.moves.finalised");
            break;
        }
        ++done[what];
        c.game_save();
        if (std::string d = save_difference(g); !d.empty() && !bad++)
            first = "step " + std::to_string(i) + ", after '" + what + "' (before it: '" + last_action + "'): " + d;
        last_action = what;
    }
    std::string mix;
    for (const auto &[what, n] : done)
        mix += (mix.empty() ? "" : ", ") + std::string(what) + " " + std::to_string(n);
    std::printf("  (%d steps: %s)\n", steps, mix.c_str());
    check(bad == 0, "the storage held the market after each of the " + std::to_string(steps) + " game saves" +
                        (bad ? ": " + std::to_string(bad) + " differ; first at " + first : std::string()));
    check(g_saves_skipped > steps / 4, "and many were skipped without building the JSON: " + std::to_string(g_saves_skipped));
    c.unload();
    end();
}

// What a race's telemetry counts. The core tells an overtake at every change of the classification, so a five-lap
// race gave 746 of them in the game (2026-11-11): places swapped back and forth, the start, and three cars of
// another addon's entry list that hold no seat in the career.
void track_events(::fake::Game &g, const json::Value &baseline)
{
    begin("on track: what the market keeps of a race's telemetry");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{track}", "lando_norris");
    // on track through another addon's entry list: Crawford and Aron, free agents of this career, and Perez, whom a
    // 2025 career doesn't have at all
    c.extra_cars = {148, 160, 14};
    c.load();
    c.enter_hub();
    g.frames(2.5);
    uint32_t ver = c.car_of("max_verstappen"), lec = c.car_of("charles_leclerc"), ham = c.car_of("lewis_hamilton"),
             rus = c.car_of("george_russell"), alo = c.car_of("fernando_alonso"), sai = c.car_of("carlos_sainz-jr"),
             cra = c.car_of("jak_crawford"), aro = c.car_of("paul_aron"), per = c.car_of("stat_14"), nor = c.car_of("lando_norris"),
             pia = c.car_of("oscar_piastri"), gas = c.car_of("pierre_gasly"), oco = c.car_of("esteban_ocon");
    check(std::max({ver, lec, ham, rus, alo, sai, cra, aro, per, nor, pia, gas, oco}) < 24, "the test's thirteen cars are on the grid");
    size_t from = ::fake::log_lines.size();
    c.session_start();
    check(::fake::Game::logged("3 car(s) hold no race seat in the market's grid: jak_crawford (sauber), paul_aron (sauber), stat_14 (sauber)", from) == 1,
          "the three cars without a seat in the career are named in the log: " + ::fake::Game::last_line("hold no race seat"));
    c.overtake(ver, lec, 1); // before lights out
    c.lights_out();
    c.overtake(ver, lec, 10);  // kept
    c.overtake(ver, lec, 20);  // the same pair again: repeated
    c.overtake(ham, rus, 22);  // kept, until
    c.overtake(rus, ham, 30);  // he takes the place back: neither stands
    c.overtake(ver, lec, 100); // well later: a new overtake
    c.overtake(cra, alo, 110); // a car without a seat in the career
    c.overtake(alo, aro, 111); // and another
    c.overtake(per, gas, 112); // a car the market can't tell
    c.pause(true);
    c.overtake(alo, gas, 120); // while paused
    c.pause(false);
    c.overtake(nor, pia, 130); // the player's
    c.overtake(sai, oco, 135); // Carlos Sainz: driver id 0
    c.collision(alo, gas, 200);
    c.collision(gas, alo, 203); // the same crash told from the other car
    c.retirement(oco, 8, 210);
    c.retirement(oco, 8, 215); // he retires once
    c.retirement(cra, 3, 216); // a car without a seat
    c.overtake(pia, ver, 300); // then a flashback to 250: this never happened
    c.overtake(lec, pia, 251);
    c.session_end();
    c.overtake(ver, lec, 400); // after the race (its replays)
    std::string line = ::fake::Game::last_line("on track: race over");
    check(line.find("18 event(s) from the game, 7 kept (5 overtake(s), 1 collision(s), 1 retirement(s))") != std::string::npos,
          "18 events in the race, 7 kept: " + line);
    check(line.find("left out: 3 repeated, 2 given back, 3 of cars with no race seat in the market, 1 of cars it can't tell, "
                    "1 while paused, 1 before a flashback or restart") != std::string::npos,
          "and each one left out has its reason");
    c.finish_race();
    c.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("on track, round 1: 5 overtake(s), 1 collision(s), 1 mechanical retirement(s)", from) == 1,
          "Team HQ hands the race to the market: " + ::fake::Game::last_line("on track, round"));
    auto passes = [](const char *id) {
        const ss::Driver *d = g_state.driver(id);
        return d ? static_cast<int>(d->s_overtakes) : -1;
    };
    check(passes("max_verstappen") == 2 && passes("carlos_sainz-jr") == 1 && passes("lando_norris") == 1 && passes("charles_leclerc") == 1,
          "Verstappen 2, Sainz 1, the player 1, Leclerc 1: " + std::to_string(passes("max_verstappen")) + ", " +
              std::to_string(passes("carlos_sainz-jr")) + ", " + std::to_string(passes("lando_norris")) + ", " +
              std::to_string(passes("charles_leclerc")));
    check(passes("jak_crawford") == 0 && passes("lewis_hamilton") == 0 && passes("george_russell") == 0 && passes("oscar_piastri") == 0,
          "nothing for the car without a seat, the place given back or the flashback");
    const ss::Driver *al = g_state.driver("fernando_alonso"), *oc = g_state.driver("esteban_ocon");
    check(al && al->s_incidents == 1 && oc && oc->s_failures == 1, "one incident for Alonso, one failure for Ocon");

    // a restart: lights out twice in one session
    from = ::fake::log_lines.size();
    c.session_start();
    c.lights_out();
    c.overtake(ver, lec, 10);
    c.overtake(ham, rus, 50);
    c.lights_out();
    c.overtake(gas, alo, 8);
    c.session_end();
    check(::fake::Game::logged("lights out again in this session (a restart): 2 event(s) of the first start are dropped", from) == 1,
          "a restarted race drops its first start");
    check(::fake::Game::last_line("on track: race over").find("1 kept (1 overtake(s)") != std::string::npos,
          "and keeps what happened after it: " + ::fake::Game::last_line("on track: race over"));
    c.finish_race();
    c.enter_hub();

    // the game's UDP output is off: no car can be told, and nothing may land on the player
    int mine = passes("lando_norris");
    c.udp = false;
    c.session_start();
    c.lights_out();
    c.overtake(ver, lec, 10);
    c.overtake(nor, pia, 20);
    c.collision(alo, gas, 30);
    c.retirement(oco, 8, 40);
    c.session_end();
    line = ::fake::Game::last_line("on track: race over");
    check(line.find("4 event(s) from the game, 0 kept") != std::string::npos && line.find("4 of cars it can't tell") != std::string::npos,
          "without driver ids nothing is kept: " + line);
    c.finish_race();
    c.enter_hub();
    check(passes("lando_norris") == mine, "and the player isn't given the others' overtakes");
    c.unload();
    end();
}

// My Team: the core can't write race seats there, so the market only watches the game's own moves
void my_team(::fake::Game &g, const json::Value &baseline)
{
    begin("My Team: the market observes and writes no seat");
    g.pick("intensity", "caos");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.my_team:{own}", "", SWOFTY_CAREER_MY_TEAM);
    size_t from = ::fake::log_lines.size();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    check(g_state.observer, "the career is an observer's");
    check(::fake::Game::logged("My Team: only the game changes race seats here", from) == 1, "and the log says so");
    check(g_state.own_mate == "victor_martins" && g_state.player_team.empty(),
          "the player's own team: his team-mate is Martins (" + g_state.own_mate + ")");
    int bad = 0;
    std::string first;
    for (int r = 1; r <= 24; ++r) {
        if (r == 12)
            c.game_moves("jak_crawford", "esteban_ocon"); // the game's own market: Crawford takes Ocon's Haas
        c.weekend();
        if (std::string d = grid_difference(c); !d.empty() && !bad++)
            first = "round " + std::to_string(r) + ": " + d;
    }
    check(bad == 0, "the market's grid is the game's after every race" + (bad ? " (" + first + ")" : std::string()));
    check(::fake::Game::logged("observer: 1 move(s) the game made told as official", from) == 1,
          "the game's move is told as a signing: " + ::fake::Game::last_line("observer:"));
    c.next_season();
    c.enter_hub();
    g.frames(2.5);
    check(g.seats_ok + g.seats_refused == 0, "no seat was asked of the game in the whole season and its turn: " +
                                                 std::to_string(g.seats_ok + g.seats_refused));
    check(::fake::Game::logged("the game's grid stands", from) == 1, "the new season keeps the game's grid");
    check(grid_difference(c).empty(), "which is still the market's " + grid_difference(c));
    c.game_save();
    check(save_difference(g).empty(), "and it is saved " + save_difference(g));
    keep_save(g, "my_team_observer");
    c.unload();
    g.pick("intensity", "realista");
    end();
}

// The game refuses every seat the market asks for (a reserve, a busy career): whatever the market announced is
// taken back, so its grid stays the game's and no news tells a move that never happened.
void refused_seats(::fake::Game &g, const json::Value &baseline, int career)
{
    begin(("the game refuses every seat change: the market takes its moves back (career " + std::to_string(career) + ")").c_str());
    g.pick("intensity", "caos");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{refused-" + std::to_string(career) + "}", "charles_leclerc");
    c.rng.seed(static_cast<unsigned>(career));
    size_t from = ::fake::log_lines.size();
    c.load();
    g.refuse_all = true;
    c.enter_hub();
    g.frames(2.5);
    int bad = 0;
    std::string first;
    for (int r = 1; r <= 24; ++r) {
        c.weekend();
        if (std::string d = grid_difference(c); !d.empty() && !bad++)
            first = "round " + std::to_string(r) + ": " + d;
    }
    c.next_season();
    c.enter_hub();
    g.frames(2.5);
    if (std::string d = grid_difference(c); !d.empty() && !bad++)
        first = "the new season: " + d;
    size_t asked = static_cast<size_t>(g.seats_refused);
    std::printf("  (%zu seat(s) asked and refused; %zu sacking(s) or swap(s) undone, %zu stand-in(s) called off)\n", asked,
                ::fake::Game::logged("the game kept its driver", from), ::fake::Game::logged("the stand-in is called off", from));
    check(asked > 0 && g.seats_ok == 0, "the market asked for seats and the game refused them all: " + std::to_string(asked));
    check(bad == 0, "the market's grid stayed the game's all along" + (bad ? " (" + first + ")" : std::string()));
    check(::fake::Game::logged("seat(s) written;", from) == 1 &&
              ::fake::Game::last_line("seat(s) written;").find(", 0 taken back") == std::string::npos &&
              ::fake::Game::last_line("seat(s) written;").find(", 0 waiting") != std::string::npos,
          "the new season's refusals are taken back: " + ::fake::Game::last_line("seat(s) written;"));
    c.game_save();
    check(save_difference(g).empty(), "and the market is saved " + save_difference(g));
    keep_save(g, ("refused_seats_" + std::to_string(career)).c_str());
    c.unload();
    g.pick("intensity", "realista");
    end();
}

// A seat is written to the game by its index in the team (0 or 1), so the market must hold each driver on the index
// the game holds him on. Here the game orders every team the other way round from the baseline, in a lively market:
// each change the market writes must take out the driver it announced, never his team-mate.
void seat_order(::fake::Game &g, const json::Value &baseline)
{
    begin("the game orders each team's seats the other way round: the market writes to the game's seat");
    g.pick("intensity", "caos");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{order}", "fernando_alonso");
    c.flipped = true;
    c.build_rows();
    size_t from = ::fake::log_lines.size();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    check(grid_difference(c).empty(), "first Team HQ: every driver is on the game's seat index " + grid_difference(c));
    int bad = 0;
    std::string first;
    for (int season = 0; season < 2; ++season) {
        for (int r = 1; r <= 24; ++r) {
            c.weekend();
            if (std::string d = grid_difference(c); !d.empty() && !bad++)
                first = "season " + std::to_string(season + 1) + ", round " + std::to_string(r) + ": " + d;
        }
        c.next_season();
        c.enter_hub();
        g.frames(2.5);
        if (std::string d = grid_difference(c); !d.empty() && !bad++)
            first = "the turn after season " + std::to_string(season + 1) + ": " + d;
    }
    std::printf("  (%d seat(s) written to the game, %d refused; %zu mid-season)\n", g.seats_ok, g.seats_refused,
                ::fake::Game::logged("] mid-season: ", from));
    check(g.seats_ok >= 4, "the market wrote seats to the game in two seasons: " + std::to_string(g.seats_ok));
    check(bad == 0, "and after every one the game's grid is the market's" + (bad ? " (" + first + ")" : std::string()));
    c.game_save();
    keep_save(g, "lively_market_two_seasons");
    c.unload();
    g.pick("intensity", "realista");
    end();
}

// The addon's own "timing" lines, written only when something is slow: a slow disk shows them, in the form
// tools/swperf.py reads ("[silly-season] timing: save 41 ms (to_json 3, storage 36)").
void timing_lines(::fake::Game &g, const json::Value &baseline)
{
    begin("slow work is told in \"timing\" lines tools/swperf.py can read");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{slow}", "alex_albon");
    c.load();
    size_t from = ::fake::log_lines.size();
    g.write_delay_ms = 35;
    c.enter_hub();
    g.write_delay_ms = 0;
    // the pattern tools/swperf.py uses, and its parts ("name ms", comma separated)
    auto parse = [](const std::string &line, std::string &what, int &total, std::map<std::string, int> &parts) {
        size_t at = line.find("[silly-season] timing: ");
        if (at == std::string::npos)
            return false;
        std::string rest = line.substr(at + 23);
        size_t ms = rest.find(" ms ("), close = rest.rfind(')');
        if (ms == std::string::npos || close == std::string::npos || close != rest.size() - 1)
            return false;
        size_t sp = rest.rfind(' ', ms - 1);
        if (sp == std::string::npos)
            return false;
        what = rest.substr(0, sp);
        total = std::atoi(rest.substr(sp + 1, ms - sp - 1).c_str());
        std::string list = rest.substr(ms + 5, close - ms - 5);
        for (size_t p = 0; p < list.size();) {
            size_t comma = list.find(", ", p);
            std::string part = list.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
            size_t s2 = part.rfind(' ');
            if (s2 == std::string::npos)
                return part == "-";
            parts[part.substr(0, s2)] = std::atoi(part.substr(s2 + 1).c_str());
            if (comma == std::string::npos)
                break;
            p = comma + 2;
        }
        return true;
    };
    std::string hq, save;
    for (size_t i = from; i < ::fake::log_lines.size(); ++i) {
        if (::fake::log_lines[i].find("timing: team hq") != std::string::npos)
            hq = ::fake::log_lines[i];
        if (::fake::log_lines[i].find("timing: save") != std::string::npos)
            save = ::fake::log_lines[i];
    }
    std::string what;
    int total = 0;
    std::map<std::string, int> parts;
    check(parse(save, what, total, parts) && what == "save" && total >= 35 && parts["storage"] >= 35,
          "a slow write shows as the save's storage part: " + save);
    parts.clear();
    check(parse(hq, what, total, parts) && what == "team hq" && total >= 35 && parts["save"] >= 35,
          "and as the Team HQ visit's save part: " + hq);
    from = ::fake::log_lines.size();
    c.game_save();
    c.leave_hub();
    c.enter_hub();
    check(::fake::Game::logged("timing:", from) == 0, "nothing is told when nothing is slow");
    c.unload();
    end();
}

// How long the addon's own work takes on the game's thread, handler by handler, over a season of a career. The fake
// game answers at once, so this is the addon's share alone (the game's memory reads and its text refresh are not in it).
void bench(::fake::Game &g, const json::Value &baseline)
{
    std::printf("\nthe addon's own time per handler (a 2025 Driver Career, 2 seasons, on this PC)\n");
    struct Stat {
        std::vector<double> ms;
    };
    std::map<std::string, Stat> stats;
    std::vector<std::string> order;
    auto time = [&](const char *what, auto &&fn) {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        if (!stats.count(what))
            order.push_back(what);
        stats[what].ms.push_back(ms_since(t0));
    };
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:new7", "lando_norris");
    time("career.created + loaded", [&] { c.create(); });
    for (int season = 0; season < 2; ++season) {
        for (int r = 1; r <= 24; ++r) {
            time(r == 1 ? (season ? "career.hub (season's turn)" : "career.hub (first visit)") : "career.hub (after a race)",
                 [&] { c.enter_hub(); });
            for (int f = 0; f < 150; ++f)
                time("frame at Team HQ", [&] { g.frames(1.0 / 60.0); });
            time("career.saving (after Team HQ)", [&] { c.game_save(); });
            time("career.saving (nothing changed)", [&] { c.game_save(); });
            c.leave_hub();
            time("session.start", [&] { c.session_start(); });
            c.lights_out();
            for (int i = 0; i < 300; ++i)
                time("telemetry event", [&] { c.overtake(static_cast<uint32_t>(i % 20), static_cast<uint32_t>((i * 7 + 3) % 20), float(i) * 2); });
            for (int f = 0; f < 60; ++f)
                time("frame in a session", [&] { g.frames(1.0 / 60.0); });
            time("session.end", [&] { c.session_end(); });
            c.finish_race();
        }
        c.next_season();
    }
    time("career.unloaded", [&] { c.unload(); });
    time("career.loaded (saved market)", [&] { c.load(); });
    c.unload();
    std::printf("  %-34s %7s %9s %9s %9s\n", "handler", "calls", "mean ms", "p95 ms", "max ms");
    for (const std::string &k : order) {
        std::vector<double> v = stats[k].ms;
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v)
            sum += x;
        std::printf("  %-34s %7zu %9.3f %9.3f %9.3f\n", k.c_str(), v.size(), sum / double(v.size()),
                    v[std::min(v.size() - 1, static_cast<size_t>(0.95 * double(v.size() - 1) + 0.5))], v.back());
    }
}

// the human's row in the fake career: its offset, or SIZE_MAX
size_t human_row()
{
    for (size_t off = 0; off + 0x88 <= ::fake::rows.size(); off += 0x88) {
        uint32_t ordinal = 0;
        std::memcpy(&ordinal, ::fake::rows.data() + off + 0x48, 4);
        if (ordinal & 0x10000000u)
            return off;
    }
    return SIZE_MAX;
}

// F2 Revamp 0.8.1 publishes no f2_revamp.career service: 1.1.2 waited for it forever, so no market ever started in
// F1 after the F2 season (two players' reports, 11 Nov). The player's team (F2 teams 489-499) now says it.
void f2_revamp_without_service(::fake::Game &g, const json::Value &baseline)
{
    begin("an F2 Revamp career without its service: the market waits through F2, then starts in F1");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{f2rev}", "arvid_lindblad");
    c.group = "f1_career_f2_26"; // F2 Revamp's data group (the rows are the 2026 grid's)
    size_t hr = human_row();
    uint32_t f1_team = 0;
    std::memcpy(&f1_team, ::fake::rows.data() + hr + 0x58, 4);
    ::fake::put32(::fake::rows, hr + 0x58, 491u << 16); // the player in an F2 team
    size_t from = ::fake::log_lines.size();
    c.load();
    check(!g_loaded && g_f2_waiting, "in F2 the market waits, unloaded");
    check(::fake::Game::logged("the player is in F2 (his team says so: F2 Revamp publishes no service)", from) == 1,
          "and says why: " + ::fake::Game::last_line("F2 Revamp career"));
    c.enter_hub();
    g.frames(2.5);
    check(!g_loaded, "a Team HQ visit still in F2 changes nothing");
    c.leave_hub();
    ::fake::put32(::fake::rows, hr + 0x58, f1_team); // the F2 season is over: F2 Revamp moves him to his F1 team
    c.enter_hub();
    g.frames(2.5);
    check(g_loaded && !g_f2_waiting, "the first Team HQ in F1 starts the market");
    check(::fake::Game::logged("the player is in F1 now; the market starts", from) == 1, "and the log says so");
    check(g_state.group == "f1_career_f2_26" && g_state.year == 2027, "on F2 Revamp's group, in 2027: " + std::to_string(g_state.year));
    check(grid_difference(c).empty(), "with the game's grid " + grid_difference(c));
    c.unload();
    end();
}

// A created driver whose career state carries a stock driver's stat id (Felipe Massa's) became that driver for good
// in 1.1.2 when the first Team HQ couldn't read the career's rows (players' reports: "player felipe_massa"). Now a
// stock driver is the player only when the market already has him in the player's team; a save stuck that way heals.
void borrowed_identity(::fake::Game &g, const json::Value &baseline)
{
    begin("a created driver never takes a stock driver's identity, and a save that did is mended");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{created}", "lance_stroll");
    size_t hr = human_row();
    ::fake::put32(::fake::rows, hr + 0x50, 3u << 16);        // his row: Alonso's stat (3), as the reports show
    ::fake::career_state.player_driver = 4u << 16;            // his career state: Massa's (4)
    c.load();
    ::fake::entity[0x2f8] = 1; // the first visit can't read the rows (the game is busy with them)
    c.enter_hub();
    g.frames(2.5);
    check(g_state.player == "player", "the career state's Massa isn't taken for the player: " + g_state.player);
    c.leave_hub();
    ::fake::entity[0x2f8] = 0;
    c.enter_hub();
    g.frames(2.5);
    const ss::Team *t = g_state.team("forceindia_26");
    check(g_state.player == "player" && t && t->seats[1] == "fernando_alonso",
          "with the rows: the player is his own driver and Alonso races beside him: " + g_state.player + ", " +
              (t ? t->seats[0] + " / " + t->seats[1] : std::string("?")));
    // a save of 1.1.2 that made him Massa: the next visit gives him back his own identity
    {
        std::vector<ss::GridCar> cars;
        for (const ss::Team &x : g_state.teams)
            for (int k = 0; k < 2; ++k)
                if (!x.seats[static_cast<size_t>(k)].empty())
                    cars.push_back({x.seats[static_cast<size_t>(k)] == "player" ? "felipe_massa" : x.seats[static_cast<size_t>(k)],
                                    x.id, x.seats[static_cast<size_t>(k)] == "player", k});
        ss::sync_grid(g_state, cars);
    }
    check(g_state.player == "felipe_massa", "(set up: the market took him for Massa)");
    size_t from = ::fake::log_lines.size();
    c.leave_hub();
    c.enter_hub();
    g.frames(2.5);
    const ss::Driver *massa = g_state.driver("felipe_massa");
    check(g_state.player == "player" && massa && massa->team.empty(), "the next Team HQ mends it: " + g_state.player);
    check(::fake::Game::logged("the player went by felipe_massa, another driver's identity", from) == 1, "and says so");
    int you = 0;
    for (const UiTeam &u : g_ov.teams)
        for (const UiSeat &s : u.seat)
            you += s.player;
    check(you == 1, "the hub shows the player in one seat: " + std::to_string(you));
    c.unload();
    ::fake::career_state.player_driver = 0;
    end();
}

// A new market whose first Team HQ finds a race in the standings while the calendar hasn't run one (a player's
// screenshot: "after 1 races" before his first race): those results aren't taken
void results_before_the_first_race(::fake::Game &g, const json::Value &baseline)
{
    begin("a new market leaves out standings the calendar hasn't run yet");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{start}", "oscar_piastri");
    std::vector<uint32_t> order;
    for (const Career::Seat &s : c.seats())
        order.push_back(s.driver);
    ::fake::add_round(order); // another championship's race in the table the market reads
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    int points = 0;
    for (const auto &[id, d] : g_state.drivers)
        points += d.points;
    check(points == 0, "no driver has points before the first race: " + std::to_string(points));
    check(::fake::Game::logged("the calendar hasn't run a race yet; left out", from) == 1, "and the log says so");
    check(std::any_of(g_ov.drivers.begin(), g_ov.drivers.end(), [](const UiDriver &u) { return u.player; }),
          "the player is on the Drivers list, marked as him");
    // his first real race counts as usual
    c.weekend();
    points = 0;
    for (const auto &[id, d] : g_state.drivers)
        points += d.points;
    // 1.1.3: the other championship's row stays out of the count: the first race is round 1 (it was taken as round 2,
    // the left-out row counted as read)
    check(points == 101 && g_state.results_read == 1 && g_state.round == 1,
          "the first race run counts, as round 1: " + std::to_string(points) + " points, " + std::to_string(g_state.results_read) +
              " round(s) read");
    c.unload();
    end();
}

// A Progressive career (Progressive Career on the Swofty Seasons, read from the addon installed on this PC; skipped
// without it). The game's rows hold each driver at +0x50 and his look at +0x54, each with its own stat id; the season
// addons' own drivers (sw16_rosberg) carry a stat id the launcher gives them, which years.json can't know. 1.1.0-1.1.2
// matched the drivers against the looks, never recognised the year and the market waited for good (players' reports,
// 12 Nov: no rumours from round 3 on, the panel in 2026).
void progressive_career(::fake::Game &g, const json::Value &baseline)
{
    begin("a Progressive career in 2015: the year is told from the looks, and each driver by his");
    if (!load_real_data()) {
        std::printf("  (Progressive Career on the Swofty Seasons isn't installed: skipped)\n");
        end();
        return;
    }
    const int year = 2015, rounds = 19;
    Career c(g, baseline, "f1_career_2026", "f1_career_sw_progressive.driver_career:{prog}", "lewis_hamilton");
    c.group = kRealGroup;
    // the rows as Progressive leaves them: the year's line-ups in their looks, the rest of its pool as free agents
    ::fake::rows.clear();
    std::map<std::string, uint32_t> stat_of; // the driver's stat id in the game
    uint32_t next_stat = 300;                // the launcher's for a season addon's driver: numbers the looks also use
    for (const auto &[stat, id] : g_real_stats) // a stock driver keeps his own (Hamilton: 7)
        stat_of[id] = stat;
    auto stat_for = [&](const std::string &id) {
        auto it = stat_of.find(id);
        return it != stat_of.end() ? it->second : (stat_of[id] = next_stat++);
    };
    std::set<std::string> seated;
    uint32_t human_team = 0;
    size_t cars = 0;
    for (const json::Value &t : *g_real["seasons"][std::to_string(year)]["teams"].if_array()) {
        auto slot = g_real_slots.find(static_cast<int>(t["slot"].as_int()));
        if (slot == g_real_slots.end() || !g.team_stats.count(slot->second))
            continue;
        uint32_t team = g.team_stats[slot->second];
        for (const json::Value &d : *t["drivers"].if_array()) {
            std::string id(d["driver"].as_string());
            bool human = id == "lewis_hamilton";
            human_team = human ? team : human_team;
            ::fake::add_row(stat_for(id), team, static_cast<int>(d["position"].as_int()), human, d["look"]["stat_id"].as_int());
            seated.insert(id);
            ++cars;
        }
    }
    for (const json::Value &p : *g_real["pool"].if_array()) {
        std::string id(p["driver"].as_string());
        if (!seated.count(id))
            ::fake::add_row(stat_for(id), -1, -1, false, p["default_look"]["stat_id"].as_int(-1));
    }
    ::fake::publish();
    ::fake::career_state.player_driver = stat_for("lewis_hamilton") << 16;
    ::fake::career_state.player_team = human_team << 16;
    g.rounds = rounds;
    size_t from = ::fake::log_lines.size();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("the grid's looks match " + std::to_string(year) + " best (" + std::to_string(cars) + " of " +
                                   std::to_string(cars) + " cars",
                               from) == 1,
          "the looks tell the year: " + ::fake::Game::last_line("the grid's looks match"));
    check(g_loaded && g_state.real && g_state.year == year && g_state.season_index == 0,
          "the market takes the real season " + std::to_string(year) + ": " + std::to_string(g_state.year));
    check(::fake::Game::logged("the real year can't be told", from) == 0, "and doesn't wait for another Team HQ");
    check(!g_ov.waiting_year, "the panel isn't left on \"Reading the real season\"");
    const ss::Team *m = g_state.team("mercedes_26");
    check(m && m->seats[0] == "lewis_hamilton" && m->seats[1] == "sw16_rosberg",
          "Mercedes: Hamilton and Rosberg: " + (m ? m->seats[0] + " / " + m->seats[1] : std::string("?")));
    std::string wrong;
    for (const Career::Seat &s : c.seats()) {
        const ss::Team *t = g_state.team(c.team_of_stat(s.team));
        std::string who = t ? t->seats[static_cast<size_t>(s.seat)] : "";
        if (!s.human && known_driver(s.driver) != who)
            wrong += " " + std::to_string(s.driver) + "=" + known_driver(s.driver) + "/" + who;
    }
    check(wrong.empty(), "every car's driver is the market's driver in that seat" + wrong);
    auto strays = [] {
        std::string out;
        for (const auto &[id, d] : g_state.drivers)
            if (id.rfind("stat_", 0) == 0)
                out += " " + id;
        return out;
    };
    check(strays().empty(), "no driver is added by his stat id:" + strays());
    check(g_state.game_seatable.count("sw16_rosberg") && g_state.game_seatable.count("sw15_maldonado"),
          "the season addons' drivers can take a seat (career rows)");
    // 15 Nov: the player stays in the team the game's rows give him (the other cars: the check above)
    {
        std::string mine;
        for (const Career::Seat &s : c.seats())
            if (s.human)
                mine = c.team_of_stat(s.team);
        check(!mine.empty() && g_state.player_team == mine, "the first historical Team HQ keeps the player in his team: " +
                                                                 g_state.player_team + " / " + mine);
    }
    // a race: every classified car's points go to the market's drivers
    c.weekend(false);
    int points = 0;
    for (const auto &[id, d] : g_state.drivers)
        points += d.points;
    check(points == 101 && strays().empty(), "the race's 101 points all go to the market's drivers: " + std::to_string(points));
    check(g_state.round == 1 && g_state.year == year, "the market moves on to round 1 of " + std::to_string(year));
    c.unload();
    end();
}

// ---------------------------------------------------------------------------------------------------
// 1.1.3, the fixes of 13 Nov: each case reproduced in the fake game first

int player_starts()
{
    const ss::Driver *me = g_state.driver(g_state.player);
    return me ? me->starts : -1;
}

void visit(Career &c, ::fake::Game &g)
{
    c.leave_hub();
    c.enter_hub();
    g.frames(2.5);
}

// The game's standings, empty for one Team HQ visit after two races (the calendar at round 3): the market was reset
// and the player's starts went to 0, and the table back whole didn't bring them back.
void shorter_standings(::fake::Game &g, const json::Value &baseline)
{
    begin("standings shorter for a moment keep the market; another career's start a new one");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{short}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    c.weekend(false);
    c.weekend(false);
    check(g_state.results_read == 2 && player_starts() == 2, "(set up: two races taken, the player has 2 starts)");
    auto whole = ::fake::sessions;
    ::fake::sessions.clear();
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    visit(c, g);
    check(::fake::Game::logged("a new market starts", from) == 0, "an empty table for a visit doesn't start a new market");
    check(::fake::Game::logged("shorter than the market's 2 round(s) for now; the market is kept", from) == 1,
          "and the log says it kept it: " + ::fake::Game::last_line("the standings"));
    check(player_starts() == 2 && g_state.results_read == 2, "the player still has 2 starts: " + std::to_string(player_starts()));
    ::fake::sessions = whole;
    ::fake::publish_standings();
    visit(c, g);
    check(player_starts() == 2 && g_state.results_read == 2, "the table whole again: nothing is taken twice");
    // 15 Nov: round 1 now reads with its winner and runner-up the other way round (a penalty after the flag): a
    // correction of that race, not another career (two careers under one id: another_career_same_id)
    uint32_t a = 0, b = 0;
    std::memcpy(&a, ::fake::records[0].data(), 4);
    std::memcpy(&b, ::fake::records[0].data() + 0x10, 4);
    std::memcpy(::fake::records[0].data(), &b, 4);
    std::memcpy(::fake::records[0].data() + 0x10, &a, 4);
    from = ::fake::log_lines.size();
    visit(c, g);
    check(::fake::Game::logged("round 1 in the standings isn't the round 1 the market took", from) == 1 &&
              ::fake::Game::logged("a new market starts", from) == 0 && player_starts() == 2 && g_state.results_read == 2,
          "a corrected race keeps the market and what it counted");
    check(::fake::Game::logged("a correction", from) >= 1, "and the log calls it a correction: " + ::fake::Game::last_line("a correction"));
    c.unload();
    end();
}

// Standings of another championship before the first race, then the game replaces the whole table with the first
// race's: its points are round 1's
void first_race_replaces_the_table(::fake::Game &g, const json::Value &baseline)
{
    begin("another championship's standings replaced by the first race: its points are round 1's");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{replace}", "oscar_piastri");
    std::vector<uint32_t> order;
    for (const Career::Seat &s : c.seats())
        order.push_back(s.driver);
    ::fake::add_round(order);
    ::fake::publish_standings();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 0 && g_state.results_read == 0 && g_state.foreign_keys.size() == 1,
          "(set up: the other table is left out, and the round doesn't move)");
    ::fake::sessions.clear();
    ::fake::records.clear();
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    c.weekend(false);
    int points = 0;
    for (const auto &[id, d] : g_state.drivers)
        points += d.points;
    check(points == 101 && g_state.results_read == 1 && g_state.round == 1,
          "the first race's 101 points, as round 1: " + std::to_string(points) + " points, " +
              std::to_string(g_state.results_read) + " round(s) read");
    check(::fake::Game::logged("no longer start with the 1 round(s) of another championship", from) == 1 &&
              g_state.foreign_keys.empty(),
          "and the other table is forgotten");
    c.unload();
    end();
}

// Progressive's rows for a year: its line-ups in their looks, the rest of its pool as free agents. The cars on the grid.
size_t progressive_rows(::fake::Game &g, int year)
{
    ::fake::rows.clear();
    std::map<std::string, uint32_t> stat_of;
    uint32_t next_stat = 300;
    for (const auto &[stat, id] : g_real_stats)
        stat_of[id] = stat;
    auto stat_for = [&](const std::string &id) {
        auto it = stat_of.find(id);
        return it != stat_of.end() ? it->second : (stat_of[id] = next_stat++);
    };
    std::set<std::string> seated;
    uint32_t human_team = 0;
    size_t cars = 0;
    for (const json::Value &t : *g_real["seasons"][std::to_string(year)]["teams"].if_array()) {
        auto slot = g_real_slots.find(static_cast<int>(t["slot"].as_int()));
        if (slot == g_real_slots.end() || !g.team_stats.count(slot->second))
            continue;
        uint32_t team = g.team_stats[slot->second];
        for (const json::Value &d : *t["drivers"].if_array()) {
            std::string id(d["driver"].as_string());
            bool human = id == "lewis_hamilton";
            human_team = human ? team : human_team;
            ::fake::add_row(stat_for(id), team, static_cast<int>(d["position"].as_int()), human, d["look"]["stat_id"].as_int());
            seated.insert(id);
            ++cars;
        }
    }
    for (const json::Value &p : *g_real["pool"].if_array()) {
        std::string id(p["driver"].as_string());
        if (!seated.count(id))
            ::fake::add_row(stat_for(id), -1, -1, false, p["default_look"]["stat_id"].as_int(-1));
    }
    ::fake::publish();
    ::fake::career_state.player_driver = stat_for("lewis_hamilton") << 16;
    ::fake::career_state.player_team = human_team << 16;
    return cars;
}

// A Progressive career of 2015 closed, another loaded while the game still fills its rows: its first Team HQ took the
// year from the first career's rows (left in memory) and started in 2015
void progressive_switch(::fake::Game &g, const json::Value &baseline)
{
    begin("Progressive: a career loaded after another, its rows not ready, waits for its own year");
    if (!load_real_data()) {
        std::printf("  (Progressive Career on the Swofty Seasons isn't installed: skipped)\n");
        end();
        return;
    }
    Career a(g, baseline, "f1_career_2026", "f1_career_sw_progressive.driver_career:{prog-a}", "lewis_hamilton");
    a.group = kRealGroup;
    progressive_rows(g, 2015);
    g.rounds = 19;
    a.load();
    a.enter_hub();
    g.frames(2.5);
    check(g_state.real && g_state.year == 2015, "(set up: the first career is in 2015)");
    a.unload();
    Career b(g, baseline, "f1_career_2026", "f1_career_sw_progressive.driver_career:{prog-b}", "lewis_hamilton");
    b.group = kRealGroup;
    size_t cars = progressive_rows(g, 2017);
    g.rounds = 20;
    ::fake::entity[0x2f8] = 1; // the game is still filling the rows
    size_t from = ::fake::log_lines.size();
    b.load();
    b.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("this career's rows weren't read in this visit; the real year waits", from) == 1,
          "the first read fails: the year waits for this career's rows");
    check(::fake::Game::logged("the grid's looks match", from) == 0 && g_state.season_index < 0,
          "the first career's rows tell nothing: " + ::fake::Game::last_line("the grid's looks match"));
    check(g_ov.waiting_year, "the panel says it is reading the real season");
    ::fake::entity[0x2f8] = 0;
    from = ::fake::log_lines.size();
    visit(b, g);
    check(::fake::Game::logged("the grid's looks match 2017 best (" + std::to_string(cars) + " of " + std::to_string(cars), from) == 1 &&
              g_state.real && g_state.year == 2017,
          "the next Team HQ reads 2017 from its own rows: " + std::to_string(g_state.year));
    check(!g_ov.waiting_year, "and the panel opens the market");
    b.unload();
    end();
}

// a career played to the winter's Team HQ (not entered yet)
void to_winter(Career &c, ::fake::Game &g)
{
    c.load();
    c.enter_hub();
    g.frames(2.5);
    for (int r = 1; r <= 24; ++r)
        c.weekend(false);
    c.next_season();
}

// New season's seats: written, then confirmed by a read of the rows; never taken from the market's own plan
void seats_confirmed(::fake::Game &g, const json::Value &baseline)
{
    g.pick("intensity", "caos");
    {
        begin("new season: a seat the game says ok to but doesn't apply is taken back");
        Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{ignored}", "charles_leclerc");
        c.rng.seed(21);
        to_winter(c, g);
        g.seats_ignored = true;
        size_t from = ::fake::log_lines.size();
        c.enter_hub();
        g.frames(2.5);
        // 15 Nov: the game can be slow to show a seat: the first read waits, the next Team HQ takes them back
        std::string line = ::fake::Game::last_line("seat(s) written;");
        check(::fake::Game::logged("seat(s) written;", from) == 1 && line.find(", 0 taken back") != std::string::npos &&
                  line.find(", 0 waiting for the game to show them") == std::string::npos,
              "the writes the game didn't show yet wait one Team HQ: " + line);
        check(g_ov.open_seats > 0, "and the panel says the grid is pending confirmation, not confirmed");
        from = ::fake::log_lines.size();
        visit(c, g);
        line = ::fake::Game::last_line("seat(s) written;");
        check(line.find(", 0 taken back") == std::string::npos && line.find("(0 the game took but never showed)") == std::string::npos,
              "at the next Team HQ they are taken back: " + line);
        check(::fake::Game::logged("his deal goes back to", from) >= 1, "and each driver's old deal comes back: " +
                                                                            ::fake::Game::last_line("his deal goes back"));
        check(grid_difference(c).empty(), "the market's grid is the game's " + grid_difference(c));
        check(g_state.seat_checks.empty(), "no seat is left open");
        g.seats_ignored = false;
        c.unload();
        end();
    }
    {
        begin("new season: the rows can't be read after the writes: the seats wait, the next Team HQ settles them");
        Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{reread}", "charles_leclerc");
        c.rng.seed(22);
        to_winter(c, g);
        g.busy_after_writes = 1;
        size_t from = ::fake::log_lines.size();
        c.enter_hub();
        g.frames(2.5);
        std::string line = ::fake::Game::last_line("seat(s) written;");
        check(line.find(", 0 waiting for the next read") == std::string::npos && line.find(", 0 taken back") != std::string::npos,
              "nothing is taken as done or taken back without a read: " + line);
        check(!g_state.seat_checks.empty(), "the seats stay open");
        ::fake::entity[0x2f8] = 0;
        g.busy_after_writes = 0;
        from = ::fake::log_lines.size();
        visit(c, g);
        check(::fake::Game::logged("new season (checked again)", from) == 1, "the next Team HQ checks them again: " +
                                                                               ::fake::Game::last_line("checked again"));
        check(g_state.seat_checks.empty() && grid_difference(c).empty(), "settled, and the market's grid is the game's " +
                                                                             grid_difference(c));
        c.unload();
        end();
    }
    {
        begin("new season: the rows can't be read at the winter's Team HQ: nothing written, the next one writes");
        Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{skipped}", "charles_leclerc");
        c.rng.seed(23);
        to_winter(c, g);
        ::fake::entity[0x2f8] = 1;
        int ok_before = g.seats_ok;
        size_t from = ::fake::log_lines.size();
        c.enter_hub();
        g.frames(2.5);
        check(::fake::Game::logged("no seat is written yet", from) == 1 && g.seats_ok == ok_before, "no seat is written without a read");
        check(!g_state.seat_checks.empty(), "the planned seats wait, announced");
        ::fake::entity[0x2f8] = 0;
        visit(c, g);
        check(g.seats_ok > ok_before && g_state.seat_checks.empty() && grid_difference(c).empty(),
              "the next Team HQ writes and confirms them " + grid_difference(c));
        c.unload();
        end();
    }
    {
        begin("new season: some seats refused, the rest confirmed");
        Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{partial}", "charles_leclerc");
        c.rng.seed(24);
        to_winter(c, g);
        for (const auto &[id, d] : g_state.drivers)
            if (d.stat > 0 && d.stat % 3 == 0)
                g.refuse.insert(static_cast<uint32_t>(d.stat));
        size_t from = ::fake::log_lines.size();
        c.enter_hub();
        g.frames(2.5);
        std::string line = ::fake::Game::last_line("seat(s) written;");
        check(::fake::Game::logged("seat(s) written;", from) == 1 && line.find("; 0 confirmed") == std::string::npos,
              "the seats the game took are confirmed: " + line);
        check(grid_difference(c).empty() && g_state.seat_checks.empty(), "and the market's grid is the game's " + grid_difference(c));
        g.refuse.clear();
        c.unload();
        end();
    }
    g.pick("intensity", "realista");
}

// A collision at 300 s, a flashback to 25 s and nothing else in the race: the collision stayed (Verstappen's
// incident). And the give-back the flashback undid takes nothing from the overtake before it.
void flashback_without_events(::fake::Game &g, const json::Value &baseline)
{
    begin("on track: a flashback with nothing after it undoes what came after its moment");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{flash}", "lando_norris");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    uint32_t ver = c.car_of("max_verstappen"), alo = c.car_of("fernando_alonso"), ham = c.car_of("lewis_hamilton"),
             rus = c.car_of("george_russell");
    size_t from = ::fake::log_lines.size();
    c.session_start();
    c.lights_out();
    c.overtake(ham, rus, 22);
    c.overtake(rus, ham, 30); // given back
    c.collision(ver, alo, 300);
    c.clock(25); // the flashback, and the race runs on without another event
    g.frames(1.5);
    c.session_end();
    c.finish_race();
    c.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("the session clock went back from 300 s to 25 s (a flashback): 2 event(s)", from) == 1,
          "the clock going back is seen without another event: " + ::fake::Game::last_line("the session clock went back"));
    const ss::Driver *v = g_state.driver("max_verstappen"), *a = g_state.driver("fernando_alonso"), *h = g_state.driver("lewis_hamilton");
    check(v && a && v->s_incidents == 0 && a->s_incidents == 0, "no incident for Verstappen or Alonso");
    check(h && h->s_overtakes == 1, "Hamilton's overtake stands: its give-back never happened");
    c.unload();
    end();
}

// A breakdown classified at the back (P20 in the standings) took the form from 0.00 to -0.25
void breakdown_keeps_form(::fake::Game &g, const json::Value &baseline)
{
    begin("a breakdown classified at the back leaves the driver's form as it was");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{failure}", "lando_norris");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    const std::string who = "esteban_ocon";
    uint32_t car = c.car_of(who);
    ss::Driver *d = g_state.driver(who);
    float before = d ? d->form : 99;
    uint32_t stat = d ? static_cast<uint32_t>(d->stat) : 0;
    c.session_start();
    c.lights_out();
    c.retirement(car, 8, 100);
    c.session_end();
    std::vector<uint32_t> order;
    for (const Career::Seat &s : c.seats())
        if (s.driver != stat)
            order.push_back(s.driver);
    order.push_back(stat); // classified last
    ::fake::add_round(order);
    ::fake::publish_standings();
    ++g.round;
    c.enter_hub();
    g.frames(2.5);
    d = g_state.driver(who);
    check(d && d->failed_round == 1 && d->starts == 1 && d->race_pos.size() == 1 && d->race_pos[0] == static_cast<int>(order.size()),
          "his start counts, at the back");
    check(d && std::fabs(d->form - before) < 1e-4f,
          "his form stays: " + std::to_string(before) + " -> " + std::to_string(d ? d->form : 0));
    c.unload();
    end();
}

// Two careers in one game run: the overlays' count of shown reports used to carry over, and the second career's
// reports, numbered from 1 again, never showed
void overlays_across_careers(::fake::Game &g, const json::Value &baseline)
{
    begin("two careers in one game run: nothing of the first waits, and the second's report is new to the overlays");
    Career a(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{ov-a}", "oscar_piastri");
    a.load();
    a.enter_hub();
    g.frames(2.5);
    a.weekend(false);
    a.weekend(false);
    ovl::report_shown = g_ov.report.id; // the render thread showed it
    ovl::generation_seen = g_ov_generation.load();
    int shown = ovl::report_shown;
    a.unload();
    {
        std::lock_guard lock(g_ov_lock);
        check(g_ov.signings.empty() && g_ov.farewells.empty() && g_ov.strips.empty() && g_ov.alerts.empty() && g_ov.report.id == 0,
              "nothing of the first career waits to be shown");
    }
    Career b(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{ov-b}", "lando_norris");
    b.load();
    b.enter_hub();
    g.frames(2.5);
    b.weekend(false);
    // the render thread's next paint
    if (g_ov_generation.load() != ovl::generation_seen) {
        ovl::generation_seen = g_ov_generation.load();
        reset_paint_state();
    }
    check(shown > 0 && ovl::report_shown == 0 && g_ov.report.id > ovl::report_shown,
          "the second career's report " + std::to_string(g_ov.report.id) + " is new to the overlays");
    b.unload();
    end();
}

// ---------------------------------------------------------------------------------------------------
// 1.1.3, the audit of 15 Nov: each case reproduced in the fake game first

std::vector<uint32_t> race_order(Career &c)
{
    std::vector<uint32_t> order;
    for (const Career::Seat &s : c.seats())
        order.push_back(s.driver);
    std::shuffle(order.begin(), order.end(), c.rng);
    return order;
}

std::string counted_now() { return g_state.counted.count(g_state.season_index) ? g_state.counted[g_state.season_index] : ""; }

// a race run without visiting Team HQ after it: lights out, the session's end, the classification, the next round
void race_no_hub(Career &c, bool typed = false)
{
    c.session_start();
    if (typed)
        c.g.set_session(events::SessionType::race);
    c.lights_out();
    c.session_end();
    c.finish_race();
}

// Two careers under one id: the market of the first is loaded for the second, whose races are all different
void another_career_same_id(::fake::Game &g, const json::Value &baseline)
{
    begin("two careers under one id: every race reads differently, a new market starts and the old one is kept");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{twins}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    for (int i = 0; i < 3; ++i)
        c.weekend(false);
    check(g_state.results_read == 3 && player_starts() == 3, "(set up: three races taken)");
    c.unload();
    ::fake::sessions.clear();
    ::fake::records.clear();
    c.rng.seed(999);
    for (int r = 0; r < 3; ++r)
        ::fake::add_round(race_order(c));
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    c.load();
    c.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("another career's, by its results", from) == 1, "the market is another career's: " +
                                                                                    ::fake::Game::last_line("a new market starts"));
    bool kept = false;
    for (const auto &[path, text] : g.files)
        kept = kept || path.rfind("careers/backup/", 0) == 0;
    check(kept && ::fake::Game::logged("the market before the reset is kept in careers/backup/", from) == 1,
          "a copy of the old market is kept before the reset");
    check(g_state.results_read == 3 && player_starts() == 3, "the new market takes this career's own three races: " +
                                                                std::to_string(player_starts()));
    c.unload();
    end();
}

// Practice, qualifying and a sprint never count as the round's race; the race does, once
void sessions_before_the_race(::fake::Game &g, const json::Value &baseline)
{
    begin("a round counts once its race is over: practice, qualifying and a sprint don't");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{sessions}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    // qualifying: the standings get round 1's qualifying (as many cars as the race); the calendar stays at round 1
    c.session_start();
    c.session_end();
    ::fake::add_round(race_order(c));
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 0 && g_state.results_read == 0 && player_starts() == 0,
          "qualifying in the table, the calendar still at round 1: no round run, no start (" + std::to_string(player_starts()) + ")");
    check(::fake::Game::logged("round 1 not over yet", from) == 1, "the log says it waits: " + ::fake::Game::last_line("not over yet"));
    // a sprint ends (its session says so): still not the race
    c.session_start();
    g.set_session(events::SessionType::sprint);
    c.session_end();
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 0 && player_starts() == 0, "a sprint over: still no round run");
    // the race: its row replaces qualifying's, a race session ends; the calendar hasn't moved yet
    ::fake::sessions.pop_back();
    ::fake::records.pop_back();
    ::fake::add_round(race_order(c));
    ::fake::publish_standings();
    c.session_start();
    g.set_session(events::SessionType::race);
    c.lights_out();
    c.session_end();
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 1 && g_state.results_read == 1 && player_starts() == 1 && counted_now() == "t",
          "the race over (its session said so): round 1 counts, once: " + counted_now());
    ++g.round;
    visit(c, g);
    check(g_state.round == 1 && player_starts() == 1, "back at Team HQ with the calendar moved on: nothing counts twice");
    c.unload();
    end();
}

// A race the market simulated (the standings weren't there yet), whose results come at a later visit
void late_standings(::fake::Game &g, const json::Value &baseline)
{
    begin("a race simulated before its results came: the results add nothing when they come");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{late}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    c.session_start();
    c.lights_out();
    c.session_end();
    ++g.round; // the calendar moves on, the standings stay empty
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 1 && counted_now() == "s" && player_starts() == 1, "round 1 simulated: one start (" + counted_now() + ")");
    ::fake::add_round(race_order(c));
    ::fake::publish_standings();
    size_t from = ::fake::log_lines.size();
    visit(c, g);
    check(player_starts() == 1 && counted_now() == "k", "the late results add nothing: still one start (" + counted_now() + ")");
    check(::fake::Game::logged("came in the standings later", from) == 1, "and the log says why");
    c.unload();
    end();
}

// Three races before one Team HQ visit: each round once, from the standings, none simulated
void several_rounds_at_once(::fake::Game &g, const json::Value &baseline)
{
    begin("three races before one Team HQ visit: each round counted once, with its results");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{batch}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    for (int i = 0; i < 3; ++i)
        race_no_hub(c);
    c.enter_hub();
    g.frames(2.5);
    check(g_state.round == 3 && g_state.results_read == 3 && player_starts() == 3 && counted_now() == "ttt",
          "three rounds, three starts, all from the standings: " + counted_now());
    c.unload();
    end();
}

// The first Team HQ of a new season, with 0, 1 and 2 of its races already run, after a year with one race
void first_visit_of_a_season(::fake::Game &g, const json::Value &baseline)
{
    for (int n = 0; n <= 2; ++n) {
        std::string title = "the new season's first Team HQ with " + std::to_string(n) + " race(s) run: the two years apart";
        begin(title.c_str());
        Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{turn" + std::to_string(n) + "}", "oscar_piastri");
        c.load();
        c.enter_hub();
        g.frames(2.5);
        c.weekend(false); // the year's one race in the market
        ss::CareerTotals before = ss::career_totals(g_state, *g_state.driver(g_state.player));
        int year = g_state.year;
        check(before.starts == 1, "(set up: one start in the year that closes)");
        c.leave_hub();
        c.next_season();
        for (int i = 0; i < n; ++i)
            race_no_hub(c);
        c.enter_hub();
        g.frames(2.5);
        ss::CareerTotals after = ss::career_totals(g_state, *g_state.driver(g_state.player));
        check(g_state.year == year + 1 && g_state.results_read == n && player_starts() == n,
              "the new year has its " + std::to_string(n) + " race(s): " + std::to_string(player_starts()));
        check(after.starts == 1 + n, "the career's starts: the old year's one and the new year's " + std::to_string(n) + " (" +
                                         std::to_string(after.starts) + ")");
        check(std::any_of(g_state.awards.begin(), g_state.awards.end(), [&](const ss::State::Award &a) { return a.year == year; }),
              "the closed year has its awards");
        c.unload();
        end();
    }
}

// A winter whose grid read is rejected (more than two cars on teams the career doesn't know): nothing is written
void rejected_grid(::fake::Game &g, const json::Value &baseline)
{
    begin("a grid read rejected for unknown teams: no winter seat is written until a valid read");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{rejected}", "charles_leclerc");
    c.rng.seed(31);
    g.pick("intensity", "caos");
    to_winter(c, g);
    auto rows = ::fake::rows;
    int moved = 0;
    for (size_t off = 0; off + 0x88 <= ::fake::rows.size() && moved < 3; off += 0x88) {
        uint32_t t = 0, ordinal = 0;
        uint16_t pos = 0;
        std::memcpy(&t, ::fake::rows.data() + off + 0x58, 4);
        std::memcpy(&pos, ::fake::rows.data() + off + 0x80, 2);
        std::memcpy(&ordinal, ::fake::rows.data() + off + 0x48, 4);
        if ((t & 0xffff0000u) == 0x3fff0000u || !(pos >> 8) || (ordinal & 0x10000000u))
            continue; // a race seat of an AI driver (the human's team would be taken for his own)
        uint32_t unknown = (900u + static_cast<uint32_t>(moved)) << 16;
        std::memcpy(::fake::rows.data() + off + 0x58, &unknown, 4);
        ++moved;
    }
    int ok_before = g.seats_ok;
    size_t from = ::fake::log_lines.size();
    c.enter_hub();
    g.frames(2.5);
    check(g.seats_ok == ok_before && ::fake::Game::logged("no valid read of the game's grid", from) == 1,
          "no seat is written on a rejected grid: " + ::fake::Game::last_line("new season:"));
    check(!g_state.seat_checks.empty(), "the planned seats wait");
    ::fake::rows = rows;
    visit(c, g);
    check(g.seats_ok > ok_before && grid_difference(c).empty(), "a valid read writes them " + grid_difference(c));
    g.pick("intensity", "realista");
    c.unload();
    end();
}

// A new career's first save: the write under its real id fails, then works
void provisional_until_read_back(::fake::Game &g, const json::Value &baseline)
{
    begin("a new career's first save: the provisional copy stays until the real one reads back");
    std::string prov = "f1_career_2026.driver_career:new7", real = "f1_career_2026.driver_career:{first-save}";
    Career c(g, baseline, "f1_career_2026", prov, "oscar_piastri");
    c.create();
    c.enter_hub();
    g.frames(2.5);
    c.weekend(false);
    check(g.files.count(storage_path(prov)) == 1, "(set up: saved under the provisional id)");
    c.id = real;
    c.context();
    g.fail_writes.insert(storage_path(real));
    g.fail_data_set = true;
    c.game_save();
    auto listed = [&](const std::string &id) {
        auto it = g.files.find(kProvisionalList);
        return it != g.files.end() && it->second.find(id) != std::string::npos;
    };
    check(g.files.count(storage_path(prov)) == 1 && listed(prov), "both writes failed: the provisional copy and its entry stay");
    g.fail_writes.clear();
    g.fail_data_set = false;
    c.game_save();
    check(g.files.count(storage_path(real)) == 1 && g.files.count(storage_path(prov)) == 0 && !listed(prov),
          "the real copy reads back: the provisional one goes");
    c.unload();
    end();
}

// Two copies of one career with different revisions, and another career's copy under this career's path
void copies_on_load(::fake::Game &g, const json::Value &baseline)
{
    begin("loading takes the newest copy of this career, and never another career's");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{copies}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    c.weekend(false);
    g.fail_data_set = true; // the career data stays behind from here
    c.weekend(false);
    c.weekend(false);
    int starts = player_starts();
    c.unload();
    g.fail_data_set = false;
    size_t from = ::fake::log_lines.size();
    c.load();
    check(player_starts() == starts && ::fake::Game::logged("the newer (storage) is taken", from) == 1,
          "the storage's newer copy is taken: " + std::to_string(player_starts()) + " start(s)");
    c.unload();
    Career b(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{not-copies}", "oscar_piastri");
    g.files[storage_path(b.id)] = g.files[storage_path(c.id)];
    g.career_data.erase(b.id + "|" + kAddon);
    from = ::fake::log_lines.size();
    b.load();
    check(::fake::Game::logged("is career f1_career_2026.driver_career:{copies}'s; it is left alone", from) == 1 &&
              g_state.season_index < 0,
          "a copy that names another career is left alone: a new market");
    b.unload();
    end();
}

// A save written before 15 Nov: no revision, no counted rounds, the first key scheme
void older_save(::fake::Game &g, const json::Value &baseline)
{
    begin("a save from before 15 Nov loads, and its rounds aren't counted again");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{older}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    c.weekend(false);
    c.weekend(false);
    int starts = player_starts();
    // the save as an older build wrote it: no revision, no counted rounds, the first fingerprints
    json::Error err;
    json::Value v = json::parse(g.files[storage_path(c.id)], &err);
    std::vector<game_data::Round> rounds;
    std::string why;
    game_data::read_results(rounds, why);
    v.erase("revision");
    v.erase("counted");
    v.erase("key_scheme");
    json::Value keys = json::Value::array();
    for (const game_data::Round &r : rounds)
        keys.push_back(static_cast<int64_t>(round_key(r, 1)));
    v["result_keys"] = std::move(keys);
    c.unload();
    g.files[storage_path(c.id)] = json::dump(v);
    g.career_data.erase(c.id + "|" + kAddon);
    size_t from = ::fake::log_lines.size();
    c.load();
    check(g_state.revision == 0 && counted_now() == "tt", "it loads, its two rounds known as taken: " + counted_now());
    c.enter_hub();
    g.frames(2.5);
    check(player_starts() == starts && ::fake::Game::logged("a new market starts", from) == 0, "nothing counted twice, no reset");
    check(g_state.key_scheme == 2 && ::fake::Game::logged("fingerprints move to the whole classification", from) == 1,
          "its fingerprints move to the new scheme");
    c.unload();
    end();
}

// Two provisional markets of the same kind of career, either of which could be the loaded one
void two_provisionals(::fake::Game &g, const json::Value &baseline)
{
    begin("two provisional markets that could both be this career's: neither is taken");
    for (const char *id : {"f1_career_2026.driver_career:new8", "f1_career_2026.driver_career:new9"}) {
        Career p(g, baseline, "f1_career_2026", id, "oscar_piastri");
        p.create();
        p.enter_hub();
        g.frames(2.5);
        p.unload();
    }
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{which}", "oscar_piastri");
    size_t from = ::fake::log_lines.size();
    c.load();
    check(::fake::Game::logged("2 provisional markets could be this career's", from) == 1 && g_adopted_from.empty(),
          "neither is adopted: " + ::fake::Game::last_line("provisional markets"));
    c.unload();
    end();
}

// On track: a restart in a new session, two GPs before one Team HQ visit, a clean race, and a feed that comes late
void track_attempts(::fake::Game &g, const json::Value &baseline)
{
    begin("on track: a restart's new session, two GPs before one Team HQ, a clean race, a late feed");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{attempts}", "lando_norris");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    uint32_t ver = c.car_of("max_verstappen"), lec = c.car_of("charles_leclerc"), ham = c.car_of("lewis_hamilton"),
             rus = c.car_of("george_russell"), alo = c.car_of("fernando_alonso"), gas = c.car_of("pierre_gasly");
    auto passes = [](const char *id) {
        const ss::Driver *d = g_state.driver(id);
        return d ? static_cast<int>(d->s_overtakes) : -1;
    };
    size_t from = ::fake::log_lines.size();
    // GP 1: an overtake, then a restart that opens a new session
    c.session_start();
    c.lights_out();
    c.overtake(ver, lec, 10);
    g.fire("session.end", 1);
    c.session_end();
    c.session_start();
    c.lights_out();
    c.overtake(ham, rus, 20);
    c.session_end();
    c.finish_race();
    // GP 2, with no Team HQ in between: a collision
    c.session_start();
    c.lights_out();
    c.collision(alo, gas, 30);
    c.session_end();
    c.finish_race();
    c.enter_hub();
    g.frames(2.5);
    check(::fake::Game::logged("on track, round 1: 1 overtake(s), 0 collision(s)", from) == 1 &&
              ::fake::Game::logged("on track, round 2: 0 overtake(s), 1 collision(s)", from) == 1,
          "each GP to its own round: " + ::fake::Game::last_line("on track, round"));
    check(passes("max_verstappen") == 0 && passes("lewis_hamilton") == 1, "the restarted attempt counts nothing");
    // a clean race: data, and 0 incidents
    int rounds = g_state.track_rounds;
    c.session_start();
    c.lights_out();
    c.session_end();
    c.finish_race();
    visit(c, g);
    check(g_state.track_rounds == rounds + 1, "a clean race read with the telemetry is a race with data");
    // the feed isn't there at lights out, then it is
    g_feed = nullptr;
    g_feed_retry_seconds = 0;
    g.feed_late = true;
    c.session_start();
    c.lights_out();
    g.feed_late = false;
    c.overtake(ver, lec, 40);
    c.session_end();
    check(::fake::Game::last_line("on track: race over").find("1 kept (1 overtake(s)") != std::string::npos,
          "a feed that comes late is picked up: " + ::fake::Game::last_line("on track: race over"));
    g_feed_retry_seconds = 10;
    c.finish_race();
    c.enter_hub();
    c.unload();
    end();
}

// The banner shows four items at most: the ones left out are neither marked seen nor skipped
void banner_cursor()
{
    begin("breaking news: only the banners shown are marked seen; the rest come next time");
    ss::State st;
    for (int i = 1; i <= 6; ++i) {
        ss::NewsItem n;
        n.id = i;
        n.kind = "official";
        n.official = true;
        n.body = "x";
        st.news.push_back(n);
    }
    auto first = ss::take_breaking(st, 4);
    check(first.size() == 4 && st.news[3].shown && !st.news[4].shown && !st.news[5].shown, "four shown, two not marked seen");
    auto second = ss::take_breaking(st, 4);
    check(second.size() == 2 && second[0].id == 5, "the two left out come next time");
}

// 1.1.4: the overlays' data goes from the flow thread to the render thread in one hand-over: what waits to be shown is
// carried into the next data (oldest first, within each queue's cap), and another career's data drops it
void overlay_hand_over()
{
    begin("1.1.4: the overlays' data is handed over between threads without losing what waits");
    OverlayData saved = std::move(g_ov);
    g_ov = OverlayData{};
    g_ov.alerts.push_back({"Ferrari", "FER", 0xE8002D, 70});
    {
        // built twice before the render thread takes it: both builds' new items wait, the oldest first
        OverlayData a;
        a.alerts.push_back({"Mercedes", "MER", 0x27F4D2, 80});
        auto made = std::make_unique<OverlayData>(std::move(a));
        std::lock_guard lock(g_ov_lock);
        g_ov_next = std::move(made);
    }
    {
        OverlayData b;
        b.alerts.push_back({"McLaren", "MCL", 0xFF8000, 90});
        b.alerts.push_back({"Alpine", "ALP", 0x0093CC, 60});
        auto made = std::make_unique<OverlayData>(std::move(b));
        std::lock_guard lock(g_ov_lock);
        carry_queues(*made, *g_ov_next); // what publish_overlay does when the last one wasn't taken
        g_ov_next = std::move(made);
    }
    check(adopt_overlay(), "the render thread takes the newest data");
    check(g_ov.alerts.size() == 3 && g_ov.alerts[0].code == "MER" && g_ov.alerts[2].code == "ALP",
          "the queue keeps its cap of 3, the oldest dropped first: " + std::to_string(g_ov.alerts.size()));
    check(!adopt_overlay(), "nothing new: nothing taken");
    {
        OverlayData blank;
        blank.fresh = true;
        auto made = std::make_unique<OverlayData>(std::move(blank));
        std::lock_guard lock(g_ov_lock);
        g_ov_next = std::move(made);
    }
    adopt_overlay();
    check(g_ov.alerts.empty() && !g_ov.career, "another career: nothing of the last one waits");
    g_ov = std::move(saved);
}

// 1.1.4: the panel's sections: every page once, in order, Q/E reach them all and come back
void panel_sections()
{
    begin("1.1.4: the F7 panel's five sections hold every page once");
    std::set<int> seen;
    int covered = 0;
    for (int s = 0; s < kSectionCount; ++s) {
        covered += kSections[s].count;
        for (int j = 0; j < kSections[s].count; ++j) {
            int page = kOrder[kSections[s].first + j];
            seen.insert(page);
            check(section_of(page) == s, std::string("page ") + kPages[page].en + " is in its section");
        }
    }
    check(covered == kPageCount && static_cast<int>(seen.size()) == kPageCount, "the sections hold the 17 pages, each once");
    int at = P_PADDOCK, steps = 0;
    std::set<int> visited;
    do {
        visited.insert(at);
        at = kOrder[(order_of(at) + 1) % kPageCount];
        ++steps;
    } while (at != P_PADDOCK && steps < 100);
    check(steps == kPageCount && static_cast<int>(visited.size()) == kPageCount, "E steps through every page and back to the Paddock");
    check(section_of(P_PRESS) == section_of(P_FUTURE), "the press and Your Future share the YOU section");
}

// 1.1.4: the Grid page's seats say what the game shows: written and waiting, or confirmed
void grid_seat_states(::fake::Game &g, const json::Value &baseline)
{
    begin("1.1.4: next season's seats carry their state in the game");
    Career c(g, baseline, "f1_career_2025", "f1_career_2025.driver_career:{seatstate}", "lando_norris");
    c.load();
    c.enter_hub();
    ss::Settings s = settings();
    {
        std::lock_guard lock(g_lock);
        g_state.seat_checks.clear();
        // a seat the market planned for next season, written and not shown yet, and one the game confirmed
        auto grid = ss::projected_grid(g_state, s);
        int made = 0;
        for (const ss::Team &t : g_state.teams) {
            const std::string &who = grid[t.id][0];
            if (who.empty() || made >= 2)
                continue;
            ss::State::SeatCheck sc;
            sc.team = t.id;
            sc.driver = who;
            sc.year = g_state.year;
            sc.state = made == 0 ? 1 : 2;
            g_state.seat_checks.push_back(sc);
            ++made;
        }
        update_overlay(s);
    }
    int waiting = 0, confirmed = 0;
    for (const UiTeam &t : g_ov.grid)
        for (const UiSeat &seat : t.seat) {
            waiting += seat.game == 1;
            confirmed += seat.game == 2;
        }
    check(waiting == 1 && confirmed == 1, "one seat waits for the game, one is confirmed: " + std::to_string(waiting) + "/" +
                                              std::to_string(confirmed));
    {
        std::lock_guard lock(g_lock);
        g_state.seat_checks.clear();
    }
    c.leave_hub();
    c.unload();
}

// The press hint at Team HQ names the panel's key as set
void press_key(::fake::Game &g, const json::Value &baseline)
{
    begin("the press hint names the panel key as set");
    Career c(g, baseline, "f1_career_2026", "f1_career_2026.driver_career:{presskey}", "oscar_piastri");
    c.load();
    c.enter_hub();
    g.frames(2.5);
    ss::State::Press q;
    q.id = 99;
    q.question = "?";
    q.answers = {"a", "b", "c"};
    g.pick("panel_key", "f9");
    visit(c, g); // Team HQ takes the setting
    g_state.press = q;
    g.pick("menu", "prensa");
    check(g_report.find("with F9.") != std::string::npos || g_report.find("con F9.") != std::string::npos,
          "the hint says F9: " + g_report.substr(g_report.rfind('\n', g_report.size() - 2) + 1));
    g.pick("panel_key", "f7");
    visit(c, g);
    g_state.press.reset();
    c.unload();
    end();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("plugin_test <baseline.json> <swofty.json> [verbose | bench]\n");
        return 2;
    }
    std::string mode = argc > 3 ? argv[3] : "";
    g_verbose = mode == "verbose";
    ::fake::log_quiet = !g_verbose;
    // the core's stand-in, next to this exe: the SDK's typed events and the pause query look for its exports
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dll(exe);
    dll.resize(dll.find_last_of(L"\\/") + 1);
    char exe_a[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_a, MAX_PATH);
    g_saves_dir = exe_a;
    g_saves_dir.resize(g_saves_dir.find_last_of("\\/") + 1);
    g_saves_dir += "saves";
    dll += L"swofty_core.dll";
    if (!LoadLibraryW(dll.c_str())) {
        std::printf("swofty_core.dll (the core's stand-in) is missing next to the exe: build.bat plugin builds it\n");
        return 2;
    }
    std::string baseline_text = read_file(argv[1]);
    json::Error err;
    json::Value baseline = json::parse(baseline_text, &err), manifest = json::parse(read_file(argv[2]), &err);
    if (!baseline.is_object() || !manifest.is_object()) {
        std::printf("the baseline or the manifest can't be read\n");
        return 2;
    }

    ::fake::Game g(manifest);
    g.files["data/baseline.json"] = baseline_text;
    SwoftyAddonRegister(&g.api);
    if (!SwoftyAddonInit(&g.api)) {
        std::printf("SwoftyAddonInit failed\n");
        return 1;
    }
    for (void (*fn)() : g.on_load)
        fn();

    if (mode == "bench") {
        bench(g, baseline);
        return 0;
    }
    registration(g);
    // 1.1.3 (15 Nov): on-track reputation is experimental and off by default; the scenarios below switch it on
    begin("on-track reputation: off by default, experimental");
    check(!settings().on_track, "on-track reputation is off until the player switches it on");
    end();
    g.toggle("on_track", true);
    whole_season(g, baseline);
    for (unsigned seed : {1u, 2u, 3u})
        saves_never_lose(g, baseline, seed, 400);
    track_events(g, baseline);
    my_team(g, baseline);
    for (int career = 1; career <= 6; ++career)
        refused_seats(g, baseline, career);
    seat_order(g, baseline);
    timing_lines(g, baseline);
    f2_revamp_without_service(g, baseline);
    borrowed_identity(g, baseline);
    results_before_the_first_race(g, baseline);
    progressive_career(g, baseline);
    // 1.1.3, 13 Nov
    shorter_standings(g, baseline);
    first_race_replaces_the_table(g, baseline);
    progressive_switch(g, baseline);
    seats_confirmed(g, baseline);
    flashback_without_events(g, baseline);
    breakdown_keeps_form(g, baseline);
    overlays_across_careers(g, baseline);
    // 1.1.3, the audit of 15 Nov
    another_career_same_id(g, baseline);
    sessions_before_the_race(g, baseline);
    late_standings(g, baseline);
    several_rounds_at_once(g, baseline);
    first_visit_of_a_season(g, baseline);
    rejected_grid(g, baseline);
    provisional_until_read_back(g, baseline);
    copies_on_load(g, baseline);
    older_save(g, baseline);
    two_provisionals(g, baseline);
    track_attempts(g, baseline);
    banner_cursor();
    press_key(g, baseline);
    // 1.1.4: the F7 panel and the overlays
    overlay_hand_over();
    panel_sections();
    grid_seat_states(g, baseline);

    std::printf("\n%s\n", failures ? "plugin_test: FAILURES" : "all plugin checks passed");
    return failures ? 1 : 0;
}
