#include "market.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#include "names.hpp"
#include "text.hpp"

namespace ss {

namespace json = swofty::json;

// ---------------------------------------------------------------------------------------------------
// Small helpers

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
    uint64_t next()
    {
        uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance(double p) { return uniform() < p; }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1)); }
    double normal()
    {
        double u1 = std::max(uniform(), 1e-12), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
};

uint64_t mix(uint64_t a, uint64_t b)
{
    Rng r(a ^ (b * 0x2545f4914f6cdd1dull));
    return r.next();
}

uint64_t hash_str(std::string_view s)
{
    uint64_t h = 1469598103934665603ull;
    for (char c : s)
        h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return h;
}

// The engine's random stream lives in the state, so a reloaded career continues it.
struct StateRng {
    State &st;
    Rng r;
    explicit StateRng(State &s) : st(s), r(s.rng) {}
    ~StateRng() { st.rng = r.s; }
};

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

double noise_sigma(Intensity i) { return i == Intensity::chaos ? 8.0 : i == Intensity::lively ? 4.5 : 2.5; }
double poach_chance(Intensity i) { return i == Intensity::chaos ? 0.65 : i == Intensity::lively ? 0.40 : 0.20; }
int midseason_cap(Intensity i) { return i == Intensity::chaos ? 3 : i == Intensity::lively ? 2 : 1; }

float value(const Driver &d) { return d.overall() + 4.0f * d.form; }

std::string team_name(const State &st, std::string_view id)
{
    if (const Team *t = st.team(id))
        return t->name;
    return std::string(id);
}

std::string driver_name(const State &st, std::string_view id)
{
    const Driver *d = st.driver(id);
    if (!st.player.empty() && id == st.player && !st.player_name.empty())
        return d && d->kind != "player" ? d->name + " (" + st.player_name + ")" : st.player_name;
    return d ? d->name : std::string(id);
}

text::Lang L(const Settings &s) { return text::lang_of(s.lang); }

// millions a season: grows fast with the driver's level, and big brands pay more
float estimate_salary(const Driver &d, const Team *t)
{
    float base = std::max(0.5f, std::pow(std::max(0.0f, d.overall() - 60.0f), 1.9f) / 22.0f);
    float brand = t ? 0.7f + 0.006f * t->prestige : 1.0f;
    float greed = 1.0f + 0.25f * std::max(0.0f, d.money - 0.5f) * 2.0f; // a driver after the money costs more
    float purse = t ? clampf(0.85f + t->budget / 700.0f, 0.9f, 1.1f) : 1.0f; // a rich team pays a little more
    return std::round(base * brand * greed * purse * 2.0f) / 2.0f;
}

// who reports a rumour: true stories come mostly from reliable outlets, empty ones from tabloids
int pick_outlet(const State &st, Rng &r, bool fake)
{
    int w[text::kOutlets], total = 0;
    bool known = st.outlets.size() == static_cast<size_t>(text::kOutlets);
    for (int i = 0; i < text::kOutlets; ++i) {
        // each outlet's hidden accuracy decides who gets the true stories and who runs the empty ones
        float acc = known ? st.outlets[static_cast<size_t>(i)].accuracy : 0.15f + 0.17f * static_cast<float>(text::outlet_reliability(i) - 1);
        total += w[i] = std::max(1, static_cast<int>(100.0f * (fake ? 1.05f - acc : acc + 0.05f)));
    }
    int x = r.range(1, total);
    for (int i = 0; i < text::kOutlets; ++i)
        if ((x -= w[i]) <= 0)
            return i;
    return 0;
}

// every item is written in both languages, so the language option applies to old news too
using Words = std::function<std::string(text::Lang)>;
constexpr text::Lang kEn{false}, kEs{true};

void add_news(State &st, int round, std::string kind, const Words &title, const Words &body, bool official)
{
    NewsItem n;
    n.id = st.next_news_id++;
    n.year = st.year;
    n.round = round;
    n.kind = std::move(kind);
    n.title_en = title(kEn);
    n.body_en = body(kEn);
    n.title_es = title(kEs);
    n.body_es = body(kEs);
    n.title = st.view_es ? n.title_es : n.title_en;
    n.body = st.view_es ? n.body_es : n.body_en;
    n.official = official;
    st.news.push_back(std::move(n));
    if (st.news.size() > 200)
        st.news.erase(st.news.begin(), st.news.begin() + static_cast<long>(st.news.size() - 200));
}

// the story the last news item tells: its driver, the teams and a signing's contract
void tag_news(State &st, const std::string &driver, const std::string &to, const std::string &from = {}, int until = 0,
              bool big = false)
{
    if (st.news.empty())
        return;
    NewsItem &n = st.news.back();
    n.driver = driver;
    n.to = to;
    n.from = from;
    n.until = until;
    n.big = big;
}

void add_move(State &st, int round, const std::string &driver, const std::string &from, const std::string &to,
              const std::string &kind)
{
    st.history.push_back({st.year, round, driver, from, to, kind});
    if (st.history.size() > 400)
        st.history.erase(st.history.begin(), st.history.begin() + static_cast<long>(st.history.size() - 400));
}

int contract_length(Rng &r, const Driver &d, int year, bool traits = true)
{
    int age = d.age(year);
    if (age >= 36)
        return 1;
    int n = value(d) >= 88 ? r.range(2, 4) : age <= 22 ? r.range(2, 3) : r.range(1, 3);
    // a loyal driver signs for longer; an ambitious star keeps his options open
    if (traits && d.loyalty >= 0.7f && n < 4 && r.chance(0.5))
        ++n;
    if (traits && d.ambition >= 0.75f && value(d) >= 84 && n > 1 && r.chance(0.5))
        --n;
    return n;
}

double retire_chance(const Driver &d, int year, bool contracted_beyond, Intensity in)
{
    if (d.kind == "legend")
        return 0;
    int age = d.age(year);
    if (age < 34)
        return 0;
    static const double table[] = {0.03, 0.05, 0.08, 0.12, 0.18, 0.25, 0.32, 0.40, 0.48, 0.55, 0.62, 0.70};
    double p = table[std::min(age - 34, 11)];
    float v = d.overall();
    if (v < 76)
        p *= 1.5;
    else if (v > 88)
        p *= 0.6;
    if (contracted_beyond)
        p *= 0.25;
    if (in == Intensity::chaos)
        p *= 1.3;
    return std::min(p, 0.9);
}

// --- personality ----------------------------------------------------------------------------------------

// a driver's character, drawn once from the career's seed and his id: the same driver is the same person in
// every career with that seed. Young stars lean ambitious, veterans loyal or after the money.
void draw_traits(uint64_t seed, int year, Driver &d)
{
    if (d.ambition >= 0 && d.loyalty >= 0 && d.money >= 0)
        return;
    Rng r(mix(seed, hash_str(d.id) ^ 0x7ea175ull));
    int age = d.age(year);
    float v = d.overall();
    float amb = 0.50f + (age <= 25 ? 0.12f : age >= 33 ? -0.10f : 0.0f) + (v >= 85 ? 0.10f : 0.0f);
    float loy = 0.45f + (age >= 32 ? 0.10f : 0.0f) + (!d.academy.empty() ? 0.08f : 0.0f);
    float mon = 0.40f + (age >= 31 ? 0.12f : 0.0f) + (d.backing > 0 ? -0.08f : 0.0f);
    d.ambition = clampf(amb + static_cast<float>(r.normal() * 0.20), 0.05f, 0.95f);
    d.loyalty = clampf(loy + static_cast<float>(r.normal() * 0.20), 0.05f, 0.95f);
    d.money = clampf(mon + static_cast<float>(r.normal() * 0.20), 0.05f, 0.95f);
}

void draw_all_traits(State &st)
{
    for (auto &[id, d] : st.drivers)
        draw_traits(st.seed, st.year, d);
}

// the trait that shows most in a driver: "ambition", "loyalty", "money" or "" when none stands out
std::string main_trait(const Driver &d)
{
    float a = d.ambition, l = d.loyalty, m = d.money;
    float top = std::max({a, l, m});
    if (top < 0.68f)
        return {};
    return top == a ? "ambition" : top == l ? "loyalty" : "money";
}

// --- the teams' plans -----------------------------------------------------------------------------------

// 1 = leads the constructors' points; teams not in this career (an optional team the game doesn't show) last
int rank_of(const State &st, const Team &t)
{
    std::vector<const Team *> order;
    for (const Team &x : st.teams)
        if (x.active)
            order.push_back(&x);
    std::stable_sort(order.begin(), order.end(), [](const Team *a, const Team *b) {
        return a->points != b->points ? a->points > b->points : a->performance > b->performance;
    });
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == &t)
            return static_cast<int>(i + 1);
    return static_cast<int>(st.teams.size());
}

// how a team builds, from its car, its brand, its money and where it stands
std::string pick_strategy(const State &st, const Team &t)
{
    bool scored = std::any_of(st.teams.begin(), st.teams.end(), [](const Team &x) { return x.points > 0; });
    int rank = scored ? rank_of(st, t) : 0;
    if (t.junior)
        return "youth";
    if (t.performance >= 85 || (scored && rank <= 2))
        return "title";
    if (t.budget < 80 && t.performance < 70)
        return "survival";
    if (t.prestige - t.performance >= 12 || (scored && rank >= 7 && t.prestige >= 75))
        return "rebuild";
    return "contend";
}

// --- deals ----------------------------------------------------------------------------------------------

Deal &add_deal(State &st, Deal deal)
{
    deal.id = st.next_deal_id++;
    st.deals.push_back(std::move(deal));
    return st.deals.back();
}

Deal *deal_by_id(State &st, int id)
{
    for (Deal &d : st.deals)
        if (d.id == id)
            return &d;
    return nullptr;
}

// --- the outlets --------------------------------------------------------------------------------------

void ensure_outlets(State &st)
{
    if (st.outlets.size() == static_cast<size_t>(text::kOutlets))
        return;
    st.outlets.resize(static_cast<size_t>(text::kOutlets));
    Rng r(mix(st.seed, 0x0de75ull));
    for (int i = 0; i < text::kOutlets; ++i) {
        State::Outlet &o = st.outlets[static_cast<size_t>(i)];
        // the starting reputation (1..5) is about right, give or take
        o.accuracy = clampf(0.15f + 0.17f * static_cast<float>(text::outlet_reliability(i) - 1) +
                                static_cast<float>(r.normal() * 0.05),
                            0.08f, 0.92f);
    }
}

// a story the outlet ran came true (official) or fell through
void outlet_scored(State &st, int source, bool hit)
{
    if (source < 0 || source >= static_cast<int>(st.outlets.size()))
        return;
    State::Outlet &o = st.outlets[static_cast<size_t>(source)];
    (hit ? o.hits : o.misses) += 1;
    (hit ? o.season_hits : o.season_misses) += 1;
}

// --- 1.1.0 phase 5: the team principals ---------------------------------------------------------------

// an invented name for the paddock's staff
std::string invent_person(Rng &r, double female)
{
    std::string nat = names::nationality(r.next());
    std::string letters = std::string(1, "BCDFGHKLMPRSTV"[r.next() % 14]) + "AEIOU"[r.next() % 5] + "LNRST"[r.next() % 5];
    return names::first(nat, r.chance(female), r.next()) + " " + names::surname(nat, letters, r.next());
}

// every team has a principal and a budget at the season's start (a new career, or a save from before 1.1.0)
void ensure_principals(State &st)
{
    for (Team &t : st.teams) {
        if (t.tp_name.empty()) {
            Rng tr(mix(st.seed, hash_str(t.id) ^ 0x7e4c4ull));
            t.tp_name = invent_person(tr, 0.12);
            t.tp_since = st.year - tr.range(0, 6);
        }
        if (t.budget_start <= 0)
            t.budget_start = t.budget;
    }
}

// 1.1.0 phase 6: each team's reserve at the start is the game's own (the best of the drivers the career lists as its
// reserves); a team the game gives none waits for the market's first reserve signings
void ensure_reserves(State &st)
{
    if (st.reserves_year != 0 || std::any_of(st.teams.begin(), st.teams.end(), [](const Team &t) { return !t.reserve.empty(); }))
        return;
    for (Team &t : st.teams) {
        const Driver *best = nullptr;
        for (const auto &[id, d] : st.drivers)
            if (d.reserve_of == t.id && d.team.empty() && !d.retired && (!best || value(d) > value(*best)))
                best = &d;
        if (best) {
            t.reserve = best->id;
            t.reserve_until = st.year;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Settings parsing

Intensity intensity_of(std::string_view s)
{
    if (s == "caos" || s == "chaos")
        return Intensity::chaos;
    if (s == "movido" || s == "lively")
        return Intensity::lively;
    return Intensity::realistic;
}

Rumours rumours_of(std::string_view s)
{
    if (s == "off" || s == "ninguno")
        return Rumours::off;
    if (s == "pocos" || s == "few")
        return Rumours::few;
    if (s == "muchos" || s == "many")
        return Rumours::many;
    return Rumours::normal;
}

NewsFilter news_of(std::string_view s)
{
    if (s == "ninguna" || s == "none")
        return NewsFilter::none;
    if (s == "oficiales" || s == "official")
        return NewsFilter::official;
    return NewsFilter::all;
}

const char *stage_name(Stage s)
{
    switch (s) {
    case Stage::rumour: return "rumour";
    case Stage::talks: return "talks";
    case Stage::agreed: return "agreed";
    case Stage::official: return "official";
    default: return "collapsed";
    }
}

static Stage stage_of(std::string_view s)
{
    if (s == "talks")
        return Stage::talks;
    if (s == "agreed")
        return Stage::agreed;
    if (s == "official")
        return Stage::official;
    if (s == "collapsed")
        return Stage::collapsed;
    return Stage::rumour;
}

// ---------------------------------------------------------------------------------------------------
// Model helpers

namespace {
float model_overall(const Driver &d)
{
    return 0.40f * d.pace + 0.25f * d.racecraft + 0.20f * d.awareness + 0.15f * d.experience;
}
} // namespace

// the game's own rating once it was read (it is what the player sees in the game); the market's model before
float Driver::overall() const { return game_overall > 0 ? game_overall : model_overall(*this); }

// the room to grow in the market's model, on top of what the game rates him now
float Driver::potential() const
{
    float model = 0.40f * std::max(pace, pace_peak) + 0.25f * std::max(racecraft, racecraft_peak) +
                  0.20f * std::max(awareness, awareness_peak) + 0.15f * std::max(experience, experience_peak);
    if (game_overall <= 0)
        return model;
    return std::max(game_overall, game_overall + model - model_overall(*this));
}

Team *State::team(std::string_view id)
{
    for (Team &t : teams)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Team *State::team(std::string_view id) const
{
    for (const Team &t : teams)
        if (t.id == id)
            return &t;
    return nullptr;
}

Driver *State::driver(std::string_view id)
{
    auto it = drivers.find(std::string(id));
    return it == drivers.end() ? nullptr : &it->second;
}

const Driver *State::driver(std::string_view id) const
{
    auto it = drivers.find(std::string(id));
    return it == drivers.end() ? nullptr : &it->second;
}

bool State::shielded(const std::string &id) const
{
    if (protected_ids.count(id))
        return true;
    const Driver *d = driver(id);
    if (d && !d->home.empty()) {
        const Team *t = team(d->home);
        return !t || !t->active;
    }
    return false;
}

bool State::locked(const Team &t, int seat, const Settings &s) const
{
    const std::string &who = t.seats[static_cast<size_t>(seat)];
    if (!player.empty() && who == player)
        return true;
    if (!player_team.empty() && t.id == player_team) {
        // the player's own seat is never touched; the team-mate's only when protected
        const std::string &other = t.seats[static_cast<size_t>(1 - seat)];
        if (s.protect_teammate)
            return true;
        if (who.empty() && other != player)
            return true; // the player's seat while the game hasn't told us who sits where
    }
    return false;
}

// --- 1.1.0 phase 6: who shares whose academy and reserve -----------------------------------------------------

std::string partner_family(const std::string &family)
{
    // Haas runs Ferrari's young drivers (Bearman's way in); add a pair here to give another team a partner
    if (family == "haas")
        return "ferrari";
    return {};
}

const Team *lead_team(const State &st, const Team &t)
{
    std::string fam = partner_family(t.family);
    if (fam.empty() && t.junior)
        fam = t.family;
    if (fam.empty())
        return &t;
    for (const Team &x : st.teams)
        if (&x != &t && x.family == fam && !x.junior && x.active)
            return &x;
    return &t;
}

std::string reserve_for(const State &st, const Team &t)
{
    if (!t.reserve.empty())
        return t.reserve;
    const Team *lead = lead_team(st, t);
    return lead ? lead->reserve : std::string();
}

bool in_academy(const Team &t, const Driver &d)
{
    if (d.academy.empty())
        return false;
    return d.academy == t.family || d.academy == partner_family(t.family);
}

const Team *reserve_team_of(const State &st, const std::string &driver)
{
    if (driver.empty())
        return nullptr;
    for (const Team &t : st.teams)
        if (t.reserve == driver)
            return &t;
    return nullptr;
}

static void set_seat(State &st, Team &t, int seat, const std::string &driver)
{
    std::string &slot = t.seats[static_cast<size_t>(seat)];
    if (!slot.empty())
        if (Driver *old = st.driver(slot)) {
            old->team.clear();
            old->seat = -1;
        }
    slot = driver;
    if (Driver *d = st.driver(driver)) {
        if (!d->team.empty() && (d->team != t.id || d->seat != seat))
            if (Team *prev = st.team(d->team))
                if (d->seat >= 0 && prev->seats[static_cast<size_t>(d->seat)] == driver)
                    prev->seats[static_cast<size_t>(d->seat)].clear();
        d->team = t.id;
        d->seat = seat;
    }
}

// ---------------------------------------------------------------------------------------------------
// Baseline

static Driver driver_from(const std::string &id, const Value &v)
{
    Driver d;
    d.id = id;
    d.name = std::string(v["name"].as_string(id));
    d.nat = std::string(v["nat"].as_string());
    d.kind = std::string(v["kind"].as_string("pool"));
    d.academy = std::string(v["academy"].as_string());
    d.born = static_cast<int>(v["born"].as_int(2000));
    d.stat = static_cast<int>(v["stat"].as_int(0));
    d.seasons = static_cast<int>(v["seasons"].as_int(0));
    d.pace = static_cast<float>(v["pace"].as_double(70));
    d.racecraft = static_cast<float>(v["racecraft"].as_double(70));
    d.awareness = static_cast<float>(v["awareness"].as_double(70));
    d.experience = static_cast<float>(v["experience"].as_double(50));
    d.initials = std::string(v["initials"].as_string());
    d.female = v["female"].as_bool();
    if (const json::Array *keys = v["name_keys"].if_array())
        for (const Value &k : *keys)
            d.name_keys.emplace_back(k.as_string());
    d.pace_peak = static_cast<float>(v["pace_peak"].as_double(d.pace));
    d.racecraft_peak = static_cast<float>(v["racecraft_peak"].as_double(d.racecraft));
    d.awareness_peak = static_cast<float>(v["awareness_peak"].as_double(d.awareness));
    d.experience_peak = static_cast<float>(v["experience_peak"].as_double(d.experience));
    d.home = std::string(v["home"].as_string());
    return d;
}

// a team of the baseline (careers.<group>.grid names it)
static Team team_from(const std::string &label, const Value &tv, uint64_t seed)
{
    Team t;
    t.id = label;
    t.name = std::string(tv["name"].as_string(label));
    t.code = std::string(tv["code"].as_string());
    t.family = std::string(tv["family"].as_string(label));
    t.stat = static_cast<int>(tv["stat"].as_int(0));
    t.prestige = static_cast<float>(tv["prestige"].as_double(70));
    t.performance = static_cast<float>(tv["performance"].as_double(70));
    t.patience = static_cast<float>(tv["patience"].as_double(0.5));
    t.junior = tv["junior"].as_bool();
    t.colour = static_cast<uint32_t>(tv["colour"].as_int(0x808080));
    t.budget = std::max(50.0f, t.prestige * 1.4f - 20.0f);
    t.optional = tv["optional"].as_bool();
    t.active = !t.optional; // an optional team joins once the game shows it
    Rng tr(mix(seed, hash_str(t.id)));
    std::string nat = names::nationality(tr.next());
    std::string ini = "ABCDEFGHKLMPRSTV";
    std::string letters = std::string(1, ini[tr.next() % ini.size()]) + "AEIOU"[tr.next() % 5] + "LNRST"[tr.next() % 5];
    t.td_name = names::first(nat, tr.chance(0.15), tr.next()) + " " + names::surname(nat, letters, tr.next());
    t.td_rating = clampf(t.performance + static_cast<float>(tr.normal() * 5.0), 55, 95);
    return t;
}

bool init(State &st, const Value &baseline, std::string_view group, uint64_t seed, std::string *error)
{
    const Value &career = baseline["careers"][group];
    if (!career.is_object()) {
        if (error)
            *error = "the baseline has no career " + std::string(group);
        return false;
    }
    st = State{};
    st.group = std::string(group);
    st.year = static_cast<int>(career["year"].as_int(2026));
    st.seed = seed ? seed : 1;
    st.rng = mix(st.seed, hash_str(group));

    std::set<std::string> wanted;
    if (const json::Object *grid = career["grid"].if_object())
        for (const auto &[team, pair] : *grid)
            for (int k = 0; k < 2; ++k)
                wanted.insert(std::string(pair[k].as_string()));
    if (const json::Array *pool = career["pool"].if_array())
        for (const Value &id : *pool)
            wanted.insert(std::string(id.as_string()));
    wanted.erase("");
    for (const std::string &id : wanted)
        st.drivers.emplace(id, driver_from(id, baseline["drivers"][id]));

    if (const json::Object *grid = career["grid"].if_object())
        for (const auto &[label, pair] : *grid) {
            st.teams.push_back(team_from(std::string(label), baseline["teams"][label], st.seed));
            Team &placed = st.teams.back();
            if (!placed.active)
                continue; // its drivers wait off the grid until the game shows the team
            for (int k = 0; k < 2; ++k) {
                std::string id(pair[k].as_string());
                if (!id.empty() && st.driver(id))
                    set_seat(st, placed, k, id);
            }
        }
    const Value &contracts = career["contracts"];
    Rng br(mix(st.seed, 0xbac4ull));
    for (auto &[id, d] : st.drivers) {
        d.reserve_of = std::string(career["reserves"][id].as_string());
        if (d.kind == "reserve" && br.chance(0.35))
            d.backing = static_cast<float>(br.range(3, 25)); // invented drivers only: sponsors of their own
        draw_traits(st.seed, st.year, d);
        if (d.team.empty())
            continue;
        int c = static_cast<int>(contracts[id].as_int(0));
        d.contract = c ? c : st.year + 1;
        d.salary = estimate_salary(d, st.team(d.team));
    }
    // an optional team's drivers hold the contracts of the baseline for when the team shows up
    for (Team &t : st.teams)
        if (!t.active)
            for (int k = 0; k < 2; ++k)
                if (Driver *d = st.driver(std::string(career["grid"][t.id][k].as_string()))) {
                    int c = static_cast<int>(contracts[d->id].as_int(0));
                    d->contract = c ? c : st.year + 1;
                }
    for (Team &t : st.teams)
        t.strategy = pick_strategy(st, t);
    ensure_outlets(st);
    ensure_principals(st);
    ensure_reserves(st);
    return true;
}

void refresh_from_baseline(State &st, const Value &baseline)
{
    const Value &career = baseline["careers"][st.group];
    if (const json::Array *pool = career["pool"].if_array())
        for (const Value &v : *pool) {
            std::string id(v.as_string());
            if (!id.empty() && !st.driver(id))
                st.drivers.emplace(id, driver_from(id, baseline["drivers"][id]));
        }
    // a team the baseline added after the career began (Konnersport): it waits until the game shows it
    if (const json::Object *grid = career["grid"].if_object())
        for (const auto &[label, pair] : *grid)
            if (!st.team(label)) {
                Team t = team_from(std::string(label), baseline["teams"][label], st.seed);
                t.active = false;
                t.strategy = pick_strategy(st, t);
                st.teams.push_back(t);
                for (int k = 0; k < 2; ++k) {
                    std::string id(pair[k].as_string());
                    if (!st.driver(id) && !id.empty())
                        st.drivers.emplace(id, driver_from(id, baseline["drivers"][id]));
                }
            }
    for (auto &[id, d] : st.drivers) {
        const Value &b = baseline["drivers"][id];
        if (d.home.empty() && !d.regen)
            d.home = std::string(b["home"].as_string());
        draw_traits(st.seed, st.year, d);
        if (d.reserve_of.empty() && !d.regen)
            d.reserve_of = std::string(career["reserves"][id].as_string());
        if (d.salary <= 0 && !d.team.empty())
            d.salary = estimate_salary(d, st.team(d.team));
        if (d.initials.empty())
            d.initials = std::string(b["initials"].as_string());
        if (!d.female)
            d.female = b["female"].as_bool();
        if (d.name_keys.empty())
            if (const json::Array *keys = b["name_keys"].if_array())
                for (const Value &k : *keys)
                    d.name_keys.emplace_back(k.as_string());
    }
    // a state saved before teams had colours and technical directors
    for (Team &t : st.teams) {
        const Value &tv = baseline["teams"][t.id];
        if (t.colour == 0x808080)
            t.colour = static_cast<uint32_t>(tv["colour"].as_int(0x808080));
        if (t.td_name.empty()) {
            Rng tr(mix(st.seed, hash_str(t.id)));
            std::string nat = names::nationality(tr.next());
            std::string letters = std::string(1, "BCDFGHKLMPRSTV"[tr.next() % 14]) + "AEIOU"[tr.next() % 5] +
                                  "LNRST"[tr.next() % 5];
            t.td_name = names::first(nat, false, tr.next()) + " " + names::surname(nat, letters, tr.next());
            t.td_rating = t.performance;
        }
    }
    ensure_principals(st); // a team added now (Konnersport) gets its principal too
}

// ---------------------------------------------------------------------------------------------------
// What the game reports

void sync_grid(State &st, const std::vector<GridCar> &shown)
{
    if (shown.empty())
        return;
    // a reserve standing in counts as the driver he replaces: the seat is still his
    std::vector<GridCar> cars = shown;
    for (GridCar &c : cars)
        for (const State::Standin &sb : st.standins)
            if (c.team == sb.team && c.driver == sb.in)
                c.driver = sb.out;
    std::map<std::string, std::vector<std::string>> by_team;
    for (const GridCar &c : cars) {
        if (c.human) {
            st.player = c.driver;
            st.player_team = c.team;
        }
        if (!st.team(c.team) || c.driver.empty())
            continue; // an empty seat says nothing (an optional team isn't shown by one)
        if (!st.driver(c.driver)) {
            Driver d;
            d.id = c.driver;
            d.name = c.driver;
            d.kind = c.human ? "player" : "pool";
            draw_traits(st.seed, st.year, d);
            st.drivers.emplace(c.driver, d);
        }
        by_team[c.team].push_back(c.driver);
    }
    // everyone off, then seat the grid as shown, keeping each driver's seat index where possible
    std::map<std::string, std::array<std::string, 2>> old;
    for (Team &t : st.teams)
        old[t.id] = t.seats;
    for (auto &[id, d] : st.drivers) {
        d.team.clear();
        d.seat = -1;
    }
    for (Team &t : st.teams) {
        auto it = by_team.find(t.id);
        t.seats = {"", ""};
        if (t.optional)
            t.active = it != by_team.end(); // the game shows the team (Konnersport) or doesn't
        if (it == by_team.end())
            continue;
        std::vector<std::string> rest;
        for (const std::string &who : it->second) {
            int k = old[t.id][0] == who ? 0 : old[t.id][1] == who ? 1 : -1;
            if (k >= 0 && t.seats[static_cast<size_t>(k)].empty())
                t.seats[static_cast<size_t>(k)] = who;
            else
                rest.push_back(who);
        }
        for (const std::string &who : rest)
            for (int k = 0; k < 2; ++k)
                if (t.seats[static_cast<size_t>(k)].empty()) {
                    t.seats[static_cast<size_t>(k)] = who;
                    break;
                }
        for (int k = 0; k < 2; ++k)
            if (Driver *d = st.driver(t.seats[static_cast<size_t>(k)])) {
                d->team = t.id;
                d->seat = k;
                if (d->contract < st.year)
                    d->contract = st.year; // on the grid this season: at least a one-year deal
                d->released = false;       // sacked by the market but still racing (1.0.1: the game kept him): not free
                d->retired = false;        // retired by the market, but the game refused his successor: he races on
            }
    }
}

void observe_rating(State &st, std::string_view driver, int stat, float v)
{
    Driver *d = st.driver(driver);
    if (!d || v <= 0 || v > 100)
        return;
    switch (stat) {
    case 0: d->experience = v; break;
    case 1: d->racecraft = v; break;
    case 2: d->awareness = v; break;
    case 3: d->pace = v; break;
    default: return;
    }
    d->ratings_seen = true;
}

void record_result(State &st, std::string_view driver, int position, int round)
{
    Driver *d = st.driver(driver);
    // the player counts even in his own My Team team, which the model doesn't hold (the awards)
    if (!d || (d->team.empty() && (d->id != st.player || st.player.empty()) && (d->id != st.own_mate || st.own_mate.empty())))
        return;
    static const int points[] = {25, 18, 15, 12, 10, 8, 6, 4, 2, 1};
    int pts = position >= 1 && position <= 10 ? points[position - 1] : 0;
    // the race, for the team-mate duel: where he finished and for whom
    if (round <= 0)
        round = st.round + 1;
    if (round >= 1 && round <= 40) {
        size_t i = static_cast<size_t>(round - 1);
        if (d->race_pos.size() <= i) {
            d->race_pos.resize(i + 1, 0);
            d->race_team.resize(i + 1);
        }
        d->race_pos[i] = position >= 1 ? position : -1;
        d->race_team[i] = d->team.empty() ? std::string("#own") : d->team;
    }
    if (d->starts == 0)
        d->value_start = value(*d);
    d->points += pts;
    d->starts += 1;
    d->wins += position == 1 ? 1 : 0;
    d->podiums += position >= 1 && position <= 3 ? 1 : 0;
    if (d->team.empty())
        return;
    if (Team *t = st.team(d->team))
        t->points += pts;
    // expected finish from the driver's value among the grid
    int better = 0, total = 0;
    float mine = d->overall();
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *o = st.driver(who)) {
                ++total;
                if (o->overall() + 0.5f * (t.performance - 70.0f) > mine + 0.5f * (st.team(d->team)->performance - 70.0f))
                    ++better;
            }
    if (position <= 0)
        position = total ? total : 20; // a retirement counts as last
    float expected = static_cast<float>(better + 1);
    float delta = clampf((expected - static_cast<float>(position)) / 8.0f, -1, 1);
    d->form = clampf(0.75f * d->form + 0.25f * delta, -1, 1);
}

// ---------------------------------------------------------------------------------------------------
// The market planner

namespace {

struct Plan {
    std::map<std::string, std::array<std::string, 2>> grid;
    std::vector<Move> moves;                 // transfer, renewal, promotion, comeback, left
    std::map<std::string, int> until;        // new contract end for each signed driver
    std::map<std::string, std::string> kind; // driver -> kind of his move
    std::vector<std::pair<std::string, std::string>> refusals; // driver, team: offers turned down
    std::set<std::string> buyouts;           // "driver|team" refusals of a buy-out (he stays loyal)
};

bool eligible(const Driver &d, const State &st, const Settings &s)
{
    if (d.retired || d.retiring || d.id == st.player || d.kind == "player" || st.shielded(d.id))
        return false;
    if (d.kind == "legend" && !s.legends)
        return false;
    return true;
}

float appeal_with(const State &st, const Driver &d, const Team &t, float amb, float loy, float mon);

// what the seat says to the driver over what he expects: > 0 he wants it, < 0 he'd rather not. With the
// personalities off, every driver weighs a seat the same way.
float interest(const State &st, const Driver &d, const Team &t, const Settings &s)
{
    bool on = s.personality && d.ambition >= 0;
    float amb = on ? d.ambition : 0.5f, loy = on ? d.loyalty : 0.5f, mon = on ? d.money : 0.4f;
    float demand = 50.0f + std::max(0.0f, value(d) - 70.0f) * 1.3f * amb;
    return appeal_with(st, d, t, amb, loy, mon) - demand;
}

float seat_score(const State &st, const Settings &s, const Team &t, const Driver &d, const std::string &mate, Rng &r)
{
    float v = value(d);
    float score = v;
    int age = d.age(st.year + 1);
    bool rookie = d.kind == "f2" || d.kind == "reserve" || d.kind == "pool";
    // the team's plan: a title push wants proven drivers now, a rebuild or a youth team wants upside
    // without the team plans, the old rule: a top car wants proven drivers, a second team young ones
    const std::string plan = s.strategies ? t.strategy : t.performance >= 85 ? "title" : t.junior ? "youth" : "contend";
    bool top = plan == "title";
    bool young_plan = plan == "rebuild" || plan == "youth";
    float stay = s.intensity == Intensity::chaos ? 2.0f : s.intensity == Intensity::lively ? 4.0f : 6.0f;
    if (d.team == t.id) {
        score += stay; // continuity
        if (s.personality && d.loyalty >= 0)
            score += (d.loyalty - 0.5f) * 6.0f; // a loyal driver is easy to keep
    }
    if (s.academies && in_academy(t, d)) {
        // the team's own junior counts for more; a family with a second team or a partner (Racing Bulls, Haas) breaks
        // its rookies in there before the senior team
        bool feeder = t.junior || !partner_family(t.family).empty();
        // it opens the door: a driver already racing for the team stays on his own merits (and his character)
        score += d.team == t.id ? 3.0f : feeder ? 12.0f : 9.0f;
        bool has_feeder = std::any_of(st.teams.begin(), st.teams.end(), [&](const Team &x) {
            return x.active && &x != &t && ((x.junior && x.family == t.family) || partner_family(x.family) == t.family);
        });
        if (rookie && d.seasons == 0) {
            if (has_feeder && !t.junior)
                score -= 8.0f;
            if (feeder)
                score += 4.0f;
        }
    } else if (!d.academy.empty() && d.academy == t.family) {
        score += t.junior ? 10.0f : 6.0f; // the team's own junior (1.1.0's first blocks)
    }
    if (!d.team.empty() && d.team != t.id)
        if (const Team *cur = st.team(d.team))
            if (cur->family == t.family && cur->junior)
                score += 5.0f; // promotion from the second team (Racing Bulls -> Red Bull)
    if (age <= 24) {
        float upside = std::max(0.0f, d.potential() - d.overall());
        score += upside * (t.junior ? 0.9f : young_plan ? 0.8f : top ? 0.25f : plan == "survival" ? 0.4f : 0.55f);
        if (rookie && !top)
            score += t.junior ? 6.0f : young_plan ? 4.5f : 3.0f; // teams outside the top hunt for talent
    }
    if (t.junior && age > 26)
        score -= static_cast<float>(age - 26) * 2.5f; // the second team is for young drivers
    if (d.kind == "legend")
        score -= s.intensity == Intensity::chaos ? 0.0f : 8.0f;
    else if (age >= 33)
        score -= static_cast<float>(age - 32) * (top ? 1.0f : young_plan ? 2.2f : 1.3f);
    else if (plan == "rebuild" && age >= 30)
        score -= static_cast<float>(age - 29) * 0.8f;
    if (rookie && top)
        score -= 5.0f; // top teams want proven drivers
    // the driver's side: a seat he wants is easier to land, one below his standing harder
    if (s.personality && d.ambition >= 0 && !rookie)
        score += clampf(interest(st, d, t, s), -12.0f, 8.0f) * 0.3f;
    if (const Driver *m = st.driver(mate)) {
        // pair a rookie with experience, and the other way round
        if (m->experience < 55 && d.experience > 75)
            score += 2.0f;
        if (m->experience > 85 && d.age(st.year + 1) <= 23)
            score += 1.0f;
        // two drivers who can't stand each other never share a garage
        if (std::find(d.feuds.begin(), d.feuds.end(), m->id) != d.feuds.end() ||
            std::find(m->feuds.begin(), m->feuds.end(), d.id) != m->feuds.end())
            score -= 15.0f;
    }
    // a poor team likes a driver who brings sponsors; one fighting to survive loves him
    if (t.budget < 95.0f && d.backing > 0)
        score += d.backing * (95.0f - t.budget) / 60.0f * (plan == "survival" ? 1.6f : 1.0f);
    // the F2 podium gets noticed
    if (d.f2_pos == 1)
        score += 6.0f;
    else if (d.f2_pos == 2)
        score += 4.0f;
    else if (d.f2_pos == 3)
        score += 3.0f;
    else if (d.f2_pos > 0 && d.f2_pos <= 6)
        score += 1.0f;
    // the player asked for him in the press
    if (t.id == st.player_team && d.id == st.teammate_wish)
        score += 8.0f + 2.0f * static_cast<float>(std::max(0, st.team_comfort)); // a team happy with him listens more
    // the player asked his team for reinforcements: proven drivers move up its list
    if (t.id == st.player_team && st.push_signing && !rookie)
        score += std::max(0.0f, v - 80.0f) * 0.4f;
    score += static_cast<float>(r.normal() * noise_sigma(s.intensity));
    return score;
}

float team_floor(const Team &t) { return 0.60f * t.performance + 0.25f * t.prestige + 8.0f; }

std::string move_kind(const State &st, const Driver &d, const Team &to)
{
    if (d.team == to.id)
        return "renewal";
    if (!d.team.empty())
        return "transfer";
    if (d.kind == "f2" || d.kind == "reserve" || (d.kind == "pool" && d.age(st.year + 1) <= 24))
        return "promotion";
    return "comeback";
}

Plan plan_market(const State &st, const Settings &s, uint64_t seed)
{
    Rng r(seed);
    Plan plan;
    std::set<std::string> assigned;
    std::set<std::string> by_contract; // drivers held by contract (poachable)
    for (const Team &t : st.teams)
        plan.grid[t.id] = {"", ""};

    // 1. locked seats (the player, a protected team-mate)
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k)
            if (st.locked(t, k, s) && !t.seats[static_cast<size_t>(k)].empty()) {
                plan.grid[t.id][static_cast<size_t>(k)] = t.seats[static_cast<size_t>(k)];
                assigned.insert(t.seats[static_cast<size_t>(k)]);
            }

    // 2. deals already agreed or official
    for (const Deal &deal : st.deals) {
        if (deal.fake || (deal.stage != Stage::agreed && deal.stage != Stage::official) || assigned.count(deal.driver))
            continue;
        const Team *t = st.team(deal.team);
        const Driver *d = st.driver(deal.driver);
        if (!t || !d || !eligible(*d, st, s) || !st.seatable(d->id))
            continue;
        auto &g = plan.grid[t->id];
        int k = (d->team == t->id && d->seat >= 0 && g[static_cast<size_t>(d->seat)].empty() && !st.locked(*t, d->seat, s))
                    ? d->seat
                : g[0].empty() && !st.locked(*t, 0, s) ? 0
                : g[1].empty() && !st.locked(*t, 1, s) ? 1
                                                        : -1;
        if (k < 0)
            continue;
        g[static_cast<size_t>(k)] = d->id;
        assigned.insert(d->id);
        plan.until[d->id] = deal.until;
        plan.kind[d->id] = deal.kind;
    }

    // 3. drivers under contract for next season keep their seats
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            const std::string &who = t.seats[static_cast<size_t>(k)];
            const Driver *d = st.driver(who);
            if (!d || assigned.count(who) || !plan.grid[t.id][static_cast<size_t>(k)].empty())
                continue;
            if (d->retiring || d->retired || d->released || d->contract <= st.year || d->wants_out)
                continue; // wants_out: an exit clause or a feud lets him go despite the contract
            plan.grid[t.id][static_cast<size_t>(k)] = who;
            assigned.insert(who);
            by_contract.insert(who);
        }

    // 4. fill the open seats, best teams first; a driver held by contract can be bought out by a
    //    clearly better team, which opens his seat in turn
    struct Open {
        std::string team;
        int seat;
    };
    std::vector<Open> open;
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k)
            if (t.active && plan.grid[t.id][static_cast<size_t>(k)].empty())
                open.push_back({t.id, k});
    std::set<std::pair<std::string, std::string>> declined; // driver, team: he said no to this team
    // a driver may turn a seat down while a seat he'd rather have is still open
    auto better_open = [&](const Driver &d, const Team &offer) {
        float here = interest(st, d, offer, s);
        for (const Open &o : open)
            if (o.team != offer.id)
                if (const Team *x = st.team(o.team))
                    if (interest(st, d, *x, s) > here + 6.0f && interest(st, d, *x, s) >= -2.0f)
                        return true;
        return false;
    };
    int guard = 0;
    while (!open.empty() && guard++ < 260) {
        auto best_it = std::max_element(open.begin(), open.end(), [&](const Open &a, const Open &b) {
            return st.team(a.team)->attraction() < st.team(b.team)->attraction();
        });
        Open seat = *best_it;
        open.erase(best_it);
        const Team &t = *st.team(seat.team);
        const std::string mate = plan.grid[t.id][static_cast<size_t>(1 - seat.seat)];

        const Driver *pick = nullptr;
        float pick_score = -1e9f;
        bool pick_poached = false;
        for (const auto &[id, d] : st.drivers) {
            // the game takes race drivers and free agents only: a reserve would be refused at the new season
            if (!eligible(d, st, s) || declined.count({id, t.id}) || !st.seatable(id))
                continue;
            bool poach = false;
            if (assigned.count(id)) {
                if (!by_contract.count(id) || d.team.empty())
                    continue;
                const Team *cur = st.team(d.team);
                if (!cur || cur->id == t.id || t.attraction() - cur->attraction() < 10.0f || value(d) < 84.0f)
                    continue;
                poach = true;
            }
            float sc = seat_score(st, s, t, d, mate, r);
            if (sc < team_floor(t) - 14.0f)
                continue;
            if (poach)
                sc -= 3.0f;
            if (sc > pick_score) {
                pick = &d;
                pick_score = sc;
                pick_poached = poach;
            }
        }
        bool fallback = !pick;
        if (!pick) {
            // nobody meets the bar: take the best free driver at all
            for (const auto &[id, d] : st.drivers)
                if (eligible(d, st, s) && !assigned.count(id) && st.seatable(id)) {
                    float sc = value(d);
                    if (sc > pick_score) {
                        pick = &d;
                        pick_score = sc;
                    }
                }
            if (!pick)
                continue;
        }
        if (pick_poached) {
            // a loyal driver is hard to buy out; an ambitious one jumps at a better car
            const Team *cur = st.team(pick->team);
            float pull = cur ? interest(st, *pick, t, s) - interest(st, *pick, *cur, s) : 0.0f;
            double p = poach_chance(s.intensity);
            if (s.personality && pick->ambition >= 0)
                p = clampf(static_cast<float>(p) + (pick->ambition - 0.5f) * 0.4f - (pick->loyalty - 0.5f) * 0.5f +
                               clampf(pull, -10, 10) * 0.01f,
                           0.03f, 0.9f);
            if (!r.chance(p)) {
                // the buy-out fails: the team keeps searching for this seat among the rest
                if (s.personality && pick->loyalty >= 0.6f) {
                    plan.refusals.push_back({pick->id, t.id});
                    plan.buyouts.insert(pick->id + "|" + t.id);
                }
                open.push_back(seat);
                by_contract.erase(pick->id); // not poachable again this pass
                continue;
            }
            // open the seat he leaves
            for (auto &[team_id, g] : plan.grid)
                for (int k = 0; k < 2; ++k)
                    if (g[static_cast<size_t>(k)] == pick->id) {
                        g[static_cast<size_t>(k)].clear();
                        open.push_back({team_id, k});
                    }
            by_contract.erase(pick->id);
        } else if (s.personality && !fallback && !pick->team.empty() && pick->team != t.id && pick->ambition >= 0.6f &&
                   pick->kind != "legend" && interest(st, *pick, t, s) < -6.0f && better_open(*pick, t)) {
            // an ambitious driver turns down a seat below his standing while a better one is still open
            plan.refusals.push_back({pick->id, t.id});
            declined.insert({pick->id, t.id});
            open.push_back(seat);
            continue;
        }
        plan.grid[t.id][static_cast<size_t>(seat.seat)] = pick->id;
        assigned.insert(pick->id);
        std::string kind = move_kind(st, *pick, t);
        plan.kind[pick->id] = kind;
        if (!plan.until.count(pick->id)) {
            Rng lr(mix(seed, hash_str(pick->id)));
            plan.until[pick->id] = st.year + contract_length(lr, *pick, st.year + 1, s.personality);
        }
    }

    // 5. the moves this plan makes
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            const std::string &who = plan.grid[t.id][static_cast<size_t>(k)];
            const Driver *d = st.driver(who);
            if (!d || st.locked(t, k, s))
                continue;
            auto it = plan.kind.find(who);
            if (it == plan.kind.end())
                continue;
            plan.moves.push_back({st.year, st.round, who, d->team, t.id, it->second});
        }
    for (const Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            const std::string &who = t.seats[static_cast<size_t>(k)];
            if (who.empty() || assigned.count(who) || st.locked(t, k, s))
                continue;
            const Driver *d = st.driver(who);
            plan.moves.push_back({st.year, st.round, who, t.id, "", d && d->retiring ? "retirement" : "left"});
        }
    return plan;
}

uint64_t plan_seed(const State &st, bool final)
{
    return mix(mix(st.seed, static_cast<uint64_t>(st.year)), final ? 0xf17a1ull : 0x7e57ull);
}

Deal *find_deal(State &st, std::string_view driver)
{
    for (Deal &d : st.deals)
        if (d.driver == driver && d.stage != Stage::collapsed)
            return &d;
    return nullptr;
}

const Move *plan_move(const Plan &plan, std::string_view driver)
{
    for (const Move &m : plan.moves)
        if (m.driver == driver)
            return &m;
    return nullptr;
}

void announce_official(State &st, const Settings &s, Rng &r, Deal &deal, int round, bool real = false, bool swoop = false)
{
    deal.stage = Stage::official;
    deal.waits.clear();
    if (deal.kind == "renewal")
        return; // renewals go in a summary
    const Driver *d = st.driver(deal.driver);
    std::string from = d && !d->team.empty() ? team_name(st, d->team) : "";
    bool bomb = d && value(*d) >= 88 && deal.kind == "transfer";
    // a real move is a bombshell only when the star goes to a car at least as good as his
    if (real && bomb && d) {
        const Team *from_t = st.team(d->team), *to_t = st.team(deal.team);
        bomb = from_t && to_t && to_t->performance >= from_t->performance;
    }
    int pick_sign = r.range(0, 9);
    bool quote = d && r.chance(0.6);
    int pick_quote = quote ? r.range(0, 9) : 0;
    // ambitious drivers sign with a way out if the team doesn't deliver
    const Team *to = st.team(deal.team);
    if (!real && d && to && (deal.kind == "transfer" || deal.kind == "comeback") && d->overall() >= 82 &&
        r.chance(s.personality && d->ambition >= 0 ? 0.1 + 0.4 * d->ambition : 0.3))
        deal.clause = to->performance >= 85 ? 3 : to->performance >= 72 ? 5 : 7;
    // what kind of driver he is, now and then
    std::string trait = d && !real && s.personality && r.chance(0.45) ? main_trait(*d) : std::string();
    std::string from_id = d ? d->team : std::string();
    int pick_trait = r.range(0, 9);
    auto body = [&](text::Lang l) {
        std::string b = text::signing(l, pick_sign, driver_name(st, deal.driver), team_name(st, deal.team), from, deal.until,
                                      deal.kind);
        if (quote)
            b += " " + text::boss_quote(l, pick_quote, d->name, team_name(st, deal.team), deal.kind);
        if (!trait.empty())
            b += " " + text::trait_line(l, pick_trait, trait, d->name);
        if (d && deal.kind == "transfer" && d->overall() >= 80)
            b += " " + text::salary_line(l, estimate_salary(*d, st.team(deal.team)));
        if (d && d->backing >= 5)
            b += " " + text::backing_line(l, d->backing);
        if (deal.clause > 0)
            b += " " + text::clause_line(l, deal.clause);
        return b;
    };
    add_news(st, round, "official",
             [&](text::Lang l) {
                 return swoop ? text::h_swoop(l, driver_name(st, deal.driver), team_name(st, deal.team))
                              : text::h_signing(l, pick_sign, driver_name(st, deal.driver), team_name(st, deal.team), bomb);
             },
             body, true);
    tag_news(st, deal.driver, deal.team, from_id, deal.until, bomb || (swoop && d && value(*d) >= 85));
}

// the seat a deal leads to is still held by a driver under contract whose exit isn't official: the domino
// waits for him. Returns that driver, or "" when the deal is free to go.
// a driver of this team whose exit is known: retiring, released, out of contract, or signed elsewhere
bool exit_known(const State &st, const Driver &o, const std::string &team)
{
    if (o.retiring || o.released || o.contract <= st.year)
        return true;
    for (const Deal &x : st.deals)
        if (x.driver == o.id && x.stage == Stage::official && !x.fake && x.team != team)
            return true;
    return false;
}

// a driver of the team now whom the plan has elsewhere next season (whichever seat he sits in)
std::string leaving(const State &st, const Plan &plan, const std::string &team, const std::string &except, bool known)
{
    const Team *t = st.team(team);
    auto it = plan.grid.find(team);
    if (!t || it == plan.grid.end())
        return {};
    for (const std::string &who : t->seats) {
        const Driver *o = st.driver(who);
        if (!o || who == except || it->second[0] == who || it->second[1] == who)
            continue;
        if (exit_known(st, *o, team) == known)
            return who;
    }
    return {};
}

std::string blocker(const State &st, const Plan &plan, const Deal &deal)
{
    const Team *t = st.team(deal.team);
    auto it = plan.grid.find(deal.team);
    if (!t || it == plan.grid.end() || (it->second[0] != deal.driver && it->second[1] != deal.driver))
        return {};
    // a seat that is free already, his own, or one whose driver's exit is known
    for (const std::string &who : t->seats) {
        const Driver *o = st.driver(who);
        if (!o || who == deal.driver || exit_known(st, *o, t->id))
            return {};
    }
    // both seats held: the driver the plan moves out must go first
    return leaving(st, plan, t->id, deal.driver, false);
}

// the outlets' record: every story that came true or fell through counts once
void score_outlets(State &st)
{
    for (Deal &d : st.deals) {
        if (d.scored || d.source < 0 || (d.stage != Stage::official && d.stage != Stage::collapsed))
            continue;
        d.scored = true;
        outlet_scored(st, d.source, d.stage == Stage::official && !d.fake);
    }
}

// a transfer went official: the seat the driver leaves opens, and the plan's pick for it moves at once
int dominoes(State &st, const Settings &s, Rng &r, const Plan &plan, const std::vector<std::pair<int, std::string>> &opened,
             int round, int budget)
{
    int told = 0;
    for (const auto &[deal_id, from] : opened) {
        if (told >= budget)
            break;
        const Deal *trigger = deal_by_id(st, deal_id);
        const Team *t = st.team(from);
        auto it = plan.grid.find(from);
        if (!trigger || !t || it == plan.grid.end())
            continue;
        std::string leaver = trigger->driver;
        const Driver *ld = st.driver(leaver);
        if (!ld || ld->team != from)
            continue;
        // who the plan brings in at that team (whichever seat the plan gives him)
        std::string next;
        for (const std::string &who : it->second)
            if (const Driver *x = st.driver(who); x && x->team != from && who != leaver)
                next = who;
        const Driver *nd = st.driver(next);
        if (!nd || next == st.player)
            continue;
        auto kind = plan.kind.find(next);
        if (kind == plan.kind.end() || kind->second == "renewal")
            continue;
        Deal *existing = find_deal(st, next);
        if (existing && (existing->fake || existing->team != from))
            continue; // a story about him elsewhere: the plan will sort it out
        int chain_id = trigger->id;
        if (existing) {
            existing->chain = chain_id;
            if (existing->stage != Stage::rumour)
                continue; // already in talks or agreed: nothing new to tell
            existing->stage = Stage::talks;
        } else {
            auto until = plan.until.find(next);
            Deal fresh{next, from, kind->second, st.year + 1, until != plan.until.end() ? until->second : st.year + 2, Stage::talks,
                       false, round};
            fresh.chain = chain_id;
            add_deal(st, fresh);
        }
        std::string leaver_name = driver_name(st, leaver), team = t->name, next_name = driver_name(st, next);
        std::string next_from = nd->team.empty() ? std::string() : team_name(st, nd->team);
        int pick = r.range(0, 9);
        add_news(st, round, "domino", [&](text::Lang l) { return text::h_domino(l, pick, next_name, team); },
                 [&](text::Lang l) { return text::domino(l, pick, leaver_name, team, next_name, next_from); }, false);
        tag_news(st, next, from, nd->team);
        ++told;
    }
    (void)s;
    return told;
}

// offers turned down, told once a season: "X says no to Y"
void refusal_news(State &st, const Settings &s, Rng &r, const Plan &plan, int round, double progress)
{
    if (progress < 0.25 || s.rumours == Rumours::off)
        return;
    for (const auto &[driver, team] : plan.refusals) {
        std::string key = driver + "|" + team;
        if (st.refusals_told.count(key) || driver == st.player)
            continue;
        const Driver *d = st.driver(driver);
        const Team *t = st.team(team);
        if (!d || !t || !r.chance(0.5))
            continue;
        st.refusals_told.insert(key);
        bool buyout = plan.buyouts.count(key) != 0;
        std::string name = d->name, tname = t->name, from = d->team.empty() ? std::string() : team_name(st, d->team);
        std::string trait = main_trait(*d);
        if (trait.empty())
            trait = buyout ? "loyalty" : "ambition";
        int pick = r.range(0, 9);
        add_news(st, round, "refusal", [&](text::Lang l) { return text::h_refusal(l, pick, name, tname); },
                 [&](text::Lang l) { return text::refusal(l, pick, name, tname, from, buyout, trait); }, false);
        tag_news(st, driver, team, d->team);
        return; // one a round
    }
}

// the deadline day opens: the seats still open with two races to go
void deadline_news(State &st, const Settings &s, int round)
{
    st.deadline_told = true;
    auto g = projected_grid(st, s);
    std::string list;
    int open = 0;
    for (const Team &t : st.teams) {
        if (!t.active)
            continue;
        for (const std::string &who : g[t.id])
            if (who.empty()) {
                ++open;
                if (list.find(t.name) == std::string::npos)
                    list += (list.empty() ? "" : ", ") + t.name;
            }
    }
    int left = deadline_races_left(st);
    int year = st.year + 1;
    add_news(st, round, "deadline", [&](text::Lang l) { return text::h_deadline(l, left); },
             [&](text::Lang l) { return text::deadline(l, year, left, open, list); }, true);
}

// rumours, talks and deals for one round
void market_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (round < 2)
        return; // the opening race belongs to the season preview
    Plan plan = plan_market(st, s, plan_seed(st, false));
    int dl = s.deadline ? deadline_races_left(st) : 0;
    std::vector<std::pair<int, std::string>> opened; // transfers made official this round: deal id, the team left

    // existing deals move on, cool off or turn out to be nothing
    for (Deal &deal : st.deals) {
        if (deal.stage == Stage::official || deal.stage == Stage::collapsed)
            continue;
        // rumours live a few weekends: the board should never be empty mid-season
        if (deal.fake) {
            if (r.chance(dl ? 0.45 : round - deal.opened >= 2 ? 0.22 : 0.08)) {
                deal.stage = Stage::collapsed;
                if (s.rumours != Rumours::off)
                    { int pick0 = r.range(0, 9);
                    add_news(st, round, "collapse",
                 [&](text::Lang l) { return text::h_collapse(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team), true); },
                 [&](text::Lang l) { return std::string(text::collapse(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team), true)); }, false);
                    tag_news(st, deal.driver, deal.team); }
            }
            continue;
        }
        const Move *m = plan_move(plan, deal.driver);
        bool still = m && m->to == deal.team;
        if (!still && deal.stage < Stage::agreed) {
            // early plans shift with every result: a talk only dies once the shift has lasted
            if (r.chance(dl ? 0.6 : progress < 0.4 ? 0.18 : 0.4)) {
                deal.stage = Stage::collapsed;
                if (s.rumours != Rumours::off)
                    { int pick0 = r.range(0, 9);
                    add_news(st, round, "collapse",
                 [&](text::Lang l) { return text::h_collapse(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team), false); },
                 [&](text::Lang l) { return std::string(text::collapse(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team), false)); }, false);
                    tag_news(st, deal.driver, deal.team); }
            }
            continue;
        }
        if (deal.stage == Stage::rumour && progress >= 0.2 && round - deal.opened >= 1 && r.chance(dl ? 0.6 : 0.35)) {
            deal.stage = Stage::talks;
            if (s.rumours != Rumours::off)
                { int pick0 = r.range(0, 9);
                add_news(st, round, "talks",
                 [&](text::Lang l) { return text::h_talks(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team)); },
                 [&](text::Lang l) { return std::string(text::talks(l, pick0, driver_name(st, deal.driver), team_name(st, deal.team))); }, false);
                tag_news(st, deal.driver, deal.team); }
        } else if (deal.stage == Stage::talks && progress >= 0.50 && r.chance(dl ? 0.6 : 0.30)) {
            deal.stage = Stage::agreed;
        } else if (deal.stage == Stage::agreed && progress >= 0.60 && !st.observer) {
            // an observer career's deals stay agreed: the game's grid says whether they happen (observe_grid)
            // the domino: the seat's driver must be gone first
            deal.waits = blocker(st, plan, deal);
            if (deal.waits.empty() && r.chance(dl ? 0.75 : 0.35)) {
                const Driver *d = st.driver(deal.driver);
                std::string from = d ? d->team : std::string();
                announce_official(st, s, r, deal, round);
                if (deal.kind == "transfer" && !from.empty())
                    opened.push_back({deal.id, from});
            }
        }
    }

    // the seats those signings leave open move next (references into st.deals end here: dominoes add deals)
    dominoes(st, s, r, plan, opened, round, dl ? 3 : 2);
    refusal_news(st, s, r, plan, round, progress);

    // renewals of the drivers the plan keeps, announced along the way
    if (progress >= 0.55 && !st.observer)
        for (const Move &m : plan.moves)
            if (m.kind == "renewal" && !find_deal(st, m.driver) && r.chance(dl ? 0.35 : 0.18)) {
                Deal deal{m.driver, m.to, "renewal", st.year + 1, 0, Stage::official, false, round};
                Rng lr(mix(st.seed, hash_str(m.driver) ^ static_cast<uint64_t>(st.year)));
                deal.until = st.year + contract_length(lr, *st.driver(m.driver), st.year + 1, s.personality);
                add_deal(st, deal);
                { int pick0 = r.range(0, 9);
                add_news(st, round, "official",
                 [&](text::Lang l) { return text::h_renewal(l, pick0, driver_name(st, m.driver), team_name(st, m.to)); },
                 [&](text::Lang l) { return std::string(text::renewal(l, pick0, driver_name(st, m.driver), team_name(st, m.to), deal.until)); }, true);
                tag_news(st, m.driver, m.to, m.to, deal.until); }
            }

    // deadline day: one move nobody saw coming, signed on the spot
    if (dl && !st.observer && r.chance(s.intensity == Intensity::chaos ? 0.8 : s.intensity == Intensity::lively ? 0.55 : 0.35)) {
        std::vector<const Move *> pool;
        for (const Move &m : plan.moves)
            if ((m.kind == "transfer" || m.kind == "comeback" || m.kind == "promotion") && !find_deal(st, m.driver) &&
                m.driver != st.player)
                pool.push_back(&m);
        if (!pool.empty()) {
            const Move &m = *pool[static_cast<size_t>(r.range(0, static_cast<int>(pool.size()) - 1))];
            auto until = plan.until.find(m.driver);
            Deal fresh{m.driver, m.to, m.kind, st.year + 1, until != plan.until.end() ? until->second : st.year + 2, Stage::agreed,
                       false, round};
            Deal probe = fresh;
            if (blocker(st, plan, probe).empty()) {
                Deal &deal = add_deal(st, fresh);
                const Driver *d = st.driver(m.driver);
                std::string from = d ? d->team : std::string();
                int id = deal.id;
                announce_official(st, s, r, deal, round, false, true);
                if (m.kind == "transfer" && !from.empty())
                    dominoes(st, s, r, plan, {{id, from}}, round, 1);
            }
        }
    }

    // new rumours, until the last couple of races
    if (s.rumours == Rumours::off || progress > 0.92)
        return;
    // fewer rumours early on, the full flow from a third of the season
    double ramp = std::min(1.0, 0.35 + progress * 2.0);
    double p1 = ramp * (s.rumours == Rumours::many ? 0.9 : s.rumours == Rumours::normal ? 0.7 : 0.4);
    double p2 = ramp * (s.rumours == Rumours::many ? 0.5 : s.rumours == Rumours::normal ? 0.25 : 0.0);
    int count = (r.chance(p1) ? 1 : 0) + (r.chance(p2) ? 1 : 0);
    double fake_share = s.intensity == Intensity::chaos ? 0.5 : s.intensity == Intensity::lively ? 0.35 : 0.25;
    for (int i = 0; i < count; ++i) {
        if (!r.chance(fake_share)) {
            std::vector<const Move *> pool;
            for (const Move &m : plan.moves)
                if ((m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback") && !find_deal(st, m.driver))
                    pool.push_back(&m);
            if (!pool.empty()) {
                const Move &m = *pool[static_cast<size_t>(r.range(0, static_cast<int>(pool.size()) - 1))];
                Deal deal{m.driver, m.to, m.kind, st.year + 1, 0, Stage::rumour, false, round};
                auto it = plan.until.find(m.driver);
                deal.until = it != plan.until.end() ? it->second : st.year + 2;
                deal.source = pick_outlet(st, r, false);
                add_deal(st, deal);
                { int pick0 = r.range(0, 9);
                add_news(st, round, "rumour",
                 [&](text::Lang l) { return text::h_rumour(l, pick0, driver_name(st, m.driver), team_name(st, m.to)); },
                 [&](text::Lang l) { return std::string(text::outlet_label(l, deal.source, outlet_rating(st, deal.source)) + ": " +
                             text::rumour(l, pick0, driver_name(st, m.driver), team_name(st, m.to),
                                          m.from.empty() ? "" : team_name(st, m.from))); }, false);
                tag_news(st, m.driver, m.to, m.from); }
                continue;
            }
        }
        // a rumour with nothing behind it
        std::vector<const Driver *> names;
        for (const Team &t : st.teams)
            for (const std::string &who : t.seats)
                if (const Driver *d = st.driver(who))
                    if (d->id != st.player && !find_deal(st, d->id))
                        names.push_back(d);
        if (names.empty())
            continue;
        const Driver *d = names[static_cast<size_t>(r.range(0, static_cast<int>(names.size()) - 1))];
        const Team *home = st.team(d->team);
        std::vector<const Team *> dest;
        for (const Team &t : st.teams)
            if (t.active && t.id != d->team && t.id != st.player_team && (!home || t.attraction() >= home->attraction() - 6.0f))
                dest.push_back(&t);
        if (dest.empty())
            continue;
        const Team *t = dest[static_cast<size_t>(r.range(0, static_cast<int>(dest.size()) - 1))];
        int source = pick_outlet(st, r, true);
        add_deal(st, {d->id, t->id, "transfer", st.year + 1, 0, Stage::rumour, true, round, source});
        { int pick0 = r.range(0, 9);
        add_news(st, round, "rumour",
                 [&](text::Lang l) { return text::h_rumour(l, pick0, d->name, t->name); },
                 [&](text::Lang l) { return std::string(text::outlet_label(l, source, outlet_rating(st, source)) + ": " + text::fake_rumour(l, pick0, d->name, t->name)); }, false);
        tag_news(st, d->id, t->id, d->team); }
    }
}

// an ambitious star stuck in a slow car wants out despite his contract (once a season, one driver)
void ambition_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (st.ambition_checked || progress < 0.45 || !s.personality)
        return;
    st.ambition_checked = true;
    for (Team &t : st.teams) {
        if (!t.active)
            continue;
        for (int k = 0; k < 2; ++k) {
            Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            if (!d || st.locked(t, k, s) || d->id == st.player || d->contract <= st.year || d->wants_out || d->retiring)
                continue;
            if (d->ambition < 0.72f || value(*d) < 83 || t.performance >= 80 || team_rank(st, t.id) < 6)
                continue;
            if (!r.chance(0.4))
                continue;
            d->wants_out = true;
            std::string name = d->name, team = t.name;
            int pick = r.range(0, 9);
            add_news(st, round, "wants_out", [&](text::Lang l) { return text::h_wants_out(l, name, team); },
                     [&](text::Lang l) { return text::wants_out(l, pick, name, team); }, false);
            tag_news(st, d->id, "", t.id);
            return;
        }
    }
}

// half way through the season, the teams look at where they stand: a plan can change (at most two told)
void strategy_review(State &st, const Settings &s, int round, double progress)
{
    if (st.strategy_reviewed || progress < 0.5 || !s.strategies)
        return;
    st.strategy_reviewed = true;
    int told = 0;
    for (Team &t : st.teams) {
        if (!t.active)
            continue;
        std::string now = pick_strategy(st, t);
        if (now == t.strategy)
            continue;
        std::string was = t.strategy;
        t.strategy = now;
        if (told >= 2 || (now != "rebuild" && now != "title" && now != "survival"))
            continue; // a quiet change
        ++told;
        std::string name = t.name;
        add_news(st, round, "strategy", [&](text::Lang l) { return text::h_strategy(l, name, now); },
                 [&](text::Lang l) { return text::strategy_switch(l, name, was, now); }, false);
    }
    (void)s;
}

void draw_retirements(State &st, const Settings &s, Rng &r, int round)
{
    st.retirements_drawn = true;
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            if (!d || d->id == st.player || d->retiring)
                continue;
            if (Deal *deal = find_deal(st, d->id); deal && !deal->fake && deal->stage >= Stage::agreed)
                continue; // he has just signed for next season
            double p = retire_chance(*d, st.year, d->contract > st.year, s.intensity);
            if (p > 0 && r.chance(p)) {
                d->retiring = true;
                for (Deal &deal : st.deals)
                    if (deal.driver == d->id && deal.stage != Stage::official)
                        deal.stage = Stage::collapsed;
                { int pick0 = r.range(0, 9);
                add_news(st, round, "retirement",
                 [&](text::Lang l) { return text::h_retirement(l, pick0, d->name); },
                 [&](text::Lang l) { return std::string(text::retirement(l, pick0, d->name, d->age(st.year), std::max(d->seasons, 1))); }, true); }
            }
        }
}

void midseason(State &st, const Settings &s, Rng &r, int round)
{
    if (st.midseason_moves >= midseason_cap(s.intensity))
        return;
    double base = s.intensity == Intensity::chaos ? 0.45 : s.intensity == Intensity::lively ? 0.28 : 0.14;
    for (Team &t : st.teams) {
        if (st.midseason_moves >= midseason_cap(s.intensity))
            break;
        for (int k = 0; k < 2; ++k) {
            if (st.locked(t, k, s))
                continue;
            Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            const Driver *mate = st.driver(t.seats[static_cast<size_t>(1 - k)]);
            if (!d || !mate || d->kind == "legend")
                continue;
            bool covered = std::any_of(st.standins.begin(), st.standins.end(),
                                       [&](const State::Standin &sb) { return sb.team == t.id; });
            if (covered)
                continue; // no sacking while a reserve covers a seat of this team
            bool struggling = d->form <= -0.30f && value(*d) < value(*mate) - 4.0f;
            // a title push has no patience; a rebuild or a youth team gives a driver time
            const std::string plan = s.strategies ? t.strategy : std::string("contend");
            double patience = plan == "title"                        ? t.patience * 0.6
                              : plan == "rebuild" || plan == "youth" ? std::min(1.0, t.patience + 0.25)
                                                                     : t.patience;
            if (!struggling || !r.chance(base * (1.0 - patience)))
                continue;
            // the replacement: the team's own junior or reserve first, then the best free driver. Only a driver the
            // game takes for a race seat now: a reserve (a team, no seat) or a driver without a career row is refused
            const Driver *in = nullptr;
            float best = -1e9f;
            for (const auto &[id, c] : st.drivers) {
                if (!c.team.empty() || c.retired || c.retiring || c.id == st.player || c.kind == "player" || st.shielded(c.id))
                    continue;
                if (c.kind == "legend" && !s.legends)
                    continue;
                if (!st.seatable(c.id))
                    continue;
                if (std::any_of(st.standins.begin(), st.standins.end(),
                                [&](const State::Standin &sb) { return sb.in == c.id; }))
                    continue; // already in a car, standing in

                // phase 6: the team's reserve and its academy drivers are first in line
                float sc = value(c) + static_cast<float>(r.normal() * 2.0);
                if (s.academies)
                    sc += (reserve_for(st, t) == c.id ? 8.0f : 0.0f) + (in_academy(t, c) ? 7.0f : 0.0f);
                else
                    sc += c.academy == t.family ? 6.0f : 0.0f;
                if (sc > best) {
                    best = sc;
                    in = &c;
                }
            }
            if (!in)
                continue;
            std::string out_id = d->id, in_id = in->id;
            // a reserve called up for good leaves his reserve role open (the teams sign again when the grid is final);
            // the change remembers whose reserve he was, in case the game turns it down
            std::string reserve_team;
            for (Team &x : st.teams)
                if (x.reserve == in_id) {
                    reserve_team = x.id;
                    x.reserve.clear();
                }
            State::Change ch;
            ch.other = reserve_team;
            ch.kind = "sacking";
            ch.team = t.id;
            ch.seat = k;
            ch.out = out_id;
            ch.in = in_id;
            ch.out_contract = d->contract;
            ch.in_contract = in->contract;
            ch.round = round;
            add_move(st, round, out_id, t.id, "", "sacked");
            add_move(st, round, in_id, "", t.id, "replaced");
            { int pick0 = r.range(0, 9); int pick1 = r.range(0, 9);
            add_news(st, round, "sacking",
                 [&](text::Lang l) { return text::h_sacking(l, pick0, d->name, t.name); },
                 [&](text::Lang l) { return std::string(text::sacking(l, pick0, d->name, t.name, in->name) + " " +
                         text::boss_quote(l, pick1, d->name, t.name, "sacking")); }, true); }
            tag_news(st, out_id, "", t.id);
            ch.news = st.news.back().id;
            d->released = true;
            d->contract = 0;
            set_seat(st, t, k, in_id);
            Driver *nd = st.driver(in_id);
            nd->contract = st.year;
            nd->form = 0;
            st.pending.push_back({t.id, k, in_id, "midseason"});
            ++st.midseason_moves;
            // any deal of the sacked driver with this team is off
            for (Deal &deal : st.deals)
                if (deal.driver == out_id && deal.team == t.id && deal.stage != Stage::official) {
                    ch.deals.push_back({deal.id, static_cast<int>(deal.stage)});
                    deal.stage = Stage::collapsed;
                }
            st.changes.push_back(std::move(ch));
            break;
        }
    }
}

// a season without race results from the game still moves: form drifts with the gap to the team-mate
void synthetic_form(State &st, Rng &r)
{
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            const Driver *m = st.driver(t.seats[static_cast<size_t>(1 - k)]);
            if (!d)
                continue;
            float gap = m ? (d->overall() - m->overall()) / 10.0f : 0.0f;
            d->form = clampf(0.85f * d->form + 0.15f * clampf(gap + static_cast<float>(r.normal() * 0.6), -1, 1), -1, 1);
        }
}

// the driver stock: each grid driver's market value against the last round
void update_stock(State &st)
{
    for (Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (Driver *d = st.driver(who)) {
                float v = value(*d);
                d->trend = d->last_value > 0 ? v - d->last_value : 0.0f;
                d->last_value = v;
                if (st.round >= 1 && st.round <= 40) { // the value chart: one point a round
                    size_t i = static_cast<size_t>(st.round - 1);
                    if (d->values.size() <= i)
                        d->values.resize(i + 1, 0.0f);
                    d->values[i] = std::round(v * 10.0f) / 10.0f;
                }
            }
}

// a driver who keeps losing to his team-mate makes the news before the team acts
void pressure_news(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (progress < 0.15 || progress > 0.70)
        return;
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            const Driver *mate = st.driver(t.seats[static_cast<size_t>(1 - k)]);
            if (!d || !mate || d->pressured || st.locked(t, k, s) || d->id == st.player)
                continue;
            if (d->form <= -0.20f && value(*d) < value(*mate) - 2.0f && r.chance(0.45 * (1.0 - t.patience) + 0.1)) {
                d->pressured = true;
                { int pick0 = r.range(0, 9);
                add_news(st, round, "pressure",
                 [&](text::Lang l) { return text::h_pressure(l, pick0, d->name); },
                 [&](text::Lang l) { return std::string(text::pressure(l, pick0, d->name, t.name)); }, false); }
                return; // one a round is plenty
            }
        }
}

// the player's own market: interest from better teams, or his team wanting to keep him
void player_news(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (st.player.empty() || st.player_team.empty() || progress < 0.30 || s.rumours == Rumours::off)
        return;
    const Team *own = st.team(st.player_team);
    const Driver *me = st.driver(st.player);
    if (!own || !me)
        return;
    float form = me->form; // from the race results when the core reads them, else 0
    double p = (s.rumours == Rumours::many ? 0.22 : s.rumours == Rumours::normal ? 0.14 : 0.07) * (1.0 + form);
    // what the player told the press: "listening to offers" brings suitors, commitment brings renewal talk
    p *= st.player_loyalty < 0 ? 1.6 : st.player_loyalty > 0 ? 0.8 : 1.0;
    // a team unhappy with him: the paddock smells a way out
    p *= st.team_comfort < 0 ? 1.3 : 1.0;
    if (!r.chance(p))
        return;
    if (st.player_loyalty + st.team_comfort > 0 && r.chance(0.4 + 0.1 * std::max(0, st.team_comfort))) {
        { int pick0 = r.range(0, 9);
        add_news(st, round, "player",
                 [&](text::Lang l) { return text::h_player_renewal(l, pick0, own->name); },
                 [&](text::Lang l) { return std::string(text::player_renewal(l, pick0, own->name)); }, false); }
        return;
    }
    std::vector<const Team *> suitors;
    for (const Team &t : st.teams)
        if (t.id != own->id && t.attraction() > own->attraction() - 4.0f + (form < 0 ? 6.0f : 0.0f))
            suitors.push_back(&t);
    if (suitors.empty() || r.chance(0.3)) {
        { int pick0 = r.range(0, 9);
        add_news(st, round, "player",
                 [&](text::Lang l) { return text::h_player_renewal(l, pick0, own->name); },
                 [&](text::Lang l) { return std::string(text::player_renewal(l, pick0, own->name)); }, false); }
        return;
    }
    const Team *t = suitors[static_cast<size_t>(r.range(0, static_cast<int>(suitors.size()) - 1))];
    { int pick0 = r.range(0, 9);
    add_news(st, round, "player",
                 [&](text::Lang l) { return text::h_player_interest(l, pick0, t->name); },
                 [&](text::Lang l) { return std::string(text::player_interest(l, pick0, t->name)); }, false); }
}

// a stand-in whose race is done hands the seat back
void end_standins(State &st, const Settings &s, int completed)
{
    for (auto it = st.standins.begin(); it != st.standins.end();) {
        if (it->race > completed) {
            ++it;
            continue;
        }
        const Team *t = st.team(it->team);
        if (t && t->seats[static_cast<size_t>(it->seat)] == it->out) {
            st.pending.push_back({it->team, it->seat, it->out, "return"});
            add_news(st, completed, "standin_return",
                 [&](text::Lang l) { return text::h_standin_return(l, driver_name(st, it->out), t->name); },
                 [&](text::Lang l) { return std::string(text::standin_return(l, driver_name(st, it->out), t->name)); }, false);
        }
        it = st.standins.erase(it);
    }
}

// now and then a driver misses a race; his team's reserve, or one of its juniors, drives instead
void maybe_standin(State &st, const Settings &s, Rng &r, int completed)
{
    if (!s.standins || !st.standins.empty() || completed + 1 >= st.rounds)
        return;
    double p = s.intensity == Intensity::chaos ? 0.10 : s.intensity == Intensity::lively ? 0.06 : 0.035;
    if (!r.chance(p))
        return;
    std::vector<std::pair<Team *, int>> seats;
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            const Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            if (d && !st.locked(t, k, s) && d->id != st.player && d->kind != "legend" && d->kind != "player")
                seats.push_back({&t, k});
        }
    if (seats.empty())
        return;
    auto [t, k] = seats[static_cast<size_t>(r.range(0, static_cast<int>(seats.size()) - 1))];
    const Driver *in = nullptr;
    float best = -1e9f;
    for (const auto &[id, c] : st.drivers) {
        if (!c.team.empty() || c.retired || c.retiring || c.kind == "legend" || c.kind == "player" || c.id == st.player ||
            st.shielded(c.id) || !st.seatable(c.id))
            continue;
        float sc;
        if (s.academies) {
            // the team's reserve and its academy drivers: the better of them on the day (the reserve knows the car)
            bool reserve = reserve_for(st, *t) == c.id || c.reserve_of == t->id;
            bool academy = in_academy(*t, c);
            sc = reserve || academy ? 100.0f + value(c) + 3.0f * c.form + (reserve ? 2.0f : 0.0f) : value(c) * 0.1f - (c.kind == "pool" ? 20.0f : 0.0f);
        } else {
            sc = c.reserve_of == t->id ? 100.0f : c.academy == t->family ? 50.0f : 0.0f;
            sc += value(c) * 0.1f;
            if (c.kind == "pool" && c.reserve_of != t->id)
                sc -= 20.0f;
        }
        if (sc > best) {
            best = sc;
            in = &c;
        }
    }
    if (!in)
        return;
    const std::string out = t->seats[static_cast<size_t>(k)];
    st.standins.push_back({out, in->id, t->id, k, completed + 1});
    st.pending.push_back({t->id, k, in->id, "standin"});
    { int pick0 = r.range(0, 9);
    add_news(st, completed, "standin",
                 [&](text::Lang l) { return text::h_standin(l, in->name, driver_name(st, out)); },
                 [&](text::Lang l) { return std::string(text::standin(l, pick0, driver_name(st, out), t->name, in->name, completed + 1)); }, true); }
}

// the season in a few lines, at the market's close
void season_review(State &st, const Settings &s)
{
    std::string signing, revelation, leap, retired;
    float best_signing = -1, best_upside = -1, best_leap = 0;
    int moves = 0, rookies = 0;
    struct Tribute {
        std::string name;
        int seasons, wins;
    };
    std::vector<Tribute> tributes;
    for (const Move &m : st.history) {
        if (m.year != st.year)
            continue;
        const Driver *d = st.driver(m.driver);
        if (!d)
            continue;
        if (m.kind == "retirement") {
            retired += (retired.empty() ? "" : ", ") + d->name;
            CareerTotals c = career_totals(st, *d); // the tribute: his career in a few numbers
            tributes.push_back({d->name, c.seasons, c.wins});
            continue;
        }
        if (m.kind != "transfer" && m.kind != "promotion" && m.kind != "comeback")
            continue;
        ++moves;
        std::string to = team_name(st, m.to);
        if (m.kind == "promotion") {
            ++rookies;
            float upside = d->potential();
            if (upside > best_upside) {
                best_upside = upside;
                revelation = d->name + " (" + to + ")";
            }
            continue;
        }
        if (value(*d) > best_signing) {
            best_signing = value(*d);
            signing = d->name + " (" + (m.from.empty() ? "-" : team_name(st, m.from)) + " > " + to + ")";
        }
        const Team *a = st.team(m.from), *b = st.team(m.to);
        if (a && b && b->attraction() - a->attraction() > best_leap) {
            best_leap = b->attraction() - a->attraction();
            leap = d->name + " (" + a->name + " > " + b->name + ")";
        }
    }
    add_news(st, st.round, "review",
                 [&](text::Lang l) { return std::string(text::t_review(l, st.year)); },
                 [&](text::Lang l) {
                     std::string farewells;
                     for (const Tribute &t : tributes)
                         farewells += (farewells.empty() ? "" : ", ") + text::tribute(l, t.name, t.seasons, t.wins);
                     return std::string(text::review(l, signing, revelation, leap, farewells.empty() ? retired : farewells, moves, rookies));
                 }, true);
}

// when the game doesn't report race results, a race is simulated: points, form and the constructors' order
void simulate_race(State &st, Rng &r)
{
    std::vector<std::pair<double, std::string>> order;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                order.push_back({t.performance * 0.7 + d->overall() * 0.5 + r.uniform() * 18.0 - (r.chance(0.06) ? 100.0 : 0.0),
                                 who});
    std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); ++i) // the round being completed (complete_round set st.round to it)
        record_result(st, order[i].second, order[i].first < 0 ? 0 : static_cast<int>(i + 1), st.round);
}

bool feuding(const Driver &a, const Driver &b)
{
    return std::find(a.feuds.begin(), a.feuds.end(), b.id) != a.feuds.end() ||
           std::find(b.feuds.begin(), b.feuds.end(), a.id) != b.feuds.end();
}

void start_feud(Driver &a, Driver &b)
{
    if (feuding(a, b))
        return;
    a.feuds.push_back(b.id);
    b.feuds.push_back(a.id);
}

// the F2 field: young drivers off the grid. The season's order is seeded, so the mid-season leader is
// usually the champion
void f2_update(State &st, const Settings &s, Rng &r, int round, bool final)
{
    Rng luck(mix(st.seed, 0xf2f2ull ^ static_cast<uint64_t>(st.year)));
    std::vector<std::pair<float, std::string>> field;
    for (const auto &[id, d] : st.drivers) {
        if (!d.team.empty() || d.retired || d.kind != "f2" || d.age(st.year) > 27 || st.shielded(d.id))
            continue;
        float score = d.overall() * 0.7f + d.potential() * 0.2f + static_cast<float>(luck.normal() * 3.0);
        if (final)
            score += static_cast<float>(r.normal() * 1.2);
        field.push_back({score, id});
    }
    std::sort(field.begin(), field.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    st.f2_table.clear();
    for (const auto &[score, id] : field)
        st.f2_table.push_back(id);
    if (field.empty())
        return;
    if (!final) {
        st.f2_reported = 1;
        const Driver *lead = st.driver(field[0].second);
        add_news(st, round, "f2",
                 [&](text::Lang l) { return std::string(text::t_f2(l)); },
                 [&](text::Lang l) { return std::string(text::f2_leader(l, lead->name, lead->age(st.year))); }, false);
        return;
    }
    st.f2_reported = 2;
    for (size_t i = 0; i < field.size(); ++i)
        if (Driver *d = st.driver(field[i].second))
            d->f2_pos = static_cast<int>(i + 1);
    if (field.size() >= 3)
        add_news(st, round, "f2",
                 [&](text::Lang l) { return std::string(text::t_f2(l)); },
                 [&](text::Lang l) { return std::string(text::f2_champion(l, driver_name(st, field[0].second), driver_name(st, field[1].second),
                                   driver_name(st, field[2].second))); }, true);
}

// the technical market: a top designer at a smaller team goes where the money is
void staff_market(State &st, const Settings &s, Rng &r, int round)
{
    st.staff_done = true;
    double p = s.intensity == Intensity::chaos ? 0.9 : s.intensity == Intensity::lively ? 0.7 : 0.5;
    if (!r.chance(p))
        return;
    Team *from = nullptr, *to = nullptr;
    float best = -1e9f;
    for (Team &a : st.teams)
        for (Team &b : st.teams) {
            if (&a == &b || a.td_rating < 74 || b.budget <= a.budget || a.td_rating <= b.td_rating + 3)
                continue;
            float gain = (b.budget - a.budget) + (a.td_rating - b.td_rating) * 1.5f + static_cast<float>(r.normal() * 8);
            if (gain > best) {
                best = gain;
                from = &a;
                to = &b;
            }
        }
    if (!from)
        return;
    std::string star = from->td_name, gone = to->td_name;
    to->td_name = star;
    to->td_rating = from->td_rating;
    std::string nat = names::nationality(r.next());
    std::string letters = std::string(1, "BCDFGHKLMPRSTV"[r.next() % 14]) + "AEIOU"[r.next() % 5] + "LNRST"[r.next() % 5];
    from->td_name = names::first(nat, r.chance(0.15), r.next()) + " " + names::surname(nat, letters, r.next());
    from->td_rating = static_cast<float>(60.0 + r.uniform() * 16.0);
    { int pick0 = r.range(0, 9);
    add_news(st, round, "staff",
                 [&](text::Lang l) { return text::h_staff(l, star, to->name); },
                 [&](text::Lang l) { return std::string(text::staff_move(l, pick0, star, from->name, to->name) + " " +
                 text::staff_hire(l, from->td_name, from->name, star)); }, true); }
}

// team-mates of a similar level fall out now and then
void feuds_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (progress < 0.2 || progress > 0.85)
        return;
    double p = s.intensity == Intensity::chaos ? 0.035 : s.intensity == Intensity::lively ? 0.02 : 0.012;
    for (Team &t : st.teams) {
        Driver *a = st.driver(t.seats[0]), *b = st.driver(t.seats[1]);
        if (!a || !b || a->id == st.player || b->id == st.player || feuding(*a, *b))
            continue;
        if (std::fabs(value(*a) - value(*b)) < 5.0f && r.chance(p)) {
            start_feud(*a, *b);
            { int pick0 = r.range(0, 9);
            add_news(st, round, "feud",
                 [&](text::Lang l) { return text::h_feud(l, a->name, b->name); },
                 [&](text::Lang l) { return std::string(text::feud(l, pick0, a->name, b->name, t.name)); }, false); }
            return;
        }
    }
}

// the press asks the player about the market; one question at a time, open for two rounds
void press_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (st.press && round > st.press->asked + 2) {
        add_news(st, round, "press_reply",
                 [&](text::Lang l) { return text::h_press_silence(l); },
                 [&](text::Lang l) { return std::string(text::r_silence(l)); }, false);
        st.press.reset();
    }
    if (!s.press || st.press || st.player.empty() || st.player_team.empty() || progress < 0.12 || progress > 0.95)
        return;
    if (!r.chance(0.3))
        return;
    const Team *own = st.team(st.player_team);
    if (!own)
        return;
    const Driver *mate = nullptr;
    for (const std::string &who : own->seats)
        if (who != st.player)
            mate = st.driver(who);
    std::vector<std::string> topics = {"future"};
    const Deal *mate_deal = nullptr;
    if (mate)
        for (const Deal &d : st.deals)
            if (d.driver == mate->id && d.stage != Stage::collapsed && d.stage != Stage::official && d.kind != "renewal")
                mate_deal = &d;
    if (mate_deal)
        topics.push_back("mate");
    if (!st.real && !s.protect_teammate && mate && mate->contract <= st.year && progress >= 0.3)
        topics.push_back("wish");
    const Deal *signing = nullptr;
    for (const Deal &d : st.deals)
        if (d.stage == Stage::official && !d.fake && d.kind == "transfer" && d.team != st.player_team && d.driver != st.player)
            signing = &d;
    if (signing)
        topics.push_back("signing");
    // 1.1.0 phase 5: deadline day, a rival's bombshell, the player's own renewal, last season's awards
    const Driver *me = st.driver(st.player);
    if (s.deadline && deadline_races_left(st) > 0 && !st.real)
        topics.push_back("deadline");
    const NewsItem *bomb = nullptr;
    for (auto it = st.news.rbegin(); it != st.news.rend(); ++it)
        if (it->year == st.year && it->big && it->to != st.player_team && it->driver != st.player && round - it->round <= 3) {
            bomb = &*it;
            break;
        }
    if (bomb)
        topics.push_back("rival");
    if (me && me->contract > 0 && me->contract <= st.year && progress >= 0.25 && !st.real)
        topics.push_back("renewal");
    const State::Award *last_award = nullptr;
    if (progress < 0.35)
        for (const State::Award &a : st.awards)
            if (a.year == st.year - 1 && a.key != "constructors" && a.who != st.player && st.driver(a.who))
                last_award = &a;
    if (last_award)
        topics.push_back("awards");
    std::string topic = topics[static_cast<size_t>(r.range(0, static_cast<int>(topics.size()) - 1))];

    State::Press p;
    p.id = st.next_press_id++;
    p.topic = topic;
    p.asked = round;
    // the question and answers in both languages: [0] English, [1] Spanish
    auto ask = [&](auto question, auto answer) {
        for (int k = 0; k < 2; ++k) {
            text::Lang l{k == 1};
            (k ? p.question_es : p.question_en) = question(l);
            for (int i = 0; i < 3; ++i)
                (k ? p.answers_es : p.answers_en).push_back(answer(l, i));
        }
        p.question = st.view_es ? p.question_es : p.question_en;
        p.answers = st.view_es ? p.answers_es : p.answers_en;
    };
    if (topic == "future") {
        ask([&](text::Lang l) { return text::q_future(l, own->name); },
            [&](text::Lang l, int i) { return text::a_future(l, i, own->name); });
    } else if (topic == "mate") {
        p.subject = mate->id;
        ask([&](text::Lang l) { return text::q_mate(l, mate->name, team_name(st, mate_deal->team)); },
            [&](text::Lang l, int i) { return text::a_mate(l, i); });
    } else if (topic == "wish") {
        std::vector<const Driver *> pool;
        for (const auto &[id, d] : st.drivers)
            if (eligible(d, st, s) && d.team != own->id && (d.team.empty() || d.contract <= st.year) && d.kind != "legend")
                pool.push_back(&d);
        std::sort(pool.begin(), pool.end(), [](const Driver *a, const Driver *b) { return value(*a) > value(*b); });
        if (pool.size() < 3)
            return;
        ask([&](text::Lang l) { return text::q_wish(l, own->name); },
            [&](text::Lang, int i) { return "\"" + pool[static_cast<size_t>(i)]->name + "\""; });
        for (int i = 0; i < 3; ++i)
            p.options.push_back(pool[static_cast<size_t>(i)]->id);
    } else if (topic == "deadline") {
        int left = deadline_races_left(st);
        ask([&](text::Lang l) { return text::q_deadline(l, own->name, left); },
            [&](text::Lang l, int i) { return text::a_deadline(l, i); });
    } else if (topic == "rival") {
        p.subject = bomb->driver;
        std::string who_name = driver_name(st, bomb->driver), where_name = team_name(st, bomb->to);
        ask([&](text::Lang l) { return text::q_rival(l, who_name, where_name); },
            [&](text::Lang l, int i) { return text::a_rival(l, i); });
    } else if (topic == "renewal") {
        ask([&](text::Lang l) { return text::q_renewal(l, own->name); },
            [&](text::Lang l, int i) { return text::a_renewal(l, i, own->name); });
    } else if (topic == "awards") {
        p.subject = last_award->who;
        std::string who_name = last_award->name, award = last_award->key;
        int year = last_award->year;
        ask([&](text::Lang l) { return text::q_award(l, who_name, text::award_title(l, award), year); },
            [&](text::Lang l, int i) { return text::a_award(l, i); });
    } else {
        p.subject = signing->driver;
        ask([&](text::Lang l) { return text::q_signing(l, driver_name(st, signing->driver), team_name(st, signing->team)); },
            [&](text::Lang l, int i) { return text::a_signing(l, i); });
    }
    std::string about = p.subject.empty() ? std::string() : driver_name(st, p.subject);
    add_news(st, round, "press", [&](text::Lang l) { return text::h_press(l, p.topic, about, false); },
             [&](text::Lang l) { return l.es ? p.question_es : p.question_en; }, true);
    st.press = p;
}

// new talents: fictional reserves the game no longer uses come back as young prospects with new names
void make_talents(State &st, const Settings &s, Rng &r)
{
    st.talents_year = st.year;
    if (!s.talents)
        return;
    int want = st.season_index <= 0 ? 2 : s.intensity == Intensity::chaos ? 4 : s.intensity == Intensity::lively ? 3 : 2;
    std::vector<Driver *> pool;
    for (auto &[id, d] : st.drivers) {
        if (d.name_keys.size() != 2 || !d.team.empty() || d.id == st.player || st.shielded(d.id) || !d.home.empty())
            continue;
        bool busy = std::any_of(st.standins.begin(), st.standins.end(), [&](const State::Standin &sb) { return sb.in == d.id; });
        bool young_talent = d.regen && d.age(st.year) < 24;
        if (!busy && !young_talent && (d.retired || d.contract == 0))
            pool.push_back(&d);
    }
    for (size_t i = pool.size(); i > 1; --i)
        std::swap(pool[i - 1], pool[static_cast<size_t>(r.range(0, static_cast<int>(i) - 1))]);
    std::set<std::string> used;
    for (const auto &[id, d] : st.drivers)
        used.insert(d.name);
    static const char *const families[] = {"redbull", "mercedes", "ferrari", "mclaren", "alpine", "williams", "audi", "astonmartin"};
    std::string list;
    int made = 0;
    for (Driver *d : pool) {
        if (made >= want)
            break;
        std::string first, last;
        for (int tries = 0; tries < 6; ++tries) {
            uint64_t pick = r.next();
            first = names::first(d->nat, d->female, pick);
            last = names::surname(d->nat, d->initials, pick >> 7);
            if (!used.count(first + " " + last))
                break;
        }
        used.insert(first + " " + last);
        bool gem = made == 0 && r.chance(0.5);
        d->alias_first = first;
        d->alias_last = last;
        d->name = first + " " + last;
        d->born = st.year - r.range(17, 19);
        d->pace = static_cast<float>(58 + r.uniform() * 10);
        d->racecraft = static_cast<float>(52 + r.uniform() * 12);
        d->awareness = static_cast<float>(55 + r.uniform() * 10);
        d->experience = static_cast<float>(15 + r.uniform() * 10);
        float peak = static_cast<float>(74 + r.uniform() * 12 + (gem ? 6 : 0));
        d->pace_peak = std::min(95.0f, peak + static_cast<float>(r.normal() * 2));
        d->racecraft_peak = std::min(95.0f, peak + static_cast<float>(r.normal() * 3));
        d->awareness_peak = std::min(95.0f, peak - 2 + static_cast<float>(r.normal() * 3));
        d->experience_peak = 80;
        d->kind = "f2";
        d->regen = true;
        d->retired = d->retiring = d->released = d->wants_out = false;
        d->contract = 0;
        d->form = 0;
        d->seasons = 0;
        d->salary = 0;
        d->clause_rank = 0;
        d->f2_pos = 0;
        d->feuds.clear();
        d->reserve_of.clear();
        d->academy = r.chance(0.45) ? families[r.next() % 8] : "";
        d->backing = r.chance(0.3) ? static_cast<float>(r.range(3, 20)) : 0.0f;
        d->ambition = d->loyalty = d->money = -1; // a new person: a new character
        draw_traits(mix(st.seed, static_cast<uint64_t>(st.year)), st.year, *d);
        d->number = 0;
        d->number_tries = 0;
        list += (made ? ", " : "") + d->name + " (" + std::to_string(d->age(st.year)) + ")";
        ++made;
    }
    if (made)
        add_news(st, st.round, "talents",
                 [&](text::Lang l) { return std::string(text::t_talents(l, st.year)); },
                 [&](text::Lang l) { return std::string(text::talents(l, list)); }, true);
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Rounds and seasons

// ---------------------------------------------------------------------------------------------------
// History mode: the real seasons

namespace {

// a real move goes one step further: rumour, talks, official (a retirement is announced at once)
void real_step(State &st, const Settings &s, Rng &r, State::RealMove &m, int at, int to_stage)
{
    Driver *d = st.driver(m.driver);
    if (!d || m.done >= to_stage)
        return;
    if (m.kind == "left") {
        m.done = 3; // told with the final grid
        return;
    }
    if (m.kind == "retirement") {
        if (to_stage < 3)
            return;
        m.done = 3;
        d->retiring = true;
        for (Deal &deal : st.deals)
            if (deal.driver == d->id && deal.stage != Stage::official)
                deal.stage = Stage::collapsed;
        int pick = r.range(0, 9);
        std::string name = d->name;
        int age = d->age(st.year), seasons = std::max(d->seasons, 1);
        add_news(st, at, "retirement", [&](text::Lang l) { return text::h_retirement(l, pick, name); },
                 [&](text::Lang l) { return std::string(text::retirement(l, pick, name, age, seasons)); }, true);
        return;
    }
    std::string from = d->team.empty() ? "" : team_name(st, d->team);
    Deal *deal = nullptr;
    for (Deal &x : st.deals)
        if (x.driver == m.driver && !x.fake && x.team == m.to && x.stage != Stage::collapsed)
            deal = &x;
    if (m.done < 1 && to_stage >= 1) {
        m.done = 1;
        if (!deal) {
            deal = &add_deal(st, {m.driver, m.to, m.kind, st.year + 1, m.until, Stage::rumour, false, at, pick_outlet(st, r, false)});
        }
        if (s.rumours != Rumours::off && to_stage == 1) {
            int pick = r.range(0, 9), source = deal->source;
            std::string name = d->name, to = team_name(st, m.to);
            add_news(st, at, "rumour", [&](text::Lang l) { return text::h_rumour(l, pick, name, to); },
                     [&](text::Lang l) { return text::outlet_label(l, source, outlet_rating(st, source)) + ": " + text::rumour(l, pick, name, to, from); }, false);
        }
    }
    if (m.done < 2 && to_stage >= 2) {
        m.done = 2;
        deal->stage = Stage::talks;
        if (s.rumours != Rumours::off && to_stage == 2) {
            int pick = r.range(0, 9);
            std::string name = d->name, to = team_name(st, m.to);
            add_news(st, at, "talks", [&](text::Lang l) { return text::h_talks(l, pick, name, to); },
                     [&](text::Lang l) { return std::string(text::talks(l, pick, name, to)); }, false);
        }
    }
    if (m.done < 3 && to_stage >= 3) {
        m.done = 3;
        deal->until = m.until;
        announce_official(st, s, r, *deal, at, true);
    }
}

// a story from the tabloids that won't happen, and the old ones falling apart
void real_fakes(State &st, const Settings &s, Rng &r, int at, double progress)
{
    for (Deal &deal : st.deals)
        if (deal.fake && deal.stage == Stage::rumour && at - deal.opened >= 2 && r.chance(0.35)) {
            deal.stage = Stage::collapsed;
            int pick = r.range(0, 9);
            std::string name = driver_name(st, deal.driver), to = team_name(st, deal.team);
            add_news(st, at, "collapse", [&](text::Lang l) { return text::h_collapse(l, pick, name, to, true); },
                     [&](text::Lang l) { return std::string(text::collapse(l, pick, name, to, true)); }, false);
        }
    double chance = s.rumours == Rumours::many ? 0.35 : s.rumours == Rumours::normal ? 0.22 : s.rumours == Rumours::few ? 0.12 : 0;
    if (progress < 0.12 || progress > 0.9 || !r.chance(chance))
        return;
    std::vector<const Driver *> stay;
    for (const Team &t : st.teams)
        for (const std::string &id : t.seats)
            if (const Driver *d = st.driver(id))
                if (d->id != st.player && !d->retiring && !find_deal(st, d->id) &&
                    std::none_of(st.real_moves.begin(), st.real_moves.end(), [&](const State::RealMove &m) { return m.driver == d->id; }))
                    stay.push_back(d);
    if (stay.empty())
        return;
    const Driver *d = stay[static_cast<size_t>(r.range(0, static_cast<int>(stay.size()) - 1))];
    std::vector<const Team *> dest;
    for (const Team &t : st.teams)
        if (t.id != d->team && (!t.seats[0].empty() || !t.seats[1].empty()))
            dest.push_back(&t);
    if (dest.empty())
        return;
    const Team *t = dest[static_cast<size_t>(r.range(0, static_cast<int>(dest.size()) - 1))];
    int source = pick_outlet(st, r, true), pick = r.range(0, 9);
    add_deal(st, {d->id, t->id, "transfer", st.year + 1, 0, Stage::rumour, true, at, source});
    std::string name = d->name, to = t->name;
    add_news(st, at, "rumour", [&](text::Lang l) { return text::h_rumour(l, pick, name, to); },
             [&](text::Lang l) { return text::outlet_label(l, source, outlet_rating(st, source)) + ": " + text::fake_rumour(l, pick, name, to); }, false);
}

void real_round(State &st, const Settings &s, Rng &r, int at, double progress)
{
    for (State::RealMove &m : st.real_moves) {
        if (m.driver == st.player)
            continue; // the player's own career is his to write
        int stage = at >= m.official_at ? 3 : at >= m.talks_at ? 2 : at >= m.rumour_at ? 1 : 0;
        if (stage > m.done) {
            // one step at a time, so each step makes its own news (a missed visit catches up quietly)
            for (int to = m.done + 1; to <= stage; ++to)
                real_step(st, s, r, m, at, to);
        }
    }
    // the real season's stand-ins and swaps, told before their race
    for (State::RealChange &c : st.real_changes) {
        if (c.told || c.round != at + 1)
            continue;
        c.told = true;
        const Team *t = st.team(c.team);
        if (!t)
            continue;
        std::string out = driver_name(st, c.out), in = driver_name(st, c.in), team = t->name;
        int pick = r.range(0, 9), race = c.round;
        add_news(st, at, "standin", [&](text::Lang l) { return text::h_standin(l, in, out); },
                 [&](text::Lang l) { return std::string(text::standin(l, pick, out, team, in, race)); }, true);
    }
    real_fakes(st, s, r, at, progress);
}

void real_finalise(State &st, const Settings &s, Rng &r)
{
    // whatever wasn't told yet is official now
    for (State::RealMove &m : st.real_moves)
        if (m.driver != st.player)
            for (int to = m.done + 1; to <= 3; ++to)
                real_step(st, s, r, m, st.round, to);
    st.next_grid = st.real_next;
    for (const State::RealMove &m : st.real_moves) {
        if (m.kind == "left") {
            add_move(st, st.round, m.driver, m.from, "", "left");
            add_news(st, st.round, "official", [&](text::Lang l) { return text::h_left(l, driver_name(st, m.driver)); },
                     [&](text::Lang l) { return std::string(text::left_grid(l, driver_name(st, m.driver))); }, true);
        } else if (m.kind == "retirement") {
            add_move(st, st.round, m.driver, m.from, "", "retirement");
        } else {
            add_move(st, st.round, m.driver, m.from, m.to, m.kind);
        }
    }
    season_review(st, s);
    auto body = [&](text::Lang l) {
        std::string b;
        for (const Team &t : st.teams) {
            auto it = st.next_grid.find(t.id);
            if (it == st.next_grid.end() || (it->second[0].empty() && it->second[1].empty()))
                continue;
            b += t.name + ": " + (it->second[0].empty() ? text::free_seat(l) : driver_name(st, it->second[0])) + " / " +
                 (it->second[1].empty() ? text::free_seat(l) : driver_name(st, it->second[1])) + "\n";
        }
        return b;
    };
    add_news(st, st.round, "grid", [&](text::Lang l) { return std::string(text::t_grid(l, st.year + 1)); }, body, true);
    for (Deal &deal : st.deals)
        if (deal.stage != Stage::official)
            deal.stage = Stage::collapsed;
}

} // namespace

void real_season(State &st, const RealSeason &now, const RealSeason *next, const RealContext &ctx)
{
    st.real = next != nullptr;
    st.year = now.year;
    st.real_moves.clear();
    st.real_changes.clear();
    st.real_next.clear();
    // everyone on this year's grid and next year's exists in the market
    auto ensure = [&](const std::string &id) {
        if (id.empty() || st.driver(id))
            return;
        Driver d;
        d.id = id;
        d.kind = "f1";
        auto it = ctx.people.find(id);
        d.name = it != ctx.people.end() ? it->second.name : id;
        d.nat = it != ctx.people.end() ? it->second.nat : "";
        d.born = it != ctx.people.end() && it->second.born ? it->second.born : now.year - 25;
        st.drivers.emplace(id, d);
    };
    for (const RealSeason *y : {&now, next})
        if (y)
            for (const auto &[team, pair] : y->grid)
                for (const std::string &id : pair)
                    ensure(id);
    // the year's teams: names, codes and car pace (the fastest near 96, each % of gap a few points back)
    for (Team &t : st.teams) {
        if (auto it = now.names.find(t.id); it != now.names.end()) {
            if (!it->second.first.empty())
                t.name = it->second.first;
            if (!it->second.second.empty())
                t.code = it->second.second;
        }
        if (auto it = now.gap.find(t.id); it != now.gap.end())
            t.performance = clampf(96.0f - it->second * 6.0f, 60, 96);
    }
    // the year's ratings
    for (const auto &[id, r4] : now.ratings)
        if (Driver *d = st.driver(id)) {
            d->experience = r4[0];
            d->racecraft = r4[1];
            d->awareness = r4[2];
            d->pace = r4[3];
            d->pace_peak = std::max(d->pace_peak, d->pace);
            d->racecraft_peak = std::max(d->racecraft_peak, d->racecraft);
            d->awareness_peak = std::max(d->awareness_peak, d->awareness);
        }
    // the year's grid (the game's rows confirm it at every Team HQ)
    for (auto &[id, d] : st.drivers) {
        d.team.clear();
        d.seat = -1;
    }
    for (Team &t : st.teams) {
        t.seats = {"", ""};
        auto it = now.grid.find(t.id);
        if (it == now.grid.end())
            continue;
        for (int k = 0; k < 2; ++k)
            if (Driver *d = st.driver(it->second[static_cast<size_t>(k)])) {
                t.seats[static_cast<size_t>(k)] = d->id;
                d->team = t.id;
                d->seat = k;
                d->retired = d->retiring = false;
                auto u = ctx.until_now.find(d->id);
                d->contract = u != ctx.until_now.end() ? u->second : now.year + 1;
                if (d->salary <= 0)
                    d->salary = estimate_salary(*d, &t);
            }
    }
    // the real season's mid-season changes, at the same point of this calendar
    int rounds = std::max(1, st.rounds);
    for (const RealSeason::Change &c : now.changes) {
        if (!st.team(c.team) || c.in.empty())
            continue;
        int at = std::clamp(static_cast<int>(std::lround(static_cast<double>(c.round) * rounds / std::max(1, c.rounds))), 2, rounds);
        st.real_changes.push_back({c.team, c.out, c.in, c.kind, c.seat, at, c.until, false});
    }
    if (!next)
        return;
    // the moves that lead to next year's grid, each with its moment in the season
    std::map<std::string, std::string> team_now, team_next;
    for (const auto &[team, pair] : now.grid)
        for (const std::string &id : pair)
            if (!id.empty())
                team_now[id] = team;
    for (const auto &[team, pair] : next->grid)
        for (const std::string &id : pair)
            if (!id.empty())
                team_next[id] = team;
    for (const Team &t : st.teams)
        if (auto it = next->grid.find(t.id); it != next->grid.end())
            st.real_next[t.id] = it->second;
    auto schedule = [&](State::RealMove &m, double rumour_lo, double rumour_hi, double official_lo, double official_hi) {
        Rng mr(mix(st.seed, hash_str(m.driver) ^ static_cast<uint64_t>(now.year)));
        auto at = [&](double lo, double hi) {
            return std::clamp(static_cast<int>(std::lround(rounds * (lo + mr.uniform() * (hi - lo)))), 1, rounds);
        };
        m.rumour_at = at(rumour_lo, rumour_hi);
        m.official_at = std::max(m.rumour_at + 2, at(official_lo, official_hi));
        m.talks_at = std::clamp(m.rumour_at + 1 + static_cast<int>(mr.uniform() * (m.official_at - m.rumour_at - 1)), m.rumour_at + 1,
                                m.official_at);
        if (m.official_at > rounds)
            m.official_at = rounds;
    };
    for (const auto &[id, team] : team_next) {
        auto was = team_now.find(id);
        if (was != team_now.end() && was->second == team)
            continue; // stays
        State::RealMove m;
        m.driver = id;
        m.from = was != team_now.end() ? was->second : "";
        m.to = team;
        m.kind = was != team_now.end() ? "transfer" : ctx.earlier.count(id) ? "comeback" : "promotion";
        auto u = ctx.until_next.find(id);
        m.until = u != ctx.until_next.end() ? u->second : now.year + 2;
        if (m.kind == "promotion")
            schedule(m, 0.35, 0.65, 0.70, 1.0); // a rookie's seat is settled late
        else
            schedule(m, 0.15, 0.55, 0.50, 0.95);
        st.real_moves.push_back(m);
    }
    for (const auto &[id, team] : team_now) {
        if (team_next.count(id))
            continue;
        State::RealMove m;
        m.driver = id;
        m.from = team;
        // gone for good: a retirement for a veteran, a lost seat for a young driver. A young driver who chose to stop
        // is the exception: Nico Rosberg, champion in 2016 at 31
        static const std::set<std::string> young_retirements = {"sw16_rosberg"};
        const Driver *who = st.driver(id);
        m.kind = ctx.later.count(id) || (who && who->age(now.year) < 33 && !young_retirements.count(id)) ? "left" : "retirement";
        schedule(m, 0.4, 0.6, 0.45, 0.9);
        m.rumour_at = m.talks_at = m.official_at;
        st.real_moves.push_back(m);
    }
}

namespace {

// the season's awards, from the results the game reported (or the simulated races): once a season
void season_awards(State &st)
{
    if (std::any_of(st.awards.begin(), st.awards.end(), [&](const State::Award &a) { return a.year == st.year; }))
        return;
    std::vector<const Driver *> field;
    for (const auto &[id, d] : st.drivers)
        if (d.starts > 0)
            field.push_back(&d);
    if (field.size() < 4)
        return;
    std::sort(field.begin(), field.end(), [](const Driver *a, const Driver *b) {
        return a->points != b->points ? a->points > b->points : a->wins != b->wins ? a->wins > b->wins : a->id < b->id;
    });
    struct Got {
        State::Award award;
        int a = 0, b = 0;
        float c = 0;
    };
    std::vector<Got> got;
    auto add = [&](const char *key, const std::string &who, const std::string &name, int a, int b, float c) {
        got.push_back({{st.year, key, who, name, text::award_detail(kEn, key, a, b, c)}, a, b, c});
    };
    // the champions
    const Driver *champ = field[0];
    add("champion", champ->id, driver_name(st, champ->id), champ->points, champ->wins, 0);
    if (Driver *cd = st.driver(champ->id))
        cd->titles += 1;
    const Team *best_team = nullptr;
    for (const Team &t : st.teams)
        if (t.active && t.points > 0 && (!best_team || t.points > best_team->points))
            best_team = &t;
    // My Team: the player's own team (not in the model) scores what its two drivers scored
    int own_points = -1;
    if (!st.player.empty() && !st.team(st.player_team)) {
        own_points = 0;
        for (const std::string &who : {st.player, st.own_mate})
            if (const Driver *d = st.driver(who))
                own_points += d->points;
    }
    if (own_points > 0 && (!best_team || own_points > best_team->points))
        add("constructors", "#own", "My Team", own_points, 0, 0);
    else if (best_team)
        add("constructors", best_team->id, best_team->name, best_team->points, 0, 0);
    // the rookie of the year: the best of the drivers in their first season
    int min_starts = std::max(3, st.rounds / 3);
    for (const Driver *d : field)
        if (d->seasons == 0 && d->kind != "player" && d->id != st.player && d->starts >= min_starts) {
            add("rookie", d->id, driver_name(st, d->id), d->points, d->podiums, 0);
            break;
        }
    // beat the car: the driver who finished furthest above where his car belongs
    std::vector<const Team *> by_car;
    for (const Team &t : st.teams)
        if (t.active)
            by_car.push_back(&t);
    std::sort(by_car.begin(), by_car.end(), [](const Team *a, const Team *b) { return a->performance > b->performance; });
    const Driver *over = nullptr;
    float over_gap = 2.5f;
    for (size_t i = 0; i < field.size(); ++i) {
        const Driver *d = field[i];
        if (d == champ || d->team.empty() || d->starts < min_starts)
            continue;
        size_t car = 0;
        for (size_t j = 0; j < by_car.size(); ++j)
            if (by_car[j]->id == d->team)
                car = j;
        float gap = static_cast<float>(car * 2 + 1) - static_cast<float>(i);
        if (gap > over_gap) {
            over_gap = gap;
            over = d;
        }
    }
    if (over)
        add("overachiever", over->id, driver_name(st, over->id), over->points, static_cast<int>(std::lround(over_gap)), 0);
    // the most improved: the biggest rise in market value since the season's first race
    const Driver *up = nullptr;
    float rise = 1.0f;
    for (const Driver *d : field)
        if (d->value_start > 0 && d->kind != "player" && value(*d) - d->value_start > rise) {
            rise = value(*d) - d->value_start;
            up = d;
        }
    if (up)
        add("improved", up->id, driver_name(st, up->id), 0, 0, rise);

    for (const Got &g : got)
        st.awards.push_back(g.award);
    if (st.awards.size() > 60)
        st.awards.erase(st.awards.begin(), st.awards.begin() + static_cast<long>(st.awards.size() - 60));
    int year = st.year;
    add_news(st, st.round, "awards", [&](text::Lang l) { return text::h_awards(l, year); },
             [&](text::Lang l) {
                 std::string b;
                 for (const Got &g : got)
                     b += text::award_title(l, g.award.key) + ": " + g.award.name + " (" +
                          text::award_detail(l, g.award.key, g.a, g.b, g.c) + ")\n";
                 return b;
             },
             true);
}

// which outlet got the season right, and which didn't
void media_scorecard(State &st, int round)
{
    int best = -1, worst = -1;
    float best_r = -1, worst_r = 2;
    for (int i = 0; i < static_cast<int>(st.outlets.size()); ++i) {
        const State::Outlet &o = st.outlets[static_cast<size_t>(i)];
        int n = o.season_hits + o.season_misses;
        if (n < 2)
            continue;
        float ratio = static_cast<float>(o.season_hits) / static_cast<float>(n);
        if (ratio > best_r) {
            best_r = ratio;
            best = i;
        }
        if (ratio < worst_r) {
            worst_r = ratio;
            worst = i;
        }
    }
    if (best < 0 || worst < 0 || best == worst)
        return;
    const State::Outlet &b = st.outlets[static_cast<size_t>(best)], &w = st.outlets[static_cast<size_t>(worst)];
    int bh = b.season_hits, bn = b.season_hits + b.season_misses, wh = w.season_hits, wn = w.season_hits + w.season_misses;
    int br = outlet_rating(st, best), wr = outlet_rating(st, worst);
    add_news(st, round, "media", [&](text::Lang l) { return text::h_media(l); },
             [&](text::Lang l) {
                 return text::media_scorecard(l, text::outlet_name(best), bh, bn, br, text::outlet_name(worst), wh, wn, wr);
             },
             true);
}

// My Team: the scout's shortlist for the second seat, twice a season
void scout_news(State &st, const Settings &s, int round, double progress)
{
    if (st.player.empty() || st.team(st.player_team) || st.real)
        return; // only a player in his own team, whom the model doesn't seat
    if (!((st.scout_told == 0 && progress >= 0.35) || (st.scout_told == 1 && progress >= 0.7)))
        return;
    ++st.scout_told;
    std::vector<ScoutPick> picks = scout(st, s, "", 3);
    if (picks.empty())
        return;
    int year = st.year + 1;
    add_news(st, round, "scout", [&](text::Lang l) { return text::h_scout(l, year); },
             [&](text::Lang l) {
                 std::string b = text::scout_intro(l, year) + "\n";
                 for (const ScoutPick &p : picks) {
                     const Driver *d = st.driver(p.driver);
                     if (!d)
                         continue;
                     std::string where = d->team.empty() ? text::scout_status(l, p.status) : team_name(st, d->team);
                     b += "- " + d->name + " (" + where + ", " + std::to_string(d->age(st.year + 1)) + "): " +
                          text::scout_interest(l, p.interest) + "\n";
                 }
                 return b;
             },
             false);
}

} // namespace

// 1.1.0 phase 5
#include "phase5.inl"
// 1.1.0 phase 6
#include "phase6.inl"
// 1.1.0, from player reports: refused seats at the new season, observer careers (My Team)
#include "phase7.inl"

void complete_round(State &st, const Settings &s, int round, int rounds)
{
    if (rounds > 0)
        st.rounds = rounds;
    if (round <= st.round || st.finalised)
        return;
    {
    StateRng sr(st);
    Rng &r = sr.r;
    draw_all_traits(st); // drivers the plugin added (the real seasons' people, unknown drivers) get a character too
    for (int at = st.round + 1; at <= round && at <= st.rounds; ++at) {
        st.round = at;
        double progress = static_cast<double>(at) / std::max(1, st.rounds);
        // the game's results for this round arrived: the drivers have a start for every round the market saw
        bool real_results = false;
        for (const auto &[id, d] : st.drivers)
            if (d.starts >= at - st.round_base) {
                real_results = true;
                break;
            }
        if (!real_results)
            simulate_race(st, r);
        end_standins(st, s, at);
        update_stock(st);
        pressure_news(st, s, r, at, progress);
        player_news(st, s, r, at, progress);
        if (st.real) {
            real_round(st, s, r, at, progress);
        } else {
            // an observer career (My Team): only the game changes seats, so the market retires, swaps and sacks nobody
            if (!st.retirements_drawn && progress >= 0.60 && !st.observer)
                draw_retirements(st, s, r, at);
            // a team with a sister team swaps drivers with it before it sacks one (Red Bull and Racing Bulls)
            if (s.living && !st.observer)
                sister_swap(st, s, r, at, progress);
            if (s.midseason && !st.midseason_done && progress >= 0.40 && !st.observer) {
                midseason(st, s, r, at);
                if (progress >= 0.65)
                    st.midseason_done = true;
            }
            if (s.living) {
                sponsor_round(st, s, r, at, progress);
                principal_round(st, s, r, at, progress);
            }
            academy_round(st, s, r, at, progress);
            ambition_round(st, s, r, at, progress);
            strategy_review(st, s, at, progress);
            market_round(st, s, r, at, progress);
            if (s.deadline && deadline_races_left(st) > 0 && !st.deadline_told)
                deadline_news(st, s, at);
            scout_news(st, s, at, progress);
            if (progress >= 0.5 && st.f2_reported == 0)
                f2_update(st, s, r, at, false);
            if (!st.staff_done && progress >= 0.45 && (progress > 0.85 || r.chance(0.35)))
                staff_market(st, s, r, at);
            feuds_round(st, s, r, at, progress);
        }
        if (at == round) { // not while catching up: the race would already be gone
            if (!st.real && !st.observer)
                maybe_standin(st, s, r, at);
            press_round(st, s, r, at, progress);
        }
        score_outlets(st);
    }
    }
    if (st.round >= st.rounds)
        finalise_season(st, s);
}

void finalise_season(State &st, const Settings &s)
{
    if (st.finalised)
        return;
    {
        StateRng sr(st);
        Rng &r = sr.r;
        if (st.real) {
            real_finalise(st, s, r);
            farewells(st, st.round);
            season_awards(st);
            score_outlets(st);
            st.finalised = true;
            return;
        }
        if (st.observer) {
            // the game makes next season's grid: the market plans none, and its deals wait for the grid to tell
            // whether they came true (observe_grid, then start_new_season)
            f2_update(st, s, r, st.round, true);
            for (Team &t : st.teams)
                st.next_grid[t.id] = t.seats;
            season_review(st, s);
            reserve_market(st, s, r, st.round);
            season_awards(st);
            score_outlets(st);
            media_scorecard(st, st.round);
            st.finalised = true;
            return;
        }
        if (!st.retirements_drawn)
            draw_retirements(st, s, r, st.round);
        farewells(st, st.round);
        f2_update(st, s, r, st.round, true);
        // exit clauses and feuds free drivers despite their contracts
        for (Team &t : st.teams)
            for (int k = 0; k < 2; ++k) {
                Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
                if (!d || st.locked(t, k, s) || d->contract <= st.year || d->retiring)
                    continue;
                if (d->clause_rank > 0 && team_rank(st, t.id) > d->clause_rank) {
                    d->wants_out = true;
                    add_news(st, st.round, "clause",
                 [&](text::Lang l) { return text::h_clause(l, d->name); },
                 [&](text::Lang l) { return std::string(text::clause_triggered(l, d->name, t.name, d->clause_rank)); }, true);
                    continue;
                }
                const Driver *mate = st.driver(t.seats[static_cast<size_t>(1 - k)]);
                if (mate && feuding(*d, *mate) && (value(*d) < value(*mate) || mate->id == st.player)) {
                    d->wants_out = true;
                    add_news(st, st.round, "feud",
                 [&](text::Lang l) { return text::h_feud_exit(l, d->name, t.name); },
                 [&](text::Lang l) { return std::string(text::feud_exit(l, d->name, t.name)); }, true);
                }
            }
        Plan plan = plan_market(st, s, plan_seed(st, true));
        st.next_grid = plan.grid;
        std::string renewals;
        int renewal_count = 0;
        std::vector<const Move *> todo;
        for (const Move &m : plan.moves) {
            if (m.kind == "left" || m.kind == "retirement") {
                add_move(st, st.round, m.driver, m.from, "", m.kind);
                if (m.kind == "left")
                    add_news(st, st.round, "official",
                 [&](text::Lang l) { return text::h_left(l, driver_name(st, m.driver)); },
                 [&](text::Lang l) { return std::string(text::left_grid(l, driver_name(st, m.driver))); }, true);
                continue;
            }
            todo.push_back(&m);
        }
        // the signings in the order the dominoes fall: a seat's new driver comes once its driver has gone
        auto occupant = [&](const Move &m) -> std::string {
            const Team *t = st.team(m.to);
            if (!t || m.from == m.to)
                return {};
            for (const std::string &who : t->seats)
                if (who.empty())
                    return {}; // a seat is free already
            // the driver leaving that team: his move is told first
            std::string out = leaving(st, plan, m.to, m.driver, false);
            return out.empty() ? leaving(st, plan, m.to, m.driver, true) : out;
        };
        std::set<std::string> done;
        std::map<std::string, int> deal_of; // driver -> his deal's id this winter
        auto waiting = [&](const std::string &who) {
            return !who.empty() && !done.count(who) &&
                   std::any_of(todo.begin(), todo.end(), [&](const Move *x) { return x->driver == who; });
        };
        bool force = false; // a ring of moves (two drivers swapping seats): one goes first
        while (done.size() < todo.size()) {
            bool moved = false;
            for (const Move *mp : todo) {
                const Move &m = *mp;
                if (done.count(m.driver))
                    continue;
                std::string occ = occupant(m);
                if (waiting(occ) && !force)
                    continue;
                force = false;
                moved = true;
                done.insert(m.driver);
                Deal *deal = find_deal(st, m.driver);
                if (deal && (deal->team != m.to || deal->fake)) {
                    deal->stage = Stage::collapsed;
                    deal = nullptr;
                }
                if (!deal)
                    deal = &add_deal(st, {m.driver, m.to, m.kind, st.year + 1, plan.until[m.driver], Stage::agreed, false, st.round});
                if (deal->chain == 0 && deal_of.count(occ))
                    deal->chain = deal_of[occ];
                deal_of[m.driver] = deal->id;
                if (deal->stage != Stage::official) {
                    if (m.kind == "renewal") {
                        deal->stage = Stage::official;
                        if (!renewals.empty())
                            renewals += ", ";
                        renewals += driver_name(st, m.driver);
                        ++renewal_count;
                    } else {
                        announce_official(st, s, r, *deal, st.round);
                    }
                }
                add_move(st, st.round, m.driver, m.from, m.to, m.kind);
            }
            if (!moved)
                force = true;
        }
        if (!renewals.empty())
            add_news(st, st.round, "official",
                 [&](text::Lang l) { return text::h_renewals(l, renewal_count); },
                 [&](text::Lang l) { return std::string(text::renewals_summary(l, renewals)); }, true);
        season_review(st, s);
        // the grid in one item
        auto body = [&](text::Lang l) {
            std::string b;
            for (const Team &t : st.teams) {
                if (!t.active)
                    continue;
                auto &g = st.next_grid[t.id];
                b += t.name + ": " + (g[0].empty() ? text::free_seat(l) : driver_name(st, g[0])) + " / " +
                     (g[1].empty() ? text::free_seat(l) : driver_name(st, g[1])) + "\n";
            }
            return b;
        };
        add_news(st, st.round, "grid", [&](text::Lang l) { return std::string(text::t_grid(l, st.year + 1)); }, body, true);
        for (Deal &deal : st.deals)
            if (deal.stage != Stage::official)
                deal.stage = Stage::collapsed;
        reserve_market(st, s, r, st.round); // next season's reserve drivers, now that the race seats are settled
        season_awards(st);
        score_outlets(st);
        media_scorecard(st, st.round);
    }
    st.finalised = true;
}

std::vector<SeatChange> start_new_season(State &st, const Settings &s, std::vector<RatingChange> *ratings)
{
    if (!st.finalised)
        finalise_season(st, s);
    std::vector<SeatChange> changes;
    StateRng sr(st);
    Rng &r = sr.r;
    if (st.observer) {
        // the grid is the game's, as the last read showed it; the deals it didn't bear out fell through
        for (Team &t : st.teams)
            st.next_grid[t.id] = t.seats;
        for (Deal &deal : st.deals)
            if (deal.stage != Stage::official)
                deal.stage = Stage::collapsed;
        score_outlets(st);
    }

    // stand-ins end with the season: the new grid puts everyone in place
    st.standins.clear();
    st.pending.erase(std::remove_if(st.pending.begin(), st.pending.end(),
                                    [](const SeatChange &c) { return c.reason == "standin" || c.reason == "return"; }),
                     st.pending.end());

    // contracts of the deals
    std::map<std::string, int> until, clause;
    for (const Deal &deal : st.deals)
        if (deal.stage == Stage::official && !deal.fake) {
            until[deal.driver] = deal.until;
            clause[deal.driver] = deal.clause;
        }

    // seat changes against the grid as it is now
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k) {
            if (st.locked(t, k, s))
                continue;
            const std::string &target = st.next_grid[t.id][static_cast<size_t>(k)];
            if (target.empty() || target == t.seats[static_cast<size_t>(k)])
                continue;
            changes.push_back({t.id, k, target, "season"});
        }

    // 1.1.0 phase 5: the season's numbers into each career, and the prize money by the constructors' order
    bank_season(st);
    season_money(st, s);

    // the model follows: everyone off, then the new grid
    std::map<std::string, std::array<std::string, 2>> keep;
    for (Team &t : st.teams)
        for (int k = 0; k < 2; ++k)
            if (st.locked(t, k, s))
                keep[t.id][static_cast<size_t>(k)] = t.seats[static_cast<size_t>(k)];
    for (auto &[id, d] : st.drivers) {
        bool was_on = !d.team.empty();
        d.team.clear();
        d.seat = -1;
        if (d.retiring) {
            d.retiring = false;
            d.retired = true;
        }
        d.form *= 0.5f;
        d.points = 0;
        d.starts = 0;
        d.wins = 0;
        d.podiums = 0;
        d.value_start = 0;
        d.values_last = std::move(d.values);
        d.values.clear();
        d.released = false;
        d.pressured = false;
        d.trend = 0;
        d.last_value = 0;
        d.wants_out = false;
        d.f2_pos = 0;
        if (was_on && d.kind != "player")
            d.seasons += 1;
    }
    std::vector<std::string> newcomers;
    for (Team &t : st.teams) {
        std::array<std::string, 2> before = t.seats;
        for (int k = 0; k < 2; ++k) {
            std::string who = keep.count(t.id) && !keep[t.id][static_cast<size_t>(k)].empty()
                                  ? keep[t.id][static_cast<size_t>(k)]
                                  : st.next_grid[t.id][static_cast<size_t>(k)];
            t.seats[static_cast<size_t>(k)].clear();
            if (who.empty())
                continue;
            t.seats[static_cast<size_t>(k)] = who;
            if (Driver *d = st.driver(who)) {
                d->team = t.id;
                d->seat = k;
                if (until.count(who)) {
                    d->contract = until[who];
                    d->clause_rank = clause[who];
                    d->salary = estimate_salary(*d, &t); // a new deal, a new salary
                }
                if (d->salary <= 0)
                    d->salary = estimate_salary(*d, &t);
                if (d->contract <= st.year)
                    d->contract = st.year + 1;
            }
            if (who != before[0] && who != before[1])
                newcomers.push_back(driver_name(st, who) + " (" + t.name + ")");
        }
    }
    // an observer career's newcomers came with the game's grid over the winter (observe_grid told them)
    if (st.observer)
        for (const Move &m : st.history)
            if (m.year == st.year && m.round >= st.rounds && !m.to.empty() &&
                (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback"))
                if (const Team *t = st.team(m.to))
                    newcomers.push_back(driver_name(st, m.driver) + " (" + t->name + ")");
    for (auto &[id, d] : st.drivers) {
        if (!d.team.empty() || d.retired || st.shielded(d.id))
            continue;
        if (d.contract <= st.year + 1)
            d.contract = 0;
        // a veteran without a seat calls it a day
        int age = d.age(st.year + 1);
        if (!st.real && d.kind == "f1" && age >= 34 && r.chance(age >= 38 ? 0.9 : 0.6)) {
            d.retired = true;
            add_move(st, 0, d.id, "", "", "retirement");
        }
        // juniors keep racing (F2, reserve duties) and get better, whatever the ratings option says
        if (age <= 25 && d.kind != "legend") {
            auto grow = [&](float &v, float peak) {
                if (peak > v)
                    v = clampf(v + (peak - v) * 0.30f + static_cast<float>(r.normal() * 0.8), 30, 99);
            };
            grow(d.pace, d.pace_peak);
            grow(d.racecraft, d.racecraft_peak);
            grow(d.awareness, d.awareness_peak);
            d.experience = clampf(d.experience + 4.0f, 0, 99);
        }
    }

    // cars change over a winter: the best teams of this year carry some of it over
    std::vector<Team *> ranked;
    for (Team &t : st.teams)
        ranked.push_back(&t);
    bool scored = std::any_of(st.teams.begin(), st.teams.end(), [](const Team &t) { return t.points > 0; });
    if (scored)
        std::stable_sort(ranked.begin(), ranked.end(), [](const Team *a, const Team *b) { return a->points > b->points; });
    double sigma = s.intensity == Intensity::chaos ? 7.0 : s.intensity == Intensity::lively ? 4.0 : 2.0;
    for (size_t i = 0; i < ranked.size(); ++i) {
        Team &t = *ranked[i];
        float from_points = scored ? 95.0f - static_cast<float>(i) * (40.0f / std::max<size_t>(1, ranked.size() - 1))
                                   : t.performance;
        float design = (t.td_rating - 72.0f) * 0.15f; // the technical director's hand on next year's car
        t.performance =
            clampf(0.65f * t.performance + 0.35f * from_points + design + static_cast<float>(r.normal() * sigma), 45, 98);
        t.td_rating = clampf(t.td_rating + static_cast<float>(r.normal() * 1.5), 55, 97);
        t.points = 0;
    }
    // each team's plan for the new season, from its new car
    for (Team &t : st.teams)
        t.strategy = pick_strategy(st, t);
    // the outlets: a new season's newsrooms (a new editor, a source gone) are a little better or worse
    ensure_outlets(st);
    for (State::Outlet &o : st.outlets) {
        o.accuracy = clampf(o.accuracy + static_cast<float>(r.normal() * 0.06), 0.08f, 0.92f);
        o.season_hits = o.season_misses = 0;
    }

    // drivers grow and fade
    if (s.progression)
        for (auto &[id, d] : st.drivers) {
            if (d.retired || d.kind == "legend" || d.kind == "player" || d.team.empty())
                continue; // drivers off the grid grew above
            int age = d.age(st.year + 1);
            auto grow = [&](float &v, float peak) {
                if (age <= 25 && peak > v)
                    v += (peak - v) * 0.25f + static_cast<float>(r.normal() * 0.8);
                else if (age >= 33)
                    v -= static_cast<float>(0.3 + r.uniform() * 1.5);
                v = clampf(v, 30, 99);
            };
            grow(d.pace, d.pace_peak);
            grow(d.racecraft, d.racecraft_peak);
            grow(d.awareness, d.awareness_peak);
            if (!d.team.empty())
                d.experience = clampf(d.experience + 3.0f, 0, 99);
            // only our own talents' ratings go into the game: the game develops its drivers itself, the market can't
            // read where it took them, and writing the market's numbers over them swung ratings by 20-30 points
            // after a few seasons (a 1.0.1 report). The market's model still grows and fades everyone.
            if (ratings && !d.team.empty() && d.regen)
                ratings->push_back({d.id, d.experience, d.racecraft, d.awareness, d.pace});
        }

    st.year += 1;
    st.season_index += 1;
    st.round = 0;
    st.round_base = 0;
    st.results_read = 0;
    st.deals.clear();
    st.retirements_drawn = false;
    st.midseason_moves = 0;
    st.midseason_done = false;
    st.finalised = false;
    st.next_grid.clear();
    st.f2_table.clear();
    st.f2_reported = 0;
    st.staff_done = false;
    st.teammate_wish.clear();
    st.press.reset();
    st.refusals_told.clear();
    st.strategy_reviewed = false;
    st.ambition_checked = false;
    st.deadline_told = false;
    st.scout_told = 0;
    st.changes.clear();
    st.push_signing = false;
    st.sponsor_news = 0;
    st.principals_out = 0;
    st.team_comfort += st.team_comfort > 0 ? -1 : st.team_comfort < 0 ? 1 : 0; // a new season softens it
    academy_new_season(st);
    for (Team &t : st.teams)
        t.budget_start = t.budget;
    std::string list;
    for (size_t i = 0; i < newcomers.size(); ++i)
        list += (i ? ", " : "") + newcomers[i];
    add_news(st, 0, "summary",
                 [&](text::Lang l) { return std::string(text::t_season(l, st.year)); },
                 [&](text::Lang l) { return std::string(text::season_start(l, st.year, static_cast<int>(newcomers.size()), list)); }, true);
    season_preview(st, s);
    return changes;
}

void report_meeting(State &st, const std::string &team)
{
    const Team *t = st.team(team);
    if (!t)
        return;
    StateRng sr(st);
    int pick = sr.r.range(0, 9);
    std::string name = t->name;
    add_news(st, st.round, "player", [&](text::Lang l) { return text::h_meeting(l, name); },
             [&](text::Lang l) { return text::meeting(l, pick, name); }, false);
}

void cancel_standin(State &st, const std::string &team, int seat, const std::string &in)
{
    size_t before = st.standins.size();
    st.standins.erase(std::remove_if(st.standins.begin(), st.standins.end(),
                                     [&](const State::Standin &sb) { return sb.team == team && sb.seat == seat && sb.in == in; }),
                      st.standins.end());
    if (st.standins.size() == before)
        return;
    st.pending.erase(std::remove_if(st.pending.begin(), st.pending.end(),
                                    [&](const SeatChange &c) { return c.team == team && c.seat == seat && c.reason == "return"; }),
                     st.pending.end());
    for (auto it = st.news.rbegin(); it != st.news.rend(); ++it)
        if (it->kind == "standin") {
            st.news.erase(std::next(it).base());
            break;
        }
}

std::vector<SeatChange> take_pending(State &st)
{
    std::vector<SeatChange> out;
    out.swap(st.pending);
    return out;
}

std::map<std::string, std::array<std::string, 2>> projected_grid(const State &st, const Settings &s)
{
    if (st.finalised)
        return st.next_grid;
    // only what is settled: locked seats, official and agreed deals, and contracts that run on
    std::map<std::string, std::array<std::string, 2>> g;
    for (const Team &t : st.teams) {
        g[t.id] = {"", ""};
        for (int k = 0; k < 2; ++k) {
            const Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            if (st.locked(t, k, s) || (d && d->contract > st.year && !d->retiring && !d->released))
                g[t.id][static_cast<size_t>(k)] = t.seats[static_cast<size_t>(k)];
        }
    }
    for (const Deal &deal : st.deals) {
        // an observer career's agreed deals aren't settled: the game's grid decides them
        if (deal.fake || (deal.stage != Stage::official && (deal.stage != Stage::agreed || st.observer)))
            continue;
        for (auto &[team, pair] : g)
            for (auto &who : pair)
                if (who == deal.driver && team != deal.team)
                    who.clear();
        auto &pair = g[deal.team];
        if (pair[0] != deal.driver && pair[1] != deal.driver) {
            if (pair[0].empty())
                pair[0] = deal.driver;
            else if (pair[1].empty())
                pair[1] = deal.driver;
        }
    }
    return g;
}

// ---------------------------------------------------------------------------------------------------
// Languages

void localize(State &st, bool es)
{
    st.view_es = es;
    for (NewsItem &n : st.news) {
        n.title = es ? n.title_es : n.title_en;
        n.body = es ? n.body_es : n.body_en;
    }
    if (st.press) {
        st.press->question = es ? st.press->question_es : st.press->question_en;
        st.press->answers = es ? st.press->answers_es : st.press->answers_en;
    }
}

// ---------------------------------------------------------------------------------------------------
// Persistence

Value to_json(const State &st)
{
    Value v = Value::object();
    v["schema"] = st.schema;
    v["group"] = st.group;
    v["career"] = st.career;
    v["year"] = st.year;
    v["season_index"] = st.season_index;
    v["round"] = st.round;
    v["rounds"] = st.rounds;
    v["round_base"] = st.round_base;
    v["results_read"] = st.results_read;
    v["last_meeting"] = static_cast<int64_t>(st.last_meeting);
    v["seed"] = std::to_string(st.seed);
    v["rng"] = std::to_string(st.rng);
    v["player"] = st.player;
    v["player_team"] = st.player_team;
    v["retirements_drawn"] = st.retirements_drawn;
    v["midseason_moves"] = st.midseason_moves;
    v["midseason_done"] = st.midseason_done;
    v["finalised"] = st.finalised;
    Value &drivers = v["drivers"];
    drivers = Value::object();
    for (const auto &[id, d] : st.drivers) {
        Value o = Value::object();
        o["name"] = d.name;
        o["nat"] = d.nat;
        o["kind"] = d.kind;
        o["academy"] = d.academy;
        o["born"] = d.born;
        o["stat"] = d.stat;
        o["pace"] = d.pace;
        o["racecraft"] = d.racecraft;
        o["awareness"] = d.awareness;
        o["experience"] = d.experience;
        o["pace_peak"] = d.pace_peak;
        o["racecraft_peak"] = d.racecraft_peak;
        o["awareness_peak"] = d.awareness_peak;
        o["experience_peak"] = d.experience_peak;
        o["team"] = d.team;
        o["seat"] = d.seat;
        o["contract"] = d.contract;
        o["form"] = d.form;
        o["points"] = d.points;
        o["starts"] = d.starts;
        o["seasons"] = d.seasons;
        o["retiring"] = d.retiring;
        o["retired"] = d.retired;
        o["released"] = d.released;
        o["ratings_seen"] = d.ratings_seen;
        o["game_overall"] = d.game_overall;
        o["focus"] = d.focus;
        o["pressured"] = d.pressured;
        o["reserve_of"] = d.reserve_of;
        o["salary"] = d.salary;
        o["initials"] = d.initials;
        o["female"] = d.female;
        Value keys = Value::array();
        for (const std::string &k : d.name_keys)
            keys.push_back(k);
        o["name_keys"] = std::move(keys);
        o["alias_first"] = d.alias_first;
        o["alias_last"] = d.alias_last;
        o["regen"] = d.regen;
        o["backing"] = d.backing;
        o["clause_rank"] = d.clause_rank;
        o["wants_out"] = d.wants_out;
        o["f2_pos"] = d.f2_pos;
        Value feuds = Value::array();
        for (const std::string &f : d.feuds)
            feuds.push_back(f);
        o["feuds"] = std::move(feuds);
        o["last_value"] = d.last_value;
        o["trend"] = d.trend;
        o["ambition"] = d.ambition;
        o["loyalty"] = d.loyalty;
        o["money"] = d.money;
        o["wins"] = d.wins;
        o["podiums"] = d.podiums;
        o["value_start"] = d.value_start;
        o["number"] = d.number;
        o["number_tries"] = d.number_tries;
        o["home"] = d.home;
        auto floats = [](const std::vector<float> &v) {
            Value a = Value::array();
            for (float x : v)
                a.push_back(x);
            return a;
        };
        o["values"] = floats(d.values);
        o["values_last"] = floats(d.values_last);
        o["c_starts"] = d.c_starts;
        o["c_wins"] = d.c_wins;
        o["c_podiums"] = d.c_podiums;
        o["c_points"] = d.c_points;
        o["titles"] = d.titles;
        Value pos = Value::array(), rteam = Value::array();
        for (int p : d.race_pos)
            pos.push_back(p);
        for (const std::string &t : d.race_team)
            rteam.push_back(t);
        o["race_pos"] = std::move(pos);
        o["race_team"] = std::move(rteam);
        drivers[id] = std::move(o);
    }
    Value &teams = v["teams"];
    teams = Value::array();
    for (const Team &t : st.teams)
        teams.push_back(Value::object({{"id", t.id}, {"name", t.name}, {"code", t.code}, {"family", t.family},
                                       {"stat", t.stat}, {"prestige", t.prestige}, {"performance", t.performance},
                                       {"patience", t.patience}, {"junior", t.junior}, {"seat0", t.seats[0]},
                                       {"seat1", t.seats[1]}, {"points", t.points}, {"colour", static_cast<int64_t>(t.colour)},
                                       {"budget", t.budget}, {"td_name", t.td_name}, {"td_rating", t.td_rating},
                                       {"strategy", t.strategy}, {"optional", t.optional}, {"active", t.active},
                                       {"tp_name", t.tp_name}, {"tp_since", t.tp_since}, {"budget_start", t.budget_start},
                                       {"reserve", t.reserve}, {"reserve_until", t.reserve_until}}));
    Value &deals = v["deals"];
    deals = Value::array();
    for (const Deal &d : st.deals)
        deals.push_back(Value::object({{"driver", d.driver}, {"team", d.team}, {"kind", d.kind}, {"year", d.year},
                                       {"until", d.until}, {"stage", stage_name(d.stage)}, {"fake", d.fake},
                                       {"opened", d.opened}, {"source", d.source}, {"clause", d.clause}, {"id", d.id},
                                       {"chain", d.chain}, {"waits", d.waits}, {"scored", d.scored}}));
    Value &standins = v["standins"];
    standins = Value::array();
    for (const State::Standin &sb : st.standins)
        standins.push_back(Value::object(
            {{"out", sb.out}, {"in", sb.in}, {"team", sb.team}, {"seat", sb.seat}, {"race", sb.race}}));
    Value &news = v["news"];
    news = Value::array();
    for (const NewsItem &n : st.news)
        news.push_back(Value::object({{"id", n.id}, {"year", n.year}, {"round", n.round}, {"kind", n.kind},
                                      {"title", n.title_en}, {"body", n.body_en}, {"title_es", n.title_es},
                                      {"body_es", n.body_es}, {"official", n.official}, {"shown", n.shown}, {"driver", n.driver},
                                      {"from", n.from}, {"to", n.to}, {"until", n.until}, {"big", n.big}}));
    Value &history = v["history"];
    history = Value::array();
    for (const Move &m : st.history)
        history.push_back(Value::object({{"year", m.year}, {"round", m.round}, {"driver", m.driver}, {"from", m.from},
                                         {"to", m.to}, {"kind", m.kind}}));
    Value &pending = v["pending"];
    pending = Value::array();
    for (const SeatChange &c : st.pending)
        pending.push_back(Value::object({{"team", c.team}, {"seat", c.seat}, {"driver", c.driver}, {"reason", c.reason}}));
    Value &next = v["next_grid"];
    next = Value::object();
    for (const auto &[team, pair] : st.next_grid)
        next[team] = Value::array({pair[0], pair[1]});
    v["real"] = st.real;
    Value &real_moves = v["real_moves"];
    real_moves = Value::array();
    for (const State::RealMove &m : st.real_moves)
        real_moves.push_back(Value::object({{"driver", m.driver}, {"from", m.from}, {"to", m.to}, {"kind", m.kind}, {"until", m.until},
                                            {"rumour_at", m.rumour_at}, {"talks_at", m.talks_at}, {"official_at", m.official_at},
                                            {"done", m.done}}));
    Value &real_changes = v["real_changes"];
    real_changes = Value::array();
    for (const State::RealChange &c : st.real_changes)
        real_changes.push_back(Value::object({{"team", c.team}, {"out", c.out}, {"in", c.in}, {"kind", c.kind}, {"seat", c.seat},
                                              {"round", c.round}, {"until", c.until}, {"told", c.told}}));
    Value &real_next = v["real_next"];
    real_next = Value::object();
    for (const auto &[team, pair] : st.real_next)
        real_next[team] = Value::array({pair[0], pair[1]});
    v["next_news_id"] = st.next_news_id;
    v["seen_news"] = st.seen_news;
    v["banner_news"] = st.banner_news;
    v["preview_year"] = st.preview_year;
    auto mail = [](const State::Mail &m) { return Value::object({{"subject", m.subject}, {"body", m.body}}); };
    v["mail_preview"] = mail(st.mail_preview);
    v["mail_radar"] = mail(st.mail_radar);
    v["mail_grid"] = mail(st.mail_grid);
    if (st.press) {
        Value p = Value::object({{"id", st.press->id}, {"topic", st.press->topic}, {"subject", st.press->subject},
                                 {"question", st.press->question_en}, {"question_es", st.press->question_es},
                                 {"asked", st.press->asked}});
        Value answers = Value::array(), answers_es = Value::array(), options = Value::array();
        for (const std::string &a : st.press->answers_en)
            answers.push_back(a);
        for (const std::string &a : st.press->answers_es)
            answers_es.push_back(a);
        p["answers_es"] = std::move(answers_es);
        for (const std::string &o : st.press->options)
            options.push_back(o);
        p["answers"] = std::move(answers);
        p["options"] = std::move(options);
        v["press"] = std::move(p);
    }
    v["next_press_id"] = st.next_press_id;
    v["player_loyalty"] = st.player_loyalty;
    v["teammate_wish"] = st.teammate_wish;
    Value f2 = Value::array();
    for (const std::string &id : st.f2_table)
        f2.push_back(id);
    v["f2_table"] = std::move(f2);
    v["f2_reported"] = st.f2_reported;
    v["talents_year"] = st.talents_year;
    v["staff_done"] = st.staff_done;
    v["next_deal_id"] = st.next_deal_id;
    Value outlets = Value::array();
    for (const State::Outlet &o : st.outlets)
        outlets.push_back(Value::object({{"accuracy", o.accuracy}, {"hits", o.hits}, {"misses", o.misses},
                                         {"season_hits", o.season_hits}, {"season_misses", o.season_misses}}));
    v["outlets"] = std::move(outlets);
    Value awards = Value::array();
    for (const State::Award &a : st.awards)
        awards.push_back(Value::object({{"year", a.year}, {"key", a.key}, {"who", a.who}, {"name", a.name}, {"line", a.line}}));
    v["awards"] = std::move(awards);
    Value refusals = Value::array();
    for (const std::string &k : st.refusals_told)
        refusals.push_back(k);
    v["refusals_told"] = std::move(refusals);
    v["strategy_reviewed"] = st.strategy_reviewed;
    v["ambition_checked"] = st.ambition_checked;
    v["deadline_told"] = st.deadline_told;
    v["scout_told"] = st.scout_told;
    v["reveal_year"] = st.reveal_year;
    v["observer"] = st.observer;
    // 1.1.0 phase 5
    Value changes = Value::array();
    for (const State::Change &c : st.changes) {
        Value deals = Value::array();
        for (const auto &[id, stage] : c.deals)
            deals.push_back(Value::array({id, stage}));
        changes.push_back(Value::object({{"kind", c.kind}, {"team", c.team}, {"other", c.other}, {"seat", c.seat},
                                         {"other_seat", c.other_seat}, {"out", c.out}, {"in", c.in},
                                         {"out_contract", c.out_contract}, {"in_contract", c.in_contract}, {"news", c.news},
                                         {"round", c.round}, {"deals", std::move(deals)}}));
    }
    v["changes"] = std::move(changes);
    v["team_comfort"] = st.team_comfort;
    v["push_signing"] = st.push_signing;
    v["sponsor_news"] = st.sponsor_news;
    v["principals_out"] = st.principals_out;
    v["farewells_year"] = st.farewells_year;
    Value outside = Value::array();
    for (const State::Outside &o : st.outside)
        outside.push_back(Value::object({{"driver", o.driver}, {"team", o.team}, {"source", o.source}, {"until", o.until},
                                         {"year", o.year}, {"told", o.told}}));
    v["outside"] = std::move(outside);
    v["academy_year"] = st.academy_year;
    v["reserves_year"] = st.reserves_year;
    v["awards_shown"] = st.awards_shown;
    v["cover_shown"] = st.cover_shown;
    return v;
}

bool from_json(State &st, const Value &v)
{
    if (!v.is_object() || v["schema"].as_int() != 1)
        return false;
    State out;
    out.group = std::string(v["group"].as_string());
    out.career = std::string(v["career"].as_string());
    out.year = static_cast<int>(v["year"].as_int(2026));
    out.season_index = static_cast<int>(v["season_index"].as_int());
    out.round = static_cast<int>(v["round"].as_int());
    out.rounds = static_cast<int>(v["rounds"].as_int(24));
    out.round_base = static_cast<int>(v["round_base"].as_int(0));
    out.results_read = static_cast<int>(v["results_read"].as_int(0));
    out.last_meeting = static_cast<uint32_t>(v["last_meeting"].as_int(0));
    out.seed = std::stoull(std::string(v["seed"].as_string("1")));
    out.rng = std::stoull(std::string(v["rng"].as_string("1")));
    out.player = std::string(v["player"].as_string());
    out.player_team = std::string(v["player_team"].as_string());
    out.retirements_drawn = v["retirements_drawn"].as_bool();
    out.midseason_moves = static_cast<int>(v["midseason_moves"].as_int());
    out.midseason_done = v["midseason_done"].as_bool();
    out.finalised = v["finalised"].as_bool();
    if (const json::Object *drivers = v["drivers"].if_object())
        for (const auto &[id, o] : *drivers) {
            Driver d = driver_from(std::string(id), o);
            d.team = std::string(o["team"].as_string());
            d.seat = static_cast<int>(o["seat"].as_int(-1));
            d.contract = static_cast<int>(o["contract"].as_int());
            d.form = static_cast<float>(o["form"].as_double());
            d.points = static_cast<int>(o["points"].as_int());
            d.starts = static_cast<int>(o["starts"].as_int());
            d.seasons = static_cast<int>(o["seasons"].as_int());
            d.retiring = o["retiring"].as_bool();
            d.retired = o["retired"].as_bool();
            d.released = o["released"].as_bool();
            d.ratings_seen = o["ratings_seen"].as_bool();
            d.game_overall = static_cast<float>(o["game_overall"].as_double());
            d.focus = static_cast<float>(o["focus"].as_double());
            d.pressured = o["pressured"].as_bool();
            d.reserve_of = std::string(o["reserve_of"].as_string());
            d.salary = static_cast<float>(o["salary"].as_double());
            d.alias_first = std::string(o["alias_first"].as_string());
            d.alias_last = std::string(o["alias_last"].as_string());
            d.regen = o["regen"].as_bool();
            d.backing = static_cast<float>(o["backing"].as_double());
            d.clause_rank = static_cast<int>(o["clause_rank"].as_int());
            d.wants_out = o["wants_out"].as_bool();
            d.f2_pos = static_cast<int>(o["f2_pos"].as_int());
            if (const json::Array *feuds = o["feuds"].if_array())
                for (const Value &f : *feuds)
                    d.feuds.emplace_back(f.as_string());
            d.last_value = static_cast<float>(o["last_value"].as_double());
            d.trend = static_cast<float>(o["trend"].as_double());
            d.ambition = static_cast<float>(o["ambition"].as_double(-1));
            d.loyalty = static_cast<float>(o["loyalty"].as_double(-1));
            d.money = static_cast<float>(o["money"].as_double(-1));
            d.wins = static_cast<int>(o["wins"].as_int());
            d.podiums = static_cast<int>(o["podiums"].as_int());
            d.value_start = static_cast<float>(o["value_start"].as_double());
            d.number = static_cast<int>(o["number"].as_int());
            d.number_tries = static_cast<int>(o["number_tries"].as_int());
            if (const json::Array *a = o["values"].if_array())
                for (const Value &x : *a)
                    d.values.push_back(static_cast<float>(x.as_double()));
            if (const json::Array *a = o["values_last"].if_array())
                for (const Value &x : *a)
                    d.values_last.push_back(static_cast<float>(x.as_double()));
            d.c_starts = static_cast<int>(o["c_starts"].as_int());
            d.c_wins = static_cast<int>(o["c_wins"].as_int());
            d.c_podiums = static_cast<int>(o["c_podiums"].as_int());
            d.c_points = static_cast<int>(o["c_points"].as_int());
            d.titles = static_cast<int>(o["titles"].as_int());
            if (const json::Array *a = o["race_pos"].if_array())
                for (const Value &x : *a)
                    d.race_pos.push_back(static_cast<int>(x.as_int()));
            if (const json::Array *a = o["race_team"].if_array())
                for (const Value &x : *a)
                    d.race_team.emplace_back(x.as_string());
            d.race_team.resize(d.race_pos.size());
            out.drivers.emplace(std::string(id), d);
        }
    if (const json::Array *teams = v["teams"].if_array())
        for (const Value &o : *teams) {
            Team t;
            t.id = std::string(o["id"].as_string());
            t.name = std::string(o["name"].as_string());
            t.code = std::string(o["code"].as_string());
            t.family = std::string(o["family"].as_string());
            t.stat = static_cast<int>(o["stat"].as_int());
            t.prestige = static_cast<float>(o["prestige"].as_double(70));
            t.performance = static_cast<float>(o["performance"].as_double(70));
            t.patience = static_cast<float>(o["patience"].as_double(0.5));
            t.junior = o["junior"].as_bool();
            t.colour = static_cast<uint32_t>(o["colour"].as_int(0x808080));
            t.budget = static_cast<float>(o["budget"].as_double(std::max(50.0f, t.prestige * 1.4f - 20.0f)));
            t.td_name = std::string(o["td_name"].as_string());
            t.td_rating = static_cast<float>(o["td_rating"].as_double(t.performance));
            t.seats = {std::string(o["seat0"].as_string()), std::string(o["seat1"].as_string())};
            t.points = static_cast<int>(o["points"].as_int());
            t.strategy = std::string(o["strategy"].as_string());
            t.optional = o["optional"].as_bool();
            t.active = o["active"].is_bool() ? o["active"].as_bool() : true;
            t.tp_name = std::string(o["tp_name"].as_string());
            t.tp_since = static_cast<int>(o["tp_since"].as_int());
            t.budget_start = static_cast<float>(o["budget_start"].as_double(0));
            t.reserve = std::string(o["reserve"].as_string());
            t.reserve_until = static_cast<int>(o["reserve_until"].as_int());
            out.teams.push_back(t);
        }
    if (const json::Array *deals = v["deals"].if_array())
        for (const Value &o : *deals) {
            Deal d{std::string(o["driver"].as_string()), std::string(o["team"].as_string()),
                   std::string(o["kind"].as_string()), static_cast<int>(o["year"].as_int()),
                   static_cast<int>(o["until"].as_int()), stage_of(o["stage"].as_string()),
                   o["fake"].as_bool(), static_cast<int>(o["opened"].as_int()),
                   static_cast<int>(o["source"].as_int(-1)), static_cast<int>(o["clause"].as_int())};
            d.id = static_cast<int>(o["id"].as_int());
            d.chain = static_cast<int>(o["chain"].as_int());
            d.waits = std::string(o["waits"].as_string());
            // a deal saved before the outlets kept a record: settled already, so it isn't scored again
            d.scored = o["scored"].is_bool() ? o["scored"].as_bool() : d.stage == Stage::official || d.stage == Stage::collapsed;
            out.deals.push_back(std::move(d));
        }
    if (const json::Array *standins = v["standins"].if_array())
        for (const Value &o : *standins)
            out.standins.push_back({std::string(o["out"].as_string()), std::string(o["in"].as_string()),
                                    std::string(o["team"].as_string()), static_cast<int>(o["seat"].as_int()),
                                    static_cast<int>(o["race"].as_int())});
    if (const json::Array *news = v["news"].if_array())
        for (const Value &o : *news) {
            NewsItem n;
            n.id = static_cast<int>(o["id"].as_int());
            n.year = static_cast<int>(o["year"].as_int());
            n.round = static_cast<int>(o["round"].as_int());
            n.kind = std::string(o["kind"].as_string());
            // a save from before the English texts were stored: its items may be written in another
            // language, so they go; the season preview is written again (see legacy_news)
            if (!o["title_es"].is_string()) {
                ++out.legacy_news;
                continue;
            }
            n.title_en = std::string(o["title"].as_string());
            n.body_en = std::string(o["body"].as_string());
            n.title_es = std::string(o["title_es"].as_string());
            n.body_es = std::string(o["body_es"].as_string());
            n.title = n.title_en;
            n.body = n.body_en;
            n.official = o["official"].as_bool();
            n.shown = o["shown"].as_bool();
            n.driver = std::string(o["driver"].as_string());
            n.from = std::string(o["from"].as_string());
            n.to = std::string(o["to"].as_string());
            n.until = static_cast<int>(o["until"].as_int());
            n.big = o["big"].as_bool();
            out.news.push_back(std::move(n));
        }
    if (const json::Array *history = v["history"].if_array())
        for (const Value &o : *history)
            out.history.push_back({static_cast<int>(o["year"].as_int()), static_cast<int>(o["round"].as_int()),
                                   std::string(o["driver"].as_string()), std::string(o["from"].as_string()),
                                   std::string(o["to"].as_string()), std::string(o["kind"].as_string())});
    if (const json::Array *pending = v["pending"].if_array())
        for (const Value &o : *pending)
            out.pending.push_back({std::string(o["team"].as_string()), static_cast<int>(o["seat"].as_int()),
                                   std::string(o["driver"].as_string()), std::string(o["reason"].as_string())});
    if (const json::Object *next = v["next_grid"].if_object())
        for (const auto &[team, pair] : *next)
            out.next_grid[std::string(team)] = {std::string(pair[0].as_string()), std::string(pair[1].as_string())};
    out.real = v["real"].as_bool();
    if (const json::Array *moves = v["real_moves"].if_array())
        for (const Value &o : *moves)
            out.real_moves.push_back({std::string(o["driver"].as_string()), std::string(o["from"].as_string()),
                                      std::string(o["to"].as_string()), std::string(o["kind"].as_string()),
                                      static_cast<int>(o["until"].as_int()), static_cast<int>(o["rumour_at"].as_int()),
                                      static_cast<int>(o["talks_at"].as_int()), static_cast<int>(o["official_at"].as_int()),
                                      static_cast<int>(o["done"].as_int())});
    if (const json::Array *changes = v["real_changes"].if_array())
        for (const Value &o : *changes)
            out.real_changes.push_back({std::string(o["team"].as_string()), std::string(o["out"].as_string()),
                                        std::string(o["in"].as_string()), std::string(o["kind"].as_string()),
                                        static_cast<int>(o["seat"].as_int()), static_cast<int>(o["round"].as_int()),
                                        static_cast<int>(o["until"].as_int()), o["told"].as_bool()});
    if (const json::Object *next = v["real_next"].if_object())
        for (const auto &[team, pair] : *next)
            out.real_next[std::string(team)] = {std::string(pair[0].as_string()), std::string(pair[1].as_string())};
    out.next_news_id = static_cast<int>(v["next_news_id"].as_int(1));
    out.seen_news = static_cast<int>(v["seen_news"].as_int());
    out.banner_news = static_cast<int>(v["banner_news"].as_int());
    out.preview_year = static_cast<int>(v["preview_year"].as_int());
    auto mail = [](const Value &m) {
        return State::Mail{std::string(m["subject"].as_string()), std::string(m["body"].as_string())};
    };
    out.mail_preview = mail(v["mail_preview"]);
    out.mail_radar = mail(v["mail_radar"]);
    out.mail_grid = mail(v["mail_grid"]);
    // a question from before the English texts were stored is dropped too: the press asks again later
    if (const Value &p = v["press"]; p.is_object() && p["question_es"].is_string()) {
        State::Press press;
        press.id = static_cast<int>(p["id"].as_int());
        press.topic = std::string(p["topic"].as_string());
        press.subject = std::string(p["subject"].as_string());
        press.question_en = std::string(p["question"].as_string());
        press.question_es = p["question_es"].is_string() ? std::string(p["question_es"].as_string()) : press.question_en;
        press.asked = static_cast<int>(p["asked"].as_int());
        if (const json::Array *a = p["answers"].if_array())
            for (const Value &x : *a)
                press.answers_en.emplace_back(x.as_string());
        if (const json::Array *a = p["answers_es"].if_array())
            for (const Value &x : *a)
                press.answers_es.emplace_back(x.as_string());
        if (press.answers_es.size() != press.answers_en.size())
            press.answers_es = press.answers_en;
        press.question = press.question_en;
        press.answers = press.answers_en;
        if (const json::Array *a = p["options"].if_array())
            for (const Value &x : *a)
                press.options.emplace_back(x.as_string());
        out.press = std::move(press);
    }
    out.next_press_id = static_cast<int>(v["next_press_id"].as_int(1));
    out.player_loyalty = static_cast<int>(v["player_loyalty"].as_int());
    out.teammate_wish = std::string(v["teammate_wish"].as_string());
    if (const json::Array *f2 = v["f2_table"].if_array())
        for (const Value &x : *f2)
            out.f2_table.emplace_back(x.as_string());
    out.f2_reported = static_cast<int>(v["f2_reported"].as_int());
    out.talents_year = static_cast<int>(v["talents_year"].as_int());
    out.staff_done = v["staff_done"].as_bool();
    out.next_deal_id = static_cast<int>(v["next_deal_id"].as_int(1));
    if (const json::Array *outlets = v["outlets"].if_array())
        for (const Value &o : *outlets)
            out.outlets.push_back({static_cast<float>(o["accuracy"].as_double(0.5)), static_cast<int>(o["hits"].as_int()),
                                   static_cast<int>(o["misses"].as_int()), static_cast<int>(o["season_hits"].as_int()),
                                   static_cast<int>(o["season_misses"].as_int())});
    if (const json::Array *awards = v["awards"].if_array())
        for (const Value &o : *awards)
            out.awards.push_back({static_cast<int>(o["year"].as_int()), std::string(o["key"].as_string()),
                                  std::string(o["who"].as_string()), std::string(o["name"].as_string()),
                                  std::string(o["line"].as_string())});
    if (const json::Array *refusals = v["refusals_told"].if_array())
        for (const Value &k : *refusals)
            out.refusals_told.insert(std::string(k.as_string()));
    out.strategy_reviewed = v["strategy_reviewed"].as_bool();
    out.ambition_checked = v["ambition_checked"].as_bool();
    out.deadline_told = v["deadline_told"].as_bool();
    out.scout_told = static_cast<int>(v["scout_told"].as_int());
    out.reveal_year = static_cast<int>(v["reveal_year"].as_int());
    out.observer = v["observer"].as_bool(); // a save from before: the plugin sets it again from the career's mode
    // 1.1.0 phase 5 (a save from before it: no changes to undo, a neutral press record, principals drawn now)
    if (const json::Array *changes = v["changes"].if_array())
        for (const Value &o : *changes) {
            State::Change c;
            c.kind = std::string(o["kind"].as_string());
            c.team = std::string(o["team"].as_string());
            c.other = std::string(o["other"].as_string());
            c.seat = static_cast<int>(o["seat"].as_int());
            c.other_seat = static_cast<int>(o["other_seat"].as_int());
            c.out = std::string(o["out"].as_string());
            c.in = std::string(o["in"].as_string());
            c.out_contract = static_cast<int>(o["out_contract"].as_int());
            c.in_contract = static_cast<int>(o["in_contract"].as_int());
            c.news = static_cast<int>(o["news"].as_int());
            c.round = static_cast<int>(o["round"].as_int());
            if (const json::Array *deals = o["deals"].if_array())
                for (const Value &d : *deals)
                    c.deals.push_back({static_cast<int>(d[0].as_int()), static_cast<int>(d[1].as_int())});
            out.changes.push_back(std::move(c));
        }
    out.team_comfort = std::clamp(static_cast<int>(v["team_comfort"].as_int()), -2, 2);
    out.push_signing = v["push_signing"].as_bool();
    out.sponsor_news = static_cast<int>(v["sponsor_news"].as_int());
    out.principals_out = static_cast<int>(v["principals_out"].as_int());
    out.farewells_year = static_cast<int>(v["farewells_year"].as_int());
    if (const json::Array *outside = v["outside"].if_array())
        for (const Value &o : *outside)
            out.outside.push_back({std::string(o["driver"].as_string()), std::string(o["team"].as_string()),
                                   std::string(o["source"].as_string()), static_cast<int>(o["until"].as_int()),
                                   static_cast<int>(o["year"].as_int()), o["told"].as_bool()});
    out.academy_year = static_cast<int>(v["academy_year"].as_int());
    out.reserves_year = static_cast<int>(v["reserves_year"].as_int());
    out.awards_shown = static_cast<int>(v["awards_shown"].as_int());
    out.cover_shown = static_cast<int>(v["cover_shown"].as_int());
    ensure_principals(out);
    // a save from before phase 6 (its teams have no reserve field): the game's own reserves to start with
    bool phase6_save = false;
    if (const json::Array *teams = v["teams"].if_array())
        for (const Value &o : *teams)
            phase6_save = phase6_save || o.contains("reserve");
    if (!phase6_save)
        ensure_reserves(out);
    // a save from before 1.1.0's market: characters, plans, deal ids and the outlets' record start now
    draw_all_traits(out);
    for (Team &t : out.teams)
        if (t.strategy.empty())
            t.strategy = pick_strategy(out, t);
    for (Deal &d : out.deals)
        if (d.id <= 0)
            d.id = out.next_deal_id++;
    ensure_outlets(out);
    // a state saved before news had ids: number them now
    for (NewsItem &n : out.news)
        if (n.id <= 0)
            n.id = out.next_news_id++;
    st = std::move(out);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// Reports for the in-game menu

std::vector<NewsItem> take_news(State &st, const Settings &s, size_t max)
{
    std::vector<NewsItem> out;
    for (NewsItem &n : st.news) {
        if (n.shown)
            continue;
        if (s.news == NewsFilter::none || (s.news == NewsFilter::official && !n.official)) {
            n.shown = true;
            continue;
        }
        if (out.size() >= max)
            break;
        n.shown = true;
        out.push_back(n);
    }
    return out;
}

std::string report_rumours(const State &st, const Settings &s)
{
    std::string out = text::h_rumours(L(s)) + "\n";
    int n = 0;
    for (auto it = st.deals.rbegin(); it != st.deals.rend() && n < 10; ++it) {
        if (it->stage == Stage::official || it->stage == Stage::collapsed || it->kind == "renewal")
            continue;
        out += "- " + driver_name(st, it->driver) + " > " + team_name(st, it->team) + " (" +
               text::stage(L(s), stage_name(it->stage == Stage::agreed ? Stage::talks : it->stage)) + ", " +
               text::outlet_name(it->source) + ")\n";
        ++n;
    }
    if (!n)
        out += text::none(L(s));
    return out;
}

std::string report_deals(const State &st, const Settings &s)
{
    std::string out = text::h_deals(L(s), st.year + 1) + "\n";
    int n = 0;
    for (const Deal &d : st.deals) {
        if (d.stage != Stage::official || d.fake)
            continue;
        out += "- " + driver_name(st, d.driver) + " > " + team_name(st, d.team) + " (" + text::kind(L(s), d.kind) +
               ", " + std::to_string(d.until) + ")\n";
        ++n;
    }
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                if (d->retiring) {
                    out += "- " + d->name + ": " + text::kind(L(s), "retirement") + "\n";
                    ++n;
                }
    if (!n)
        out += text::none(L(s));
    return out;
}

std::string report_contracts(const State &st, const Settings &s)
{
    std::string out = text::h_contracts(L(s), st.year) + "\n";
    for (const Team &t : st.teams) {
        if (!t.active)
            continue;
        out += t.name + ": ";
        for (int k = 0; k < 2; ++k) {
            const Driver *d = st.driver(t.seats[static_cast<size_t>(k)]);
            if (k)
                out += ", ";
            if (!d) {
                out += text::free_seat(L(s));
                continue;
            }
            out += driver_name(st, d->id);
            if (d->id == st.player)
                continue;
            out += " (" + (d->retiring ? text::kind(L(s), "retirement") : std::to_string(d->contract)) +
                   (d->salary > 0 ? ", " + text::salary_short(L(s), d->salary) : std::string()) + ")";
        }
        out += "\n";
    }
    return out;
}

std::string report_grid(const State &st, const Settings &s)
{
    auto g = projected_grid(st, s);
    std::string out = text::h_grid(L(s), st.year + 1, st.finalised) + "\n";
    for (const Team &t : st.teams) {
        if (!t.active)
            continue;
        auto &pair = g[t.id];
        out += t.name + ": " + (pair[0].empty() ? text::unknown_seat(L(s)) : driver_name(st, pair[0])) + " / " +
               (pair[1].empty() ? text::unknown_seat(L(s)) : driver_name(st, pair[1])) + "\n";
    }
    return out;
}

std::string report_history(const State &st, const Settings &s)
{
    std::string out = text::h_history(L(s)) + "\n";
    int n = 0;
    for (auto it = st.history.rbegin(); it != st.history.rend() && n < 12; ++it) {
        if (it->kind == "renewal")
            continue;
        if (it->from.empty() && it->to.empty()) {
            out += std::to_string(it->year) + " - " + driver_name(st, it->driver) + " (" + text::kind(L(s), it->kind) + ")\n";
            ++n;
            continue;
        }
        out += std::to_string(it->year) + " - " + driver_name(st, it->driver) + ": " +
               (it->from.empty() ? std::string("-") : team_name(st, it->from)) + " > " +
               (it->to.empty() ? std::string("-") : team_name(st, it->to)) + " (" + text::kind(L(s), it->kind) + ")\n";
        ++n;
    }
    if (!n)
        out += text::none(L(s));
    return out;
}

std::vector<NewsItem> take_breaking(State &st, size_t max)
{
    std::vector<NewsItem> out;
    for (const NewsItem &n : st.news) {
        if (n.id <= st.banner_news)
            continue;
        st.banner_news = n.id;
        bool breaking = n.official && (n.kind == "official" || n.kind == "retirement" || n.kind == "sacking" ||
                                       n.kind == "standin" || n.kind == "staff" || n.kind == "clause" ||
                                       n.kind == "press" || n.kind == "talents" || n.kind == "f2" || n.kind == "deadline" ||
                                       n.kind == "media" || n.kind == "swap" || n.kind == "principal" || n.kind == "farewell");
        if (breaking && n.body.find('\n') == std::string::npos && out.size() < max)
            out.push_back(n);
    }
    return out;
}

int unread(const State &st)
{
    int n = 0;
    for (const NewsItem &item : st.news)
        if (item.id > st.seen_news)
            ++n;
    return n;
}

void mark_seen(State &st) { st.seen_news = st.next_news_id - 1; }

void season_preview(State &st, const Settings &s)
{
    if (st.talents_year != st.year) {
        Rng tr(mix(st.seed, 0x7a1e47ull ^ static_cast<uint64_t>(st.year))); // its own stream: callers may hold the state's
        make_talents(st, s, tr);
    }
    if (st.preview_year == st.year)
        return;
    st.preview_year = st.year;
    std::vector<std::string> names;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                if (d->id != st.player && d->contract <= st.year && d->kind != "player")
                    names.push_back(d->name + " (" + t.name + ")");
    std::string list;
    for (size_t i = 0; i < names.size(); ++i)
        list += (i ? ", " : "") + names[i];
    // the teams' plans: who goes for the title, who rebuilds
    std::map<std::string, std::string> plans;
    for (const Team &t : st.teams)
        if (t.active && !st.real && s.strategies)
            plans[t.strategy] += (plans[t.strategy].empty() ? "" : ", ") + t.name;
    add_news(st, st.round, "preseason",
                 [&](text::Lang l) { return std::string(text::t_preseason(l, st.year)); },
                 [&](text::Lang l) {
                     std::string b = text::preseason(l, st.year, static_cast<int>(names.size()), list);
                     for (const char *k : {"title", "contend", "rebuild", "youth", "survival"})
                         if (!plans[k].empty())
                             b += "\n" + text::strategy_name(l, k) + ": " + plans[k];
                     return b;
                 }, true);
}

void refresh_mails(State &st, const Settings &s)
{
    text::Lang l = L(s);
    // the preview: what happened since the last race weekend
    {
        std::string body = text::mail_intro(l) + "\n\n";
        int n = 0;
        for (const NewsItem &item : st.news)
            if (item.year == st.year && item.round == st.round && item.kind != "grid" && n < 6) {
                body += "• " + item.title + "\n" + item.body + "\n\n";
                ++n;
            }
        if (!n)
            body += text::mail_quiet(l) + "\n\n";
        body += text::mail_sign(l);
        st.mail_preview = {text::mail_preview_subject(l, st.round + 1), body};
    }
    // the radar: talks, the driver stock, pressure, contracts running out
    {
        std::string body;
        int n = 0;
        for (auto it = st.deals.rbegin(); it != st.deals.rend() && n < 5; ++it) {
            if (it->stage == Stage::official || it->stage == Stage::collapsed || it->kind == "renewal")
                continue;
            body += (n ? "" : text::h_negotiations(l) + "\n") + std::string("• ") + driver_name(st, it->driver) + " > " +
                    team_name(st, it->team) + " (" +
                    text::stage(l, stage_name(it->stage == Stage::agreed ? Stage::talks : it->stage)) + ")\n";
            ++n;
        }
        // 1.1.0 phase 5: deadline day and the seats still open, the dominoes
        if (int dl = s.deadline && !st.real && !st.finalised ? deadline_races_left(st) : 0) {
            std::string open;
            for (const auto &[team, pair] : projected_grid(st, s))
                if (const Team *t = st.team(team); t && t->active && (pair[0].empty() || pair[1].empty()))
                    open += (open.empty() ? "" : ", ") + t->name;
            body += (body.empty() ? "" : "\n") + text::h_deadline(l, dl) + "\n" + text::mail_open_seats(l, open) + "\n";
        }
        {
            int shown = 0;
            for (const std::vector<int> &chain : transfer_chains(st)) {
                if (chain.size() < 2 || shown >= 2)
                    continue;
                std::string line;
                for (int id : chain)
                    if (const Deal *d = deal_with_id(st, id))
                        line += (line.empty() ? "" : "  |  ") + driver_name(st, d->driver) + " > " + team_name(st, d->team);
                body += (shown ? "" : (body.empty() ? "" : "\n") + text::h_mail_chains(l) + "\n") + std::string("• ") + line + "\n";
                ++shown;
            }
        }
        std::vector<const Driver *> grid;
        for (const Team &t : st.teams)
            for (const std::string &who : t.seats)
                if (const Driver *d = st.driver(who))
                    if (d->id != st.player)
                        grid.push_back(d);
        std::sort(grid.begin(), grid.end(), [](const Driver *a, const Driver *b) { return a->trend > b->trend; });
        auto movers = [&](bool up) {
            std::string out;
            int k = 0;
            for (size_t i = 0; i < grid.size() && k < 3; ++i) {
                const Driver *d = up ? grid[i] : grid[grid.size() - 1 - i];
                if (up ? d->trend < 0.05f : d->trend > -0.05f)
                    break;
                char delta[32];
                std::snprintf(delta, sizeof delta, " (%+.1f)", d->trend);
                out += (k ? ", " : "") + d->name + delta;
                ++k;
            }
            return out;
        };
        std::string up = movers(true), down = movers(false);
        if (!up.empty() || !down.empty()) {
            body += (body.empty() ? "" : "\n") + text::h_stock(l) + "\n";
            if (!up.empty())
                body += text::h_up(l) + ": " + up + "\n";
            if (!down.empty())
                body += text::h_down(l) + ": " + down + "\n";
        }
        std::string pressured, expiring;
        int expiring_n = 0;
        for (const Driver *d : grid) {
            if (d->pressured)
                pressured += (pressured.empty() ? "" : ", ") + d->name;
            if (d->contract <= st.year && !d->retiring && expiring_n++ < 8)
                expiring += (expiring.empty() ? "" : ", ") + d->name;
        }
        if (!pressured.empty())
            body += "\n" + text::h_pressure(l) + "\n" + pressured + "\n";
        if (!expiring.empty())
            body += "\n" + text::h_expiring(l, st.year) + "\n" + expiring + "\n";
        // the outlet with the best record this season
        {
            int best = -1;
            float ratio = -1;
            for (int i = 0; i < static_cast<int>(st.outlets.size()); ++i) {
                const State::Outlet &o = st.outlets[static_cast<size_t>(i)];
                int seen = o.season_hits + o.season_misses;
                if (seen >= 2 && static_cast<float>(o.season_hits) / static_cast<float>(seen) > ratio) {
                    ratio = static_cast<float>(o.season_hits) / static_cast<float>(seen);
                    best = i;
                }
            }
            if (best >= 0 && ratio >= 0.5f) { // only an outlet that got most of its stories right
                const State::Outlet &o = st.outlets[static_cast<size_t>(best)];
                body += "\n" + text::h_media(l) + "\n" +
                        text::mail_media(l, text::outlet_name(best), o.season_hits, o.season_hits + o.season_misses) + "\n";
            }
        }
        if (body.empty())
            body = text::mail_quiet(l) + "\n";
        st.mail_radar = {text::mail_radar_subject(l), text::mail_intro(l) + "\n\n" + body + "\n" + text::mail_sign(l)};
    }
    // the grid: final once the season is settled, else the projection
    {
        std::string body = report_grid(st, s) + "\n";
        int n = 0;
        for (const Move &m : st.history)
            if (m.year == st.year && m.kind != "renewal" && n < 14) {
                body += (n ? "" : text::h_moves(l, st.year) + "\n") + std::string("• ") + driver_name(st, m.driver) +
                        ": " + (m.from.empty() ? "-" : team_name(st, m.from)) + " > " +
                        (m.to.empty() ? "-" : team_name(st, m.to)) + " (" + text::kind(l, m.kind) + ")\n";
                ++n;
            }
        // the season's awards and farewells, once it's over
        if (st.finalised) {
            std::string awards;
            for (const State::Award &a : st.awards)
                if (a.year == st.year)
                    awards += "• " + text::award_title(l, a.key) + ": " + a.name + "\n";
            if (!awards.empty())
                body += "\n" + text::h_awards(l, st.year) + "\n" + awards;
            std::string bye;
            for (const Move &m : st.history)
                if (m.year == st.year && m.kind == "retirement")
                    if (const Driver *d = st.driver(m.driver)) {
                        CareerTotals c = career_totals(st, *d);
                        bye += (bye.empty() ? "" : ", ") + text::tribute(l, d->name, c.seasons, c.wins);
                    }
            if (!bye.empty())
                body += "\n" + text::h_mail_farewells(l) + "\n" + bye + "\n";
        }
        st.mail_grid = {text::mail_grid_subject(l, st.year + 1), body + "\n" + text::mail_sign(l)};
    }
}

std::string report_news(const State &st, const Settings &s)
{
    std::string out = text::h_news(L(s)) + "\n";
    int n = 0;
    for (auto it = st.news.rbegin(); it != st.news.rend() && n < 12; ++it, ++n) {
        std::string body = it->body.substr(0, it->body.find('\n'));
        out += std::string(it->id > st.seen_news ? "• " : "  ") + std::to_string(it->year) + " R" +
               std::to_string(it->round) + "  " + it->title + ": " + body + "\n";
    }
    if (!n)
        out += text::none(L(s));
    return out;
}

std::string report_stock(const State &st, const Settings &s)
{
    std::vector<const Driver *> grid;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                if (d->id != st.player)
                    grid.push_back(d);
    std::sort(grid.begin(), grid.end(), [](const Driver *a, const Driver *b) { return value(*a) > value(*b); });
    std::string out = text::h_stock(L(s)) + "\n";
    for (const Driver *d : grid) {
        char line[160];
        std::snprintf(line, sizeof line, "%s  %.1f  (%+.1f)%s\n", d->name.c_str(), value(*d), d->trend,
                      d->pressured ? "  !" : "");
        out += line;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------
// 1.1.0 phase 3: personality, strategies, the outlets' record, deadline day, the scout and racing numbers

const char *strategy_label(const std::string &strategy)
{
    if (strategy == "title")
        return "TITLE PUSH";
    if (strategy == "rebuild")
        return "REBUILD";
    if (strategy == "youth")
        return "YOUTH";
    if (strategy == "survival")
        return "SURVIVAL";
    return "CONTENDER";
}

float appeal(const State &st, const Driver &d, const Team &t)
{
    return appeal_with(st, d, t, d.ambition < 0 ? 0.5f : d.ambition, d.loyalty < 0 ? 0.5f : d.loyalty, d.money < 0 ? 0.4f : d.money);
}

namespace {
float appeal_with(const State &st, const Driver &d, const Team &t, float amb, float loy, float mon)
{
    // ambition looks at the car, everyone at the brand, the money-minded at the budget
    float wp = 0.35f + 0.4f * amb, wb = 0.3f, wm = 0.1f + 0.35f * mon;
    float money_level = clampf(t.budget * 0.8f, 40.0f, 100.0f);
    float a = (wp * t.performance + wb * t.prestige + wm * money_level) / (wp + wb + wm);
    if (d.team == t.id)
        a += 12.0f * loy; // home
    if (in_academy(t, d))
        a += 6.0f * loy; // the family that raised him (or its partner team)
    if (t.junior && d.age(st.year + 1) > 22)
        a -= 6.0f * amb; // a second team is a step back for an established driver
    return a;
}
} // namespace

int outlet_rating(const State &st, int outlet)
{
    if (outlet < 0 || outlet >= text::kOutlets)
        return 1;
    int base = text::outlet_reliability(outlet);
    if (outlet >= static_cast<int>(st.outlets.size()))
        return base;
    const State::Outlet &o = st.outlets[static_cast<size_t>(outlet)];
    // the reputation it started with counts as four stories; every story since moves it
    float prior = static_cast<float>(base - 1) / 4.0f;
    float rate = (static_cast<float>(o.hits) + 4.0f * prior) / (static_cast<float>(o.hits + o.misses) + 4.0f);
    return std::clamp(static_cast<int>(std::lround(1.0f + 4.0f * rate)), 1, 5);
}

int deadline_races_left(const State &st)
{
    if (st.real || st.finalised || st.rounds < 8 || st.round < 1)
        return 0;
    int left = st.rounds - st.round;
    return left >= 1 && left <= 2 ? left : 0;
}

std::vector<ScoutPick> scout(const State &st, const Settings &s, const std::string &team, size_t max)
{
    std::vector<ScoutPick> out;
    const std::string id = team.empty() ? st.player_team : team;
    Team own;
    const Team *t = st.team(id);
    std::string mate;
    if (t) {
        for (const std::string &who : t->seats)
            if (!who.empty() && who != st.player)
                mate = who;
    } else {
        // My Team: the player's own team, at the level his results suggest
        own.id = "#own";
        own.name = "My Team";
        own.prestige = 62;
        own.budget = 85;
        float level = 70;
        if (const Driver *me = st.driver(st.player); me && me->starts > 0) {
            int ahead = 0;
            for (const auto &[x, d] : st.drivers)
                if (d.starts > 0 && d.points > me->points)
                    ++ahead;
            level = clampf(92.0f - static_cast<float>(ahead) * 1.6f, 55.0f, 92.0f);
        }
        own.performance = level;
        own.strategy = level >= 85 ? "title" : level < 66 ? "rebuild" : "contend";
        t = &own;
        if (!st.protected_ids.empty())
            mate = *st.protected_ids.begin();
    }
    Rng r(mix(mix(st.seed, hash_str(t->id)), static_cast<uint64_t>(st.year * 100 + st.round)));
    std::set<std::string> top_f2;
    for (size_t i = 0; i < st.f2_table.size() && i < 8; ++i)
        top_f2.insert(st.f2_table[i]);
    for (const auto &[did, d] : st.drivers) {
        if (!eligible(d, st, s) || did == mate || d.team == t->id || d.kind == "legend")
            continue;
        bool free = d.team.empty();
        if (!free && d.contract > st.year && !d.wants_out)
            continue; // under contract next season
        bool f2 = free && d.kind == "f2";
        if (f2 && !top_f2.count(did) && d.potential() < 80)
            continue;
        if (free && !f2 && d.kind != "reserve" && value(d) < 70)
            continue;
        ScoutPick p;
        p.driver = did;
        p.status = !free ? "expiring" : f2 ? "f2" : "free";
        p.fit = seat_score(st, s, *t, d, mate, r);
        bool rookie = d.kind == "f2" || d.kind == "reserve" || d.kind == "pool";
        float iv = interest(st, d, *t, s);
        p.interest = rookie || free ? 2 : iv >= 2.0f ? 2 : iv >= -6.0f ? 1 : 0;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const ScoutPick &a, const ScoutPick &b) { return a.fit > b.fit; });
    if (out.size() > max)
        out.resize(max);
    return out;
}

std::vector<NumberWish> talent_numbers(const State &st)
{
    std::set<int> taken;
    for (const auto &[id, d] : st.drivers)
        if (d.number > 0)
            taken.insert(d.number);
    std::vector<NumberWish> out;
    for (const auto &[id, d] : st.drivers) {
        if (!d.regen || d.team.empty() || d.retired || d.number != 0 || d.number_tries >= 6)
            continue;
        // a number of his own: drawn from his name, never 1 (the champion's) or 17 (retired by F1)
        Rng r(mix(st.seed, hash_str(d.id + d.name) ^ 0x4e554dull));
        NumberWish w;
        w.driver = id;
        std::set<int> seen;
        while (w.numbers.size() < 8) {
            int n = r.range(2, 99);
            if (n == 17 || taken.count(n) || !seen.insert(n).second)
                continue;
            w.numbers.push_back(n);
        }
        // the numbers the game already refused go to the back
        std::rotate(w.numbers.begin(), w.numbers.begin() + std::min<size_t>(static_cast<size_t>(d.number_tries), w.numbers.size()),
                    w.numbers.end());
        out.push_back(std::move(w));
    }
    return out;
}

void number_result(State &st, const std::string &driver, int number, bool ok)
{
    Driver *d = st.driver(driver);
    if (!d || d->number != 0)
        return;
    if (!ok) {
        if (++d->number_tries >= 6)
            d->number = -1; // the game keeps his own number
        return;
    }
    d->number = number;
    std::string name = d->name, team = team_name(st, d->team);
    add_news(st, st.round, "number", [&](text::Lang l) { return text::h_number(l, name, number); },
             [&](text::Lang l) { return text::number_line(l, name, team, number); }, false);
}

int adopt_game_drivers(State &st, const std::vector<GameDriver> &list)
{
    int added = 0;
    for (const GameDriver &g : list) {
        if (g.id.empty() || g.id == st.player)
            continue;
        if (Driver *d = st.driver(g.id)) {
            // a driver found on the grid before his statistics could be read: the game's ratings now
            if (g.rated && !d->ratings_seen && d->kind == "f1" && d->id.rfind("stat_", 0) != 0 && d->pace == 70 && d->racecraft == 70) {
                d->pace = g.pace;
                d->racecraft = g.racecraft;
                d->awareness = g.awareness;
                d->experience = g.experience;
                d->pace_peak = std::max(g.pace_peak, g.pace);
                d->racecraft_peak = std::max(g.racecraft_peak, g.racecraft);
                d->awareness_peak = std::max(g.awareness_peak, g.awareness);
                d->experience_peak = std::max(g.experience_peak, g.experience);
                d->ratings_seen = true;
            }
            continue;
        }
        Driver d;
        d.id = g.id;
        d.name = g.name.empty() ? g.id : g.name;
        d.nat = g.nat;
        d.initials = g.initials;
        d.stat = g.stat;
        d.kind = g.legend ? "legend" : g.kind;
        // the game's placeholder birth year (1900): an age that fits a driver on the market
        Rng r(mix(st.seed, hash_str(g.id) ^ 0xb0a7ull));
        d.born = g.born > 1940 ? g.born : st.year - (g.kind == "f2" ? r.range(19, 23) : r.range(26, 33));
        if (g.rated) {
            d.pace = g.pace;
            d.racecraft = g.racecraft;
            d.awareness = g.awareness;
            d.experience = g.experience;
            d.pace_peak = std::max(g.pace_peak, g.pace);
            d.racecraft_peak = std::max(g.racecraft_peak, g.racecraft);
            d.awareness_peak = std::max(g.awareness_peak, g.awareness);
            d.experience_peak = std::max(g.experience_peak, g.experience);
        } else {
            // nothing to go on: a level below the grid's, like the game's own fillers
            float base = g.kind == "f2" ? 63.0f : 66.0f;
            d.pace = base + static_cast<float>(r.uniform() * 6.0);
            d.racecraft = base + static_cast<float>(r.uniform() * 6.0);
            d.awareness = base + static_cast<float>(r.uniform() * 6.0);
            d.experience = g.kind == "f2" ? 35.0f : 60.0f;
            d.pace_peak = d.pace + 4;
            d.racecraft_peak = d.racecraft + 4;
            d.awareness_peak = d.awareness + 4;
            d.experience_peak = 80;
        }
        d.seasons = g.kind == "f2" ? 0 : 1;
        draw_traits(st.seed, st.year, d);
        st.drivers.emplace(d.id, std::move(d));
        ++added;
    }
    return added;
}

const Deal *deal_with_id(const State &st, int id)
{
    for (const Deal &d : st.deals)
        if (d.id == id)
            return &d;
    return nullptr;
}

std::vector<std::vector<int>> transfer_chains(const State &st)
{
    std::set<int> live;
    for (const Deal &d : st.deals)
        if (!d.fake && d.stage != Stage::collapsed && d.id > 0)
            live.insert(d.id);
    std::map<int, std::vector<int>> next; // a deal -> the deals whose seat it opened
    for (const Deal &d : st.deals)
        if (live.count(d.id) && d.chain > 0 && live.count(d.chain))
            next[d.chain].push_back(d.id);
    std::vector<std::vector<int>> out;
    for (const Deal &d : st.deals) {
        if (!live.count(d.id) || (d.chain > 0 && live.count(d.chain)) || !next.count(d.id))
            continue; // only the first domino of a chain starts one
        std::vector<int> chain{d.id};
        std::set<int> seen{d.id};
        int at = d.id;
        while (next.count(at)) {
            int n = next[at].front();
            if (!seen.insert(n).second)
                break;
            chain.push_back(n);
            at = n;
        }
        out.push_back(std::move(chain));
    }
    return out;
}

float driver_value(const Driver &d) { return value(d); }

int team_rank(const State &st, const std::string &team)
{
    const Team *t = st.team(team);
    return t ? rank_of(st, *t) : static_cast<int>(st.teams.size());
}

const State::Press *press_pending(const State &st) { return st.press ? &*st.press : nullptr; }

bool answer_press(State &st, const Settings &s, int choice)
{
    if (!st.press || choice < 0 || choice >= static_cast<int>(st.press->answers.size()))
        return false;
    State::Press p = *st.press;
    st.press.reset();
    Driver *me = st.driver(st.player);
    Driver *about = st.driver(p.subject);
    if (p.topic == "future") {
        st.player_loyalty = std::clamp(st.player_loyalty + (choice == 0 ? 1 : choice == 1 ? -1 : 0), -2, 2);
    } else if (p.topic == "mate") {
        if (choice == 1 && me && about) {
            start_feud(*me, *about);
            if (about->contract > st.year)
                about->wants_out = true; // he won't stay after that
        }
    } else if (p.topic == "wish") {
        st.teammate_wish = p.options[static_cast<size_t>(choice)];
    } else if (p.topic == "deadline") {
        // 0 happy with the line-up, 1 wants reinforcements (his team hunts a proven driver; the bosses notice), 2 no comment
        if (choice == 0)
            st.team_comfort = std::clamp(st.team_comfort + 1, -2, 2);
        if (choice == 1) {
            st.push_signing = true;
            st.team_comfort = std::clamp(st.team_comfort - 1, -2, 2);
        }
    } else if (p.topic == "rival") {
        // 0 bring it on, 1 his team must answer (reinforcements), 2 not his business
        if (choice == 1)
            st.push_signing = true;
        if (choice == 0)
            st.team_comfort = std::clamp(st.team_comfort + 1, -2, 2);
    } else if (p.topic == "renewal") {
        // 0 close to a deal, 1 weighing offers, 2 talking to the team
        if (choice == 0) {
            st.player_loyalty = std::clamp(st.player_loyalty + 1, -2, 2);
            st.team_comfort = std::clamp(st.team_comfort + 1, -2, 2);
        } else if (choice == 1) {
            st.player_loyalty = std::clamp(st.player_loyalty - 1, -2, 2);
            st.team_comfort = std::clamp(st.team_comfort - 1, -2, 2);
        }
    } else if (p.topic == "awards") {
        // 0 well deserved, 1 overrated (a feud), 2 next year is mine
        if (choice == 1 && me && about && about != me)
            start_feud(*me, *about);
    } else if (choice == 1 && me && about) {
        start_feud(*me, *about);
    }
    std::string about_name = about ? about->name : p.subject;
    auto reply = [&](text::Lang l) {
        if (p.topic == "future")
            return text::r_future(l, choice, team_name(st, st.player_team));
        if (p.topic == "mate")
            return text::r_mate(l, choice, about_name);
        if (p.topic == "wish")
            return text::r_wish(l, driver_name(st, st.teammate_wish));
        if (p.topic == "deadline")
            return text::r_deadline(l, choice, team_name(st, st.player_team));
        if (p.topic == "rival")
            return text::r_rival(l, choice, about_name);
        if (p.topic == "renewal")
            return text::r_renewal(l, choice, team_name(st, st.player_team));
        if (p.topic == "awards")
            return text::r_award(l, choice, about_name);
        return text::r_signing(l, choice, about_name);
    };
    add_news(st, st.round, "press_reply", [&](text::Lang l) { return text::h_press(l, p.topic, about_name, true); }, reply, false);
    return true;
}

std::vector<std::pair<std::string, std::string>> name_overrides(const State &st)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &[id, d] : st.drivers)
        if (d.regen && d.name_keys.size() == 2 && !d.alias_first.empty()) {
            out.push_back({d.name_keys[0], d.alias_first});
            out.push_back({d.name_keys[1], d.alias_last});
        }
    return out;
}

std::vector<RatingChange> talent_ratings(const State &st)
{
    std::vector<RatingChange> out;
    // only the talents with an F1 seat: a reserve has no row in the career's statistics table until he races
    for (const auto &[id, d] : st.drivers)
        if (d.regen && !d.retired && !d.team.empty())
            out.push_back({d.id, d.experience, d.racecraft, d.awareness, d.pace});
    return out;
}

std::string report_f2(const State &st, const Settings &s)
{
    std::string out = text::h_f2(L(s), st.year) + "\n";
    if (st.f2_table.empty()) {
        // before half distance: the field by level
        std::vector<const Driver *> field;
        for (const auto &[id, d] : st.drivers)
            if (d.team.empty() && !d.retired && d.kind == "f2" && d.age(st.year) <= 27)
                field.push_back(&d);
        std::sort(field.begin(), field.end(), [](const Driver *a, const Driver *b) { return a->potential() > b->potential(); });
        for (size_t i = 0; i < field.size() && i < 12; ++i) {
            char line[160];
            std::snprintf(line, sizeof line, "- %s (%d)%s  %.0f / %.0f\n", field[i]->name.c_str(), field[i]->age(st.year),
                          field[i]->regen ? " *" : "", field[i]->overall(), field[i]->potential());
            out += line;
        }
        return out;
    }
    for (size_t i = 0; i < st.f2_table.size() && i < 12; ++i)
        if (const Driver *d = st.driver(st.f2_table[i])) {
            char line[160];
            std::snprintf(line, sizeof line, "%zu. %s (%d)%s\n", i + 1, d->name.c_str(), d->age(st.year), d->regen ? " *" : "");
            out += line;
        }
    return out;
}

std::string report_staff(const State &st, const Settings &s)
{
    std::string out = text::h_staff(L(s)) + "\n";
    for (const Team &t : st.teams) {
        char line[200];
        std::snprintf(line, sizeof line, "%s: %s (%.0f)\n", t.name.c_str(), t.td_name.c_str(), t.td_rating);
        out += line;
    }
    return out;
}

std::string trajectory(const State &st, const std::string &driver)
{
    std::vector<std::string> steps;
    for (const Move &m : st.history) {
        if (m.driver != driver || m.to.empty() || m.kind == "renewal" || m.kind == "replaced")
            continue;
        bool now = m.kind == "promoted" || m.kind == "demoted"; // a move made during that season
        steps.push_back(team_name(st, m.to) + " " + std::to_string(now ? m.year : m.year + 1));
    }
    std::string out;
    size_t from = steps.size() > 4 ? steps.size() - 4 : 0;
    for (size_t i = from; i < steps.size(); ++i)
        out += (out.empty() ? "" : ", ") + steps[i];
    return out;
}

std::string report_driver_market(const State &st, const Settings &s)
{
    std::vector<const Driver *> free;
    for (const auto &[id, d] : st.drivers)
        if (d.team.empty() && !d.retired && d.kind != "player" && !st.shielded(id) && (d.kind != "legend" || s.legends))
            free.push_back(&d);
    std::sort(free.begin(), free.end(), [](const Driver *a, const Driver *b) { return a->potential() > b->potential(); });
    std::string out = text::h_market(L(s)) + "\n";
    for (size_t i = 0; i < free.size() && i < 10; ++i) {
        const Driver *d = free[i];
        char line[160];
        std::snprintf(line, sizeof line, "- %s (%d) %.0f / %.0f\n", d->name.c_str(), d->age(st.year), d->overall(),
                      d->potential());
        out += line;
    }
    return out;
}

} // namespace ss
