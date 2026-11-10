// Silly Season 1.1.0 phase 6: academies and reserves. The teams sign young drivers into their academies, sign a
// reserve driver for next season once the grid is final, and call on the better of their reserve and their academy
// drivers for a stand-in (market.cpp, maybe_standin). Racing Bulls shares Red Bull's academy and reserve, Haas
// Ferrari's (partner_family). Reserves live in the market only: Swofty can set a team's race seats, not the game's
// own reserve slot. Included by market.cpp after phase5.inl.

namespace {

// the families that run an academy: each active senior team's (a second team or a partner shares its senior's)
std::vector<const Team *> academy_teams(const State &st)
{
    std::vector<const Team *> out;
    for (const Team &t : st.teams)
        if (t.active && !t.family.empty() && t.family != "konnersport" && lead_team(st, t) == &t &&
            std::none_of(out.begin(), out.end(), [&](const Team *x) { return x->family == t.family; }))
            out.push_back(&t);
    return out;
}

bool reserve_elsewhere(const State &st, const std::string &driver, const Team *except)
{
    for (const Team &t : st.teams)
        if (&t != except && t.reserve == driver)
            return true;
    return false;
}

// once a season, from a third of the way in: the academies sign young drivers (F2, the pool, new talents)
void academy_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (!s.academies || st.real || st.academy_year == st.year || progress < 0.30)
        return;
    st.academy_year = st.year;
    int cap = s.intensity == Intensity::chaos ? 4 : s.intensity == Intensity::lively ? 3 : 2;
    double p = s.intensity == Intensity::chaos ? 0.7 : s.intensity == Intensity::lively ? 0.5 : 0.35;
    std::vector<const Team *> teams = academy_teams(st);
    for (size_t i = teams.size(); i > 1; --i) // a different order every season
        std::swap(teams[i - 1], teams[static_cast<size_t>(r.range(0, static_cast<int>(i) - 1))]);
    int signed_now = 0;
    for (const Team *t : teams) {
        if (signed_now >= cap)
            break;
        int members = 0;
        for (const auto &[id, d] : st.drivers)
            members += d.academy == t->family && d.team.empty() && !d.retired && d.age(st.year) <= 25 ? 1 : 0;
        if (members >= 3 || !r.chance(p))
            continue;
        Driver *pick = nullptr;
        float best = -1e9f;
        for (auto &[id, d] : st.drivers) {
            if (!d.team.empty() || !d.academy.empty() || d.retired || d.retiring || d.age(st.year) > 21 || id == st.player ||
                d.kind == "player" || d.kind == "legend" || st.shielded(id) || reserve_elsewhere(st, id, nullptr))
                continue;
            float sc = d.potential() + 0.3f * d.overall() + (d.f2_pos > 0 && d.f2_pos <= 5 ? 4.0f : 0.0f) +
                       static_cast<float>(r.normal() * 2.5);
            if (sc > best) {
                best = sc;
                pick = &d;
            }
        }
        if (!pick)
            break;
        pick->academy = t->family;
        ++signed_now;
        std::string name = pick->name, team = t->name;
        int age = pick->age(st.year), n = r.range(0, 9);
        add_news(st, round, "academy", [&](text::Lang l) { return text::h_academy(l, name, team); },
                 [&](text::Lang l) { return text::academy(l, n, name, team, age); }, false);
        tag_news(st, pick->id, t->id);
    }
}

// once next season's grid is final: each senior team keeps or signs its reserve driver for next season. A driver
// with a race seat next season can't be one. Told in one news item.
void reserve_market(State &st, const Settings &s, Rng &r, int round)
{
    if (!s.academies || st.real || st.reserves_year == st.year)
        return;
    st.reserves_year = st.year;
    std::set<std::string> racing; // next season's race drivers
    for (const auto &[team, pair] : st.next_grid)
        for (const std::string &who : pair)
            if (!who.empty())
                racing.insert(who);
    struct Line {
        std::string team, driver;
        bool renewed;
    };
    std::vector<Line> lines;
    for (Team &t : st.teams) {
        if (!t.active)
            continue;
        if (lead_team(st, t) != &t) { // a second team or a partner shares its senior team's reserve
            t.reserve.clear();
            continue;
        }
        // the reserve stays on when he's still free and the team is happy (most are)
        if (const Driver *d = st.driver(t.reserve);
            d && !racing.count(d->id) && !d->retiring && !d->retired && d->age(st.year + 1) <= 38 && r.chance(0.7)) {
            t.reserve_until = st.year + 1;
            lines.push_back({t.name, d->name, true});
            continue;
        }
        t.reserve.clear();
        const Driver *pick = nullptr;
        float best = -1e9f;
        for (const auto &[id, d] : st.drivers) {
            if (!d.team.empty() || racing.count(id) || d.retired || d.retiring || id == st.player || d.kind == "player" ||
                st.shielded(id) || (d.kind == "legend" && !s.legends) || d.age(st.year + 1) > 36 || reserve_elsewhere(st, id, &t))
                continue;
            // an academy driver ready to step in, else an experienced hand; a driver the game would seat helps on the day
            float sc = value(d) + (in_academy(t, d) ? 8.0f : 0.0f) + (d.seasons > 0 ? 2.0f : 0.0f) +
                       (st.seatable(id) ? 0.0f : -6.0f) + static_cast<float>(r.normal() * 2.0);
            if (sc > best) {
                best = sc;
                pick = &d;
            }
        }
        if (!pick)
            continue;
        t.reserve = pick->id;
        t.reserve_until = st.year + 1;
        lines.push_back({t.name, pick->name, false});
    }
    if (lines.empty())
        return;
    int year = st.year + 1;
    add_news(st, round, "reserve", [&](text::Lang l) { return text::h_reserves(l, year); },
             [&](text::Lang l) {
                 std::string b;
                 for (const Line &x : lines)
                     b += text::reserve_line(l, x.team, x.driver, x.renewed) + "\n";
                 return b;
             },
             true);
}

// a new season: a reserve who now races, or whose deal ran out, leaves the role; academy drivers past 25 without an
// F1 seat leave their academy
void academy_new_season(State &st)
{
    for (Team &t : st.teams) {
        const Driver *d = st.driver(t.reserve);
        if (!d || !d->team.empty() || d->retired || t.reserve_until < st.year)
            t.reserve.clear();
    }
    for (auto &[id, d] : st.drivers)
        if (!d.academy.empty() && d.team.empty() && d.age(st.year) >= 26)
            d.academy.clear();
}

} // namespace
