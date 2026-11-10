// 1.1.0 phase 2: history mode for Progressive Career on the Swofty Seasons (swofty.swofty-seasons-progressive).
// Its careers keep their own data group; every season the addon puts the real line-up on the grid. Silly Season
// reads the addon's data/years.json (the addon copies data/ to the game folder for its plugin), tells the real
// moves to next season's grid through the season, and never writes a seat while the real years last. The years
// are the data's own (2015 to 2026 with the Swofty Seasons 2015-2018): from the last one on, Progressive leaves
// the grid free and Silly Season's own market takes over.
// Included by plugin.cpp inside its anonymous namespace, after the real-grid reader.

json::Value g_real;                                      // Progressive's data/years.json
bool g_real_ok = false;
std::map<int, std::set<uint32_t>> g_real_looks;          // year -> the stat ids of that year's looks on the grid
std::map<int, std::string> g_real_slots;                 // career slot -> the market's team
int g_real_first = 0, g_real_last = 0;                   // the real years the data covers

// drivers of the real seasons the baseline doesn't have (the season addons' own and the ones who left before 2025)
const std::map<std::string, ss::RealPerson> &real_people()
{
    static const std::map<std::string, ss::RealPerson> people = {
        {"daniel_ricciardo", {"Daniel Ricciardo", "australia", 1989}}, {"sw20_kvyat", {"Daniil Kvyat", "russia", 1994}},
        {"kevin_magnussen", {"Kevin Magnussen", "denmark", 1992}},    {"sw21_raikkonen", {"Kimi Räikkönen", "finland", 1979}},
        {"sw20_grosjean", {"Romain Grosjean", "france", 1986}},       {"sw22_vettel", {"Sebastian Vettel", "germany", 1987}},
        {"sw21_giovinazzi", {"Antonio Giovinazzi", "italy", 1993}},   {"sw19_kubica", {"Robert Kubica", "poland", 1984}},
        {"jack_aitken", {"Jack Aitken", "britain", 1995}},            {"sw22_latifi", {"Nicholas Latifi", "canada", 1995}},
        {"sw22_schumacher", {"Mick Schumacher", "germany", 1999}},    {"sw21_mazepin", {"Nikita Mazepin", "russia", 1999}},
        {"guanyu_zhou", {"Guanyu Zhou", "china", 1999}},              {"sw23_devries", {"Nyck de Vries", "netherlands", 1995}},
        {"logan_sargeant", {"Logan Sargeant", "usa", 2000}},          {"jack_doohan", {"Jack Doohan", "australia", 2003}},
        {"valtteri_bottas", {"Valtteri Bottas", "finland", 1989}},    {"sergio_perez", {"Sergio Pérez", "mexico", 1990}},
        {"arvid_lindblad", {"Arvid Lindblad", "britain", 2007}},
        // 2016 to 2018 (the Swofty Seasons 2016, 2017 and 2018)
        {"sw16_rosberg", {"Nico Rosberg", "germany", 1985}},          {"sw16_gutierrez", {"Esteban Gutiérrez", "mexico", 1991}},
        {"sw16_haryanto", {"Rio Haryanto", "indonesia", 1993}},       {"sw16_nasr", {"Felipe Nasr", "brazil", 1992}},
        {"sw17_massa", {"Felipe Massa", "brazil", 1981}},             {"sw17_palmer", {"Jolyon Palmer", "britain", 1991}},
        {"sw17_wehrlein", {"Pascal Wehrlein", "germany", 1994}},      {"sw18_ericsson", {"Marcus Ericsson", "sweden", 1990}},
        {"sw18_hartley", {"Brendon Hartley", "new_zealand", 1989}},   {"sw18_sirotkin", {"Sergey Sirotkin", "russia", 1995}},
        {"sw18_vandoorne", {"Stoffel Vandoorne", "belgium", 1992}},
        // 2015 (the Swofty Seasons 2015)
        {"sw15_maldonado", {"Pastor Maldonado", "venezuela", 1985}},  {"sw15_merhi", {"Roberto Merhi", "spain", 1991}},
        {"sw15_stevens", {"Will Stevens", "britain", 1991}},
    };
    return people;
}

bool load_real_data()
{
    if (g_real_ok)
        return true;
    std::vector<std::string> paths = {dll_dir() + "\\..\\..\\swofty.swofty-seasons-progressive\\data\\years.json"};
    if (const char *la = std::getenv("LOCALAPPDATA"))
        paths.push_back(std::string(la) + "\\Swofty\\addons\\swofty.swofty-seasons-progressive\\data\\years.json");
    for (const std::string &path : paths) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            continue;
        std::stringstream text;
        text << f.rdbuf();
        json::Error err;
        json::Value v = json::parse(text.str(), &err);
        if (!v["seasons"].is_object() || !v["slots"].is_array())
            continue;
        g_real = std::move(v);
        g_real_ok = true;
        log("[silly-season] history: Progressive's line-ups read from %s", path.c_str());
        break;
    }
    if (!g_real_ok) {
        log("[silly-season] history: Progressive's data/years.json was not found; Silly Season stays out of this career");
        return false;
    }
    for (const json::Value &s : *g_real["slots"].if_array())
        g_real_slots[static_cast<int>(s["slot"].as_int())] = std::string(s["team"].as_string());
    auto stat = [](const json::Value &v, const std::string &id) {
        if (v.is_int())
            g_real_stats.emplace(static_cast<uint32_t>(v.as_int()), id);
    };
    if (const json::Array *pool = g_real["pool"].if_array())
        for (const json::Value &p : *pool) {
            std::string id(p["driver"].as_string());
            stat(p["stat_id"], id);
            stat(p["default_look"]["stat_id"], id);
            if (const json::Object *looks = p["looks"].if_object())
                for (const auto &[year, look] : *looks)
                    stat(look["stat_id"], id);
        }
    for (const auto &[ytext, season] : *g_real["seasons"].if_object()) {
        int year = std::atoi(ytext.c_str());
        g_real_first = g_real_first ? std::min(g_real_first, year) : year;
        g_real_last = std::max(g_real_last, year);
        if (const json::Array *teams = season["teams"].if_array())
            for (const json::Value &t : *teams)
                if (const json::Array *drivers = t["drivers"].if_array())
                    for (const json::Value &d : *drivers)
                        if (d["look"]["stat_id"].is_int()) {
                            uint32_t s = static_cast<uint32_t>(d["look"]["stat_id"].as_int());
                            g_real_stats[s] = std::string(d["driver"].as_string());
                            g_real_looks[year].insert(s);
                        }
    }
    log("[silly-season] history: %d to %d, %zu driver look(s) known", g_real_first, g_real_last, g_real_stats.size());
    return true;
}

// the year's real season as the market takes it
ss::RealSeason real_season_of(int year)
{
    ss::RealSeason y;
    y.year = year;
    const json::Value &sv = g_real["seasons"][std::to_string(year)];
    if (const json::Array *teams = sv["teams"].if_array())
        for (const json::Value &t : *teams) {
            auto slot = g_real_slots.find(static_cast<int>(t["slot"].as_int()));
            if (slot == g_real_slots.end())
                continue;
            const std::string &id = slot->second;
            y.names[id] = {std::string(t["name"].as_string()), std::string(t["code"].as_string())};
            if (t["pace"]["gap_pct"].is_number())
                y.gap[id] = static_cast<float>(t["pace"]["gap_pct"].as_double());
            auto &seats = y.grid[id];
            seats = {"", ""};
            if (const json::Array *drivers = t["drivers"].if_array())
                for (const json::Value &d : *drivers) {
                    int k = static_cast<int>(d["position"].as_int());
                    if (k >= 0 && k < 2)
                        seats[static_cast<size_t>(k)] = std::string(d["driver"].as_string());
                }
        }
    if (const json::Array *changes = sv["changes"].if_array())
        for (const json::Value &c : *changes) {
            auto slot = g_real_slots.find(static_cast<int>(c["slot"].as_int()));
            if (slot == g_real_slots.end())
                continue;
            ss::RealSeason::Change ch;
            ch.round = static_cast<int>(c["round"].as_int());
            ch.rounds = static_cast<int>(c["real_round_count"].as_int(24));
            ch.team = slot->second;
            ch.seat = static_cast<int>(c["seat"].as_int());
            ch.out = std::string(c["out"].as_string());
            ch.in = std::string(c["in"].as_string());
            ch.kind = std::string(c["kind"].as_string());
            ch.until = static_cast<int>(c["until_round"].as_int());
            // a change names its drivers by the season addon's label ("sw21_raikkonen"): the pool's id is the same
            y.changes.push_back(ch);
        }
    if (const json::Object *ratings = g_real["ratings"]["years"][std::to_string(year)].if_object())
        for (const auto &[id, r] : *ratings)
            y.ratings[id] = {static_cast<float>(r["experience"].as_double(60)), static_cast<float>(r["racecraft"].as_double(70)),
                             static_cast<float>(r["awareness"].as_double(70)), static_cast<float>(r["pace"].as_double(70))};
    return y;
}

// where a driver raced in a real year ("" when he didn't)
std::string real_team_of(int year, const std::string &driver)
{
    static std::map<int, std::map<std::string, std::string>> cache;
    auto &m = cache[year];
    if (m.empty()) {
        ss::RealSeason y = real_season_of(year);
        for (const auto &[team, pair] : y.grid)
            for (const std::string &id : pair)
                if (!id.empty())
                    m[id] = team;
        m[""] = ""; // the year is read
    }
    auto it = m.find(driver);
    return it == m.end() ? std::string() : it->second;
}

// how long each driver stays: his last season in the team he races for in `year` (past the data: one more)
int real_until(int year, const std::string &driver, const std::string &team)
{
    int y = year;
    while (y + 1 <= g_real_last && real_team_of(y + 1, driver) == team)
        ++y;
    return y >= g_real_last ? y + 1 : y;
}

ss::RealContext real_context(const ss::RealSeason &now, const ss::RealSeason *next)
{
    ss::RealContext ctx;
    ctx.people = real_people();
    for (const auto &[team, pair] : now.grid)
        for (const std::string &id : pair)
            if (!id.empty())
                ctx.until_now[id] = real_until(now.year, id, team);
    if (next)
        for (const auto &[team, pair] : next->grid)
            for (const std::string &id : pair)
                if (!id.empty())
                    ctx.until_next[id] = real_until(next->year, id, team);
    for (int y = g_real_first; y <= g_real_last; ++y) {
        ss::RealSeason other = real_season_of(y);
        for (const auto &[team, pair] : other.grid)
            for (const std::string &id : pair) {
                if (id.empty())
                    continue;
                if (y < now.year)
                    ctx.earlier.insert(id);
                if (next && y > next->year)
                    ctx.later.insert(id);
            }
    }
    return ctx;
}

// every driver of the real seasons is in the market from the start, with his name
void real_people_in(ss::State &st)
{
    const auto &people = real_people();
    if (const json::Array *pool = g_real["pool"].if_array())
        for (const json::Value &p : *pool) {
            std::string id(p["driver"].as_string());
            if (id.empty() || st.driver(id))
                continue;
            ss::Driver d;
            d.id = id;
            d.kind = "f1";
            auto it = people.find(id);
            d.name = it != people.end() ? it->second.name : name_from_label(id);
            d.nat = it != people.end() ? it->second.nat : "";
            d.born = it != people.end() ? it->second.born : st.year - 27;
            if (p["stat_id"].is_int())
                d.stat = static_cast<int>(p["stat_id"].as_int());
            st.drivers.emplace(id, d);
        }
}

// the real year the career is in, from the looks of the drivers on its grid (0: can't tell)
int detect_real_year()
{
    int best = 0;
    size_t best_n = 0;
    for (const auto &[year, looks] : g_real_looks) {
        size_t n = 0;
        for (const RealSeat &s : g_last_seats)
            n += looks.count(s.driver);
        if (n > best_n) {
            best_n = n;
            best = year;
        }
    }
    log("[silly-season] history: the grid's looks match %d best (%zu of %zu cars)", best, best_n, g_last_seats.size());
    return best_n >= 6 ? best : 0;
}

// g_lock held: the market takes the real season `year` (its rounds from the game's calendar)
void start_real_season(int year, int rounds)
{
    g_state.rounds = rounds;
    ss::RealSeason now = real_season_of(year);
    bool has_next = year < g_real_last;
    ss::RealSeason next = has_next ? real_season_of(year + 1) : ss::RealSeason{};
    ss::RealContext ctx = real_context(now, has_next ? &next : nullptr);
    ss::real_season(g_state, now, has_next ? &next : nullptr, ctx);
    g_real_now = g_state.real;
    log("[silly-season] history: %d, %zu real move(s) to tell before %d, %zu mid-season change(s)%s", year, g_state.real_moves.size(),
        year + 1, g_state.real_changes.size(), has_next ? "" : "; from here on the market is Silly Season's own");
}
