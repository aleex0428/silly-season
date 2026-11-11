// Drivers of the game the baseline doesn't have. Another addon can put drivers in the career's own lists
// (More Driver Models adds a hundred free agents to the 2025 and 2026 driver pools), and an editor can put
// drivers of other seasons on the grid. Silly Season reads the career's driver lists and each driver's
// career statistics from the loaded game database, so they join its market with their names and ratings.
// Read only: nothing is written to the database.

namespace game_pool {

std::string text_of(const db::Value &v)
{
    if (v.s)
        return v.s;
    if (v.tag == SWOFTY_DB_INT || v.tag == SWOFTY_DB_BOOL)
        return std::to_string(v.i);
    if (v.tag == SWOFTY_DB_FLOAT)
        return std::to_string(v.f);
    return {};
}

std::string str(const std::string &key)
{
    db::Value v{};
    return db::get(key, &v) == db::Result::ok ? text_of(v) : std::string();
}

long long num(const std::string &key, long long fallback = 0)
{
    db::Value v{};
    if (db::get(key, &v) != db::Result::ok)
        return fallback;
    if (v.s) {
        char *end = nullptr;
        long long n = std::strtoll(v.s, &end, 10);
        return end != v.s ? n : fallback;
    }
    return v.tag == SWOFTY_DB_FLOAT ? static_cast<long long>(v.f) : v.i;
}

// "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.66,0:0.66,0.9:0.8,1:0.8" -> now and peak, out of 100
bool curve(const std::string &c, float &now, float &peak)
{
    size_t at = c.rfind('@');
    std::string body = at == std::string::npos ? c : c.substr(at + 1);
    float maxy = 100;
    std::vector<float> ys;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t comma = body.find(',', pos);
        std::string part = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            std::string k = part.substr(0, colon), v = part.substr(colon + 1);
            if (k == "maxy")
                maxy = std::strtof(v.c_str(), nullptr);
            else if (!k.empty() && (std::isdigit(static_cast<unsigned char>(k[0])) || k[0] == '.'))
                ys.push_back(std::strtof(v.c_str(), nullptr));
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    if (ys.empty())
        return false;
    now = ys.front() * maxy;
    peak = ys.back() * maxy;
    return now > 0 && now <= 100;
}

// the career statistics holders: driver label -> holder index (read once)
const std::unordered_map<std::string, int> &holders()
{
    static std::unordered_map<std::string, int> map;
    static bool built = false;
    if (built || !db::available())
        return map;
    built = true;
    const std::string base = "driver_statistics.driver_statistics";
    long long n = num(base + "._DriverStatisticsHolder_instancecount_");
    for (long long i = 0; i < n && i < 2000; ++i) {
        std::string h = base + ".DriverStatisticsHolder_" + std::to_string(i);
        std::string who = str(h + ".driver");
        if (who.empty())
            who = str(h + "._metadata_");
        if (!who.empty())
            map.emplace(who, static_cast<int>(i));
    }
    return map;
}

// the driver's ratings from his career statistics: the 2026 set, else the default one, else the first
bool ratings(const std::string &label, ss::GameDriver &g)
{
    auto it = holders().find(label);
    if (it == holders().end())
        return false;
    std::string h = "driver_statistics.driver_statistics.DriverStatisticsHolder_" + std::to_string(it->second);
    long long sets = num(h + "._StatisticsDataSet_instancecount_");
    long long pick = -1;
    for (long long s = 0; s < sets; ++s) {
        std::string name = str(h + ".StatisticsDataSet_" + std::to_string(s) + "._metadata_");
        if (name == "2026") {
            pick = s;
            break;
        }
        if (name == "default" || pick < 0)
            pick = s;
    }
    if (pick < 0)
        return false;
    std::string set = h + ".StatisticsDataSet_" + std::to_string(pick);
    long long n = num(set + "._Statistic_instancecount_");
    int found = 0;
    for (long long q = 0; q < n; ++q) {
        std::string stat = set + ".Statistic_" + std::to_string(q);
        std::string name = str(stat + "._metadata_");
        float now = 0, peak = 0;
        if (!curve(str(stat + ".evolution"), now, peak))
            continue;
        if (name == "pace")
            g.pace = now, g.pace_peak = peak, ++found;
        else if (name == "racecraft")
            g.racecraft = now, g.racecraft_peak = peak, ++found;
        else if (name == "awareness")
            g.awareness = now, g.awareness_peak = peak, ++found;
        else if (name == "experience")
            g.experience = now, g.experience_peak = peak, ++found;
    }
    g.rated = found == 4;
    return g.rated;
}

// "lng_andrea_kimi" -> "Andrea Kimi"
std::string words(std::string key)
{
    if (key.rfind("lng_", 0) == 0)
        key.erase(0, 4);
    std::string out;
    bool up = true;
    for (char ch : key) {
        if (ch == '_' || ch == '-') {
            out += ' ';
            up = true;
        } else {
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
            up = false;
        }
    }
    return out;
}

// the drivers of the game's past whom only the Legends option brings back
bool icon(const std::string &label)
{
    static const char *const icons[] = {"alain_prost",      "nigel_mansell",      "james_hunt",       "ayrton_senna",    "micheal_schumacher",
                                        "michael_schumacher", "jenson_button",    "david_coulthard",  "felipe_massa",    "mark_webber",
                                        "mika_hakkinen",    "rubens_barrichello", "damon_hill",       "jacques_villeneuve", "ralf_schumacher",
                                        "nico_rosberg",     "montoya",            "juan_pablo-montoya"};
    for (const char *i : icons)
        if (label == i)
            return true;
    return false;
}

// entrants.drivers in one pass: label -> its row (1.1.2: a lookup per driver walked the whole table each time,
// and with a hundred drivers from other addons the first Team HQ after a load stalled for 12-14 s)
const std::unordered_map<std::string, db::Cursor> &driver_rows()
{
    static std::unordered_map<std::string, db::Cursor> map;
    static bool built = false;
    if (built || !db::available())
        return map;
    built = true;
    db::Cursor c{};
    for (db::Result r = db::open("entrants.drivers", &c); r == db::Result::ok; r = db::step(&c))
        if (const char *label = db::row_name(&c))
            map.emplace(label, c);
    return map;
}

// one driver of the game by his label, from entrants.drivers and his statistics; false when he has no row
bool driver(const std::string &label, const std::string &kind, ss::GameDriver &g)
{
    auto row = driver_rows().find(label);
    if (row == driver_rows().end())
        return false;
    db::Cursor c = row->second;
    auto cell = [&](const char *field) {
        db::Value v{};
        return db::cell(&c, field, &v) == db::Result::ok ? text_of(v) : std::string();
    };
    g.id = label;
    g.kind = kind;
    g.stat = std::atoi(cell("stat_id").c_str());
    g.nat = cell("nationality");
    g.initials = cell("initials");
    g.born = std::atoi(cell("date_of_birth.year").c_str());
    std::string first = cell("first_name"), last = cell("last_name");
    // the game's name keys say the name ("lng_carlos" "lng_sainz"); another addon's own keys don't
    if (first.rfind("lng_", 0) == 0 && last.rfind("lng_", 0) == 0 && first.find("mdm_") == std::string::npos &&
        first.find("_first") == std::string::npos) {
        std::string f = words(first);
        if (size_t o = f.find(" Official"); o != std::string::npos)
            f.erase(o);
        g.name = f + " " + words(last);
    } else {
        g.name = name_from_label(label);
    }
    g.legend = icon(label);
    ratings(label, g);
    return true;
}

// a story team's copy of a driver the game keeps once more (arron_barnes_konnersport, noah_visser_zenith),
// or a placeholder row: the market takes the driver himself only
bool twin(const std::string &label)
{
    auto ends = [&](const char *tail) {
        size_t n = std::strlen(tail);
        return label.size() > n && label.compare(label.size() - n, n, tail) == 0;
    };
    return ends("_konnersport") || ends("_zenith") || label.rfind("default_", 0) == 0;
}

// the label an entrant row names: its driver, or the driver of its entrant reference ("f1_2026-lando_norris")
std::string entrant_driver(const std::string &base)
{
    std::string who = str(base + ".driver");
    if (!who.empty())
        return who;
    std::string ref = str(base + ".entrant_reference");
    if (size_t dash = ref.find('-'); dash != std::string::npos && (ref.rfind("f1_", 0) == 0 || ref.rfind("f2_", 0) == 0))
        ref.erase(0, dash + 1);
    if (ref == "kimi_antonelli")
        return "andrea-kimi_antonelli";
    if (ref == "jr")
        return "carlos_sainz-jr";
    return ref;
}

// the career's driver lists, by data group: the pool (free agents), the reserves and the F2 field
std::vector<std::pair<std::string, std::string>> lists_of(const std::string &group)
{
    if (group == "f1_career_2026")
        return {{"f1_2026_driver_pool", "pool"}, {"f1_career_2026_reserve_drivers", "reserve"}, {"career_f2_2025_for_f1_2026", "f2"}};
    if (group == "f1_career_2025")
        return {{"f1_2025_driver_pool", "pool"}, {"f1_career_2025_reserve_drivers", "reserve"}, {"career_f2_2024_for_f1_2025", "f2"}};
    // F2 Revamp's career: the 2026 lists (its row names the pool f1_career_2026_driver_pool) and its own F2 field
    if (group == "f1_career_f2_26")
        return {{"f1_2026_driver_pool", "pool"}, {"f1_career_2026_driver_pool", "pool"}, {"f1_career_2026_reserve_drivers", "reserve"},
                {"f2_2026", "f2"}};
    return {};
}

// every driver of the career's lists, as the game holds them
std::vector<ss::GameDriver> read(const std::string &group, std::string &why)
{
    std::vector<ss::GameDriver> out;
    if (!db::available()) {
        why = "the game database can't be read yet";
        return out;
    }
    auto wanted = lists_of(group);
    if (wanted.empty()) {
        why = "no driver lists for this career";
        return out;
    }
    std::set<std::string> seen;
    const std::string base = "entrants.entrant_lists";
    long long lists = num(base + "._entrant_list_instancecount_");
    size_t no_row = 0;
    for (long long i = 0; i < lists && i < 400; ++i) {
        std::string list = base + ".entrant_list_" + std::to_string(i);
        std::string label = str(list + "._metadata_");
        auto w = std::find_if(wanted.begin(), wanted.end(), [&](const auto &x) { return x.first == label; });
        if (w == wanted.end())
            continue;
        long long n = num(list + "._entrant_instancecount_");
        for (long long j = 0; j < n && j < 600; ++j) {
            std::string who = entrant_driver(list + ".entrant_" + std::to_string(j));
            if (who.empty() || !seen.insert(who).second || twin(who))
                continue;
            ss::GameDriver g;
            if (driver(who, w->second, g))
                out.push_back(std::move(g));
            else
                ++no_row;
        }
    }
    why = std::to_string(out.size()) + " driver(s) in the career's lists" + (no_row ? ", " + std::to_string(no_row) + " without a driver row" : "");
    return out;
}

// The reading runs on a thread of its own (the database can be read from any thread), once per data group while
// the game runs: the game database doesn't change after boot. Team HQ only takes the result, so it never waits.
struct Work {
    std::mutex lock;   // the results, and which groups are being read
    std::mutex read;   // one read at a time (the indexes above are built by the first)
    std::map<std::string, std::pair<std::vector<ss::GameDriver>, std::string>> done;
    std::set<std::string> running;
};
Work &work()
{
    static Work w;
    return w;
}

// start reading a group's lists in the background, unless it was read or is being read
void start(const std::string &group)
{
    if (group.empty() || !db::available())
        return;
    {
        std::lock_guard l(work().lock);
        if (work().done.count(group) || !work().running.insert(group).second)
            return;
    }
    std::thread([group] {
        std::vector<ss::GameDriver> list;
        std::string why;
        {
            std::lock_guard r(work().read);
            list = read(group, why);
        }
        std::lock_guard l(work().lock);
        work().running.erase(group);
        if (!list.empty() || db::available())
            work().done[group] = {std::move(list), std::move(why)};
    }).detach();
}

// the group's lists once read; false while they are still being read (or haven't been asked for)
bool take(const std::string &group, std::vector<ss::GameDriver> &list, std::string &why)
{
    std::lock_guard l(work().lock);
    auto it = work().done.find(group);
    if (it == work().done.end())
        return false;
    list = it->second.first;
    why = it->second.second;
    return true;
}

} // namespace game_pool

// g_lock held: the career's drivers the market doesn't know join it (once per career load). The lists are read in
// the background (game_pool::start, from the career's load); until they are, the next Team HQ tries again.
void adopt_game_pool(const char *where)
{
    if (g_pool_read || g_real_career || !g_loaded)
        return;
    std::string why;
    std::vector<ss::GameDriver> list;
    if (!game_pool::take(g_state.group, list, why)) {
        game_pool::start(g_state.group);
        return;
    }
    g_pool_read = true;
    // a market saved before the twins were left out: they go, unless they raced
    for (auto it = g_state.drivers.begin(); it != g_state.drivers.end();)
        if (game_pool::twin(it->first) && it->second.team.empty() && it->second.starts == 0 &&
            !std::as_const(g_baseline)["drivers"][it->first].is_object())
            it = g_state.drivers.erase(it);
        else
            ++it;
    int added = ss::adopt_game_drivers(g_state, list);
    std::string names;
    int shown = 0;
    if (added)
        for (const ss::GameDriver &g : list)
            if (const ss::Driver *d = g_state.driver(g.id); d && shown < 6 && !std::as_const(g_baseline)["drivers"][g.id].is_object()) {
                names += (shown ? ", " : "") + d->name;
                ++shown;
            }
    log("[silly-season] game drivers (%s): %s; %d new to the market%s%s", where, why.c_str(), added, added ? ": " : "", names.c_str());
}
