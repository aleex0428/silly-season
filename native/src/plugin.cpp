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
#include <swofty/mixin.hpp>
#include <swofty/overlay.hpp>
#include <swofty/popup.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/telemetry.hpp>

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
    s.lang = "en"; // everything the player reads is in English
    return s;
}

// ---------------------------------------------------------------------------------------------------
// State

namespace {

std::recursive_mutex g_lock;
json::Value g_baseline;
bool g_baseline_ok = false;
bool g_loaded = false;         // a career this addon handles is loaded
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

// a career of a data group the baseline knows
void begin_career(const events::Context &ctx, bool fresh)
{
    std::lock_guard lock(g_lock);
    if (!g_baseline_ok && !load_baseline())
        return;
    const auto &view = ctx.career();
    std::string group = view ? view->data_group : std::string();
    if (group.empty() || !g_baseline["careers"][group].is_object()) {
        if (g_loaded)
            save_state("another career");
        g_loaded = false;
        log("[silly-season] career data group '%s' is not one Silly Season knows; the market stays the game's",
            group.c_str());
        return;
    }
    std::string id = career_id(&ctx);
    if (g_loaded && g_state.career == id && !id.empty())
        return; // created, then loaded: same career
    g_state = ss::State{};
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
        if (view && view->year > 0 && view->year != g_state.year)
            g_state.year = view->year; // an existing career met the addon after its first season
        g_state.season_index = -1;     // the first idle Team HQ says where the career is
        log("[silly-season] career %s (%s): new market for %d, %zu drivers, %zu teams", id.c_str(), group.c_str(),
            g_state.year, g_state.drivers.size(), g_state.teams.size());
    } else {
        ss::refresh_from_baseline(g_state, g_baseline);
    }
    g_state.career = id;
    g_state.group = group;
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

void apply_changes(const std::vector<ss::SeatChange> &changes, const char *why)
{
    const SwoftyCareerOps *ops = career::ops();
    for (const ss::SeatChange &c : changes) {
        const ss::Team *t = g_state.team(c.team);
        if (!t)
            continue;
        uint32_t stat = static_cast<uint32_t>(t->stat);
        if (ops && ops->version >= 7 && ops->team_stat) {
            uint32_t live = 0;
            if (ops->team_stat(("game/" + c.team).c_str(), &live) == SWOFTY_ASSET_OK)
                stat = live;
        }
        career::Result r = career::set_seat_of_team(stat, c.seat, Ref<drivers::Identity>("game/" + c.driver));
        log("[silly-season] %s: %s seat %d (team stat %u) <- %s: %s", why, c.team.c_str(), c.seat, stat,
            c.driver.c_str(), career::name(r));
    }
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
        if (s == stat)
            return t.id;
    }
    return {};
}

std::string known_driver(uint32_t stat); // the real grid's lookup (below)

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
    std::string driver = known_driver(driver_stat);
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
    for (size_t off = 0; off < buf.size(); off += rows::kSize) {
        uint32_t ordinal = rows::at<uint32_t>(buf, off + rows::kOrdinal), driver = rows::at<uint32_t>(buf, off + rows::kDriver),
                 team = rows::at<uint32_t>(buf, off + rows::kTeam), vehicle = rows::at<uint32_t>(buf, off + rows::kVehicle);
        uint16_t position = rows::at<uint16_t>(buf, off + rows::kPosition);
        bool teamless = (team & 0xffff0000u) == rows::kNoKey, car = (vehicle & 0xffff0000u) != rows::kNoKey;
        int seat = (position >> 8) ? (position & 0xff) : -1;
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

// the market's driver for a stat id: the baseline's own list first (Carlos Sainz is stat 0, which is also
// what a driver with no stat id holds), then a driver added earlier from the game; "" when unknown
std::string known_driver(uint32_t stat)
{
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
        ss::Driver d;
        d.id = id;
        d.name = label.empty() ? "Driver " + std::to_string(stat) : name_from_label(label);
        d.kind = "f1";
        d.stat = static_cast<int>(stat);
        d.born = g_state.year - 27;
        g_state.drivers.emplace(id, d);
        log("[silly-season] real grid: driver stat %u is not in the baseline; added as %s", stat, d.name.c_str());
    }
    return id;
}

std::string g_own_team_name;      // the player's own My Team team, shown in the hub ("" when he drives for a known team)
std::string g_real_grid_said;     // the last outcome logged

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
    // the player's team, and his team-mate when the team is his own (My Team): no team of the baseline
    uint32_t human_team = 0;
    bool have_human = false;
    for (const RealSeat &s : seats)
        if (s.human) {
            human_team = s.team;
            have_human = true;
        }
    std::string own_mate;
    std::vector<std::string> elsewhere; // drivers of teams Silly Season doesn't know (Konnersport, another addon's)
    std::vector<ss::GridCar> cars;
    std::vector<std::string> moved;
    size_t unknown_teams = 0;
    std::string player, player_team;
    for (const RealSeat &s : seats) {
        std::string team = team_by_stat(s.team);
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
    // the player's own team: the market leaves its seats alone
    g_state.protected_ids.clear();
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
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The overlay: a breaking-news lower third and the F7 market hub. Paint runs on the render thread, so it
// reads a snapshot the flow thread fills (update_overlay) under its own lock. Everything is drawn with the
// core's vector primitives: no images, so nothing of the game's art is shipped.

struct UiNews {
    int id = 0, year = 0, round = 0;
    std::string kind, title, body;
    bool unread = false;
};

struct UiRumour {
    std::string driver, from, to, to_code, outlet;
    uint32_t from_col = 0, to_col = 0;
    int stage = 0; // 0 rumour, 1 talks
    int rel = 0;   // outlet reliability 1..5
};

struct UiDeal {
    std::string driver, team, code, kind;
    uint32_t col = 0;
    int until = 0;
};

struct UiMove {
    int year = 0, round = 0;
    std::string driver, from, to, from_code, to_code, kind;
    uint32_t from_col = 0, to_col = 0;
};

struct UiSeat {
    std::string name;
    bool fresh = false, player = false;
};

struct UiTeam {
    std::string name, code, td;
    uint32_t col = 0;
    int rank = 0, points = 0;
    float td_rating = 0;
    UiSeat seat[2];
};

struct UiDriver {
    std::string name, team, code, salary, kind, last_team;
    uint32_t col = 0;
    int age = 0, contract = 0;
    float value = 0, potential = 0, trend = 0;
    bool clause = false, feud = false, pressure = false;
};

struct OverlayData {
    bool career = false, enabled = true, es = false, effects = true;
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
};

std::mutex g_ov_lock;
OverlayData g_ov;
std::atomic<bool> g_in_session{false};
std::atomic<bool> g_board_open{false};
std::atomic<bool> g_seen_request{false};
std::atomic<int> g_press_answer{-1};     // 0..2, from the panel or the Team HQ row
std::atomic<int> g_press_answer_id{0};
std::atomic<bool> g_press_from_hub{false};
bool g_overlay_ready = false;

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
    o.unread = ss::unread(st);

    int shown = 0;
    for (auto it = st.news.rbegin(); it != st.news.rend() && shown < 60; ++it, ++shown)
        o.news.push_back({it->id, it->year, it->round, it->kind, it->title, it->body, it->id > st.seen_news});

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
            o.deals.push_back({dname(it->driver), t->name, t->code, it->kind, t->colour, it->until});
            continue;
        }
        if (it->stage == ss::Stage::collapsed || it->kind == "renewal")
            continue;
        ++o.open_talks;
        if (o.rumours.size() >= 24)
            continue;
        UiRumour r;
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
        r.outlet = ss::text::outlet_name(it->source);
        r.rel = ss::text::outlet_reliability(it->source);
        o.rumours.push_back(std::move(r));
    }

    auto ui_move = [&](const ss::Move &m) {
        UiMove u{m.year, m.round, dname(m.driver), "", "", "", "", m.kind};
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
        UiTeam now{t.name, t.code, t.td_name, t.colour, ss::team_rank(st, t.id), t.points, t.td_rating, {}};
        UiTeam next = now;
        for (int k = 0; k < 2; ++k) {
            const std::string &cur = t.seats[static_cast<size_t>(k)];
            now.seat[k] = {cur.empty() ? std::string() : dname(cur), false, !cur.empty() && cur == st.player};
            const std::string &who = grid[t.id][static_cast<size_t>(k)];
            const ss::Driver *d = st.driver(who);
            next.seat[k] = {who.empty() ? std::string() : dname(who), d && d->team != t.id,
                            !who.empty() && who == st.player};
        }
        o.teams.push_back(std::move(now));
        o.grid.push_back(std::move(next));
    }

    auto ui_driver = [&](const ss::Driver &d) {
        UiDriver u;
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
        if (const ss::Team *t = team_of(d.team)) {
            u.team = t->name;
            u.code = t->code;
            u.col = t->colour;
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

    auto breaking = ss::take_breaking(g_state, 4);
    std::lock_guard lock(g_ov_lock);
    o.career = g_loaded;
    o.enabled = headlines;
    o.effects = effects;
    o.version = g_ov.version + 1;
    o.banners = std::move(g_ov.banners);
    if (o.enabled)
        for (ss::NewsItem &n : breaking)
            o.banners.push_back(std::move(n));
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
    if (const auto &sv = ctx.session()) {
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
    if (const auto &sv = ctx.session()) {
        type = static_cast<int>(sv->type);
        g_session_typed = true;
    }
    if (type == static_cast<int>(events::SessionType::race)) {
        g_race_ended = true;
        log("[silly-season] session end: the race of round %d is done", g_weekend_raced);
    }
    g_session_type = -1;
}

// g_lock held: a fresh market for the loaded career, when the saved one turns out to be another career's
void reset_market(const char *why, int season_index)
{
    std::string id = g_state.career, group = g_state.group;
    log("[silly-season] career %s: %s; a new market starts", id.c_str(), why);
    g_state = ss::State{};
    g_adopted_from.clear();
    std::string err;
    if (!ss::init(g_state, g_baseline, group, seed_for(id), &err)) {
        log("[silly-season] cannot start the market: %s", err.c_str());
        g_loaded = false;
        return;
    }
    if (season_index > 0)
        g_state.year += season_index;
    g_state.career = id;
    g_state.group = group;
    g_state.season_index = -1; // the first visit takes the career where it is
    if (!sync_real_grid())
        sync_player();
}

// Team HQ is idle: catch up with the season, apply seat changes, queue the news
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
    std::string player_before = g_state.player;
    if (!sync_real_grid())
        sync_player();
    if (!g_adopted_from.empty() && !player_before.empty() && player_before != "player" && player_before != g_state.player) {
        reset_market("the market taken from a provisional id was another driver's", career::current_round() ? career::current_round()->season_index : 0);
        if (!g_loaded)
            return;
    }
    for (auto &[driver, pos] : g_results)
        ss::record_result(g_state, driver_label(driver), pos);
    g_results.clear();

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
        g_weekend_raced = 0;
        g_weekend_season = -1;
        g_race_ended = g_session_typed = false;
        if (g_state.season_index < 0) {
            // first visit: take the career where it is, without replaying the rounds already raced
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
            auto changes = ss::start_new_season(g_state, s, &ratings);
            g_state.season_index = pos->season_index;
            log("[silly-season] season %d: %zu seat(s) change", g_state.year, changes.size());
            apply_changes(full_grid(s), "new season");
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
            apply_changes(full_grid(s), "new season");
            g_season_ended = g_moves_finalised = false;
        }
    }
    apply_changes(ss::take_pending(g_state), "mid-season");
    g_state.player_name = s.lang == "en" ? "You" : "Tú";
    ss::refresh_mails(g_state, s);
    apply_texts(s);
    push_talent_ratings();
    update_overlay(s);
    g_popup_wait = 1.5; // let Team HQ settle before a popup
    save_state("team hq");
}

void on_hub_event(const SwoftyEvent *ev, void *)
{
    g_hub_idle = ev->a == 1;
    if (g_hub_idle)
        g_in_session = false;
    if (g_hub_idle)
        on_hub();
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
        g_hub_idle = false;
        clear_texts();
        g_talent_ratings_year = 0;
        g_own_team_name.clear();
        g_real_grid_said.clear();
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
