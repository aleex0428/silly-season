// Silly Season: the engine plugin. It connects the market engine (market.cpp) to F1 25 through the
// Swofty core: career events, the real grid and results, the core's seat operations, popups, the
// Team HQ tab and per-career storage.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <span>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <string>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/hub.hpp>
#include <swofty/lang.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/overlay.hpp>
#include <swofty/popup.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/telemetry.hpp>

#include <swofty/services.hpp>

#include "f2_revamp_api/career.hpp"
#include "market.hpp"
#include "text.hpp"

using namespace swofty;
namespace json = swofty::json;

static constexpr const char *kAddon = "alexs.silly-season";

// ---------------------------------------------------------------------------------------------------
// Settings (swofty.json "options", all binds: native)

SWOFTY_SETTING(std::string_view, menu, "cerrar");
SWOFTY_SETTING(std::string_view, intensity, "realista");
SWOFTY_SETTING(std::string_view, rumours, "normal");
SWOFTY_SETTING(bool, midseason, true);
SWOFTY_SETTING(bool, protect_teammate, false);
SWOFTY_SETTING(bool, legends, false);
SWOFTY_SETTING(std::string_view, news, "ninguna");
SWOFTY_SETTING(bool, headlines, true); // the breaking-news banner and the F7 panel (overlay)
SWOFTY_SETTING(bool, effects, true);   // shadows, glows and animations in the panel and the banner
SWOFTY_SETTING(bool, progression, false);
SWOFTY_SETTING(bool, standins, true);
SWOFTY_SETTING(bool, talents, true);
SWOFTY_SETTING(bool, press, true);
SWOFTY_SETTING(std::string_view, press_answer, "-"); // the Team HQ row that answers the press
SWOFTY_SETTING(std::string_view, game_market, "silly_season");
// 1.1.0: each block can be turned off
SWOFTY_SETTING(bool, personality, true);   // drivers' characters shape their choices
SWOFTY_SETTING(bool, team_plans, true);    // each team follows a plan (title push, rebuild...)
SWOFTY_SETTING(bool, deadline_day, true);  // the last two races: deals close fast, a countdown in the overlay
SWOFTY_SETTING(bool, signing_cards, true); // a full card for a bombshell signing instead of the banner
SWOFTY_SETTING(bool, grid_reveal, true);   // next season's grid revealed team by team, once it's final
SWOFTY_SETTING(bool, hq_widget, true);     // a small market card in a corner of Team HQ
SWOFTY_SETTING(bool, car_numbers, true);   // new talents pick their own racing number
SWOFTY_SETTING(bool, living_market, true); // 1.1.0 phase 5: sister-team swaps, budgets and sponsors, team principals
SWOFTY_SETTING(bool, academies, true);     // 1.1.0 phase 6: reserve drivers, academies, partner teams
SWOFTY_SETTING(std::string_view, panel_size, "normal"); // the F7 panel's size: small, normal, large
// 1.1.0 overlays
SWOFTY_SETTING(bool, press_popup, true);   // a press question on screen at Team HQ, answered with 1-3
SWOFTY_SETTING(bool, race_report, true);   // a report after each race at Team HQ
SWOFTY_SETTING(bool, awards_show, true);   // the awards ceremony when the season closes
SWOFTY_SETTING(bool, season_cover, true);  // a cover for each new season

// a Progressive career (history mode): the season's real line-ups come from Progressive, so the market adds
// no stand-ins or new talents, and no mid-season sackings while the real years last
static bool g_real_career = false, g_real_now = false;
static constexpr const char *kRealGroup = "f1_career_sw_progressive"; // Progressive Career on the Swofty Seasons
static constexpr const char *kF2Group = "f1_career_f2_26";            // F2 Revamp: F2 2026, then F1 from 2027

static ss::Settings settings()
{
    ss::Settings s;
    s.intensity = ss::intensity_of(std::string_view(intensity));
    s.rumours = ss::rumours_of(std::string_view(rumours));
    s.news = ss::news_of(std::string_view(news));
    s.midseason = midseason;
    s.protect_teammate = protect_teammate;
    s.legends = legends;
    s.progression = progression;
    s.standins = standins;
    s.talents = talents;
    s.press = press;
    s.personality = personality;
    s.strategies = team_plans;
    s.deadline = deadline_day;
    s.living = living_market;
    s.academies = academies;
    s.lang = "en"; // everything the player reads is in English
    if (g_real_career) {
        s.standins = false;
        s.talents = false;
        if (g_real_now)
            s.midseason = false;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------
// State

namespace {

std::recursive_mutex g_lock;
json::Value g_baseline;
bool g_baseline_ok = false;
bool g_loaded = false;         // a career this addon handles is loaded
std::atomic<const F2RevampCareer *> g_f2{nullptr}; // F2 Revamp's career service, when that addon is on
bool g_f2_waiting = false;     // an F2 Revamp career in its F2 season: the market starts once the player is in F1
int g_report_seq = 0;          // rounds completed in this session (each a report after the race)
std::set<std::string> g_keen_prev; // the teams keen on the player at the last overlay update
bool g_keen_known = false;     // g_keen_prev was read for this career
bool g_pool_read = false;      // the game's driver lists were read for this career (game_pool.inl)
ss::State g_state;

// what the game told us between Team HQ visits
int g_weekends_ended = 0;      // fallback round counter when current_round() is unavailable
bool g_season_ended = false;
bool g_moves_finalised = false;
std::vector<std::pair<std::string, int>> g_results;  // driver, position
std::vector<ss::GridCar> g_grid;                     // last grid seen at a session start
bool g_grid_dirty = false;

// popups wait for a calm moment at Team HQ
double g_popup_wait = -1;
std::string g_report;            // a menu report to show
bool g_hub_idle = false;

std::string strip_game(std::string_view id)
{
    std::string s(id);
    size_t bar = s.find('|');
    if (bar != std::string::npos)
        s.resize(bar);
    if (!s.empty() && s.back() == '?')
        s.pop_back();
    if (s.rfind("game/", 0) == 0)
        s.erase(0, 5);
    return s;
}

std::string safe_name(std::string_view id)
{
    std::string out;
    for (char c : id)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return out.empty() ? std::string("career") : out;
}

std::string dll_dir()
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&dll_dir), &self);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string p(path);
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

bool load_baseline()
{
    std::string text;
    if (storage::available() && storage::read("data/baseline.json", text) == storage::Result::ok) {
        // read through the core
    } else {
        std::ifstream f(dll_dir() + "\\..\\data\\baseline.json", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    json::Error err;
    g_baseline = json::parse(text, &err);
    g_baseline_ok = g_baseline.is_object() && g_baseline["careers"].is_object();
    if (g_baseline_ok && g_baseline["careers"]["f1_career_2026"].is_object()) {
        json::Value stock = g_baseline["careers"]["f1_career_2026"]; // a copy first: adding the key moves the object's entries
        g_baseline["careers"]["f1_career_sw_progressive"] = std::move(stock);
        // F2 Revamp's career: the 2026 grid, raced as 2027 after the player's F2 season. Every contract runs a year
        // longer, as the stock 2026 career's would in its second season.
        json::Value f2 = g_baseline["careers"]["f1_career_2026"];
        int year = static_cast<int>(f2["year"].as_int(2026));
        f2["year"] = year + 1;
        if (json::Object *contracts = f2["contracts"].if_object())
            for (auto &[id, until] : *contracts)
                if (until.as_int(0) > 0)
                    until = until.as_int() + 1;
        g_baseline["careers"][kF2Group] = std::move(f2);
    }
    log("[silly-season] baseline: %s (%zu bytes)", g_baseline_ok ? "ok" : "missing or invalid", text.size());
    return g_baseline_ok;
}

std::string storage_path(const std::string &career) { return "careers/" + safe_name(career) + ".json"; }

// ---------------------------------------------------------------------------------------------------
// Career ids. The core runs a new career under a provisional id (<mode>:new<n>) until the game saves its
// slot, then under its real id. The market follows the id while the career is loaded; a career that was
// left before the switch is found again at its next load through the list of provisional markets.

bool provisional_id(std::string_view id) { return id.find(":new") != std::string_view::npos; }

constexpr const char *kProvisionalList = "careers/provisional.json";

json::Value read_provisional()
{
    std::string text;
    if (storage::available() && storage::read(kProvisionalList, text) == storage::Result::ok) {
        json::Error err;
        json::Value v = json::parse(text, &err);
        if (v.is_array())
            return v;
    }
    return json::Value::array();
}

void write_provisional(const json::Value &list)
{
    if (storage::available())
        storage::write(kProvisionalList, json::dump(list, 0));
}

void forget_provisional(const std::string &id)
{
    json::Value list = read_provisional(), out = json::Value::array();
    bool found = false;
    for (const json::Value &e : *list.if_array()) {
        if (e["id"].as_string() == id)
            found = true;
        else
            out.push_back(e);
    }
    if (found)
        write_provisional(out);
}

// after a save under a provisional id: remember it, newest last, a few at most
void note_provisional()
{
    json::Value list = read_provisional(), out = json::Value::array();
    for (const json::Value &e : *list.if_array())
        if (e["id"].as_string() != g_state.career)
            out.push_back(e);
    json::Value e = json::Value::object();
    e["id"] = json::Value(g_state.career);
    e["group"] = json::Value(g_state.group);
    e["player"] = json::Value(g_state.player);
    e["season_index"] = json::Value(static_cast<int64_t>(g_state.season_index));
    e["round"] = json::Value(static_cast<int64_t>(g_state.round));
    e["saved"] = json::Value(static_cast<int64_t>(std::time(nullptr)));
    out.push_back(std::move(e));
    while (out.if_array()->size() > 8)
        out.if_array()->erase(out.if_array()->begin());
    write_provisional(out);
}

// g_lock held: the loaded career's id changed (a new career's first save): the market follows it
void follow_career_id()
{
    if (!g_loaded)
        return;
    std::string now = career::saves::current();
    if (now.empty() || now == g_state.career || g_state.career.empty())
        return;
    std::string old = g_state.career;
    g_state.career = now;
    log("[silly-season] career id %s -> %s: the market moves to the new id", old.c_str(), now.c_str());
    if (provisional_id(old)) {
        if (storage::available())
            storage::remove(storage_path(old));
        forget_provisional(old);
    }
}

void save_state(const char *why)
{
    if (!g_loaded)
        return;
    follow_career_id();
    if (g_state.career.empty())
        g_state.career = career::saves::current(); // a new career gets its id with its first save
    if (g_state.career.empty()) {
        career::State cs{};
        if (career::state(&cs) == career::Result::ok && cs.key[0])
            g_state.career = std::string(cs.key, strnlen(cs.key, sizeof cs.key));
    }
    if (g_state.career.empty()) {
        log("[silly-season] not saved (%s): the career has no id yet", why);
        return;
    }
    json::Value v = ss::to_json(g_state);
    std::string text = json::dump(v, 0);
    // the game saves the profile often (menus, sessions): an unchanged state is already in storage and in
    // the core's career data, which the core writes with every save by itself
    static std::string last_career, last_text;
    if (g_state.career == last_career && text == last_text)
        return;
    storage::Result sr = storage::available() ? storage::write(storage_path(g_state.career), text)
                                              : storage::Result::unavailable;
    career::Result cr = career::Result::unavailable;
    if (career::saves::available())
        cr = career::saves::data(kAddon).set(g_state.career, v, 1);
    log("[silly-season] saved %s (%s): storage %s, career data %s, %zu bytes", g_state.career.c_str(), why,
        storage::name(sr), career::name(cr), text.size());
    if (sr == storage::Result::ok && provisional_id(g_state.career))
        note_provisional();
    if (sr == storage::Result::ok && (cr == career::Result::ok || !career::saves::available())) {
        last_career = g_state.career;
        last_text = std::move(text);
    } else {
        last_career.clear(); // try both again next time
    }
}

bool load_saved(const std::string &career)
{
    if (career::saves::available())
        if (auto stored = career::saves::data(kAddon).get(career))
            if (ss::from_json(g_state, stored->value)) {
                log("[silly-season] career %s: state from the career data (season %d, round %d)", career.c_str(),
                    g_state.year, g_state.round);
                return true;
            }
    std::string text;
    if (storage::available() && storage::read(storage_path(career), text) == storage::Result::ok) {
        json::Error err;
        if (ss::from_json(g_state, json::parse(text, &err))) {
            log("[silly-season] career %s: state from storage (season %d, round %d)", career.c_str(), g_state.year,
                g_state.round);
            return true;
        }
    }
    return false;
}

// A career loaded under its real id with no market saved there: the newest market saved under a
// provisional id of the same data group is most likely this career's, left before its first save. It is
// taken here and checked at the first Team HQ (same player, not ahead of the game), else started afresh.
std::string g_adopted_from;

// A career saved before the core named careers by their save slot ("f1_career_2025.driver_career:<guid>")
// kept its market under the career tracker's key ("dc-00070000"): take it, checked at the first Team HQ.
bool adopt_tracker_key(const std::string &id)
{
    career::State cs{};
    if (career::state(&cs) != career::Result::ok || !cs.key[0])
        return false;
    std::string key(cs.key, strnlen(cs.key, sizeof cs.key));
    if (key == id || !load_saved(key))
        return false;
    g_adopted_from = key;
    log("[silly-season] career %s: no market under this id; taking the one saved under the career's older id %s, checked "
        "at Team HQ", id.c_str(), key.c_str());
    return true;
}

bool adopt_provisional(const std::string &id, const std::string &group)
{
    json::Value list = read_provisional();
    std::string from;
    int64_t newest = -1;
    for (const json::Value &e : *list.if_array())
        if (e["group"].as_string() == group && e["saved"].as_int() > newest) {
            newest = e["saved"].as_int();
            from = std::string(e["id"].as_string());
        }
    if (from.empty() || from == id)
        return false;
    if (!load_saved(from)) {
        forget_provisional(from);
        return false;
    }
    g_adopted_from = from;
    log("[silly-season] career %s: no market under this id; taking the one saved under %s (its id before the first "
        "save), checked at Team HQ", id.c_str(), from.c_str());
    return true;
}

uint64_t seed_for(const std::string &career)
{
    uint64_t h = 1469598103934665603ull;
    for (char c : career)
        h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return h ^ static_cast<uint64_t>(GetTickCount64());
}

std::string career_id(const events::Context *ctx)
{
    std::string id = career::saves::current();
    if (id.empty() && ctx && ctx->career())
        id = ctx->career()->id;
    return id;
}

bool load_real_data();                // history.inl
void real_people_in(ss::State &st);   // history.inl

// F2 Revamp (miguelvf3.f2-revamp): its career races an F2 season first, then F1 from 2027 on the 2026 data. The
// market waits, unloaded, while the player is in F2 and starts at the first Team HQ of his F1 years.
const F2RevampCareer *f2_service()
{
    const F2RevampCareer *c = g_f2.load();
    return c && c->size >= offsetof(F2RevampCareer, promoted) + sizeof c->promoted ? c : nullptr;
}

// the loaded F2 Revamp career is still in its F2 season (or F2 Revamp can't say: the market waits)
bool f2_phase()
{
    const F2RevampCareer *c = f2_service();
    return !c || !c->in_f2 || c->in_f2() != 0;
}

void begin_market(const std::string &group, const std::string &id, bool fresh);

// a career of a data group the baseline knows
void begin_career(const events::Context &ctx, bool fresh)
{
    std::lock_guard lock(g_lock);
    if (!g_baseline_ok && !load_baseline())
        return;
    const auto &view = ctx.career();
    std::string group = view ? view->data_group : std::string();
    g_f2_waiting = false;
    if (group.empty() || !g_baseline["careers"][group].is_object()) {
        if (g_loaded)
            save_state("another career");
        g_loaded = false;
        log("[silly-season] career data group '%s' is not one Silly Season knows; the market stays the game's",
            group.c_str());
        return;
    }
    if (group == kF2Group && f2_phase()) {
        if (g_loaded)
            save_state("another career");
        g_loaded = false;
        g_f2_waiting = true;
        log("[silly-season] F2 Revamp career: %s; the market starts with the player's first F1 season",
            f2_service() ? "the player is in F2" : "F2 Revamp's service isn't there");
        return;
    }
    begin_market(group, career_id(&ctx), fresh);
}

void begin_market(const std::string &group, const std::string &id, bool fresh)
{
    std::lock_guard lock(g_lock);
    g_f2_waiting = false;
    g_report_seq = 0; // the overlays start afresh with each career
    g_keen_known = false;
    g_keen_prev.clear();
    g_real_career = group == kRealGroup;
    if (g_real_career && !load_real_data()) {
        if (g_loaded)
            save_state("another career");
        g_loaded = false;
        return;
    }
    if (g_loaded && g_state.career == id && !id.empty())
        return; // created, then loaded: same career
    g_state = ss::State{};
    g_pool_read = false;
    g_adopted_from.clear();
    bool have = !fresh && !id.empty() &&
                (load_saved(id) || adopt_tracker_key(id) || (!provisional_id(id) && adopt_provisional(id, group)));
    if (!have) {
        std::string err;
        if (!ss::init(g_state, g_baseline, group, seed_for(id), &err)) {
            log("[silly-season] cannot start the market: %s", err.c_str());
            g_loaded = false;
            return;
        }
        g_state.season_index = -1;     // the first idle Team HQ says where the career is
        log("[silly-season] career %s (%s): new market for %d, %zu drivers, %zu teams", id.c_str(), group.c_str(),
            g_state.year, g_state.drivers.size(), g_state.teams.size());
    } else {
        ss::refresh_from_baseline(g_state, g_baseline);
    }
    g_state.career = id;
    g_state.group = group;
    if (g_real_career)
        real_people_in(g_state);
    g_real_now = g_state.real;
    g_loaded = true;
    g_weekends_ended = 0;
    g_season_ended = g_moves_finalised = false;
    g_results.clear();
}

// ids from the game: "game/max_verstappen", or an entrant reference "f1_2026-max_verstappen"
std::string driver_label(std::string_view raw)
{
    std::string id = strip_game(raw);
    if (!id.empty() && !g_state.driver(id)) {
        size_t dash = id.find('-');
        if (dash != std::string::npos && (id.rfind("f1_", 0) == 0 || id.rfind("f2_", 0) == 0))
            id.erase(0, dash + 1);
    }
    return id;
}

// the team stat each team's rows carried at the last read: seat writes go to the slot the game uses
std::unordered_map<std::string, uint32_t> g_row_team_stat;

// every seat of the new grid, so whatever the game did over the winter, the grid ends as planned
std::vector<ss::SeatChange> full_grid(const ss::Settings &s)
{
    std::vector<ss::SeatChange> out;
    for (const ss::Team &t : g_state.teams)
        for (int k = 0; k < 2; ++k)
            if (!t.seats[static_cast<size_t>(k)].empty() && !g_state.locked(t, k, s))
                out.push_back({t.id, k, t.seats[static_cast<size_t>(k)], "season"});
    return out;
}

// returns the new season's seats ("season") the game refused, for the caller to take back once the grid is read again
std::vector<ss::SeatChange> apply_changes(const std::vector<ss::SeatChange> &changes, const char *why)
{
    std::vector<ss::SeatChange> refused;
    const SwoftyCareerOps *ops = career::ops();
    for (const ss::SeatChange &c : changes) {
        const ss::Team *t = g_state.team(c.team);
        if (!t)
            continue;
        uint32_t stat = static_cast<uint32_t>(t->stat);
        if (auto seen = g_row_team_stat.find(c.team); seen != g_row_team_stat.end()) {
            stat = seen->second; // the slot the game's rows use for this team (Konnersport: the custom team's)
        } else if (ops && ops->version >= 7 && ops->team_stat) {
            uint32_t live = 0;
            if (ops->team_stat(("game/" + c.team).c_str(), &live) == SWOFTY_ASSET_OK)
                stat = live;
        }
        career::Result r = career::set_seat_of_team(stat, c.seat, Ref<drivers::Identity>("game/" + c.driver));
        // a reserve's "game/<label>" may not resolve (jack_tremblay): ask by his stat id instead
        const ss::Driver *d = g_state.driver(c.driver);
        if (r == career::Result::invalid && d && ops && ops->version >= 4 && ops->set_seat) {
            std::string req = "{\"driver\":" + std::to_string(d->stat) + ",\"team\":" + std::to_string(stat) +
                              ",\"seat\":" + std::to_string(c.seat) + "}";
            char report[2048];
            size_t len = 0;
            int raw = ops->set_seat({req.data(), req.size()}, report, sizeof report, &len);
            r = raw == SWOFTY_ASSET_OK || raw == SWOFTY_TOO_SMALL ? career::Result::ok
                : raw == SWOFTY_ASSET_LATE                         ? career::Result::late
                                                                   : career::Result::invalid;
        }
        log("[silly-season] %s: %s seat %d (team stat %u) <- %s: %s", why, c.team.c_str(), c.seat, stat,
            c.driver.c_str(), career::name(r));
        if (r != career::Result::ok && c.reason == "standin") {
            ss::cancel_standin(g_state, c.team, c.seat, c.driver); // the regular driver races after all
            log("[silly-season] %s: the stand-in is called off", why);
        }
        // a sacking or a sister-team swap the game didn't take: the market takes it back before anyone reads it,
        // so the news never says a driver is out while the game still races him
        if (r != career::Result::ok && (c.reason == "midseason" || c.reason == "swap")) {
            bool undone = ss::cancel_change(g_state, c.team, c.seat, c.driver);
            log("[silly-season] %s: the game kept its driver; the %s is %s", why, c.reason == "swap" ? "swap" : "sacking",
                undone ? "undone in the market and its news withdrawn" : "not found in the market (left as it is)");
        }
        if (r != career::Result::ok && c.reason == "season")
            refused.push_back(c);
    }
    return refused;
}

// ---------------------------------------------------------------------------------------------------
// Texts in the game: the inbox mails' lng keys (db/emails.json), the Team HQ tab title and the names of
// the new talents (fictional reserves renamed through their own name keys)

std::vector<std::string> g_name_keys; // talent name keys this addon overrides, cleared at unload
int g_talent_ratings_year = 0;
// the text each key holds in the game now: only changed keys are set again, and the game's language
// listeners (a ~25-45 ms refresh) are only told when something changed
std::unordered_map<std::string, std::string> g_lang_now;
bool g_lang_refresh_due = false;

// refresh: false while a menu of ours is open (the refresh is a visible hitch); it then waits for the
// next call that may refresh (Team HQ)
void apply_texts(const ss::Settings &s, bool refresh = true)
{
    g_state.player_name = s.lang == "en" ? "You" : "Tú";
    if (!lang::available())
        return;
    auto set = [](const std::string &key, const std::string &text) {
        if (text.empty())
            return;
        auto it = g_lang_now.find(key);
        if (it != g_lang_now.end() && it->second == text)
            return;
        lang::Result r = lang::set(key, text);
        if (r == lang::Result::ok || r == lang::Result::late) { // late: set at run time, applies on next read
            g_lang_now[key] = text;
            g_lang_refresh_due = true;
        }
    };
    set("lng_silly_season_mail_preview_subject", g_state.mail_preview.subject);
    set("lng_silly_season_mail_preview_body", g_state.mail_preview.body);
    set("lng_silly_season_mail_radar_subject", g_state.mail_radar.subject);
    set("lng_silly_season_mail_radar_body", g_state.mail_radar.body);
    set("lng_silly_season_mail_grid_subject", g_state.mail_grid.subject);
    set("lng_silly_season_mail_grid_body", g_state.mail_grid.body);
    for (auto &[key, text] : ss::name_overrides(g_state)) {
        set(key, text);
        if (std::find(g_name_keys.begin(), g_name_keys.end(), key) == g_name_keys.end())
            g_name_keys.push_back(key);
    }
    int n = ss::unread(g_state);
    set("lng_swofty_hub_silly_season", n > 0 ? "SILLY SEASON (" + std::to_string(n) + ")" : std::string("SILLY SEASON"));
    if (refresh && g_lang_refresh_due) {
        g_lang_refresh_due = false;
        lang::refresh();
    }
}

void clear_texts()
{
    g_lang_now.clear();
    g_lang_refresh_due = false;
    if (!lang::available())
        return;
    lang::clear("lng_swofty_hub_silly_season");
    for (const std::string &k : g_name_keys)
        lang::clear(k);
    g_name_keys.clear();
    lang::refresh();
}

// The new talents' ratings: their game rows still hold the old reserve driver's numbers. The game only
// takes ratings for drivers in the career's statistics table, which a reserve joins once he races in F1, so
// only the talents with an F1 seat are written, at each idle Team HQ until every one has taken them.
std::string g_talent_ratings_said; // the last outcome logged, so a refusal isn't logged at every visit

void push_talent_ratings()
{
    if (g_talent_ratings_year == g_state.year)
        return;
    size_t ok = 0, all = 0;
    career::Result last = career::Result::ok;
    for (const ss::RatingChange &rc : ss::talent_ratings(g_state)) {
        career::DriverRatings dr;
        dr.experience = rc.experience;
        dr.racecraft = rc.racecraft;
        dr.awareness = rc.awareness;
        dr.pace = rc.pace;
        ++all;
        career::Result r = career::set_driver_ratings(Ref<drivers::Identity>("game/" + rc.driver), dr);
        if (r == career::Result::ok)
            ++ok;
        else
            last = r;
    }
    // done for this season once every seated talent took his numbers; else the next idle Team HQ tries again
    if (ok == all)
        g_talent_ratings_year = g_state.year;
    if (!all)
        return;
    char said[160];
    std::snprintf(said, sizeof said, "%d: %zu of %zu written%s%s", g_state.year, ok, all, ok == all ? "" : ", last refusal: ",
                  ok == all ? "" : career::name(last));
    if (g_talent_ratings_said != said) {
        g_talent_ratings_said = said;
        log("[silly-season] new talents in F1, ratings %s", said);
    }
}

// The new talents' racing numbers: a number of their own once they reach F1 (the game refuses one another
// driver holds, and the next is tried). Unavailable or late calls wait for the next idle Team HQ.
std::string g_numbers_said;

void push_talent_numbers()
{
    if (!car_numbers)
        return;
    const SwoftyCareerOps *ops = career::ops();
    std::string said;
    for (const ss::NumberWish &w : ss::talent_numbers(g_state)) {
        const ss::Driver *d = g_state.driver(w.driver);
        if (!d)
            continue;
        bool stop = false;
        for (int n : w.numbers) {
            career::Result r = career::set_car_number(Ref<drivers::Identity>("game/" + w.driver), n);
            // a renamed reserve's "game/<label>" may not resolve: ask by his stat id
            if (r == career::Result::invalid && ops && ops->version >= 4 && ops->set_car_number) {
                int raw = ops->set_car_number(static_cast<uint32_t>(d->stat), n, 0);
                r = raw == SWOFTY_ASSET_OK ? career::Result::ok
                    : raw == SWOFTY_REFUSED ? career::Result::refused
                    : raw == SWOFTY_ASSET_LATE ? career::Result::late
                                              : career::Result::invalid;
            }
            if (r == career::Result::ok) {
                ss::number_result(g_state, w.driver, n, true);
                log("[silly-season] new talent %s races with #%d", w.driver.c_str(), n);
                break;
            }
            if (r == career::Result::refused) {
                ss::number_result(g_state, w.driver, n, false);
                continue;
            }
            said = std::string(career::name(r)) + " for " + w.driver;
            stop = true;
            break;
        }
        if (stop)
            break;
    }
    if (!said.empty() && said != g_numbers_said) {
        g_numbers_said = said;
        log("[silly-season] new talents' numbers: %s; tried again at the next Team HQ", said.c_str());
    }
}

// ---------------------------------------------------------------------------------------------------
// Who the player is. The session events' context can come without participants on this core, so the
// career state (the player's driver and team stat ids) is the source that always answers.

int g_player_log_budget = 4; // diagnostic lines about the player, a few per game run

std::string team_by_stat(uint32_t stat)
{
    const SwoftyCareerOps *ops = career::ops();
    for (const ss::Team &t : g_state.teams) {
        uint32_t s = static_cast<uint32_t>(t.stat);
        if (ops && ops->version >= 7 && ops->team_stat) {
            uint32_t live = 0;
            if (ops->team_stat(("game/" + t.id).c_str(), &live) == SWOFTY_ASSET_OK)
                s = live;
        }
        // an optional team (Konnersport) races in the custom team's slot: its rows carry the baseline's stat
        if (s == stat || (t.optional && static_cast<uint32_t>(t.stat) == stat))
            return t.id;
    }
    return {};
}


std::string known_driver(uint32_t stat); // the real grid's lookup (below)
uint32_t g_player_stat = 0;               // the human's driver stat id, from the rows or the career state (0: not known)

// g_lock held. Puts the player in his team in the model (once, or when he changes team)
void sync_player()
{
    career::State cs{};
    if (career::state(&cs) != career::Result::ok || !cs.loaded || !cs.has_player)
        return;
    // the career state carries each stat id in its high 16 bits (driver 7 arrives as 0x70000)
    auto stat_of = [](uint32_t v) { return v >= 0x10000 && (v & 0xFFFF) == 0 ? v >> 16 : v; };
    uint32_t team_stat = stat_of(cs.player_team), driver_stat = stat_of(cs.player_driver);
    std::string team = team_by_stat(team_stat);
    if (team.empty())
        team = team_by_stat(cs.player_team);
    if (const ss::Team *x = g_state.team(team); x && x->optional)
        team.clear(); // the player's own My Team team sits in the custom slot, as Konnersport does
    std::string driver = known_driver(driver_stat);
    g_player_stat = driver_stat;
    if (driver.empty())
        driver = g_state.player.empty() ? std::string("player") : g_state.player; // a created driver
    const ss::Team *t = g_state.team(team);
    if (!t) {
        if (g_player_log_budget-- > 0)
            log("[silly-season] player: driver stat %u, team stat %u: no team of this career has that stat",
                cs.player_driver, cs.player_team);
        return;
    }
    if (g_state.player == driver && g_state.player_team == team && (t->seats[0] == driver || t->seats[1] == driver))
        return;
    // the grid as the model has it, with the player in his team: he takes the seat of the team's weaker
    // driver (the one the game left out), unless he is already in it
    std::vector<ss::GridCar> cars;
    for (const ss::Team &x : g_state.teams) {
        bool mine = x.id == team;
        int out = -1;
        if (mine && x.seats[0] != driver && x.seats[1] != driver) {
            const ss::Driver *a = g_state.driver(x.seats[0]), *b = g_state.driver(x.seats[1]);
            out = !a ? 0 : !b ? 1 : ss::driver_value(*a) <= ss::driver_value(*b) ? 0 : 1;
        }
        for (int k = 0; k < 2; ++k) {
            std::string who = k == out ? driver : x.seats[static_cast<size_t>(k)];
            if (who.empty() || (who == driver && !mine))
                continue;
            cars.push_back({who, x.id, who == driver});
        }
    }
    ss::sync_grid(g_state, cars);
    log("[silly-season] player: %s drives for %s (career state: driver stat %u, team stat %u)", driver.c_str(),
        team.c_str(), cs.player_driver, cs.player_team);
}

// ---------------------------------------------------------------------------------------------------
// The real grid. The career entity holds one entrant row per driver of the career: his driver key, his
// team, his seat and whether he is the human. The core's own seat operations read the same rows, and the
// SDK's game headers give their layout (swofty/game/career.hpp, entrants.hpp, marked experimental), so
// Silly Season reads them at every idle Team HQ: transfers made with the Career Mode Editor, other addons'
// line-ups, a season already under way and the player himself all come from the game. Every read is
// guarded; rows that don't look like a grid are left alone and the market keeps its own.

struct RealSeat {
    uint32_t driver = 0, team = 0; // stat ids
    int seat = 0;
    bool human = false;
};

// history mode (history.inl): the drivers' looks of each real year, the rows last read
std::unordered_map<uint32_t, std::string> g_real_stats; // a look's (or a driver's) stat id -> the driver
std::vector<RealSeat> g_last_seats;                     // the rows last read, to tell the year from them
// 1.1.0 phase 5: each driver's career row as the game holds it (stat id -> S race seat, F free agent, R reserve,
// M more than one row), from the last read
std::unordered_map<uint32_t, char> g_row_class;

namespace rows {
constexpr uintptr_t kEntrantsBegin = 0x2e0, kEntrantsEnd = 0x2e8, kBusy = 0x2f8; // CareerEntity
constexpr size_t kSize = 0x88, kOrdinal = 0x48, kDriver = 0x50, kTeam = 0x58, kVehicle = 0x5c, kPosition = 0x80;
constexpr uint32_t kNoKey = 0x3fff0000u, kHumanBit = 0x10000000u;
constexpr size_t kMaxRows = 512;

// a copy from game memory that a bad address can't crash
bool copy(void *dst, uintptr_t src, size_t n)
{
    if (!src || !n)
        return false;
    __try {
        std::memcpy(dst, reinterpret_cast<const void *>(src), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T> T at(const std::vector<uint8_t> &buf, size_t off)
{
    T v{};
    std::memcpy(&v, buf.data() + off, sizeof v);
    return v;
}
} // namespace rows

// the race seats of the loaded career; false (with why) when the rows can't be read or don't look right
bool read_real_grid(std::vector<RealSeat> &out, std::string &why)
{
    out.clear();
    uintptr_t e = reinterpret_cast<uintptr_t>(career::entity());
    if (!e) {
        why = "no career entity";
        return false;
    }
    uintptr_t begin = 0, end = 0;
    uint8_t busy = 0;
    if (!rows::copy(&begin, e + rows::kEntrantsBegin, sizeof begin) || !rows::copy(&end, e + rows::kEntrantsEnd, sizeof end) ||
        !rows::copy(&busy, e + rows::kBusy, sizeof busy)) {
        why = "the career entity is unreadable";
        return false;
    }
    if (busy) {
        why = "the game is updating the rows";
        return false;
    }
    if (!begin || end <= begin || (end - begin) % rows::kSize || (end - begin) / rows::kSize > rows::kMaxRows) {
        why = "the rows don't look like an entrant list";
        return false;
    }
    std::vector<uint8_t> buf(end - begin);
    if (!rows::copy(buf.data(), begin, buf.size())) {
        why = "the rows are unreadable";
        return false;
    }
    std::set<std::pair<uint32_t, int>> seats;
    size_t humans = 0;
    g_row_class.clear();
    for (size_t off = 0; off < buf.size(); off += rows::kSize) {
        uint32_t ordinal = rows::at<uint32_t>(buf, off + rows::kOrdinal), driver = rows::at<uint32_t>(buf, off + rows::kDriver),
                 team = rows::at<uint32_t>(buf, off + rows::kTeam), vehicle = rows::at<uint32_t>(buf, off + rows::kVehicle);
        uint16_t position = rows::at<uint16_t>(buf, off + rows::kPosition);
        bool teamless = (team & 0xffff0000u) == rows::kNoKey, car = (vehicle & 0xffff0000u) != rows::kNoKey;
        int seat = (position >> 8) ? (position & 0xff) : -1;
        // what the game makes of each driver for a seat change (the core's set_seat): a race driver ('S') and a free
        // agent ('F') take a seat; a reserve ('R': a team, no race seat) and a driver with two rows ('M') don't
        if ((driver & 0xffu) == 0) {
            char cls = car && !teamless && seat >= 0 && seat <= 1 ? 'S' : teamless ? 'F' : 'R';
            auto [it, fresh] = g_row_class.emplace(driver >> 16, cls);
            if (!fresh)
                it->second = 'M';
        }
        if (teamless || !car || seat < 0 || seat > 1)
            continue; // a reserve, a free agent or a driver without a car
        RealSeat s{driver >> 16, team >> 16, seat, (ordinal & rows::kHumanBit) != 0};
        if (!seats.insert({s.team, s.seat}).second) {
            why = "two drivers on one seat";
            return false;
        }
        humans += s.human ? 1 : 0;
        out.push_back(s);
    }
    if (out.size() < 16 || out.size() > 26 || humans > 1) {
        why = std::to_string(out.size()) + " race seats and " + std::to_string(humans) + " human(s) don't look like a grid";
        out.clear();
        return false;
    }
    return true;
}

// a driver label from the game database, for a driver the baseline doesn't know (another addon's)
std::string db_label(uint32_t stat)
{
    static std::unordered_map<uint32_t, std::string> labels;
    static bool built = false;
    if (!built && db::available()) {
        built = true;
        db::Cursor c{};
        for (db::Result r = db::open("entrants.drivers", &c); r == db::Result::ok; r = db::step(&c)) {
            db::Value v{};
            const char *label = db::row_name(&c);
            if (label && db::cell(&c, "stat_id", &v) == db::Result::ok)
                labels.emplace(static_cast<uint32_t>(v.i), label);
        }
        log("[silly-season] game database: %zu driver label(s) in entrants.drivers", labels.size());
    }
    auto it = labels.find(stat);
    return it == labels.end() ? std::string() : it->second;
}

// "2025-williams_carlos_sainz-jr" or "carlos_sainz-jr" -> "Carlos Sainz Jr"
std::string name_from_label(std::string label)
{
    if (size_t dash = label.find('-'); dash != std::string::npos && dash > 0 && std::isdigit(static_cast<unsigned char>(label[0])))
        label.erase(0, dash + 1);
    if (size_t us = label.find('_'); us != std::string::npos && label.find('_', us + 1) != std::string::npos)
        label.erase(0, us + 1); // a team prefix ("williams_carlos_sainz")
    std::string out;
    bool up = true;
    for (char ch : label) {
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

// drivers of the game the baseline doesn't have: the career's lists and their statistics
#include "game_pool.inl"

// the market's driver for a stat id: the baseline's own list first (Carlos Sainz is stat 0, which is also
// what a driver with no stat id holds), then a driver added earlier from the game; "" when unknown
std::string known_driver(uint32_t stat)
{
    // a Progressive career: each real year dresses its drivers in looks of their own, with their own stat ids
    if (g_real_career && !g_real_stats.empty())
        if (auto it = g_real_stats.find(stat); it != g_real_stats.end() && g_state.driver(it->second))
            return it->second;
    static std::unordered_map<uint32_t, std::string> by_stat;
    static bool built = false;
    if (!built && g_baseline_ok) {
        built = true;
        if (const json::Object *o = g_baseline["drivers"].if_object())
            for (const auto &[id, v] : *o)
                if (v["stat"].is_int())
                    by_stat.emplace(static_cast<uint32_t>(v["stat"].as_int()), id);
    }
    if (auto it = by_stat.find(stat); it != by_stat.end() && g_state.driver(it->second))
        return it->second;
    for (const auto &[id, d] : g_state.drivers)
        if (stat != 0 && d.stat == static_cast<int>(stat) && d.kind != "player" && d.kind != "pool")
            return id;
    // a story team's copy of a driver (aiden_jackson_konnersport): the game races it in his place, so it is him.
    // A 2025 career's results named one only by its stat id, and his races were lost (a 2026-11-10 session).
    if (stat != 0) {
        std::string label = db_label(stat);
        for (const char *tail : {"_konnersport", "_zenith"}) {
            size_t n = std::strlen(tail);
            if (label.size() > n && label.compare(label.size() - n, n, tail) == 0) {
                std::string base = label.substr(0, label.size() - n);
                if (g_state.driver(base))
                    return base;
            }
        }
    }
    return {};
}

// the market's driver for a stat id, added from the game database when the baseline doesn't have him
std::string driver_for_stat(uint32_t stat)
{
    if (std::string id = known_driver(stat); !id.empty()) {
        // a driver added before his game name could be read ("Driver 102"): name him now
        ss::Driver *d = g_state.driver(id);
        if (d && id.rfind("stat_", 0) == 0 && d->name == "Driver " + std::to_string(stat))
            if (std::string label = db_label(stat); !label.empty()) {
                d->name = name_from_label(label);
                log("[silly-season] real grid: driver stat %u is %s", stat, d->name.c_str());
            }
        return id;
    }
    std::string label = db_label(stat);
    std::string id = label.empty() ? "stat_" + std::to_string(stat) : label;
    if (!g_state.driver(id)) {
        // the game's own row: his name and his career statistics
        ss::GameDriver g;
        if (!label.empty() && game_pool::driver(label, "f1", g)) {
            g.stat = static_cast<int>(stat);
            ss::adopt_game_drivers(g_state, {g});
        }
        if (!g_state.driver(id)) {
            ss::Driver d;
            d.id = id;
            d.name = label.empty() ? "Driver " + std::to_string(stat) : name_from_label(label);
            d.kind = "f1";
            d.stat = static_cast<int>(stat);
            d.born = g_state.year - 27;
            g_state.drivers.emplace(id, d);
        }
        const ss::Driver *d = g_state.driver(id);
        log("[silly-season] real grid: driver stat %u is not in the baseline; added as %s%s", stat, d->name.c_str(),
            g.rated ? " with the game's ratings" : "");
    }
    return id;
}

std::string g_own_team_name;      // the player's own My Team team, shown in the hub ("" when he drives for a known team)
std::string g_real_grid_said;     // the last outcome logged
std::string g_rows_said;          // the last row count logged

// g_lock held, after a read of the career's rows: the drivers the game takes for a race seat now. The market's
// mid-season changes (sackings, stand-ins) pick only among them: the core refuses a reserve (a team but no race
// seat) and a driver without a single career row, and 1.0.1 announced such changes that never happened.
void take_row_classes()
{
    g_state.game_seatable.clear();
    if (g_row_class.empty()) {
        g_state.game_rows_known = false;
        return;
    }
    std::string stat0 = known_driver(0); // stat 0 is Carlos Sainz, and also what a driver with no stat id holds
    size_t seated = 0, free_agents = 0, reserves = 0, none = 0;
    auto take = [&](const std::string &id, uint32_t stat) {
        auto it = g_row_class.find(stat);
        char c = it == g_row_class.end() ? 0 : it->second;
        if (c == 'S' || c == 'F') {
            if (g_state.game_seatable.insert(id).second)
                ++(c == 'S' ? seated : free_agents);
        } else if (c == 'R') {
            ++reserves;
        } else {
            ++none;
        }
    };
    for (const auto &[id, d] : g_state.drivers) {
        if (d.kind == "player" || (d.stat == 0 && id != stat0))
            continue;
        take(id, static_cast<uint32_t>(d.stat));
    }
    for (const auto &[stat, id] : g_real_stats) // Progressive: each real year's looks have stat ids of their own
        if (g_state.driver(id))
            take(id, stat);
    g_state.game_rows_known = true;
    char said[200];
    std::snprintf(said, sizeof said, "%zu race drivers and %zu free agents can take a seat; %zu are reserves in the game, %zu have no single row",
                  seated, free_agents, reserves, none);
    if (said != g_rows_said) {
        g_rows_said = said;
        log("[silly-season] career rows (%zu): %s", g_row_class.size(), said);
    }
}

// g_lock held: the market takes the game's grid; false when it can't be read (the caller falls back)
bool sync_real_grid()
{
    std::vector<RealSeat> seats;
    std::string why;
    if (!read_real_grid(seats, why)) {
        std::string said = "unread: " + why;
        if (said != g_real_grid_said) {
            g_real_grid_said = said;
            log("[silly-season] real grid: not read (%s); the market keeps its own grid", why.c_str());
        }
        return false;
    }
    g_last_seats = seats;
    // the player's team, and his team-mate when the team is his own (My Team): no team of the baseline
    uint32_t human_team = 0;
    bool have_human = false;
    for (const RealSeat &s : seats)
        if (s.human) {
            human_team = s.team;
            have_human = true;
            g_player_stat = s.driver;
        }
    std::string own_mate;
    std::vector<std::string> elsewhere; // drivers of teams Silly Season doesn't know (Konnersport, another addon's)
    std::vector<ss::GridCar> cars;
    std::vector<std::string> moved;
    size_t unknown_teams = 0;
    std::string player, player_team;
    std::unordered_map<std::string, uint32_t> row_stats;
    for (const RealSeat &s : seats) {
        std::string team = team_by_stat(s.team);
        // My Team: the player's own team uses the custom slot Konnersport would use; it's his team, not Konnersport
        if (have_human && s.team == human_team)
            if (const ss::Team *x = g_state.team(team); x && x->optional)
                team.clear();
        if (!team.empty())
            row_stats[team] = s.team;
        std::string driver;
        if (s.human) {
            driver = known_driver(s.driver);
            if (driver.empty())
                driver = g_state.player.empty() ? std::string("player") : g_state.player; // a created driver
            player = driver;
            player_team = team;
        } else {
            driver = driver_for_stat(s.driver);
        }
        if (team.empty()) {
            if (have_human && s.team == human_team && !s.human) {
                own_mate = driver; // the player's own team
            } else if (!s.human) {
                ++unknown_teams;
                elsewhere.push_back(driver);
            }
            continue;
        }
        if (!s.human) {
            const ss::Driver *d = g_state.driver(driver);
            if (d && d->team != team)
                moved.push_back((d->name.empty() ? driver : d->name) + " -> " + team);
        }
        cars.push_back({driver, team, s.human});
    }
    take_row_classes();
    // more than a team's worth of cars on unknown teams: the team ids don't match this career (another
    // addon's teams?), and syncing would empty the market's seats. Leave the market as it is.
    if (unknown_teams > 2) {
        std::string said = "unmatched: " + std::to_string(unknown_teams);
        if (said != g_real_grid_said) {
            g_real_grid_said = said;
            log("[silly-season] real grid: %zu cars race for teams Silly Season doesn't know; the market keeps its own grid",
                unknown_teams);
        }
        return false;
    }
    g_row_team_stat = std::move(row_stats);
    // the player's own team: the market leaves its seats alone
    g_state.protected_ids.clear();
    g_state.own_mate = own_mate;
    if (!own_mate.empty())
        g_state.protected_ids.insert(own_mate);
    for (const std::string &id : elsewhere)
        g_state.protected_ids.insert(id); // they race for a team the market doesn't plan: not free agents
    g_own_team_name.clear();
    if (have_human && player_team.empty()) {
        g_own_team_name = "My Team";
        g_state.player = player;
        g_state.player_team.clear();
    }
    // an observer career (My Team): where everyone was, so the moves the game made can be told
    std::map<std::string, std::string> before;
    if (g_state.observer)
        for (const auto &[id, d] : g_state.drivers)
            if (!d.team.empty())
                before[id] = d.team;
    ss::sync_grid(g_state, cars);
    if (have_human && player_team.empty()) {
        // sync_grid only seats the known teams: the player keeps his own team
        g_state.player = player;
        g_state.player_team.clear();
        if (ss::Driver *mate = g_state.driver(own_mate)) {
            mate->team.clear();
            mate->seat = -1;
        }
    }
    char said[200];
    std::snprintf(said, sizeof said, "%zu seats, player %s (%s)%s%s, %zu on teams Silly Season doesn't know", seats.size(),
                  player.empty() ? "-" : player.c_str(), player_team.empty() ? (have_human ? "own team" : "-") : player_team.c_str(),
                  own_mate.empty() ? "" : ", team-mate ", own_mate.c_str(), unknown_teams);
    if (said != g_real_grid_said || !moved.empty()) {
        g_real_grid_said = said;
        std::string list;
        for (size_t i = 0; i < moved.size() && i < 8; ++i)
            list += (i ? ", " : "") + moved[i];
        log("[silly-season] real grid: %s%s%s", said, moved.empty() ? "" : "; the market follows the game: ", list.c_str());
    }
    if (!have_human)
        sync_player(); // no row says who the human is: the career state does
    if (g_state.observer && !moved.empty() && g_state.season_index >= 0) {
        auto pos = career::current_round();
        bool winter = pos && g_state.season_index >= 0 && pos->season_index > g_state.season_index;
        int told = ss::observe_grid(g_state, settings(), before, winter);
        log("[silly-season] observer: %d move(s) the game made told as official%s", told,
            told == 0 ? " (none: a partial read, or too many at once to be this career's)" : winter ? " (the winter's)" : "");
    }
    return true;
}

// 1.1.0 phase 0: the probe of the career data to read next (logs only)
#include "probe.inl"
// 1.1.0 phase 1: the season the game really ran (race results, secret meetings)
#include "game_data.inl"
// 1.1.0 phase 2: history mode for Progressive Career on the Swofty Seasons
#include "history.inl"

// 1.1.0: the game's own ratings of each driver
#include "game_ratings.inl"

std::string g_ratings_said; // the last outcome logged

// g_lock held, at an idle Team HQ: the market takes what the game shows for each driver (the overall and four of
// its five statistics; focus only for the hub), so values, interest and the cards match the game's screens
void take_game_ratings()
{
    std::vector<game_ratings::Reading> list;
    std::string why;
    if (!game_ratings::read_all(list, why)) {
        std::string said = "unread: " + why;
        if (said != g_ratings_said) {
            g_ratings_said = said;
            log("[silly-season] game ratings: not read (%s); the market keeps its own", why.c_str());
        }
        return;
    }
    // stat id -> the market's driver (the player by his stat; the baseline's, then any the market added)
    std::unordered_map<uint32_t, std::string> who;
    for (const auto &[id, d] : g_state.drivers)
        if (d.stat > 0 && d.kind != "player" && !d.regen)
            who.emplace(static_cast<uint32_t>(d.stat), id);
    std::set<uint32_t> seen;
    size_t taken = 0, kinds = 0;
    std::string player_line;
    for (const game_ratings::Reading &r : list) {
        kinds += r.kind != 0 ? 1 : 0;
        if (!seen.insert(r.stat).second)
            continue; // a second entry of one driver: the first is the one the game shows
        std::string id = r.stat == g_player_stat && g_player_stat && !g_state.player.empty() ? g_state.player : known_driver(r.stat);
        if (id.empty())
            if (auto it = who.find(r.stat); it != who.end())
                id = it->second;
        using namespace game_ratings;
        if (id.empty() || !ss::observe_game_ratings(g_state, id, r.overall, r.v[kExperience], r.v[kRacecraft], r.v[kFocus],
                                                    r.v[kAwareness], r.v[kPace]))
            continue;
        ++taken;
        if (id == g_state.player) {
            char b[160];
            std::snprintf(b, sizeof b, " (player %.0f: experience %.0f, racecraft %.0f, focus %.0f, awareness %.0f, pace %.0f)",
                          r.overall, r.v[kExperience], r.v[kRacecraft], r.v[kFocus], r.v[kAwareness], r.v[kPace]);
            player_line = b;
        }
    }
    char said[320];
    std::snprintf(said, sizeof said, "%zu entries read, %zu drivers of the market take them%s%s", list.size(), taken,
                  kinds ? ", some entries of another kind" : "", player_line.c_str());
    if (said != g_ratings_said) {
        g_ratings_said = said;
        log("[silly-season] game ratings: %s", said);
    }
}

int g_results_rounds = -1;       // rounds the game's standings say were run this season (-1: unread)
std::string g_results_said;      // the last outcome logged

// g_lock held, at an idle Team HQ: the game's race results go into the market, round by round, and the
// player's secret meetings into the news
void take_season_data()
{
    g_results_rounds = -1;
    std::vector<game_data::Round> rounds;
    std::string why;
    if (!game_data::read_results(rounds, why)) {
        std::string said = "unread: " + why;
        if (said != g_results_said) {
            g_results_said = said;
            log("[silly-season] results: not read (%s); races are simulated", why.c_str());
        }
    } else {
        g_results_rounds = static_cast<int>(rounds.size());
        int fed = 0;
        size_t unknown = 0;
        std::string unknown_list; // "80 (label)": what the log says of a driver the market can't tell
        for (int r = std::max(0, g_state.results_read); r < g_results_rounds; ++r) {
            for (const game_data::Finish &f : rounds[static_cast<size_t>(r)]) {
                std::string id = f.stat == g_player_stat && g_player_stat && !g_state.player.empty() ? g_state.player : known_driver(f.stat);
                if (id.empty()) {
                    if (++unknown <= 4) {
                        std::string label = db_label(f.stat);
                        unknown_list += (unknown_list.empty() ? "" : ", ") + std::to_string(f.stat) + " (" +
                                        (label.empty() ? std::string("no label") : label) + ")";
                    }
                    continue;
                }
                ss::record_result(g_state, id, f.position, r + 1); // the season's round, also when several are taken at once
            }
            ++fed;
        }
        if (g_results_rounds > g_state.results_read)
            g_state.results_read = g_results_rounds;
        char said[160];
        std::snprintf(said, sizeof said, "%d round(s) run", g_results_rounds);
        if (fed || said != g_results_said) {
            g_results_said = said;
            std::string winner;
            if (!rounds.empty())
                for (const game_data::Finish &f : rounds.back())
                    if (f.position == 1)
                        winner = f.stat == g_player_stat && g_player_stat ? g_state.player : known_driver(f.stat);
            log("[silly-season] results: %s, %d new round(s) taken%s%s%s%s", said, fed, winner.empty() ? "" : "; last winner ",
                winner.c_str(), unknown ? "; drivers the market can't tell: " : "", unknown_list.c_str());
        }
    }
    // the player's secret meeting with another team
    if (g_player_stat) {
        game_data::Meeting m;
        if (game_data::read_meeting(g_player_stat, m) && m.id != g_state.last_meeting) {
            g_state.last_meeting = m.id;
            std::string team = team_by_stat(m.team);
            if (!team.empty() && team != g_state.player_team) {
                ss::report_meeting(g_state, team);
                log("[silly-season] secret meeting %u with %s (reason %u): in the news", m.id, team.c_str(), m.reason);
            } else {
                log("[silly-season] secret meeting %u with team stat %u (reason %u): not a team the market knows", m.id, m.team, m.reason);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The overlay: a breaking-news lower third and the F7 market hub. Paint runs on the render thread, so it
// reads a snapshot the flow thread fills (update_overlay) under its own lock. Everything is drawn with the
// core's vector primitives: no images, so nothing of the game's art is shipped.

struct UiNews {
    int id = 0, year = 0, round = 0;
    std::string kind, title, body;
    bool unread = false;
    int updates = 0;                // older items of the same story stacked under this one
    std::vector<std::string> older; // their titles, newest first
    std::string driver_id, team_id; // the story's driver and team: a click opens their card
};

struct UiRumour {
    std::string driver_id;
    std::string driver, from, to, to_code, outlet;
    uint32_t from_col = 0, to_col = 0;
    int stage = 0; // 0 rumour, 1 talks
    int rel = 0;   // outlet reliability 1..5
    int odds = 0;  // 1.1.0 phase 5: how likely it ends in a signing, %
};

struct UiDeal {
    std::string driver, team, code, kind;
    uint32_t col = 0;
    int until = 0;
    std::string driver_id;
};

struct UiMove {
    int year = 0, round = 0;
    std::string driver, from, to, from_code, to_code, kind;
    uint32_t from_col = 0, to_col = 0;
    std::string driver_id;
};

struct UiSeat {
    std::string name;
    bool fresh = false, player = false;
    std::string id;
};

struct UiTeam {
    std::string id;
    std::string name, code, td;
    uint32_t col = 0;
    int rank = 0, points = 0;
    float td_rating = 0;
    UiSeat seat[2];
    std::string strategy; // the team's plan ("title", "rebuild"...)
};

struct UiDriver {
    std::string id;
    std::string name, team, code, salary, kind, last_team;
    uint32_t col = 0;
    int age = 0, contract = 0;
    float value = 0, potential = 0, trend = 0;
    bool clause = false, feud = false, pressure = false;
    std::string trait; // the trait that shows most: ambition, loyalty, money (or none)
    // 1.1.0 phase 6: a free agent's role: "reserve" or "academy", with the team's code and colour
    std::string role, role_code;
    uint32_t role_col = 0;
};

struct UiAward {
    int year = 0;
    std::string key, name, detail;
};

struct UiScout {
    std::string id;
    std::string name, team, code, status, trait, salary;
    uint32_t col = 0;
    int age = 0, interest = 0;
    float value = 0, potential = 0, fit = 0;
};

// a driver's card (F7): everything about him
struct UiDriverCard {
    std::string id, name, nat, team_id, team, code, kind, salary, academy;
    uint32_t col = 0;
    int age = 0, contract = 0, clause = 0, points = 0, wins = 0, podiums = 0, starts = 0, seasons = 0, number = 0;
    float value = 0, potential = 0, form = 0, ambition = -1, loyalty = -1, money = -1, backing = 0, trend = 0;
    bool player = false, wants_out = false, pressure = false, feud = false;
    std::vector<float> values, values_last;
    struct Step {
        int year = 0;
        std::string team, code;
        uint32_t col = 0;
    };
    std::vector<Step> path;          // his teams over the years, oldest first
    std::vector<std::string> links;  // the stories about him now
    std::vector<std::string> awards; // "Champion 2026"
    // 1.1.0 phase 5: his career in this market, and this season against his team-mate
    int c_seasons = 0, c_starts = 0, c_wins = 0, c_podiums = 0, c_points = 0, c_titles = 0, c_teams = 0;
    std::string h2h_mate;
    int h2h_ahead = 0, h2h_behind = 0;
    // 1.1.0 phase 6: reserve driver of a team, or in a team's academy
    std::string role, role_team;
    int role_until = 0;
    // the hub's radar: pace, racecraft, awareness, experience and focus (0: the game's wasn't read), now and at
    // their peak
    float rating[5] = {0, 0, 0, 0, 0}, peak[5] = {0, 0, 0, 0, 0};
    int game_rating = 0; // the game's own overall for him (its ratings screen); 0: not read
};

// a team's card (F7)
struct UiTeamCard {
    std::string id, name, code, strategy, td;
    uint32_t col = 0;
    float performance = 0, prestige = 0, budget = 0, td_rating = 0;
    int rank = 0, points = 0;
    std::string now_id[2], now[2], next_id[2], next[2];
    bool next_new[2] = {false, false};
    std::vector<std::pair<std::string, std::string>> juniors; // id, line
    std::vector<std::string> links, moves, awards;
    // 1.1.0 phase 5: the team principal and the money this season
    std::string tp;
    int tp_since = 0;
    float budget_delta = 0;
    // 1.1.0 phase 6: the reserve driver it calls on (its own or its senior team's)
    std::string reserve_id, reserve, reserve_shared; // reserve_shared: the senior team's name when shared
    int reserve_until = 0;
};

// one move of a chain (Transfer Centre)
struct UiLink {
    std::string driver_id, driver, from, from_code, to, to_code, kind, waits;
    uint32_t from_col = 0, to_col = 0;
    int stage = 0; // 0 rumour, 1 talks, 2 agreed, 3 official
    int odds = 0;  // 1.1.0 phase 5, %
};

// 1.1.0 phase 5: a line of the drivers' standings
struct UiStanding {
    std::string id, name, code;
    uint32_t col = 0;
    int points = 0, wins = 0, podiums = 0, starts = 0;
    bool player = false;
    std::vector<int> last; // the last five races, oldest first: 1.. the position, -1 not classified
};

// a team's interest in the player
struct UiSuitor {
    std::string id, name, code, status;
    uint32_t col = 0;
    int interest = 0;
    bool open = false;
};

// the player's page: his value, his contract, who wants him, what is said
struct UiFuture {
    bool on = false;
    std::string name, team, code;
    uint32_t col = 0;
    float value = 0, potential = 0, form = 0;
    int game_rating = 0;            // the game's own overall for him; 0: not read
    int contract = 0, loyalty = 0, comfort = 0;
    bool push = false;              // he asked his team for reinforcements
    std::vector<float> values, values_last;
    std::vector<UiSuitor> suitors;  // his own team first (status "own"), then the others by interest
    std::vector<UiNews> stories;    // the stories about him, newest first
    // My Team: his own team against the rest
    bool own_team = false;
    int own_rank = 0, own_points = 0, gap_ahead = 0, gap_behind = 0;
    std::string ahead, behind;
};

// a record of the market's history (History)
struct UiRecord {
    std::string key, name, detail; // key: titles, wins, podiums, starts, points
    int value = 0;
};

// a bombshell signing's full card
struct UiSigning {
    std::string driver, from, from_code, to, to_code, salary;
    uint32_t from_col = 0, to_col = 0;
    int until = 0, age = 0;
    float value = 0;
    std::string quote; // the new team's boss on it
};

// 1.1.0 overlays: a retiring driver's farewell card
struct UiFarewell {
    std::string driver, team, code;
    uint32_t col = 0;
    int seasons = 0, starts = 0, wins = 0, podiums = 0, titles = 0, age = 0;
};

// deadline day's opening strip
struct UiStrip {
    std::string title, body;
    int left = 0;
};

// the report after a race: what changed for the player and the market since the last visit
struct UiReport {
    int id = 0;             // new when a round was completed in this session
    int round = 0, rounds = 0, position = 0, rumours = 0, signings = 0, keen = 0;
    float value = 0, delta = 0;
    std::vector<std::string> headlines;
};

// a team became keen on the player
struct UiAlert {
    std::string team, code;
    uint32_t col = 0;
    int interest = 0;
};

struct OverlayData {
    bool career = false, enabled = true, es = false, effects = true;
    bool real = false; // history mode: the real season is told
    int version = 0;
    int year = 0, round = 0, rounds = 24;
    bool finalised = false;
    int unread = 0, seats_in_play = 0, open_talks = 0, done_deals = 0;
    std::vector<UiNews> news;
    // the press
    bool press_pending = false;
    int press_id = 0;
    std::string press_question;
    std::vector<std::string> press_answers;
    // the market
    std::vector<UiRumour> rumours;
    std::vector<UiDeal> deals;
    std::vector<UiMove> moves_year, history;
    std::vector<UiTeam> grid;  // next season, projected or final
    std::vector<UiTeam> teams; // this season, with their technical directors
    std::vector<UiDriver> drivers, free_agents, f2;
    // the player
    bool has_player = false;
    std::string player_team, player_code, teammate;
    uint32_t player_col = 0;
    int player_contract = 0, loyalty = 0;
    std::deque<ss::NewsItem> banners;
    // 1.1.0 phase 3
    int deadline = 0;               // deadline day: races left (1, 2), 0 when it isn't
    std::vector<UiAward> awards;    // newest first
    std::vector<UiScout> scout;     // the shortlist for the player's team's seat next season
    bool scout_own = false;         // My Team: the player's own team
    // 1.1.0 phase 4
    std::vector<UiDriverCard> cards;
    std::vector<UiTeamCard> team_cards;
    std::vector<std::vector<UiLink>> chains; // the dominoes, each chain in order
    std::vector<UiLink> waiting;             // signings waiting for their seat
    std::deque<UiSigning> signings;          // bombshell signings for the full card
    bool reveal_pending = false;             // next season's grid is final and the player hasn't seen it revealed
    bool hq_widget = true, grid_reveal = true;
    // 1.1.0 phase 5
    std::vector<UiStanding> standings;       // the drivers' championship, leader first
    UiFuture future;
    std::vector<UiRecord> records;
    // the hub's season bar: which blocks run this season
    bool midseason_on = true, deadline_on = true;
    float scale = 1.0f;                      // "Panel size": 0.9 small, 1 normal, 1.12 large
    // 1.1.0 overlays
    std::deque<UiFarewell> farewells;        // farewell cards waiting (paint takes them)
    std::deque<UiStrip> strips;              // deadline day's strip, waiting
    std::deque<UiAlert> alerts;              // teams newly keen on the player, waiting
    UiReport report;
    int awards_pending = 0;                  // the season whose awards ceremony waits (0: none)
    int cover_pending = 0;                   // the season whose cover waits (0: none)
    std::vector<UiMove> cover_moves;         // the winter's moves, for the cover
    bool press_popup = true, race_report = true;
    const UiDriverCard *card(const std::string &id) const
    {
        for (const UiDriverCard &c : cards)
            if (c.id == id)
                return &c;
        return nullptr;
    }
    const UiTeamCard *team_card(const std::string &id) const
    {
        for (const UiTeamCard &c : team_cards)
            if (c.id == id)
                return &c;
        return nullptr;
    }
};

std::mutex g_ov_lock;
OverlayData g_ov;
std::atomic<bool> g_in_session{false};
std::atomic<bool> g_board_open{false};
std::atomic<bool> g_seen_request{false};
std::atomic<bool> g_at_hq{false};       // an idle Team HQ (the widget, the grid reveal)
std::atomic<int> g_reveal_done{0};      // the season whose grid reveal the player just watched
std::atomic<int> g_awards_done{0};      // the season whose awards ceremony he just watched
std::atomic<int> g_cover_done{0};       // the season whose cover he just watched
std::atomic<int> g_press_answer{-1};     // 0..2, from the panel or the Team HQ row
std::atomic<int> g_press_answer_id{0};
std::atomic<bool> g_press_from_hub{false};
bool g_overlay_ready = false;

std::string team_name_of(const ss::State &st, const std::string &id)
{
    const ss::Team *t = st.team(id);
    return t ? t->name : id;
}

void update_overlay(const ss::Settings &s) // flow thread, g_lock held
{
    const ss::State &st = g_state;
    bool es = s.lang != "en";
    auto dname = [&](const std::string &id) {
        if (!st.player.empty() && id == st.player)
            return st.player_name;
        const ss::Driver *d = st.driver(id);
        return d ? d->name : id;
    };
    auto team_of = [&](const std::string &id) { return st.team(id); };
    OverlayData o;
    o.es = es;
    o.year = st.year;
    o.round = st.round;
    o.rounds = st.rounds;
    o.finalised = st.finalised;
    o.real = st.real;
    o.unread = ss::unread(st);

    // the news, newest first; the updates of one story (a driver and a team) stack under its latest item
    {
        std::unordered_map<std::string, size_t> story;
        int scanned = 0;
        for (auto it = st.news.rbegin(); it != st.news.rend() && scanned < 90 && o.news.size() < 60; ++it, ++scanned) {
            bool unread = it->id > st.seen_news;
            std::string key = it->driver.empty() ? std::string() : it->driver + "|" + it->to + "|" + std::to_string(it->year);
            if (!key.empty())
                if (auto f = story.find(key); f != story.end()) {
                    UiNews &top = o.news[f->second];
                    ++top.updates;
                    top.unread = top.unread || unread;
                    if (top.older.size() < 4)
                        top.older.push_back(it->title);
                    continue;
                }
            if (!key.empty())
                story[key] = o.news.size();
            o.news.push_back({it->id, it->year, it->round, it->kind, it->title, it->body, unread, 0, {}, it->driver,
                              !it->to.empty() ? it->to : it->from});
        }
    }

    if (const ss::State::Press *q = ss::press_pending(st)) {
        o.press_pending = true;
        o.press_id = q->id;
        o.press_question = q->question;
        o.press_answers = q->answers;
    }

    for (auto it = st.deals.rbegin(); it != st.deals.rend(); ++it) {
        const ss::Team *t = team_of(it->team);
        if (!t)
            continue;
        if (it->stage == ss::Stage::official && !it->fake) {
            ++o.done_deals;
            o.deals.push_back({dname(it->driver), t->name, t->code, it->kind, t->colour, it->until, it->driver});
            continue;
        }
        if (it->stage == ss::Stage::collapsed || it->kind == "renewal")
            continue;
        ++o.open_talks;
        if (o.rumours.size() >= 24)
            continue;
        UiRumour r;
        r.driver_id = it->driver;
        r.driver = dname(it->driver);
        if (const ss::Driver *d = st.driver(it->driver))
            if (const ss::Team *from = team_of(d->team)) {
                r.from = from->name;
                r.from_col = from->colour;
            }
        r.to = t->name;
        r.to_code = t->code;
        r.to_col = t->colour;
        r.stage = it->stage == ss::Stage::rumour ? 0 : 1;
        r.outlet = it->source >= 0 ? ss::text::outlet_name(it->source) : "";
        r.rel = it->source >= 0 ? ss::outlet_rating(st, it->source) : 0;
        r.odds = ss::deal_odds(st, s, *it);
        o.rumours.push_back(std::move(r));
    }

    auto ui_move = [&](const ss::Move &m) {
        UiMove u{m.year, m.round, dname(m.driver), "", "", "", "", m.kind};
        u.driver_id = m.driver;
        if (const ss::Team *t = team_of(m.from)) {
            u.from = t->name;
            u.from_code = t->code;
            u.from_col = t->colour;
        }
        if (const ss::Team *t = team_of(m.to)) {
            u.to = t->name;
            u.to_code = t->code;
            u.to_col = t->colour;
        }
        return u;
    };
    for (auto it = st.history.rbegin(); it != st.history.rend() && o.history.size() < 80; ++it) {
        o.history.push_back(ui_move(*it));
        if (it->year == st.year && it->kind != "renewal")
            o.moves_year.push_back(o.history.back());
    }

    auto grid = ss::projected_grid(st, s);
    for (const ss::Team &t : st.teams) {
        // a real season: a team that doesn't race this year or next (Cadillac before 2026) isn't shown
        if (g_real_career && t.seats[0].empty() && t.seats[1].empty() && grid[t.id][0].empty() && grid[t.id][1].empty())
            continue;
        if (!t.active)
            continue; // a team this career doesn't have (Konnersport, when the game doesn't show it)
        UiTeam now{t.id, t.name, t.code, t.td_name, t.colour, ss::team_rank(st, t.id), t.points, t.td_rating, {},
                   st.real || !s.strategies ? "" : t.strategy};
        UiTeam next = now;
        for (int k = 0; k < 2; ++k) {
            const std::string &cur = t.seats[static_cast<size_t>(k)];
            now.seat[k] = {cur.empty() ? std::string() : dname(cur), false, !cur.empty() && cur == st.player, cur};
            const std::string &who = grid[t.id][static_cast<size_t>(k)];
            const ss::Driver *d = st.driver(who);
            next.seat[k] = {who.empty() ? std::string() : dname(who), d && d->team != t.id,
                            !who.empty() && who == st.player, who};
        }
        o.teams.push_back(std::move(now));
        o.grid.push_back(std::move(next));
    }
    // My Team: the player's own team, which the market doesn't plan, as one more team: his seat and his
    // team-mate's, its points, its place in the constructors' order
    bool own_team = !st.player.empty() && !g_own_team_name.empty() && !team_of(st.player_team) && !st.real;
    int own_points = 0;
    if (own_team) {
        for (const std::string &who : {st.player, st.own_mate})
            if (const ss::Driver *d = st.driver(who))
                own_points += d->points;
        UiTeam own{"#own", g_own_team_name, "MYT", "", 0x5E5CE6, 0, own_points, 0, {}, ""};
        own.seat[0] = {dname(st.player), false, true, st.player};
        own.seat[1] = {st.own_mate.empty() ? std::string() : dname(st.own_mate), false, false, st.own_mate};
        o.teams.push_back(own);
        o.grid.push_back(own);
        // the constructors' order with it
        std::vector<std::pair<int, size_t>> order;
        for (size_t i = 0; i < o.teams.size(); ++i)
            order.push_back({o.teams[i].points, i});
        std::stable_sort(order.begin(), order.end(), [&](const auto &a, const auto &b) {
            return a.first != b.first ? a.first > b.first : o.teams[a.second].rank < o.teams[b.second].rank;
        });
        for (size_t r = 0; r < order.size(); ++r) {
            o.teams[order[r].second].rank = static_cast<int>(r + 1);
            for (UiTeam &g : o.grid)
                if (g.id == o.teams[order[r].second].id)
                    g.rank = static_cast<int>(r + 1);
        }
    }

    // the senior team of a family (its academy's name): Red Bull for "redbull", not Racing Bulls
    auto family_team = [&](const std::string &family) -> const ss::Team * {
        if (family.empty())
            return nullptr;
        const ss::Team *found = nullptr;
        for (const ss::Team &t : st.teams)
            if (t.active && t.family == family && (!found || (found->junior && !t.junior)))
                found = &t;
        return found;
    };
    auto ui_driver = [&](const ss::Driver &d) {
        UiDriver u;
        u.id = d.id;
        u.name = dname(d.id);
        u.age = d.age(st.year);
        u.contract = d.contract;
        u.value = ss::driver_value(d);
        u.potential = d.potential();
        u.trend = d.trend;
        u.salary = ss::text::salary_short(ss::text::lang_of(s.lang), d.salary);
        u.kind = d.kind;
        u.clause = d.clause_rank > 0;
        u.feud = !d.feuds.empty();
        u.pressure = d.pressured;
        float top = std::max({d.ambition, d.loyalty, d.money});
        u.trait = top < 0.68f ? "" : top == d.ambition ? "ambition" : top == d.loyalty ? "loyalty" : "money";
        if (const ss::Team *t = team_of(d.team)) {
            u.team = t->name;
            u.code = t->code;
            u.col = t->colour;
        } else if (s.academies) {
            // 1.1.0 phase 6: a reserve driver, or a driver in a team's academy
            if (const ss::Team *rt = ss::reserve_team_of(st, d.id)) {
                u.role = "reserve";
                u.role_code = rt->code;
                u.role_col = rt->colour;
            } else if (const ss::Team *at = family_team(d.academy)) {
                u.role = "academy";
                u.role_code = at->code;
                u.role_col = at->colour;
            }
        }
        return u;
    };
    for (const ss::Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const ss::Driver *d = st.driver(who))
                if (d->id != st.player) {
                    o.drivers.push_back(ui_driver(*d));
                    if (d->contract <= st.year && !d->retiring)
                        ++o.seats_in_play;
                }
    if (own_team)
        if (const ss::Driver *mate = st.driver(st.own_mate)) {
            UiDriver u = ui_driver(*mate);
            u.team = g_own_team_name;
            u.code = "MYT";
            u.col = 0x5E5CE6;
            o.drivers.push_back(std::move(u));
        }
    std::sort(o.drivers.begin(), o.drivers.end(), [](const UiDriver &a, const UiDriver &b) { return a.value > b.value; });

    for (const auto &[id, d] : st.drivers)
        if (d.team.empty() && !d.retired && !d.retiring && d.kind != "player" && !st.shielded(id) && (d.kind != "legend" || s.legends)) {
            UiDriver u = ui_driver(d);
            for (auto it = st.history.rbegin(); it != st.history.rend(); ++it)
                if (it->driver == id && !it->from.empty()) {
                    if (const ss::Team *t = team_of(it->from))
                        u.last_team = t->name;
                    break;
                }
            o.free_agents.push_back(std::move(u));
        }
    std::sort(o.free_agents.begin(), o.free_agents.end(),
              [](const UiDriver &a, const UiDriver &b) { return a.potential > b.potential; });
    if (o.free_agents.size() > 30)
        o.free_agents.resize(30);

    for (const std::string &id : st.f2_table)
        if (const ss::Driver *d = st.driver(id))
            if (o.f2.size() < 22)
                o.f2.push_back(ui_driver(*d));

    if (!st.player.empty() && !g_own_team_name.empty() && !team_of(st.player_team)) {
        o.has_player = true;
        o.player_team = g_own_team_name;
        o.player_code = "YOU";
        o.player_col = 0x5E5CE6;
        for (const std::string &id : st.protected_ids)
            o.teammate = dname(id);
        if (const ss::Driver *me = st.driver(st.player))
            o.player_contract = me->contract;
        o.loyalty = st.player_loyalty;
    }
    if (!st.player.empty())
        if (const ss::Team *t = team_of(st.player_team)) {
            o.has_player = true;
            o.player_team = t->name;
            o.player_code = t->code;
            o.player_col = t->colour;
            for (const std::string &who : t->seats)
                if (!who.empty() && who != st.player)
                    o.teammate = dname(who);
            if (const ss::Driver *me = st.driver(st.player))
                o.player_contract = me->contract;
            o.loyalty = st.player_loyalty;
        }

    // 1.1.0 phase 3: deadline day, the awards, the scout
    o.deadline = s.deadline ? ss::deadline_races_left(st) : 0;
    o.midseason_on = s.midseason && !st.real;
    o.deadline_on = s.deadline && !st.real;
    {
        std::string_view size = panel_size;
        o.scale = size == "small" ? 0.9f : size == "large" ? 1.12f : 1.0f;
    }
    for (auto it = st.awards.rbegin(); it != st.awards.rend() && o.awards.size() < 30; ++it)
        o.awards.push_back({it->year, it->key, it->name, it->line});
    if (!st.player.empty() && !st.real) {
        o.scout_own = !team_of(st.player_team);
        for (const ss::ScoutPick &pk : ss::scout(st, s, "", 12)) {
            const ss::Driver *d = st.driver(pk.driver);
            if (!d)
                continue;
            UiScout u;
            u.id = d->id;
            u.name = dname(d->id);
            u.status = pk.status;
            u.age = d->age(st.year + 1);
            u.interest = pk.interest;
            u.value = ss::driver_value(*d);
            u.potential = d->potential();
            u.fit = pk.fit;
            u.salary = ss::text::salary_short(ss::text::lang_of(s.lang), d->salary > 0 ? d->salary : 0.5f);
            float top = std::max({d->ambition, d->loyalty, d->money});
            u.trait = top < 0.68f ? "" : top == d->ambition ? "ambition" : top == d->loyalty ? "loyalty" : "money";
            if (const ss::Team *t = team_of(d->team)) {
                u.team = t->name;
                u.code = t->code;
                u.col = t->colour;
            }
            o.scout.push_back(std::move(u));
        }
    }

    // 1.1.0 phase 4: the cards, the chains, the reveal
    {
        auto T = [&](const char *es_text, const char *en_text) { return std::string(es ? es_text : en_text); };
        const char *award_en[] = {"Champion", "Constructors", "Rookie of the year", "Beat the car", "Most improved"};
        const char *award_es[] = {"Campeón", "Constructores", "Novato del año", "Más que su coche", "El que más crece"};
        auto award_name = [&](const std::string &key) {
            int i = key == "champion" ? 0 : key == "constructors" ? 1 : key == "rookie" ? 2 : key == "overachiever" ? 3 : 4;
            return std::string(es ? award_es[i] : award_en[i]);
        };
        const char *stage_en[] = {"rumour", "talks", "agreed", "official"}, *stage_es[] = {"rumor", "negociación", "acuerdo", "oficial"};
        auto stage_of = [](ss::Stage x) { return x == ss::Stage::rumour ? 0 : x == ss::Stage::talks ? 1 : x == ss::Stage::agreed ? 2 : 3; };
        for (const auto &[id, d] : st.drivers) {
            if (d.retired || (st.shielded(id) && id != st.player && id != st.own_mate) || (d.kind == "legend" && !s.legends))
                continue;
            UiDriverCard c;
            c.id = id;
            c.name = dname(id);
            c.nat = d.nat;
            c.kind = d.kind;
            c.age = d.age(st.year);
            c.contract = d.contract;
            c.clause = d.clause_rank;
            c.points = d.points;
            c.wins = d.wins;
            c.podiums = d.podiums;
            c.starts = d.starts;
            c.seasons = d.seasons;
            c.number = d.number > 0 ? d.number : 0;
            c.value = ss::driver_value(d);
            c.potential = d.potential();
            c.form = d.form;
            c.trend = d.trend;
            if (s.personality) {
                c.ambition = d.ambition;
                c.loyalty = d.loyalty;
                c.money = d.money;
            }
            c.backing = d.backing;
            c.player = id == st.player;
            c.wants_out = d.wants_out;
            c.pressure = d.pressured;
            c.feud = !d.feuds.empty();
            c.academy = d.academy;
            c.salary = ss::text::salary_short(ss::text::lang_of(s.lang), d.salary);
            c.values = d.values;
            c.values_last = d.values_last;
            if (const ss::Team *t = team_of(d.team)) {
                c.team_id = t->id;
                c.team = t->name;
                c.code = t->code;
                c.col = t->colour;
            } else if (own_team && (id == st.player || id == st.own_mate)) {
                c.team_id = "#own";
                c.team = g_own_team_name;
                c.code = "MYT";
                c.col = 0x5E5CE6;
            }
            // his teams over the years: each move into a team, then where he is now
            for (const ss::Move &m : st.history) {
                if (m.driver != id || m.to.empty() || m.kind == "renewal")
                    continue;
                const ss::Team *t = team_of(m.to);
                // a move made during a season counts from that season; the winter's moves from the next one
                bool now = m.kind == "replaced" || m.kind == "promoted" || m.kind == "demoted";
                int year = now ? m.year : m.year + 1;
                if (t && (c.path.empty() || c.path.back().team != t->name))
                    c.path.push_back({year, t->name, t->code, t->colour});
            }
            if (c.path.size() > 6)
                c.path.erase(c.path.begin(), c.path.end() - 6);
            // the stories about him now
            for (const ss::Deal &x : st.deals) {
                if (x.driver != id || x.stage == ss::Stage::collapsed)
                    continue;
                int sg = stage_of(x.stage);
                std::string line = (x.kind == "renewal" ? T("Renueva con ", "Renews with ") : T("Con ", "Linked with ")) + team_name_of(st, x.team) +
                                   " (" + (es ? stage_es[sg] : stage_en[sg]);
                if (x.source >= 0 && x.stage != ss::Stage::official)
                    line += ", " + std::string(ss::text::outlet_name(x.source));
                if (!x.waits.empty())
                    line += T(", espera a ", ", waiting for ") + dname(x.waits);
                line += ")";
                c.links.push_back(line);
            }
            for (const ss::State::Award &a : st.awards)
                if (a.who == id)
                    c.awards.push_back(award_name(a.key) + " " + std::to_string(a.year));
            // 1.1.0 phase 5: his career in this market, and the duel with his team-mate
            ss::CareerTotals ct = ss::career_totals(st, d);
            c.c_seasons = ct.seasons;
            c.c_starts = ct.starts;
            c.c_wins = ct.wins;
            c.c_podiums = ct.podiums;
            c.c_points = ct.points;
            c.c_titles = ct.titles;
            c.c_teams = ct.teams;
            ss::HeadToHead h = ss::head_to_head(st, d);
            if (!h.mate.empty()) {
                c.h2h_mate = dname(h.mate);
                c.h2h_ahead = h.ahead;
                c.h2h_behind = h.behind;
            }
            const float now_r[4] = {d.pace, d.racecraft, d.awareness, d.experience};
            const float peak_r[4] = {d.pace_peak, d.racecraft_peak, d.awareness_peak, d.experience_peak};
            for (int i = 0; i < 4; ++i) {
                c.rating[i] = now_r[i];
                c.peak[i] = std::max(now_r[i], peak_r[i]);
            }
            c.rating[4] = c.peak[4] = d.focus; // the market doesn't model where focus goes
            c.game_rating = d.game_overall > 0 ? static_cast<int>(std::lround(d.game_overall)) : 0;
            // 1.1.0 phase 6: his role off the grid
            if (s.academies && d.team.empty()) {
                if (const ss::Team *rt = ss::reserve_team_of(st, id)) {
                    c.role = "reserve";
                    c.role_team = rt->name;
                    c.role_until = rt->reserve_until;
                } else if (const ss::Team *at = family_team(d.academy)) {
                    c.role = "academy";
                    c.role_team = at->name;
                }
            }
            o.cards.push_back(std::move(c));
        }
        // the teams' cards
        for (const ss::Team &t : st.teams) {
            if (!t.active)
                continue;
            UiTeamCard c;
            c.id = t.id;
            c.name = t.name;
            c.code = t.code;
            c.col = t.colour;
            c.strategy = st.real || !s.strategies ? "" : t.strategy;
            c.td = t.td_name;
            c.td_rating = t.td_rating;
            c.performance = t.performance;
            c.prestige = t.prestige;
            c.budget = t.budget;
            c.rank = ss::team_rank(st, t.id);
            c.points = t.points;
            for (int k = 0; k < 2; ++k) {
                c.now_id[k] = t.seats[static_cast<size_t>(k)];
                c.now[k] = c.now_id[k].empty() ? std::string() : dname(c.now_id[k]);
                c.next_id[k] = grid[t.id][static_cast<size_t>(k)];
                c.next[k] = c.next_id[k].empty() ? std::string() : dname(c.next_id[k]);
                const ss::Driver *nd = st.driver(c.next_id[k]);
                c.next_new[k] = nd && nd->team != t.id;
            }
            // 1.1.0 phase 6: the reserve it calls on, then its academy (Haas: Ferrari's too)
            c.reserve_id = s.academies ? ss::reserve_for(st, t) : std::string();
            if (!c.reserve_id.empty()) {
                c.reserve = dname(c.reserve_id);
                const ss::Team *lead = ss::lead_team(st, t);
                const ss::Team *owner = t.reserve == c.reserve_id ? &t : lead;
                c.reserve_until = owner ? owner->reserve_until : 0;
                if (owner && owner != &t)
                    c.reserve_shared = owner->name;
            }
            for (const auto &[id, d] : st.drivers)
                if (d.team.empty() && !d.retired && !st.shielded(id) && id != c.reserve_id &&
                    (d.reserve_of == t.id || (s.academies ? ss::in_academy(t, d) : !d.academy.empty() && d.academy == t.family)) &&
                    c.juniors.size() < 6) {
                    char line[96];
                    // with phase 6 the team's reserve is listed above: the game's other reserves are its test drivers
                    std::string label = d.reserve_of != t.id ? std::string("junior")
                                        : s.academies     ? T("probador", "test driver")
                                                          : T("reserva", "reserve");
                    std::snprintf(line, sizeof line, "%s (%d, %.0f / %.0f)", label.c_str(),
                                  d.age(st.year), ss::driver_value(d), d.potential());
                    c.juniors.push_back({id, line});
                }
            for (const ss::Deal &x : st.deals) {
                if (x.stage == ss::Stage::collapsed || x.kind == "renewal")
                    continue;
                const ss::Driver *d = st.driver(x.driver);
                int sg = stage_of(x.stage);
                if (x.team == t.id)
                    c.links.push_back(T("Entra: ", "In: ") + dname(x.driver) + " (" + (es ? stage_es[sg] : stage_en[sg]) + ")");
                else if (d && d->team == t.id && !x.fake)
                    c.links.push_back(T("Sale: ", "Out: ") + dname(x.driver) + T(" a ", " to ") + team_name_of(st, x.team) + " (" +
                                      (es ? stage_es[sg] : stage_en[sg]) + ")");
                else if (d && d->team == t.id)
                    c.links.push_back(T("Rumor: ", "Rumour: ") + dname(x.driver) + T(" a ", " to ") + team_name_of(st, x.team));
            }
            for (auto it = st.history.rbegin(); it != st.history.rend() && c.moves.size() < 8; ++it)
                if ((it->to == t.id || it->from == t.id) && it->kind != "renewal")
                    c.moves.push_back(std::to_string(it->year) + "  " + dname(it->driver) + "  " +
                                      (it->to == t.id ? T("entra", "in") : T("sale", "out")) + " (" + ss::text::kind(ss::text::lang_of(s.lang), it->kind) + ")");
            for (const ss::State::Award &a : st.awards)
                if (a.who == t.id)
                    c.awards.push_back(award_name(a.key) + " " + std::to_string(a.year));
            c.tp = t.tp_name;
            c.tp_since = t.tp_since;
            c.budget_delta = t.budget_start > 0 ? t.budget - t.budget_start : 0.0f;
            o.team_cards.push_back(std::move(c));
        }
        // My Team's card: its two drivers, its level as the player's results tell it, its honours
        if (own_team) {
            UiTeamCard c;
            c.id = "#own";
            c.name = g_own_team_name;
            c.code = "MYT";
            c.col = 0x5E5CE6;
            c.td = es ? "Tú decides" : "Your call";
            c.prestige = 62;
            c.budget = 85;
            c.performance = 70;
            if (const ss::Driver *me = st.driver(st.player); me && me->starts > 0) {
                int ahead = 0;
                for (const auto &[x, d] : st.drivers)
                    if (d.starts > 0 && d.points > me->points)
                        ++ahead;
                c.performance = std::clamp(92.0f - static_cast<float>(ahead) * 1.6f, 55.0f, 92.0f);
            }
            c.td_rating = c.performance;
            c.points = own_points;
            for (const UiTeam &t : o.teams)
                if (t.id == "#own")
                    c.rank = t.rank;
            c.now_id[0] = c.next_id[0] = st.player;
            c.now[0] = c.next[0] = dname(st.player);
            c.now_id[1] = c.next_id[1] = st.own_mate;
            c.now[1] = c.next[1] = st.own_mate.empty() ? std::string() : dname(st.own_mate);
            for (const ss::Deal &x : st.deals)
                if (x.driver == st.own_mate && x.stage != ss::Stage::collapsed && !x.fake)
                    c.links.push_back(T("Sale: ", "Out: ") + dname(x.driver) + T(" a ", " to ") + team_name_of(st, x.team));
            for (const ss::State::Award &a : st.awards)
                if (a.who == "#own")
                    c.awards.push_back(award_name(a.key) + " " + std::to_string(a.year));
            o.team_cards.push_back(std::move(c));
        }
        // the dominoes
        auto link_of = [&](const ss::Deal &x) {
            UiLink l;
            l.driver_id = x.driver;
            l.driver = dname(x.driver);
            l.kind = x.kind;
            l.stage = stage_of(x.stage);
            if (const ss::Driver *d = st.driver(x.driver))
                if (const ss::Team *t = team_of(d->team)) {
                    l.from = t->name;
                    l.from_code = t->code;
                    l.from_col = t->colour;
                }
            if (const ss::Team *t = team_of(x.team)) {
                l.to = t->name;
                l.to_code = t->code;
                l.to_col = t->colour;
            }
            if (!x.waits.empty())
                l.waits = dname(x.waits);
            l.odds = ss::deal_odds(st, s, x);
            return l;
        };
        for (const std::vector<int> &chain : ss::transfer_chains(st)) {
            std::vector<UiLink> row;
            for (int id : chain)
                if (const ss::Deal *x = ss::deal_with_id(st, id))
                    row.push_back(link_of(*x));
            o.chains.push_back(std::move(row));
        }
        for (const ss::Deal &x : st.deals)
            if (!x.waits.empty() && !x.fake && x.stage != ss::Stage::collapsed && x.stage != ss::Stage::official)
                o.waiting.push_back(link_of(x));
        o.grid_reveal = grid_reveal;
        o.hq_widget = hq_widget;
        // an observer career (My Team) has no grid of the market's to reveal: the game's comes with the new season
        o.reveal_pending = grid_reveal && st.finalised && !st.real && !st.observer && st.reveal_year != st.year;
    }

    // 1.1.0 phase 5: the standings, the player's future, the records
    {
        auto team_look = [&](const ss::Driver &d, std::string &code, uint32_t &col) {
            if (const ss::Team *t = team_of(d.team)) {
                code = t->code;
                col = t->colour;
            } else if (own_team && (d.id == st.player || d.id == st.own_mate)) {
                code = "MYT";
                col = 0x5E5CE6;
            }
        };
        for (const auto &[id, d] : st.drivers) {
            if (d.starts <= 0)
                continue;
            UiStanding u;
            u.id = id;
            u.name = dname(id);
            team_look(d, u.code, u.col);
            u.points = d.points;
            u.wins = d.wins;
            u.podiums = d.podiums;
            u.starts = d.starts;
            u.player = id == st.player;
            for (size_t i = d.race_pos.size(); i-- > 0 && u.last.size() < 5;)
                if (d.race_pos[i] != 0)
                    u.last.insert(u.last.begin(), d.race_pos[i]);
            o.standings.push_back(std::move(u));
        }
        std::sort(o.standings.begin(), o.standings.end(), [](const UiStanding &a, const UiStanding &b) {
            return a.points != b.points ? a.points > b.points : a.wins != b.wins ? a.wins > b.wins : a.podiums > b.podiums;
        });

        // the player's future
        if (const ss::Driver *me = st.driver(st.player); me && !st.real) {
            UiFuture &f = o.future;
            f.on = true;
            f.name = dname(me->id);
            f.value = ss::driver_value(*me);
            f.potential = me->potential();
            f.game_rating = me->game_overall > 0 ? static_cast<int>(std::lround(me->game_overall)) : 0;
            f.form = me->form;
            f.contract = me->contract;
            f.loyalty = st.player_loyalty;
            f.comfort = st.team_comfort;
            f.push = st.push_signing;
            f.values = me->values;
            f.values_last = me->values_last;
            if (const ss::Team *t = team_of(st.player_team)) {
                f.team = t->name;
                f.code = t->code;
                f.col = t->colour;
            } else if (own_team) {
                f.team = g_own_team_name;
                f.code = "MYT";
                f.col = 0x5E5CE6;
                f.own_team = true;
                f.own_points = own_points;
                // his team in the constructors' order, and the teams either side of it
                std::vector<const UiTeam *> order;
                for (const UiTeam &t : o.teams)
                    order.push_back(&t);
                std::sort(order.begin(), order.end(), [](const UiTeam *a, const UiTeam *b) { return a->rank < b->rank; });
                for (size_t i = 0; i < order.size(); ++i)
                    if (order[i]->id == "#own") {
                        f.own_rank = order[i]->rank;
                        if (i > 0) {
                            f.ahead = order[i - 1]->name;
                            f.gap_ahead = order[i - 1]->points - own_points;
                        }
                        if (i + 1 < order.size()) {
                            f.behind = order[i + 1]->name;
                            f.gap_behind = own_points - order[i + 1]->points;
                        }
                    }
            }
            for (const ss::Suitor &x : ss::player_suitors(st, s)) {
                const ss::Team *t = team_of(x.team);
                if (!t)
                    continue;
                f.suitors.push_back({t->id, t->name, t->code, x.status, t->colour, x.interest, x.seat_open});
            }
            for (const UiNews &n : o.news)
                if ((n.kind == "player" || n.kind == "press_reply" || n.kind == "press") && f.stories.size() < 6)
                    f.stories.push_back(n);
        }

        // the records: every driver's career in this market, this season included
        struct Best {
            const char *key;
            int value = 0;
            std::string id;
        };
        Best best[] = {{"titles"}, {"wins"}, {"podiums"}, {"starts"}, {"points"}};
        for (const auto &[id, d] : st.drivers) {
            ss::CareerTotals ct = ss::career_totals(st, d);
            int v[] = {ct.titles, ct.wins, ct.podiums, ct.starts, ct.points};
            for (int i = 0; i < 5; ++i)
                if (v[i] > best[i].value) {
                    best[i].value = v[i];
                    best[i].id = id;
                }
        }
        for (const Best &b : best)
            if (b.value > 0) {
                const ss::Driver *d = st.driver(b.id);
                std::string detail;
                if (d) {
                    ss::CareerTotals ct = ss::career_totals(st, *d);
                    detail = std::to_string(ct.seasons) + (es ? " temporadas" : " seasons") + "  ·  " + std::to_string(ct.teams) +
                             (ct.teams == 1 ? (es ? " equipo" : " team") : (es ? " equipos" : " teams"));
                }
                o.records.push_back({b.key, dname(b.id), detail, b.value});
            }
    }

    // 1.1.0 overlays: the report after the race, the teams newly keen on the player, the awards ceremony and the
    // season's cover waiting
    o.press_popup = press_popup;
    o.race_report = race_report;
    if (g_report_seq > 0 && st.round > 0) {
        UiReport &r = o.report;
        r.id = g_report_seq;
        r.round = st.round;
        r.rounds = st.rounds;
        if (const ss::Driver *me = st.driver(st.player)) {
            size_t i = static_cast<size_t>(st.round - 1);
            r.value = ss::driver_value(*me);
            if (i < me->values.size() && i >= 1 && me->values[i] > 0 && me->values[i - 1] > 0)
                r.delta = me->values[i] - me->values[i - 1];
        }
        for (const ss::NewsItem &n : st.news)
            if (n.year == st.year && n.round == st.round) {
                r.rumours += n.kind == "rumour" || n.kind == "talks";
                r.signings += n.kind == "official";
            }
        for (const UiNews &n : o.news)
            if (n.year == st.year && n.round == st.round && r.headlines.size() < 3)
                r.headlines.push_back(n.title);
        for (const UiSuitor &x : o.future.suitors)
            r.keen += x.status == "keen";
        for (size_t i = 0; i < o.standings.size(); ++i)
            if (o.standings[i].player)
                r.position = static_cast<int>(i + 1);
    }
    std::set<std::string> keen_now;
    for (const UiSuitor &x : o.future.suitors)
        if (x.status == "keen")
            keen_now.insert(x.id);
    std::vector<UiAlert> new_alerts;
    if (g_keen_known)
        for (const UiSuitor &x : o.future.suitors)
            if (x.status == "keen" && !g_keen_prev.count(x.id))
                new_alerts.push_back({x.name, x.code, x.col, x.interest});
    g_keen_prev = std::move(keen_now);
    g_keen_known = true;
    bool has_awards = std::any_of(st.awards.begin(), st.awards.end(), [&](const ss::State::Award &a) { return a.year == st.year; });
    o.awards_pending = awards_show && st.finalised && has_awards && st.awards_shown != st.year ? st.year : 0;
    o.cover_pending = season_cover && st.round <= 1 && st.cover_shown != st.year && st.season_index >= 0 ? st.year : 0;
    if (o.cover_pending)
        for (const ss::Move &m : st.history)
            if (m.year == st.year - 1 && (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback") && o.cover_moves.size() < 10)
                o.cover_moves.push_back(ui_move(m));

    auto breaking = ss::take_breaking(g_state, 4);
    std::lock_guard lock(g_ov_lock);
    o.farewells = std::move(g_ov.farewells);
    o.strips = std::move(g_ov.strips);
    o.alerts = std::move(g_ov.alerts);
    for (UiAlert &a : new_alerts)
        o.alerts.push_back(std::move(a));
    while (o.alerts.size() > 3)
        o.alerts.pop_front();
    o.career = g_loaded;
    o.enabled = headlines;
    o.effects = effects;
    o.version = g_ov.version + 1;
    o.banners = std::move(g_ov.banners);
    o.signings = std::move(g_ov.signings);
    if (o.enabled)
        for (ss::NewsItem &n : breaking) {
            const ss::Driver *d = st.driver(n.driver);
            // a retiring driver's farewell gets its own card, deadline day its own strip
            if (n.kind == "farewell" && d) {
                ss::CareerTotals ct = ss::career_totals(st, *d);
                UiFarewell f;
                f.driver = dname(n.driver);
                f.seasons = ct.seasons;
                f.starts = ct.starts;
                f.wins = ct.wins;
                f.podiums = ct.podiums;
                f.titles = ct.titles;
                f.age = d->age(st.year);
                if (const ss::Team *t = team_of(d->team)) {
                    f.team = t->name;
                    f.code = t->code;
                    f.col = t->colour;
                }
                o.farewells.push_back(std::move(f));
                continue;
            }
            if (n.kind == "deadline") {
                o.strips.push_back({n.title, n.body, ss::deadline_races_left(st)});
                continue;
            }
            if (n.big && signing_cards && d) {
                UiSigning sg;
                sg.driver = dname(n.driver);
                sg.until = n.until;
                sg.age = d->age(st.year + 1);
                sg.value = ss::driver_value(*d);
                sg.salary = ss::text::salary_short(ss::text::lang_of(s.lang), d->salary);
                if (const ss::Team *t = team_of(n.from)) {
                    sg.from = t->name;
                    sg.from_code = t->code;
                    sg.from_col = t->colour;
                }
                if (const ss::Team *t = team_of(n.to)) {
                    sg.to = t->name;
                    sg.to_code = t->code;
                    sg.to_col = t->colour;
                }
                sg.quote = ss::text::boss_quote(ss::text::lang_of(s.lang), n.id, sg.driver, sg.to, "transfer");
                o.signings.push_back(std::move(sg));
                continue;
            }
            o.banners.push_back(std::move(n));
        }
    while (o.signings.size() > 3)
        o.signings.pop_front();
    while (o.farewells.size() > 4)
        o.farewells.pop_front();
    while (o.strips.size() > 1)
        o.strips.pop_front();
    while (o.banners.size() > 6)
        o.banners.pop_front();
    g_ov = std::move(o);
}

// The drawing kit, the F7 hub and the banner
#include "hub.inl"

// a career this addon handles was just loaded: put its mail texts and tab title in the game
void career_ready()
{
    std::lock_guard lock(g_lock);
    if (!g_loaded)
        return;
    ss::Settings s = settings();
    ss::localize(g_state, s.lang != "en");
    g_state.player_name = s.lang == "en" ? "You" : "Tú";
    if (g_state.legacy_news > 0) {
        // an older save's news may not be in English: they were dropped, the season preview is written again
        log("[silly-season] career %s: %d older news item(s) dropped, season preview written again",
            g_state.career.c_str(), g_state.legacy_news);
        g_state.legacy_news = 0;
        g_state.preview_year = 0;
        if (g_state.season_index >= 0)
            ss::season_preview(g_state, s);
    }
    ss::refresh_mails(g_state, s);
    apply_texts(s);
    update_overlay(s); // the talents' ratings wait for the first idle Team HQ
    // the career index says this career needs Silly Season: the launcher warns before Play when it's off,
    // since a session without it lets the game's grid drift away from the market
    if (career::saves::available()) {
        career::Result r = career::saves::need(kAddon, ">=1.0.1", "its driver market (contracts, deals and next season's grid)");
        log("[silly-season] career %s: marked as needing Silly Season: %s", g_state.career.c_str(), career::name(r));
    }
}

void show_news()
{
    ss::Settings s = settings();
    ss::localize(g_state, s.lang != "en");
    auto items = ss::take_news(g_state, s, 3);
    if (items.empty())
        return;
    std::string body;
    for (const ss::NewsItem &n : items) {
        if (!body.empty())
            body += "\n\n";
        body += n.title + "\n" + n.body;
    }
    std::string title = items.size() == 1 ? items[0].title : ss::text::t_paddock(ss::text::lang_of(s.lang), items.size());
    if (items.size() == 1) {
        body = items[0].body;
    }
    popup::Result r = popup::show(title, body);
    log("[silly-season] popup '%s' (%zu item(s)): %s", title.c_str(), items.size(), popup::name(r));
}

// the game's round as on_hub last read it (season_index * 100 + round), and the poll that catches a round
// the game advances a moment after Team HQ opens (its weekend bookkeeping can lag the hub event)
int g_round_seen = -1;
double g_round_poll = 0;

// The weekend the sessions since the last Team HQ visit belonged to. The core's current_round() alone
// can't tell "at weekend N, not raced yet" from "weekend N raced" and it can skip a weekend, so the
// sessions themselves say which race weekend was run: its round, or -1 when no source knew it.
// g_weekend_raced stays 0 while no career session ran since the last hub.
int g_weekend_raced = 0;
int g_weekend_season = -1;
// The weekend only counts once its race is run: practice or qualifying alone doesn't. When no session
// says its type (an older core), the weekend counts as before, at its first session.
bool g_race_ended = false;   // a race session ended since the last hub
bool g_session_typed = false; // a session since the last hub said its type
int g_session_type = -1;      // the running session's type, -1 unknown

const char *session_type_name(int t)
{
    switch (t) {
    case static_cast<int>(events::SessionType::practice): return "practice";
    case static_cast<int>(events::SessionType::qualifying): return "qualifying";
    case static_cast<int>(events::SessionType::race): return "race";
    case static_cast<int>(events::SessionType::sprint): return "sprint";
    case static_cast<int>(events::SessionType::time_trial): return "time trial";
    default: return "unknown";
    }
}

// called at a session start, g_lock held
void note_weekend(const events::Context &ctx, const char *where)
{
    int from_ops = 0, from_weekend = 0, from_career = 0, season = -1;
    if (auto pos = career::current_round()) {
        from_ops = pos->round;
        season = pos->season_index;
    }
    if (const auto &w = ctx.weekend())
        from_weekend = w->round;
    if (const auto &c = ctx.career())
        from_career = c->round;
    g_session_type = -1;
    // practice is also what the view says when the core doesn't fill the type (every session of a weekend
    // came as practice, the race included): only another type counts as known
    if (const auto &sv = ctx.session(); sv && sv->type != events::SessionType::practice) {
        g_session_type = static_cast<int>(sv->type);
        g_session_typed = true;
    }
    int best = std::max({from_ops, from_weekend, from_career});
    log("[silly-season] %s: %s session, weekend round %d (career ops %d, weekend view %d, career view %d)", where,
        session_type_name(g_session_type), best, from_ops, from_weekend, from_career);
    g_weekend_raced = std::max(g_weekend_raced, best > 0 ? best : -1);
    if (season >= 0)
        g_weekend_season = season;
}

// called at a session end, g_lock held
void note_session_end(const events::Context &ctx)
{
    int type = g_session_type;
    if (const auto &sv = ctx.session(); sv && sv->type != events::SessionType::practice) {
        type = static_cast<int>(sv->type);
        g_session_typed = true;
    }
    if (type == static_cast<int>(events::SessionType::race)) {
        g_race_ended = true;
        log("[silly-season] session end: the race of round %d is done", g_weekend_raced);
    }
    g_session_type = -1;
}

// g_lock held. A career met in a later season than the baseline's (2030 in a 2026 career): the market's
// year follows the game's season index, the contracts move on with it and the veterans no longer on the
// grid have retired. Also mends a market saved by 1.0.0, which stayed in the baseline's year.
bool realign_year(int season_index)
{
    if (g_real_career)
        return false;
    int base = static_cast<int>(g_baseline["careers"][g_state.group]["year"].as_int(0));
    if (base <= 0 || season_index <= 0)
        return false;
    int expected = base + season_index, delta = expected - g_state.year;
    if (delta <= 0)
        return false;
    int retired = 0;
    g_state.year = expected;
    for (auto &[id, d] : g_state.drivers) {
        if (d.contract > 0)
            d.contract += delta;
        if (!d.team.empty() && d.contract < expected)
            d.contract = expected;
        if (d.team.empty() && !d.retired && d.kind == "f1" && id != g_state.player && !g_state.shielded(id) &&
            d.age(expected) >= 36) {
            d.retired = true; // gone from the grid by now
            ++retired;
        }
    }
    for (ss::Deal &deal : g_state.deals) {
        deal.year += delta;
        deal.until += delta;
    }
    g_state.preview_year = 0; // the season preview is written again for the right year
    log("[silly-season] career in season index %d: the market moves from %d to %d (contracts +%d, %d veteran(s) off the "
        "grid retired)", season_index, expected - delta, expected, delta, retired);
    return true;
}

// g_lock held: a fresh market for the loaded career, when the saved one turns out to be another career's
void reset_market(const char *why, int season_index)
{
    std::string id = g_state.career, group = g_state.group;
    log("[silly-season] career %s: %s; a new market starts", id.c_str(), why);
    g_state = ss::State{};
    g_pool_read = false;
    g_adopted_from.clear();
    std::string err;
    if (!ss::init(g_state, g_baseline, group, seed_for(id), &err)) {
        log("[silly-season] cannot start the market: %s", err.c_str());
        g_loaded = false;
        return;
    }
    (void)season_index; // the first visit sets the year from the game's season (realign_year)
    g_state.career = id;
    g_state.group = group;
    g_state.season_index = -1; // the first visit takes the career where it is
    if (!sync_real_grid())
        sync_player();
}

// Team HQ is idle: catch up with the season, apply seat changes, queue the news
// ---------------------------------------------------------------------------------------------------
// F2 Revamp (miguelvf3.f2-revamp). Its promotions from F2 reach the market two ways, both the same signing: its
// promoted() list, read at each Team HQ of its career, and our silly_season.market service's sign(), which it
// calls for each promotion. A signing told again changes nothing.

// g_lock held: a signing by stat ids. 0: taken; 1: a driver or team the market can't tell; 2: no market loaded
int outside_by_stat(uint32_t driver_stat, uint32_t team_stat, int until, const char *source)
{
    if (!g_loaded)
        return 2;
    std::string team = team_by_stat(team_stat);
    std::string driver = driver_for_stat(driver_stat);
    if (team.empty() || driver.empty() || !ss::outside_signing(g_state, driver, team, until, source)) {
        log("[silly-season] %s: driver %u to team %u until %d: not a driver and team the market knows", source, driver_stat,
            team_stat, until);
        return 1;
    }
    return 0;
}

// g_lock held, at a Team HQ of an F2 Revamp career: its promotions, and their contracts once the grid shows them
void take_f2_promotions()
{
    if (g_state.group != kF2Group)
        return;
    if (const F2RevampCareer *c = f2_service()) {
        F2RevampPromotion list[16];
        uint32_t n = std::min<uint32_t>(c->promoted(list, 16), 16);
        // F2 Revamp's deals run two seasons from its first F1 year (2027 and 2028), as its sign() calls say; a
        // fixed year, so the list read again in later seasons never stretches a contract
        int first = static_cast<int>(g_baseline["careers"][kF2Group]["year"].as_int(2027));
        for (uint32_t i = 0; i < n; ++i)
            outside_by_stat(list[i].driver_stat, list[i].team_stat, first + 1, "F2 Revamp");
    }
    if (int told = ss::apply_outside(g_state))
        log("[silly-season] F2 Revamp: %d promotion(s) from F2 in the market as signings", told);
}

// silly_season.market v1, the service other addons call (F2 Revamp tells it each promotion). The layout is the one
// agreed with F2 Revamp: append new calls at the end and keep size first.
struct SillySeasonMarket {
    static constexpr const char *name = "silly_season.market";
    static constexpr uint32_t version = 1;
    uint32_t size;
    // the driver now races for the team (by stat ids), with a contract to the end of `until`. 0: taken (a repeat is the
    // same contract); 1: a driver or team the market can't tell; 2: no Silly Season market is loaded
    int (*sign)(uint32_t driver_stat, uint32_t team_stat, int until);
};

int svc_sign(uint32_t driver_stat, uint32_t team_stat, int until)
{
    std::lock_guard lock(g_lock);
    if (!g_loaded)
        return 2;
    // the caller just moved the driver: read the grid again so the market sees him in that car now
    sync_real_grid();
    int r = outside_by_stat(driver_stat, team_stat, until, "F2 Revamp");
    if (r == 0 && ss::apply_outside(g_state) > 0) {
        log("[silly-season] sign: driver %u for team %u until %d, told by another addon", driver_stat, team_stat, until);
        update_overlay(settings());
        save_state("signing from another addon");
    }
    return r;
}

const SillySeasonMarket kMarketService{sizeof(SillySeasonMarket), &svc_sign};

// g_lock held: the new season's grid into the game. A seat the game refuses is taken back: the market reads the
// game's grid again and the signing it told falls through (1.0.1 kept announcing it). An observer career (My Team)
// writes nothing: the grid is the game's.
void apply_new_season(const ss::Settings &s)
{
    if (g_state.observer) {
        log("[silly-season] new season: the game's grid stands (My Team: no seat is written)");
        return;
    }
    std::vector<ss::SeatChange> refused = apply_changes(full_grid(s), "new season");
    if (refused.empty())
        return;
    bool read = sync_real_grid();
    int taken_back = 0;
    for (const ss::SeatChange &c : refused)
        if (ss::season_change_refused(g_state, c.team, c.driver))
            ++taken_back;
    log("[silly-season] new season: the game refused %zu seat(s); %s, %d signing(s) taken back", refused.size(),
        read ? "the market follows the game's grid" : "the game's grid couldn't be read again", taken_back);
}

void on_hub()
{
    std::lock_guard lock(g_lock);
    if (!g_loaded)
        return;
    ss::Settings s = settings();
    ss::localize(g_state, s.lang != "en");

    if (g_grid_dirty) {
        g_grid_dirty = false;
        size_t known = 0;
        for (ss::GridCar &c : g_grid) {
            if (!c.human)
                c.driver = driver_label(c.driver);
            if (g_state.team(c.team) && (c.human || g_state.driver(c.driver)))
                ++known;
        }
        if (known * 2 >= g_grid.size()) {
            ss::sync_grid(g_state, g_grid);
            log("[silly-season] grid synced: %zu of %zu cars known, player %s (%s)", known, g_grid.size(),
                g_state.player.c_str(), g_state.player_team.c_str());
        } else {
            log("[silly-season] grid NOT synced: only %zu of %zu cars match known drivers and teams (first: %s / %s)",
                known, g_grid.size(), g_grid.empty() ? "" : g_grid[0].driver.c_str(),
                g_grid.empty() ? "" : g_grid[0].team.c_str());
        }
    }
    follow_career_id();
    // My Team: the core can't write race seats there (no DriverCareerContractsManagerComponent), so the market only
    // observes the game's moves. 1.0.1 announced signings that never happened (a player report).
    if (!g_state.observer && !g_state.real) {
        career::State cs{};
        if (career::state(&cs) == career::Result::ok && cs.loaded && cs.mode == SWOFTY_CAREER_MY_TEAM) {
            ss::become_observer(g_state);
            log("[silly-season] My Team: only the game changes race seats here; the market observes and reports its moves");
        }
    }
    std::string player_before = g_state.player;
    if (!sync_real_grid())
        sync_player();
    adopt_game_pool("team hq"); // the drivers other addons put in the career's lists
    take_f2_promotions();       // F2 Revamp's promotions from F2, as signings
    if (!g_adopted_from.empty() && !player_before.empty() && player_before != "player" && player_before != g_state.player) {
        reset_market("the market taken from a provisional id was another driver's", career::current_round() ? career::current_round()->season_index : 0);
        if (!g_loaded)
            return;
    }
    // 1.1.0's probe (probe::run) did its job during development: the readers it led to live in game_data.inl and
    // game_ratings.inl, and it no longer writes to the log of a released build
    take_game_ratings(); // before the market moves: it decides on what the game shows
    take_season_data();
    if (g_results_rounds < 0) // the telemetry's results, when no standings could be read
        for (auto &[driver, pos] : g_results)
            ss::record_result(g_state, driver_label(driver), pos);
    g_results.clear();

    // the report after the race: rounds this visit completes in a market already running
    bool market_running = g_state.season_index >= 0;
    int round_before = g_state.round, year_before = g_state.year;

    auto pos = career::current_round();
    // a saved market ahead of the game belongs to another career under the same id (two careers with the
    // same driver can share one on this core)
    if (pos && g_state.season_index >= 0 &&
        (g_state.season_index > pos->season_index ||
         (g_state.season_index == pos->season_index && g_state.round > pos->round + 1))) {
        char why[160];
        std::snprintf(why, sizeof why, "the saved market (season index %d, round %d) is ahead of the game (season index %d, round %d)",
                      g_state.season_index, g_state.round, pos->season_index, pos->round);
        reset_market(why, pos->season_index);
        if (!g_loaded)
            return;
    }
    if (!g_adopted_from.empty()) {
        // the adopted market checked out: it lives under the real id from now on
        log("[silly-season] career %s: the market from %s is this career's", g_state.career.c_str(), g_adopted_from.c_str());
        if (storage::available())
            storage::remove(storage_path(g_adopted_from));
        forget_provisional(g_adopted_from);
        g_adopted_from.clear();
    }
    if (pos)
        log("[silly-season] hub: game at round %d of %d (season index %d); market at round %d of season index %d",
            pos->round, pos->rounds, pos->season_index, g_state.round, g_state.season_index);
    else
        log("[silly-season] hub: the game's round is not readable yet; market at round %d", g_state.round);
    g_round_seen = pos ? pos->season_index * 100 + pos->round : -1;
    if (pos) {
        g_weekends_ended = 0;
        int completed = std::min(pos->round - 1, pos->rounds);
        // a weekend whose race was run since the last visit counts, whatever the game's round says. Practice
        // or qualifying alone doesn't; with no session type known, the weekend counts as it always did.
        if (g_weekend_raced != 0 && g_state.season_index >= 0 &&
            (g_weekend_season < 0 || g_weekend_season == pos->season_index)) {
            int raced = g_weekend_raced > 0 ? g_weekend_raced : g_state.round + 1;
            if (!g_race_ended && g_session_typed) {
                log("[silly-season] hub: sessions of round %d ran, but not its race yet", raced);
            } else if (raced > completed) {
                log("[silly-season] hub: counting round %d as raced (the game says round %d%s)", raced, pos->round,
                    g_race_ended ? "" : ", no session said its type");
                completed = std::min(raced, pos->rounds);
            }
        }
        // the standings: as many rounds as have results were run, whatever the calendar or the sessions say
        if (g_results_rounds > completed && (g_weekend_season < 0 || g_weekend_season == pos->season_index)) {
            log("[silly-season] hub: the standings hold %d round(s) (the game says round %d)", g_results_rounds, pos->round);
            completed = std::min(g_results_rounds, pos->rounds);
        }
        g_weekend_raced = 0;
        g_weekend_season = -1;
        g_race_ended = g_session_typed = false;
        if (g_state.season_index >= 0 && g_state.season_index == pos->season_index && realign_year(pos->season_index))
            ss::season_preview(g_state, s); // a market saved in the wrong year: its preview for the right one
        if (g_state.season_index < 0) {
            // first visit: take the career where it is, without replaying the rounds already raced
            if (g_real_career) {
                if (int year = detect_real_year())
                    start_real_season(year, pos->rounds);
                else {
                    log("[silly-season] history: the real year can't be told from the grid yet; the market waits for the next Team HQ");
                    update_overlay(s);
                    return;
                }
            } else {
                realign_year(pos->season_index);
            }
            g_state.season_index = pos->season_index;
            g_state.round = std::max(g_state.round, completed);
            g_state.round_base = g_state.round; // no results exist for the rounds raced before
            g_state.rounds = pos->rounds;
            log("[silly-season] career at season index %d, round %d of %d (%d)", pos->season_index, completed,
                pos->rounds, g_state.year);
            ss::season_preview(g_state, s);
        }
        if (pos->season_index > g_state.season_index) {
            std::vector<ss::RatingChange> ratings;
            bool real_season = g_state.real; // the season that ends was a real one: Progressive sets the new grid
            auto changes = ss::start_new_season(g_state, s, &ratings);
            g_state.season_index = pos->season_index;
            log("[silly-season] season %d: %zu seat(s) change", g_state.year, changes.size());
            if (g_real_career && g_real_ok && g_state.year <= g_real_last)
                start_real_season(g_state.year, pos->rounds);
            take_season_data(); // the new season's rounds, if any were run before this visit
            if (real_season) {
                log("[silly-season] history: Progressive puts %d's real line-up on the grid", g_state.year);
                ratings.clear();
            } else {
                apply_new_season(s);
            }
            for (const ss::RatingChange &rc : ratings) {
                career::DriverRatings dr;
                dr.experience = rc.experience;
                dr.racecraft = rc.racecraft;
                dr.awareness = rc.awareness;
                dr.pace = rc.pace;
                career::set_driver_ratings(Ref<drivers::Identity>("game/" + rc.driver), dr);
            }
            g_season_ended = g_moves_finalised = false;
        }
        if (completed > g_state.round)
            ss::complete_round(g_state, s, completed, pos->rounds);
    } else {
        // older core: count weekends and season ends ourselves
        if (g_weekends_ended > 0) {
            ss::complete_round(g_state, s, g_state.round + g_weekends_ended, g_state.rounds);
            g_weekends_ended = 0;
        }
        if (g_season_ended && !g_state.finalised)
            ss::finalise_season(g_state, s);
        if (g_moves_finalised) {
            ss::start_new_season(g_state, s);
            apply_new_season(s);
            g_season_ended = g_moves_finalised = false;
        }
    }
    apply_changes(ss::take_pending(g_state), "mid-season");
    g_state.player_name = s.lang == "en" ? "You" : "Tú";
    if (market_running && g_state.round > 0 && (g_state.round > round_before || g_state.year != year_before))
        ++g_report_seq; // a race since the last visit: the report after it
    ss::refresh_mails(g_state, s);
    apply_texts(s);
    push_talent_ratings();
    push_talent_numbers();
    update_overlay(s);
    g_popup_wait = 1.5; // let Team HQ settle before a popup
    save_state("team hq");
}

void on_hub_event(const SwoftyEvent *ev, void *)
{
    g_hub_idle = ev->a == 1;
    g_at_hq = g_hub_idle;
    if (g_hub_idle)
        g_in_session = false;
    if (g_hub_idle) {
        // F2 Revamp: the player's F2 season is over, his F1 years start here, and so does the market
        bool start = false;
        {
            std::lock_guard lock(g_lock);
            start = g_f2_waiting && !g_loaded && !f2_phase();
            if (start) {
                log("[silly-season] F2 Revamp career: the player is in F1 now; the market starts");
                begin_market(kF2Group, career_id(nullptr), false);
            }
        }
        if (start)
            career_ready();
        on_hub();
    }
    else
        g_popup_wait = -1;
}

bool g_reset_press_row = false;

void frame_tick(double dt, void *)
{
    if (g_reset_press_row) {
        g_reset_press_row = false;
        hub::set_option(kAddon, "press_answer", "-"); // ready for the next question
    }
    if (int choice = g_press_answer.exchange(-1); choice >= 0) {
        std::lock_guard lock(g_lock);
        const ss::State::Press *q = g_loaded ? ss::press_pending(g_state) : nullptr;
        if (q && q->id == g_press_answer_id.load()) {
            ss::Settings s = settings();
            ss::localize(g_state, s.lang != "en");
            bool from_hub = g_press_from_hub.exchange(false);
            if (ss::answer_press(g_state, s, choice)) {
                log("[silly-season] press: answered %d", choice + 1);
                if (from_hub && !g_state.news.empty())
                    g_report = g_state.news.back().title + "\n" + g_state.news.back().body;
                ss::refresh_mails(g_state, s);
                apply_texts(s);
                update_overlay(s);
                save_state("press answer");
            }
        }
    }
    // the awards ceremony and the season's cover were watched: once a season each
    for (auto [flag, field] : {std::pair{&g_awards_done, &ss::State::awards_shown}, std::pair{&g_cover_done, &ss::State::cover_shown}})
        if (int year = flag->exchange(0); year > 0) {
            std::lock_guard lock(g_lock);
            if (g_loaded && g_state.*field != year) {
                g_state.*field = year;
                update_overlay(settings());
                save_state("overlay seen");
            }
        }
    if (int year = g_reveal_done.exchange(0); year > 0) {
        // the player watched next season's grid revealed: once a season
        std::lock_guard lock(g_lock);
        if (g_loaded && g_state.year == year && g_state.reveal_year != year) {
            g_state.reveal_year = year;
            update_overlay(settings());
            save_state("grid reveal");
        }
    }
    if (g_seen_request.exchange(false)) {
        // the F7 panel was opened: everything in it counts as read
        std::lock_guard lock(g_lock);
        if (g_loaded) {
            ss::Settings s = settings();
            ss::localize(g_state, s.lang != "en");
            ss::mark_seen(g_state);
            apply_texts(s, false); // the tab title follows when the panel closes
            update_overlay(s);
        }
    }
    {
        // the panel just closed: the game's texts (the tab title's unread count) refresh now; the refresh is a
        // short hitch, so it waited while the panel was open
        static bool was_open = false;
        bool open_now = g_board_open.load();
        if (was_open && !open_now) {
            std::lock_guard lock(g_lock);
            if (g_loaded && g_lang_refresh_due && lang::available()) {
                g_lang_refresh_due = false;
                lang::refresh();
            }
        }
        was_open = open_now;
    }
    if (!g_hub_idle)
        return;
    std::lock_guard lock(g_lock);
    if ((g_round_poll -= dt) <= 0) {
        g_round_poll = 2.0;
        if (g_loaded)
            if (auto pos = career::current_round(); pos && pos->season_index * 100 + pos->round != g_round_seen) {
                log("[silly-season] hub: the game's round changed to %d (season index %d) after Team HQ opened",
                    pos->round, pos->season_index);
                on_hub();
            }
    }
    if (!g_report.empty()) {
        std::string body = g_report;
        g_report.clear();
        size_t nl = body.find('\n');
        std::string title = nl == std::string::npos ? std::string("SILLY SEASON") : body.substr(0, nl);
        if (nl != std::string::npos)
            body.erase(0, nl + 1);
        popup::show(title, body);
        g_popup_wait = 2.0; // news after the report, not on top of it
        return;
    }
    if (g_popup_wait < 0 || !g_loaded)
        return;
    g_popup_wait -= dt;
    if (g_popup_wait < 0)
        show_news();
}

bool in_career(const events::Context &ctx)
{
    events::Mode m = ctx.mode();
    if (m == events::Mode::driver_career || m == events::Mode::my_team)
        return true;
    if (m != events::Mode::unknown)
        return false;
    // a context without a mode: ask the career state
    career::State cs{};
    return career::state(&cs) == career::Result::ok && cs.loaded &&
           (cs.mode == SWOFTY_CAREER_DRIVER || cs.mode == SWOFTY_CAREER_MY_TEAM);
}

// the real grid from a list of entrants; false when the list is empty. g_lock held
bool take_grid(std::span<const events::Entrant> entrants, const char *where)
{
    std::vector<ss::GridCar> cars;
    for (const events::Entrant &e : entrants)
        cars.push_back({strip_game(e.driver.id()), strip_game(e.team.id()), e.human});
    if (!g_loaded || cars.empty())
        return false;
    g_grid = std::move(cars);
    g_grid_dirty = true;
    log("[silly-season] grid seen at %s: %zu cars", where, g_grid.size());
    return true;
}
std::atomic<bool> g_grid_wanted{false}; // this session's grid is not known yet: the first lap reads it

} // namespace

// ---------------------------------------------------------------------------------------------------
// The Team HQ menu: each value of "menu" opens a report

SWOFTY_ON_CHANGE(menu, std::string_view v)
{
    std::lock_guard lock(g_lock);
    if (!g_loaded) {
        log("[silly-season] menu %.*s: no Silly Season career is loaded", int(v.size()), v.data());
        return;
    }
    ss::Settings s = settings();
    ss::localize(g_state, s.lang != "en");
    if (v == "noticias") {
        std::string out = std::string(s.lang == "en" ? "LATEST NEWS" : "ÚLTIMAS NOTICIAS") + "\n";
        int n = 0;
        for (auto it = g_state.news.rbegin(); it != g_state.news.rend() && n < 5; ++it, ++n)
            out += "- " + it->title + ": " + it->body.substr(0, it->body.find('\n')) + "\n";
        g_report = out;
    } else if (v == "bolsa") {
        g_report = ss::report_stock(g_state, s);
    } else if (v == "prensa") {
        bool es = s.lang != "en";
        if (const ss::State::Press *q = ss::press_pending(g_state)) {
            std::string out = ss::text::t_press(ss::text::lang_of(s.lang)) + "\n" + q->question + "\n\n";
            for (size_t i = 0; i < q->answers.size(); ++i)
                out += std::to_string(i + 1) + ". " + q->answers[i] + "\n";
            out += es ? "\nResponde en la fila \"Rueda de prensa\" de esta pestaña (1, 2 o 3) o con F7."
                      : "\nAnswer on this tab's \"Press conference\" row (1, 2 or 3) or with F7.";
            g_report = out;
        } else {
            g_report = ss::text::t_press(ss::text::lang_of(s.lang)) + "\n" +
                       (es ? "No hay preguntas pendientes." : "No questions waiting.");
        }
    } else if (v == "f2") {
        g_report = ss::report_f2(g_state, s);
    } else if (v == "tecnicos") {
        g_report = ss::report_staff(g_state, s);
    } else if (v == "rumores") {
        g_report = ss::report_rumours(g_state, s);
    } else if (v == "fichajes") {
        g_report = ss::report_deals(g_state, s);
    } else if (v == "parrilla") {
        g_report = ss::report_grid(g_state, s);
    } else if (v == "contratos") {
        g_report = ss::report_contracts(g_state, s);
    } else if (v == "mercado") {
        g_report = ss::report_driver_market(g_state, s);
    } else if (v == "historial") {
        g_report = ss::report_history(g_state, s);
    }
    if (!g_report.empty()) {
        ss::mark_seen(g_state);
        apply_texts(s);
        update_overlay(s);
    }
}

// The Team HQ row "Rueda de prensa": 1, 2 or 3 answers the waiting question, then the row goes back to "-"
SWOFTY_ON_CHANGE(press_answer, std::string_view v)
{
    if (v.size() != 1 || v[0] < '1' || v[0] > '3')
        return;
    std::lock_guard lock(g_lock);
    const ss::State::Press *q = g_loaded ? ss::press_pending(g_state) : nullptr;
    if (q) {
        g_press_answer_id = q->id;
        g_press_from_hub = true;
        g_press_answer = v[0] - '1';
    } else {
        ss::Settings s = settings();
        ss::localize(g_state, s.lang != "en");
        g_report = ss::text::t_press(ss::text::lang_of(s.lang)) + "\n" +
                   (s.lang == "en" ? "No questions waiting." : "No hay preguntas pendientes.");
    }
    g_reset_press_row = true;
}

// ---------------------------------------------------------------------------------------------------
// Boot: the game's own market goes quiet so Silly Season decides who leaves and who arrives

SWOFTY_REGISTER
{
    // The manifest's hub_tab gives 2026 careers the Team HQ tab. 2025 careers get the same tab from a
    // hub.tab record: same name, so the core uses the template swofty_hub_silly_season the launcher built
    // for the manifest's tab (only one career is loaded at a time, so the two never meet).
    career::hub_tabs.add("silly_season_2025", {
        .name = "silly_season",
        .title = "Silly Season",
        .data_group = "f1_career_2025",
        .options = {"menu", "press_answer", "intensity", "rumours", "midseason", "protect_teammate"},
    });
    // Progressive Career on the Swofty Seasons keeps its own data group: the same tab there (history mode)
    career::hub_tabs.add("silly_season_sw_progressive", {
        .name = "silly_season",
        .title = "Silly Season",
        .data_group = "f1_career_sw_progressive",
        .options = {"menu", "press_answer", "intensity", "rumours"},
    });
    // F2 Revamp's career (miguelvf3.f2-revamp): the same tab in its F1 years
    career::hub_tabs.add("silly_season_f2_revamp", {
        .name = "silly_season",
        .title = "Silly Season",
        .data_group = kF2Group,
        .options = {"menu", "press_answer", "intensity", "rumours", "midseason", "protect_teammate"},
    });

    std::string mode = swofty::option<std::string>("game_market");
    if (mode == "game")
        return;
    career::markets.add("quiet", {
        .retirements = std::vector<career::Retirement>{},
        .firings = std::vector<career::Firing>{},
    });
}

// ---------------------------------------------------------------------------------------------------
// Events

SWOFTY_ON_LOAD
{
    load_baseline();
    if (!career::available())
        log("[silly-season] the career library is not available on this core; the addon stays idle");
    // the services other addons share: ours for F2 Revamp's promotions, and F2 Revamp's career when it is on
    if (services::available()) {
        log("[silly-season] service %s v%u: %s", SillySeasonMarket::name, SillySeasonMarket::version,
            services::name(services::publish(kMarketService)));
        services::Result r = services::when<F2RevampCareer>([](const F2RevampCareer &c) {
            g_f2 = &c;
            log("[silly-season] F2 Revamp's career service found: its F2 seasons wait, its promotions are signings");
        });
        log("[silly-season] lookup %s: %s", F2RevampCareer::name, services::name(r));
    }

    events::on<career::events::Created>().after([](const career::events::Created &, const events::Context &ctx) {
        begin_career(ctx, true);
        career_ready();
    });
    events::on<career::events::Loaded>().after([](const career::events::Loaded &, const events::Context &ctx) {
        auto t0 = std::chrono::steady_clock::now();
        begin_career(ctx, false);
        auto t1 = std::chrono::steady_clock::now();
        career_ready();
        auto t2 = std::chrono::steady_clock::now();
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        if (ms(t0, t2) > 50)
            log("[silly-season] career loaded in %.0f ms (state %.0f ms, texts and panel %.0f ms)", ms(t0, t2), ms(t0, t1),
                ms(t1, t2));
    });
    events::on<career::events::Saving>().before([](career::events::Saving &, const events::Context &) {
        std::lock_guard lock(g_lock);
        if (g_loaded && g_state.career.empty())
            g_state.career = career::saves::current(); // set for this event
        save_state("profile save");
    });
    events::on<career::events::Unloaded>().after([](const career::events::Unloaded &, const events::Context &) {
        std::lock_guard lock(g_lock);
        save_state("career unloaded");
        g_loaded = false;
        g_f2_waiting = false;
        g_report_seq = 0;
        g_keen_known = false;
        g_keen_prev.clear();
        g_hub_idle = false;
        clear_texts();
        g_talent_ratings_year = 0;
        g_own_team_name.clear();
        g_real_grid_said.clear();
        g_real_career = g_real_now = false;
        g_results_said.clear();
        g_player_stat = 0;
        std::lock_guard ov_lock(g_ov_lock);
        g_ov.career = false;
        g_ov.banners.clear();
    });
    // no banner or panel while a session runs
    events::on("session.unload").after([](std::span<const std::byte>, const events::Context &) { g_in_session = false; });
    events::on<career::events::SeasonEnd>().after([](const career::events::SeasonEnd &, const events::Context &) {
        std::lock_guard lock(g_lock);
        g_season_ended = true;
    });
    events::on("career.moves.finalised").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard lock(g_lock);
        g_moves_finalised = true;
    });
    events::on("career.weekend.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard lock(g_lock);
        ++g_weekends_ended;
    });

    // the real grid at every session start
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &ctx) {
        g_in_session = true;
        if (!in_career(ctx))
            return;
        std::lock_guard lock(g_lock);
        if (!g_loaded)
            return;
        note_weekend(ctx, "session start");
        if (!take_grid(ctx.participants(), "session start"))
            g_grid_wanted = true;
    });
    // the lap event's context always has the participants: the first crossing reads the grid when the
    // session start could not
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &, const events::Context &ctx) {
        if (!g_grid_wanted.load(std::memory_order_relaxed))
            return;
        std::unique_lock lock(g_lock, std::try_to_lock);
        if (!lock.owns_lock())
            return;
        if (take_grid(ctx.participants(), "first lap") || !g_loaded)
            g_grid_wanted = false;
    });

    // the race of the weekend ended; and its results, when the core's telemetry reads positions (the core of
    // 1.0.1's time leaves them at zero, so the race is simulated)
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &ctx) {
        g_grid_wanted = false;
        if (!in_career(ctx))
            return;
        {
            std::lock_guard lock(g_lock);
            if (g_loaded)
                note_session_end(ctx);
        }
        if (!ctx.session() || ctx.session()->type != events::SessionType::race)
            return;
        if (!telemetry::available())
            return;
        std::vector<std::pair<std::string, int>> results;
        for (const events::Entrant &e : ctx.participants()) {
            telemetry::Participant p{};
            p.version = SWOFTY_TELEMETRY_VERSION;
            if (telemetry::participant(e.car, &p) == telemetry::Result::ok && p.position >= 1 && p.position <= 32)
                results.push_back({strip_game(e.driver.id()), p.position});
        }
        std::lock_guard lock(g_lock);
        if (g_loaded) {
            g_results.insert(g_results.end(), results.begin(), results.end());
            log("[silly-season] race result: %zu classified car(s)", results.size());
        }
    });

    // the ratings the game uses in this career
    events::on<drivers::events::RatingsRead>().after([](const drivers::events::RatingsRead &r, const events::Context &) {
        int stat = static_cast<int>(r.stat);
        if (stat > 3)
            return;
        std::unique_lock lock(g_lock, std::try_to_lock);
        if (lock.owns_lock() && g_loaded)
            ss::observe_rating(g_state, strip_game(r.driver().id()), stat, r.value);
    });

    events::subscribe(SWOFTY_EVENT_CAREER_HUB, on_hub_event);
    events::on_frame(frame_tick);
    if (ov::available()) {
        ov::Result r = ov::on_paint(paint);
        g_overlay_ready = r == ov::Result::ok;
        if (g_overlay_ready)
            ui::load_textures();
        log("[silly-season] overlay: banner and F7 panel %s (input %s)", ov::name(r), ov::v3() ? "v3" : "missing");
    } else {
        log("[silly-season] overlay: not available (the experimental overlay is off); mails and the Team HQ tab only");
    }
    log("[silly-season] ready: market %s, intensity %.*s", std::string_view(game_market) == "game" ? "shared with the game" : "run by Silly Season",
        int(std::string_view(intensity).size()), std::string_view(intensity).data());
}

SWOFTY_ADDON()
