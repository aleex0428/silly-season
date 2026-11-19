// Silly Season: the engine plugin. It connects the market engine (market.cpp) to F1 25 through the
// Swofty core: career events, the real grid and results, the core's seat operations, popups, the
// Team HQ tab and per-career storage.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
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
#include <thread>
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
#include <swofty/systems/content.hpp>
#include <swofty/telemetry.hpp>
#include <swofty/view.hpp>
#include <swofty/systems/sessions.hpp>

#include <swofty/services.hpp>

#include "f2_revamp_api/career.hpp"
#include "market.hpp"
#include "text.hpp"

using namespace swofty;
namespace json = swofty::json;

static constexpr const char *kAddon = "alexs.silly-season";
#ifndef SS_VERSION
#define SS_VERSION "dev" // build.bat passes the manifest's version
#endif

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
// 1.1.3: overtakes, incidents and breakdowns from the game's telemetry. 15 Nov: off by default and marked experimental
// (flashbacks with the game's own clock and a feed that appears late are still to be seen in the game); the
// manifest's default is the same
SWOFTY_SETTING(bool, on_track, false);
SWOFTY_SETTING(bool, academies, true);     // 1.1.0 phase 6: reserve drivers, academies, partner teams
SWOFTY_SETTING(std::string_view, panel_size, "normal"); // the F7 panel's size: small, normal, large
SWOFTY_SETTING(std::string_view, panel_key, "f7");       // 1.1.3: the key that opens the panel (another addon may use F7)

// 1.1.3: the keys the panel can open with; the overlay reads the pick from the render thread
struct PanelKey {
    const char *id, *label;
    int vk;
};
constexpr PanelKey kPanelKeys[] = {{"f7", "F7", VK_F7}, {"f6", "F6", VK_F6}, {"f8", "F8", VK_F8},
                                   {"f9", "F9", VK_F9}, {"f10", "F10", VK_F10}, {"f11", "F11", VK_F11}};
std::atomic<int> g_panel_key{0}; // index into kPanelKeys
// 1.1.3: bumped when a career loads or unloads: what the overlays remember on the render thread (shown ids, the one on
// screen, the alerts, the focus) belongs to one career, and the next starts it again. A race report of the next career
// used to wait for an id above the last one shown, while its count started again at 1.
std::atomic<int> g_ov_generation{0};
const char *panel_key_label() { return kPanelKeys[g_panel_key.load(std::memory_order_relaxed)].label; }
int panel_key_vk() { return kPanelKeys[g_panel_key.load(std::memory_order_relaxed)].vk; }
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
    s.on_track = on_track;
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

// The market's lock. 1.1.3: each time it is taken counts as a change to the market (on purpose, the safe side), so a
// game save with nothing taken since the last write skips building the JSON (save_state). A handler that never changes
// what is saved takes quiet() instead; once it changes the market, it must take g_lock itself.
struct StateLock {
    std::recursive_mutex m;
    std::atomic<uint64_t> changes{0};
    void lock()
    {
        m.lock();
        changes.fetch_add(1, std::memory_order_relaxed);
    }
    bool try_lock()
    {
        if (!m.try_lock())
            return false;
        changes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    void unlock() { m.unlock(); }
    std::recursive_mutex &quiet() { return m; }
    void touch() { changes.fetch_add(1, std::memory_order_relaxed); } // a change made under quiet()
};
StateLock g_lock;

// 1.1.3: how long the work on the game's thread takes, part by part. One line in the log only when the whole is slow
// enough to be felt ("[silly-season] timing: team hq 74 ms (ratings 20, texts 31, ...)"; tools/swperf.py shows them)
class Timing {
public:
    Timing(const char *what, double over_ms) : what_(what), over_(over_ms) {}
    Timing(const Timing &) = delete;
    Timing &operator=(const Timing &) = delete;
    ~Timing()
    {
        double total = ms(start_, Clock::now());
        if (total >= over_)
            log("[silly-season] timing: %s %.0f ms (%s)", what_, total, parts_.empty() ? "-" : parts_.c_str());
    }
    // the part since the last mark (or the start), named when it took 1 ms or more
    void mark(const char *part)
    {
        Clock::time_point now = Clock::now();
        double t = ms(last_, now);
        last_ = now;
        if (t >= 1.0) {
            char b[96];
            std::snprintf(b, sizeof b, "%s%s %.0f", parts_.empty() ? "" : ", ", part, t);
            parts_ += b;
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
    const char *what_;
    double over_;
    Clock::time_point start_ = Clock::now(), last_ = start_;
    std::string parts_;
};

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
int g_weekend_raced = 0;       // the race weekend the sessions since the last hub belonged to (note_weekend)
int g_weekend_season = -1;     // its season index (-1: not known)
// The weekend only counts once its race is run: practice or qualifying alone doesn't. 1.1.3 (15 Nov): a weekend whose
// sessions never said their type counts only once the calendar moves on.
bool g_race_ended = false;     // a race session ended since the last hub
bool g_session_typed = false;  // a session since the last hub said its type
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

// 1.1.3: the list is read once and kept here. A storage read costs some 8 ms on the game's thread, and a new career
// saved under its provisional id read the list at every save (Team HQ, the game's saves, leaving the career). Only
// this addon writes the file. g_lock held for all of these.
json::Value g_provisional;
bool g_provisional_read = false;

json::Value read_provisional()
{
    if (!g_provisional_read && storage::available()) {
        g_provisional_read = true;
        g_provisional = json::Value::array();
        std::string text;
        storage::Result r = storage::read(kProvisionalList, text);
        if (r == storage::Result::ok) {
            json::Error err;
            json::Value v = json::parse(text, &err);
            if (v.is_array())
                g_provisional = std::move(v);
        }
        log("[silly-season] provisional markets: %zu (%s)", g_provisional.size(), storage::name(r));
    }
    return g_provisional.is_array() ? g_provisional : json::Value::array();
}

void write_provisional(const json::Value &list)
{
    if (!storage::available())
        return;
    g_provisional = list;
    g_provisional_read = true;
    storage::write(kProvisionalList, json::dump(list));
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

// 1.1.3 (15 Nov): the human's driver and team as the core's career state says them (stat ids; false: not known)
bool career_player(uint32_t &driver, uint32_t &team)
{
    career::State cs{};
    if (career::state(&cs) != career::Result::ok || !cs.has_player)
        return false;
    driver = cs.player_driver >> 16;
    team = cs.player_team >> 16;
    return true;
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
    // 1.1.3 (15 Nov): the human's driver and team, so a later load adopts it only for the same career
    if (uint32_t pd = 0, pt = 0; career_player(pd, pt)) {
        e["player_stat"] = json::Value(static_cast<int64_t>(pd));
        e["team_stat"] = json::Value(static_cast<int64_t>(pt));
    }
    e["season_index"] = json::Value(static_cast<int64_t>(g_state.season_index));
    e["round"] = json::Value(static_cast<int64_t>(g_state.round));
    e["saved"] = json::Value(static_cast<int64_t>(std::time(nullptr)));
    out.push_back(std::move(e));
    while (out.if_array()->size() > 8)
        out.if_array()->erase(out.if_array()->begin());
    write_provisional(out);
}

// 1.1.3 (15 Nov): a copy the market no longer needs (a provisional id's, or one it adopted): it is removed, with its
// entry in the provisional list, only once the market's copy under its own id has been written and read back. A
// failed write keeps it: the next save tries again, and a load before that still finds it.
std::string g_retire_after_save;

std::string market_id_now(); // 1.1.51: the core's id, a tracker key mapped to the career's save slot (below)

// g_lock held: the loaded career's id changed (a new career's first save): the market follows it
void follow_career_id()
{
    if (!g_loaded)
        return;
    std::string now = market_id_now();
    if (now.empty() || now == g_state.career || g_state.career.empty())
        return;
    std::string old = g_state.career;
    g_state.career = now;
    log("[silly-season] career id %s -> %s: the market moves to the new id", old.c_str(), now.c_str());
    if (provisional_id(old))
        g_retire_after_save = old; // removed once the new id's copy reads back (save_state)
}

// the copy under `career` reads back as this market's: its own id and this revision (storage first, else the career
// data)
bool reads_back(const std::string &career, uint64_t revision)
{
    std::string text;
    json::Value v;
    if (storage::available() && storage::read(storage_path(career), text) == storage::Result::ok) {
        json::Error err;
        v = json::parse(text, &err);
    } else if (career::saves::available()) {
        if (auto stored = career::saves::data(kAddon).get(career))
            v = stored->value;
    }
    return v.is_object() && v["career"].as_string() == career && static_cast<uint64_t>(v["revision"].as_int(-1)) == revision;
}

// the last state written, for this career: an unchanged state isn't written again
std::string g_saved_career, g_saved_text;
uint64_t g_saved_changes = 0; // g_lock.changes when the state was last written (or found unchanged)
int g_saves_written = 0, g_saves_same = 0, g_saves_skipped = 0; // this career's, logged when it unloads

// only_if_changed: the game's own saves (1.1.3). With g_lock not taken since the last write, nothing changed and the
// JSON isn't even built; Team HQ and leaving the career always compare the whole state.
void save_state(const char *why, bool only_if_changed = false)
{
    if (!g_loaded)
        return;
    Timing tm("save", 10);
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
    tm.mark("career id");
    if (only_if_changed && g_state.career == g_saved_career && g_lock.changes.load() == g_saved_changes) {
        ++g_saves_skipped;
        return;
    }
    json::Value v = ss::to_json(g_state);
    tm.mark("to_json");
    std::string text = ss::dump_save(v); // 1.1.3: compact, floats in their shortest text (some 15% smaller in all)
    tm.mark("dump");
    // the game saves the profile often (menus, sessions): an unchanged state is already in storage and in
    // the core's career data, which the core writes with every save by itself
    if (g_state.career == g_saved_career && text == g_saved_text) {
        ++g_saves_same;
        g_saved_changes = g_lock.changes.load();
        return;
    }
    // 1.1.3 (15 Nov): a new revision for a copy that changed. Its field is near the top of the text: it is changed
    // there rather than the whole tree dumped again.
    {
        std::string was = "\"revision\":" + std::to_string(g_state.revision);
        g_state.revision += 1;
        size_t at = text.find(was);
        if (at != std::string::npos && at < 64) {
            text.replace(at, was.size(), "\"revision\":" + std::to_string(g_state.revision));
        } else {
            v["revision"] = static_cast<int64_t>(g_state.revision);
            text = ss::dump_save(v);
        }
    }
    storage::Result sr = storage::available() ? storage::write(storage_path(g_state.career), text)
                                              : storage::Result::unavailable;
    tm.mark("storage");
    // the career data takes the text just built: the SDK's Data::set would dump the whole tree a second time (and
    // with the floats at 17 digits). The table call is what that wrapper makes.
    career::Result cr = career::Result::unavailable;
    if (const SwoftyCareerSaves *t = career::detail::live_saves(); t && t->data_set)
        cr = career::detail::ops_result(t->data_set(g_state.career.c_str(), kAddon, 1, {text.data(), text.size()}));
    tm.mark("career data");
    log("[silly-season] saved %s (%s): storage %s, career data %s, %zu bytes", g_state.career.c_str(), why,
        storage::name(sr), career::name(cr), text.size());
    if (sr == storage::Result::ok && provisional_id(g_state.career))
        note_provisional();
    tm.mark("provisional list");
    if (sr == storage::Result::ok && (cr == career::Result::ok || !career::saves::available())) {
        ++g_saves_written;
        g_saved_career = g_state.career;
        g_saved_text = std::move(text);
        g_saved_changes = g_lock.changes.load();
    } else {
        g_saved_career.clear(); // try both again next time
    }
    // 1.1.3 (15 Nov): the copy this market came from goes only now that its own reads back
    if (!g_retire_after_save.empty() && g_retire_after_save != g_state.career && (sr == storage::Result::ok || cr == career::Result::ok)) {
        if (reads_back(g_state.career, g_state.revision)) {
            if (storage::available())
                storage::remove(storage_path(g_retire_after_save));
            forget_provisional(g_retire_after_save);
            log("[silly-season] career %s: its copy reads back; the one under %s is removed", g_state.career.c_str(),
                g_retire_after_save.c_str());
            g_retire_after_save.clear();
        } else {
            log("[silly-season] career %s: its copy doesn't read back yet; the one under %s is kept", g_state.career.c_str(),
                g_retire_after_save.c_str());
        }
    }
}

// 1.1.3 (15 Nov): both copies are read (the career data and the storage). One that names another career is never
// taken; of the two that are this career's, the higher revision is the newer (a write that failed on one side left
// the other ahead). A save from before 1.1.3 has no revision (0) and its career field is still checked; one without a
// career at all is taken as it always was.
bool load_saved(const std::string &career)
{
    struct Copy {
        ss::State st;
        bool ok = false;
        const char *from = "";
    } data, store;
    auto take = [&](Copy &c, const json::Value &v, const char *from) {
        ss::State st;
        if (!ss::from_json(st, v))
            return;
        if (!st.career.empty() && st.career != career) {
            log("[silly-season] career %s: the copy in the %s is career %s's; it is left alone", career.c_str(), from,
                st.career.c_str());
            return;
        }
        c.st = std::move(st);
        c.ok = true;
        c.from = from;
    };
    if (career::saves::available())
        if (auto stored = career::saves::data(kAddon).get(career))
            take(data, stored->value, "career data");
    std::string text;
    if (storage::available() && storage::read(storage_path(career), text) == storage::Result::ok) {
        json::Error err;
        take(store, json::parse(text, &err), "storage");
    }
    if (!data.ok && !store.ok)
        return false;
    // the career data wins a tie, as before
    Copy &pick = !store.ok ? data : !data.ok ? store : store.st.revision > data.st.revision ? store : data;
    if (data.ok && store.ok && data.st.revision != store.st.revision)
        log("[silly-season] career %s: the career data holds revision %llu and the storage %llu; the newer (%s) is taken",
            career.c_str(), static_cast<unsigned long long>(data.st.revision), static_cast<unsigned long long>(store.st.revision),
            pick.from);
    g_state = std::move(pick.st);
    log("[silly-season] career %s: state from the %s (season %d, round %d, revision %llu)", career.c_str(), pick.from,
        g_state.year, g_state.round, static_cast<unsigned long long>(g_state.revision));
    return true;
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

// 1.1.3 (15 Nov): a provisional market is taken only when it can be this career's: the same data group, the same
// human (driver and team, when the list knows them), and not ahead of the game's calendar. Two that could both be (two
// new careers of the same kind left before their first save) are left alone: guessing could give a career another's.
bool adopt_provisional(const std::string &id, const std::string &group)
{
    json::Value list = read_provisional();
    uint32_t pd = 0, pt = 0;
    bool know_player = career_player(pd, pt);
    auto pos = career::current_round();
    std::vector<std::string> fits;
    std::string from;
    int64_t newest = -1;
    for (const json::Value &e : *list.if_array()) {
        std::string eid(e["id"].as_string());
        if (e["group"].as_string() != group || eid == id)
            continue;
        if (know_player && e["player_stat"].is_number() &&
            (static_cast<uint32_t>(e["player_stat"].as_int()) != pd || static_cast<uint32_t>(e["team_stat"].as_int()) != pt))
            continue; // another human: another career
        if (pos) {
            int si = static_cast<int>(e["season_index"].as_int(-1)), rd = static_cast<int>(e["round"].as_int(0));
            if (si > pos->season_index || (si == pos->season_index && rd > pos->round))
                continue; // ahead of this career's calendar
        }
        fits.push_back(eid);
        if (e["saved"].as_int() > newest) {
            newest = e["saved"].as_int();
            from = eid;
        }
    }
    if (fits.size() > 1) {
        std::string all;
        for (const std::string &f : fits)
            all += (all.empty() ? "" : ", ") + f;
        log("[silly-season] career %s: %zu provisional markets could be this career's (%s); none is taken", id.c_str(),
            fits.size(), all.c_str());
        return false;
    }
    if (from.empty())
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
#if defined(SILLY_SEASON_FIXED_SEED)
    return h; // plugin_test: the same career id gives the same market every run
#else
    return h ^ static_cast<uint64_t>(GetTickCount64());
#endif
}

std::string career_id(const events::Context *ctx)
{
    std::string id = career::saves::current();
    if (id.empty() && ctx && ctx->career())
        id = ctx->career()->id;
    return id;
}

// ---------------------------------------------------------------------------------------------------
// 1.1.51: careers loaded with Continue. With no save slot picked before the load (Continue on the main menu), the core
// names the career by the career tracker's key for the whole session: "dc-00070000" (the player's driver) or
// "mt-<team>-<driver>" in My Team. Every career of that driver shares the key, and the same career loaded from the save
// list is named by its slot ("f1_career_2025.driver_career:748422188a9baea0"): two careers of one driver shared one
// market, and one career loaded both ways had two. The market is kept under the save slot's id instead, found in the
// core's career index: the slot of that driver (and team) in the same profile, present, played last. (F2 Revamp 0.9.2
// does the same for its career data.)
// A new career has no slot until its first save: it runs under "<key>:new" (a provisional market) until the index
// lists a slot of its driver that wasn't there when it started.

bool tracker_key(std::string_view id) { return id.rfind("dc-", 0) == 0 || id.rfind("mt-", 0) == 0; }

std::string g_slot_raw;              // the tracker key the core names the loaded career by
std::string g_slot_id;               // the save slot's id the market is kept under ("": none found)
bool g_slot_waiting = false;         // a new career: its slot is looked for at each save
std::set<std::string> g_slots_before; // a new career: the driver's save slots when it started

void slot_reset()
{
    g_slot_raw.clear();
    g_slot_id.clear();
    g_slot_waiting = false;
    g_slots_before.clear();
}

json::Value raw_index()
{
    const SwoftyCareerSaves *t = career::detail::live_saves();
    if (!t || !t->index)
        return json::Value();
    std::string text = career::detail::grow([&](char *buf, size_t cap, size_t *len) { return t->index(buf, cap, len); });
    json::Error err;
    return text.empty() ? json::Value() : json::parse(text, &err);
}

// the driver and team a tracker key names (the game's raw ids, stat << 16), as the index's driver_raw and team_raw
struct TrackerWho {
    bool ok = false, my_team = false;
    int64_t driver = -1, team = -1;
};

TrackerWho tracker_who(const std::string &key)
{
    TrackerWho w;
    auto hex = [](std::string_view s, int64_t &out) {
        uint32_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v, 16);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size() || s.size() != 8)
            return false;
        out = static_cast<int64_t>(v >> 16);
        return true;
    };
    std::string_view k(key);
    if (k.rfind("dc-", 0) == 0) {
        w.ok = hex(k.substr(3), w.driver);
    } else if (k.rfind("mt-", 0) == 0 && k.size() == 3 + 8 + 1 + 8 && k[11] == '-') {
        w.my_team = true;
        w.ok = hex(k.substr(3, 8), w.team) && hex(k.substr(12), w.driver);
    }
    return w;
}

struct SlotPick {
    std::string id, slot, profile;
    int fits = 0;
};

// the save slot a tracker key's career is in: same profile, same driver (and team in My Team), present and listed, not
// another data group's, not in `skip`; of several, the one played last (Continue loads the last career played)
SlotPick pick_slot(const json::Value &index, const std::string &key, const std::string &group,
                   const std::set<std::string> *skip, std::set<std::string> *all = nullptr)
{
    SlotPick pick;
    TrackerWho who = tracker_who(key);
    const json::Array *list = index["careers"].if_array();
    if (!who.ok || !list)
        return pick;
    for (const json::Value &e : *list)
        if (e["career"].as_string() == key)
            pick.profile = std::string(e["profile"].as_string());
    std::string kind = who.my_team ? "my_team" : "driver_career";
    int64_t newest = -1;
    for (const json::Value &e : *list) {
        std::string slot(e["slot"].as_string());
        if (slot.empty() || e["status"].as_string() != "present" || e["mode"].as_string() != kind || e["hidden"].as_bool())
            continue;
        if (!pick.profile.empty() && e["profile"].as_string() != pick.profile)
            continue;
        if (e["driver_raw"].as_int(-2) != who.driver || (who.my_team && e["team_raw"].as_int(-2) != who.team))
            continue;
        if (all)
            all->insert(slot);
        std::string_view content = e["content"].as_string();
        if (!content.empty() && content != group)
            continue;
        if (skip && skip->count(slot))
            continue;
        ++pick.fits;
        int64_t played = e["last_played"].as_int(0);
        if (played > newest) {
            newest = played;
            pick.slot = slot;
            pick.id = std::string(e["career"].as_string());
        }
    }
    if (pick.slot.empty())
        return pick;
    // a slot the core has only listed is named "<mode>:<slot>"; once loaded from the save list it is
    // "<the data group's save container, else <group>.<mode>>:<slot>", the name the market must use. Another career
    // of this data group loaded from its slot gives that name; else the core's default.
    if (pick.id == kind + ":" + pick.slot) {
        std::string prefix = group + "." + kind;
        for (const json::Value &e : *list) {
            std::string id(e["career"].as_string()), slot(e["slot"].as_string());
            if (e["content"].as_string() != group || e["mode"].as_string() != kind || slot.empty() ||
                id == kind + ":" + slot || provisional_id(id) || tracker_key(id) || id.size() <= slot.size() + 1 ||
                id.compare(id.size() - slot.size() - 1, std::string::npos, ":" + slot) != 0)
                continue;
            prefix = id.substr(0, id.size() - slot.size() - 1);
            break;
        }
        pick.id = prefix + ":" + pick.slot;
    }
    return pick;
}

// g_lock held: the id the market of the career just loaded is kept under (the career's own id unless the core names it
// by the career tracker's key)
std::string market_id(const std::string &raw, const std::string &group, bool fresh)
{
    // created, then loaded: the new career keeps the id it started with (its Loaded must not look for a slot: the
    // driver's newest slot is another career's until this one saves)
    if (g_loaded && !raw.empty() && raw == g_slot_raw)
        return g_state.career;
    slot_reset();
    if (!tracker_key(raw))
        return raw;
    g_slot_raw = raw;
    const json::Value index = raw_index();
    if (!index["careers"].is_array()) {
        log("[silly-season] career %s: no save slot was picked before this load and the core's career index can't be "
            "read; the market stays under this id", raw.c_str());
        return raw;
    }
    if (fresh) {
        std::set<std::string> before;
        pick_slot(index, raw, group, nullptr, &before);
        g_slots_before = std::move(before);
        g_slot_waiting = true;
        log("[silly-season] career %s: a new career named by the career tracker's key (%zu other save slot(s) of this "
            "driver); its market moves to its own save slot once the game has saved it", raw.c_str(), g_slots_before.size());
        return raw + ":new";
    }
    SlotPick p = pick_slot(index, raw, group, nullptr);
    if (p.id.empty()) {
        log("[silly-season] career %s: no save slot was picked before this load and the core's index has no slot of this "
            "driver; the market stays under this id", raw.c_str());
        return raw;
    }
    g_slot_id = p.id;
    log("[silly-season] career %s (loaded with Continue: no save slot picked): the market is kept under its save slot "
        "%s (%d save slot(s) of this driver in profile '%s')", raw.c_str(), p.id.c_str(), p.fits, p.profile.c_str());
    return p.id;
}

// g_lock held, at each save: the id the core gives now, mapped to the market's (follow_career_id)
std::string market_id_now()
{
    std::string now = career::saves::current();
    if (now.empty() || now != g_slot_raw)
        return now;
    if (!g_slot_id.empty())
        return g_slot_id;
    if (g_slot_waiting) {
        SlotPick p = pick_slot(raw_index(), g_slot_raw, g_state.group, &g_slots_before);
        if (p.id.empty())
            return g_state.career; // not saved yet: the market stays where it is
        g_slot_waiting = false;
        g_slot_id = p.id;
        log("[silly-season] career %s: the new career's save slot is %s", g_slot_raw.c_str(), p.id.c_str());
        return p.id;
    }
    return now;
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

int human_team_stat(); // the human's team stat id from the career's rows; -1 when they can't be read (below)

// F2 Revamp's F2 teams: the player is in F2 while he races for one (the same test its own service makes)
constexpr int kF2TeamFirst = 489, kF2TeamLast = 499;

// the loaded F2 Revamp career is still in its F2 season. F2 Revamp's service says so when it publishes it (0.8.2 on);
// 1.1.3: without it (0.8.1, the version in the catalogue for a while) the player's team says it. 1.1.2 waited for the
// service forever, so after the F2 season no Silly Season market ever started (two players' reports, 11 Nov).
bool f2_phase()
{
    if (const F2RevampCareer *c = f2_service(); c && c->in_f2)
        return c->in_f2() != 0;
    int team = human_team_stat();
    if (team < 0) {
        career::State cs{};
        if (career::state(&cs) == career::Result::ok && cs.loaded && cs.has_player)
            team = static_cast<int>(cs.player_team >= 0x10000 ? cs.player_team >> 16 : cs.player_team);
    }
    return team < 0 || (team >= kF2TeamFirst && team <= kF2TeamLast); // unknown: wait for the next Team HQ
}

void begin_market(const std::string &group, const std::string &id, bool fresh);
namespace game_pool {
void start(const std::string &group); // game_pool.inl: read the career's driver lists in the background
}
void start_db_labels(); // the game database's driver labels, read in the background (below)

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
        int team = human_team_stat();
        log("[silly-season] F2 Revamp career: the player is in F2 (%s%s); the market starts with the player's first F1 "
            "season", f2_service() ? "F2 Revamp's service says so" : "his team says so: F2 Revamp publishes no service",
            team < 0 ? ", his row not read yet" : "");
        return;
    }
    begin_market(group, market_id(career_id(&ctx), group, fresh), fresh);
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
    g_ov_generation.fetch_add(1); // 1.1.3: the overlays start this career afresh
    if (!g_real_career)
        game_pool::start(group); // ready by the first Team HQ, which then doesn't wait for it
    start_db_labels();
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


// returns the new season's seats ("season") the game refused, for the caller to take back once the grid is read again
// results: when given, each change's result in order (1.1.3: the new season's seats are "requested" on ok)
std::vector<ss::SeatChange> apply_changes(const std::vector<ss::SeatChange> &changes, const char *why,
                                          std::vector<career::Result> *results = nullptr)
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
        if (results)
            results->push_back(r);
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
    const ss::Team *t = g_state.team(team);
    // 1.1.3: a stock driver is the player only when the market already has him in the player's team. A created
    // driver's career state can carry another driver's stat id (Felipe Massa's, a legend's): 1.1.2 made the player
    // that driver for good when the first visit couldn't read the career's rows (players' reports, 11 Nov)
    if (!driver.empty() && !(t && (t->seats[0] == driver || t->seats[1] == driver))) {
        if (g_player_log_budget-- > 0)
            log("[silly-season] player: the career state names %s (stat %u), who doesn't race for %s; he is a created driver",
                driver.c_str(), driver_stat, team.c_str());
        driver.clear();
    }
    if (driver.empty()) {
        const ss::Driver *was = g_state.driver(g_state.player);
        driver = g_state.player.empty() || (was && was->kind != "player") ? std::string("player") : g_state.player; // created
    }
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

constexpr uint32_t kNoLook = 0xffffffffu;

struct RealSeat {
    uint32_t driver = 0, team = 0; // stat ids
    int seat = 0;
    bool human = false;
    uint32_t look = kNoLook; // 1.1.3: the look the row dresses him in (its stat id; kNoLook when the row has none)
};

// history mode (history.inl). 1.1.3: a row holds the driver at +0x50 and his look at +0x54, each its own stat id
// (Progressive's dump: "seat sw16_rosberg(238) ... look sw16-nico_rosberg(1861)"). 1.1.0-1.1.2 matched the rows'
// drivers against the looks of years.json, which never agree, so no real year was ever recognised.
std::unordered_map<uint32_t, std::string> g_real_stats;    // a driver's stat id in Progressive's pool -> the driver
std::unordered_map<uint32_t, std::string> g_real_look_ids; // a look's stat id (any year's, any change's) -> the driver
std::unordered_map<uint32_t, uint32_t> g_row_look;         // each career row's driver stat id -> his look's, last read
std::vector<RealSeat> g_last_seats;                        // the rows last read, to tell the year from them
std::string g_last_seats_owner; // 1.1.3: the career (its id and entity) whose rows g_last_seats holds
std::vector<ss::GridCar> g_last_cars;   // 1.1.3: the cars the market took from the last read of the rows
// 1.1.0 phase 5: each driver's career row as the game holds it (stat id -> S race seat, F free agent, R reserve,
// M more than one row), from the last read
std::unordered_map<uint32_t, char> g_row_class;
std::unordered_map<uint32_t, std::string> g_row_raw; // 1.1.2: each driver's row as read (hex), by stat id

namespace rows {
constexpr uintptr_t kEntrantsBegin = 0x2e0, kEntrantsEnd = 0x2e8, kBusy = 0x2f8; // CareerEntity
constexpr size_t kSize = 0x88, kOrdinal = 0x48, kDriver = 0x50, kLook = 0x54, kTeam = 0x58, kVehicle = 0x5c, kPosition = 0x80;
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

int human_team_stat()
{
    uintptr_t e = reinterpret_cast<uintptr_t>(career::entity());
    uintptr_t begin = 0, end = 0;
    uint8_t busy = 0;
    if (!e || !rows::copy(&begin, e + rows::kEntrantsBegin, sizeof begin) || !rows::copy(&end, e + rows::kEntrantsEnd, sizeof end) ||
        !rows::copy(&busy, e + rows::kBusy, sizeof busy) || busy || !begin || end <= begin || (end - begin) % rows::kSize ||
        (end - begin) / rows::kSize > rows::kMaxRows)
        return -1;
    std::vector<uint8_t> buf(end - begin);
    if (!rows::copy(buf.data(), begin, buf.size()))
        return -1;
    for (size_t off = 0; off < buf.size(); off += rows::kSize)
        if (rows::at<uint32_t>(buf, off + rows::kOrdinal) & rows::kHumanBit) {
            uint32_t team = rows::at<uint32_t>(buf, off + rows::kTeam);
            return (team & 0xffff0000u) == rows::kNoKey ? -1 : static_cast<int>(team >> 16);
        }
    return -1;
}

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
    g_row_raw.clear();
    g_row_look.clear();
    for (size_t off = 0; off < buf.size(); off += rows::kSize) {
        uint32_t ordinal = rows::at<uint32_t>(buf, off + rows::kOrdinal), driver = rows::at<uint32_t>(buf, off + rows::kDriver),
                 team = rows::at<uint32_t>(buf, off + rows::kTeam), vehicle = rows::at<uint32_t>(buf, off + rows::kVehicle),
                 look_key = rows::at<uint32_t>(buf, off + rows::kLook);
        uint16_t position = rows::at<uint16_t>(buf, off + rows::kPosition);
        bool teamless = (team & 0xffff0000u) == rows::kNoKey, car = (vehicle & 0xffff0000u) != rows::kNoKey;
        int seat = (position >> 8) ? (position & 0xff) : -1;
        // the look is a key like the driver's (stat << 16); none, or not a key, is no look
        uint32_t look = (look_key & 0xffffu) == 0 && (look_key & 0xffff0000u) != rows::kNoKey ? look_key >> 16 : kNoLook;
        if ((driver & 0xffu) == 0 && look != kNoLook)
            g_row_look[driver >> 16] = look;
        // what the game makes of each driver for a seat change (the core's set_seat): a race driver ('S') and a free
        // agent ('F') take a seat; a reserve ('R': a team, no race seat) and a driver with two rows ('M') don't
        if ((driver & 0xffu) == 0) {
            char cls = car && !teamless && seat >= 0 && seat <= 1 ? 'S' : teamless ? 'F' : 'R';
            auto [it, fresh] = g_row_class.emplace(driver >> 16, cls);
            if (!fresh)
                it->second = 'M';
            // 1.1.2: the whole row, for the log line of a driver who leaves the grid (what a retirement looks like)
            char hex[rows::kSize * 2 + 1];
            for (size_t b = 0; b < rows::kSize; ++b)
                std::snprintf(hex + b * 2, 3, "%02x", buf[off + b]);
            g_row_raw[driver >> 16] = hex;
        }
        if (teamless || !car || seat < 0 || seat > 1)
            continue; // a reserve, a free agent or a driver without a car
        RealSeat s{driver >> 16, team >> 16, seat, (ordinal & rows::kHumanBit) != 0, look};
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

// The driver labels of the game database (entrants.drivers), for drivers the baseline doesn't know (another addon's).
// 1.1.3: read once in the background, started with the career's market. 1.1.2 read the whole table on the game's
// thread at the first lookup, in Team HQ, and with many addons the table is long (a 2.1 s freeze in a player's
// report). Until it is ready a lookup answers "" (a driver added meanwhile is named once it is: driver_for_stat).
namespace db_labels {
std::mutex lock;
std::unordered_map<uint32_t, std::string> labels;
std::atomic<int> state{0}; // 0 not started, 1 being read, 2 ready

void read()
{
    std::unordered_map<uint32_t, std::string> found;
    if (db::available()) {
        db::Cursor c{};
        for (db::Result r = db::open("entrants.drivers", &c); r == db::Result::ok; r = db::step(&c)) {
            db::Value v{};
            const char *label = db::row_name(&c);
            if (label && db::cell(&c, "stat_id", &v) == db::Result::ok)
                found.emplace(static_cast<uint32_t>(v.i), label);
        }
    }
    {
        std::lock_guard l(lock);
        labels = std::move(found);
    }
    state = 2;
    log("[silly-season] game database: %zu driver label(s) in entrants.drivers (read in the background)", labels.size());
}

void start()
{
    int none = 0;
    if (db::available() && state.compare_exchange_strong(none, 1))
        std::thread(read).detach();
}
} // namespace db_labels

void start_db_labels() { db_labels::start(); }

std::string db_label(uint32_t stat)
{
    if (db_labels::state.load() != 2) {
        db_labels::start();
        return {};
    }
    std::lock_guard l(db_labels::lock);
    auto it = db_labels::labels.find(stat);
    return it == db_labels::labels.end() ? std::string() : it->second;
}

// "2025-williams_carlos_sainz-jr" or "carlos_sainz-jr" -> "Carlos Sainz Jr"
std::string name_from_label(std::string label)
{
    // 1.1.3: a name's accented variant another addon adds ("noel_leon-accent" for Noel León) is the same driver: the
    // suffix isn't part of his name (F2 Revamp's test showed "Noel Leon Accent" in the news)
    for (const char *tail : {"-accent", "_accent", "-accented", "_accented"}) {
        size_t n = std::strlen(tail);
        if (label.size() > n && label.compare(label.size() - n, n, tail) == 0) {
            label.resize(label.size() - n);
            break;
        }
    }
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
    // a Progressive career: each real year dresses its drivers in looks of their own, with their own stat ids. 1.1.3:
    // the row's look names the driver (the season addons' own drivers, sw16_rosberg, get a stat id from the launcher
    // that years.json can't know), then Progressive's pool by the driver's own stat id
    if (g_real_career) {
        if (auto row = g_row_look.find(stat); row != g_row_look.end())
            if (auto it = g_real_look_ids.find(row->second); it != g_real_look_ids.end() && g_state.driver(it->second))
                return it->second;
        if (auto it = g_real_stats.find(stat); it != g_real_stats.end() && g_state.driver(it->second))
            return it->second;
    }
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
bool g_grid_read_now = false;     // 1.1.3: this Team HQ visit read the game's rows (they can tell the real year)
// 1.1.3 (15 Nov): and the grid they make passed every check (the teams this career's, the market synced to it): only
// then may seats be written. A read rejected for its unknown teams left the writes allowed before.
bool g_grid_valid_now = false;

// 1.1.3: whose rows a read belongs to: the loaded career's id and its entity. Rows read for another career (the one
// before, with the game still filling the new one's) never tell this one's real year.
std::string rows_owner()
{
    char b[40];
    std::snprintf(b, sizeof b, "|%p", career::entity());
    return g_state.career + b;
}
int g_dup_logged = 0;              // 1.1.2: times the player's row named another car's driver (logged 3 times)
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
    // Progressive: a driver the market knows by his row's look (a season addon's own, with a stat id from the launcher)
    if (g_real_career)
        for (const auto &[stat, look] : g_row_look) {
            auto it = g_real_look_ids.find(look);
            const ss::Driver *d = it == g_real_look_ids.end() ? nullptr : g_state.driver(it->second);
            if (d && static_cast<uint32_t>(d->stat) != stat)
                take(it->second, stat);
        }
    g_state.game_rows_known = true;
    char said[200];
    std::snprintf(said, sizeof said, "%zu race drivers and %zu free agents can take a seat; %zu are reserves in the game, %zu have no single row",
                  seated, free_agents, reserves, none);
    if (said != g_rows_said) {
        g_rows_said = said;
        log("[silly-season] career rows (%zu): %s", g_row_class.size(), said);
    }
}

// 1.1.2: an id the market keeps for the player that is really another car's driver (a save of 1.1.1 or earlier
// where the player took the team-mate's identity)
bool shared_player(const std::string &id, const std::vector<RealSeat> &seats)
{
    return std::any_of(seats.begin(), seats.end(), [&](const RealSeat &o) { return !o.human && known_driver(o.driver) == id; });
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
    g_grid_read_now = true;
    g_grid_valid_now = false; // set at the end, once the grid passed every check
    g_last_seats = seats;
    g_last_seats_owner = rows_owner();
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
            // 1.1.2: the human's row can name the same driver as another car's row (a created driver in a 2025
            // career): then he is a driver of his own, never the team-mate, or the hub showed "You" in both seats
            bool shared = std::any_of(seats.begin(), seats.end(), [&](const RealSeat &o) {
                return !o.human && (o.driver == s.driver || (!driver.empty() && known_driver(o.driver) == driver));
            });
            if (shared) {
                if (g_dup_logged++ < 3)
                    log("[silly-season] real grid: the player's row names %s (stat %u), as another car does; he is his own driver",
                        driver.empty() ? "nobody" : driver.c_str(), s.driver);
                driver.clear();
            }
            if (driver.empty()) {
                // a created driver keeps the market's own id for him; 1.1.3: never a stock driver's id the market
                // borrowed for him earlier (a save of 1.1.2 or before where he became Felipe Massa): he is "player"
                // again, and the borrowed driver is himself once more
                const ss::Driver *was = g_state.driver(g_state.player);
                bool borrowed = was && was->kind != "player" && static_cast<uint32_t>(was->stat) != s.driver;
                if (borrowed && g_dup_logged++ < 3)
                    log("[silly-season] real grid: the player went by %s, another driver's identity; he is his own driver again",
                        g_state.player.c_str());
                driver = g_state.player.empty() || !was || borrowed || shared_player(g_state.player, seats) ? std::string("player")
                                                                                                          : g_state.player;
            }
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
        cars.push_back({driver, team, s.human, s.seat}); // with the game's seat index: seats are written by it
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
    for (const auto &[id, d] : g_state.drivers)
        if (!d.team.empty())
            before[id] = d.team;
    ss::sync_grid(g_state, cars);
    g_last_cars = cars; // 1.1.3: the game's grid as read, for the market to go back to when a write can't be checked
    // 1.1.2: a driver who left the grid since the last read, as the game holds him now (a retired driver may keep
    // his row: what tells him apart is what this line is for). A handful per read, and only when the grid is whole.
    if (cars.size() >= 16) {
        int told = 0;
        for (const auto &[id, team] : before) {
            const ss::Driver *d = g_state.driver(id);
            if (!d || !d->team.empty() || id == g_state.player || told >= 6)
                continue;
            ++told;
            uint32_t stat = static_cast<uint32_t>(d->stat);
            auto cls = g_row_class.find(stat);
            auto raw = g_row_raw.find(stat);
            log("[silly-season] real grid: %s (stat %u) left %s; career row: %s%s%s", id.c_str(), stat, team.c_str(),
                cls == g_row_class.end() ? "none" : std::string(1, cls->second).c_str(), raw == g_row_raw.end() ? "" : ", ",
                raw == g_row_raw.end() ? "" : raw->second.c_str());
        }
    }
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
    g_grid_valid_now = true;
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
    // 1.1.3: the drivers the game doesn't rate join its scale
    float shift = ss::calibrate_ratings(g_state);
    char said[360];
    std::snprintf(said, sizeof said, "%zu entries read, %zu drivers of the market take them, the rest shifted by %+.1f%s%s",
                  list.size(), taken, shift, kinds ? ", some entries of another kind" : "", player_line.c_str());
    if (said != g_ratings_said) {
        g_ratings_said = said;
        log("[silly-season] game ratings: %s", said);
    }
}

// 1.1.3: what the game's telemetry saw in this weekend's races (overtakes, collisions, retirements), kept until the
// next idle Team HQ hands them to the market. g_lock held for all of it.
struct PendingTrack {
    ss::TrackEvent e;
    float time = 0;  // the session clock when it happened
    int session = 0; // the session it happened in (g_track_session)
    // 1.1.3 (15 Nov): the GP it belongs to (the career's season index and round at lights out; -1/0: not known), so
    // two GPs before one Team HQ each go to their own round
    int season = -1, round = 0;
};
int g_track_season_now = -1, g_track_round_now = 0; // the running race's GP, set at lights out
std::set<int> g_track_done; // 1.1.3 (15 Nov): sessions whose race ended normally (not a restart)
std::set<int> g_track_restarted; // 1.1.3 (15 Nov): sessions that ended in a restart: nothing of them counts
// 1.1.3: every event that passed the identity checks, as it came (only an exact repeat of a pair's last event is left
// out at once). What the market keeps is worked out from it when it is read (kept_track): so an event a flashback
// undoes takes back what it caused, an overtake given back included.
std::vector<PendingTrack> g_track;
float g_track_clock = -1;      // 1.1.3: the latest session clock seen in this race (a lower one: a flashback)
// a race is running: set when the race starts (lights out, the core's race.start), cleared when the session ends, a
// session starts or one unloads. The feed's session type can't tell (the UDP 2026 format has no session packet: it
// stays 0), and the view's session type comes as practice in careers.
std::atomic<bool> g_track_race{false};
bool g_track_told_off = false; // the reason no event counts was logged
int g_track_session = 0;       // counts this game run's sessions: a race's events belong to its session
int g_track_started = -1;      // the session whose race last started (lights out twice in one session: a restart)

// The core (1.9) tells an overtake at every change of the classification between two cars on track, so a start, two
// cars swapping places back and forth and cars that are no part of the grid (another addon's entry list can add some)
// made hundreds in a five-lap race. What the market keeps of a race, and why the rest is left out, logged at its end.
struct TrackTally {
    int raw = 0;       // every event the game told while the option is on
    int outside = 0;   // not between lights out and the race's end
    int paused = 0;    // while the game was paused (the pause menu, a flashback being picked)
    int unread = 0;    // the feed couldn't be read
    int unknown = 0;   // a car the market can't tell (no driver id: the game's UDP output is off)
    int strangers = 0; // a car whose driver holds no race seat in the market's grid
    int repeated = 0;  // the same pair again within kTrackPairWindow (or a driver retiring twice)
    int undone = 0;    // an overtake given back within kTrackPairWindow: both are dropped
    int rewound = 0;   // dropped after a flashback or a restart: they never happened
};
TrackTally g_tally;
constexpr float kTrackPairWindow = 30.0f; // seconds of the session clock

// Swofty's shared-memory telemetry feed, read straight. The core's telemetry::session/car/participant reads answered
// unavailable all session long in the game (launcher 1.8.38), while this mapping held every field.
const SwoftyTelemetryFeed *g_feed = nullptr;
// 1.1.3 (15 Nov): a feed that can't be opened yet (the core not ready, a feed that appears late) is tried again, at
// most every 10 s; before, the first failure was the last try of the game run
std::chrono::steady_clock::time_point g_feed_next_try{};
int g_feed_failures = 0;
int g_feed_retry_seconds = 10; // plugin_test sets 0

void open_feed()
{
    if (g_feed)
        return;
    auto now = std::chrono::steady_clock::now();
    if (now < g_feed_next_try)
        return; // tried and failed less than g_feed_retry_seconds ago
    const char *name = nullptr;
    telemetry::Result r = telemetry::shared_memory(&name);
    if (r != telemetry::Result::ok || !name) {
        g_feed_next_try = now + std::chrono::seconds(g_feed_retry_seconds);
        if (++g_feed_failures <= 3)
            log("[silly-season] on track: the telemetry feed can't be opened (%s)%s", telemetry::name(r),
                g_feed_failures == 1 ? "; tried again every 10 s" : "");
        return;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L' ');
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w.data(), n);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, w.c_str());
    void *view = h ? MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(SwoftyTelemetryFeed)) : nullptr;
    g_feed = static_cast<const SwoftyTelemetryFeed *>(view);
    if (!g_feed)
        g_feed_next_try = now + std::chrono::seconds(g_feed_retry_seconds);
    log("[silly-season] on track: telemetry feed %s %s", name, g_feed ? "mapped" : "could not be opened");
}

// one consistent copy of the feed (its sequence counter is odd while the core writes it)
bool feed_snapshot(SwoftyTelemetryFeed &out)
{
    if (!g_feed)
        open_feed(); // 1.1.3 (15 Nov): one that appears late is picked up (at most a try every 10 s)
    if (!g_feed)
        return false;
    for (int i = 0; i < 64; ++i) {
        uint64_t before = reinterpret_cast<const volatile uint64_t &>(g_feed->seq);
        if (before & 1)
            continue;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(&out, g_feed, sizeof out);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (reinterpret_cast<const volatile uint64_t &>(g_feed->seq) == before)
            return out.size == sizeof out;
    }
    return false;
}

// the market's driver for a telemetry participant; "" when it can't be told
std::string track_driver(const SwoftyTelemetryFeed &f, uint32_t index)
{
    if (index >= 24)
        return {};
    // The driver ids come with the game's UDP output. Without it every slot holds zeros, which would read as "a
    // human's car" and "driver 0" (Carlos Sainz's id) at once: then no car can be told.
    int with_id = 0, ai_without = 0;
    for (const SwoftyParticipant &q : f.participants) {
        with_id += q.driver_id != 0;
        ai_without += q.driver_id == 0 && q.ai_controlled;
    }
    if (with_id < 10)
        return {};
    const SwoftyParticipant &p = f.participants[index];
    if (!p.ai_controlled) // the human's car; an empty slot (no id, not the player's index) is nobody's
        return p.driver_id || index == f.session.player_index ? g_state.player : std::string();
    if (p.driver_id)
        return known_driver(p.driver_id);
    // id 0 is Carlos Sainz: his car, when it is the session's only AI car without an id
    return ai_without == 1 ? known_driver(0) : std::string();
}

// g_lock held: the driver races in this career now: a race seat in the market's grid, the player, his own team's
// other driver (My Team) or a driver of a team the market doesn't plan. A free agent or a reserve the game puts on
// track anyway (a car another addon's entry list adds) doesn't: what he does is no part of the season.
bool races_now(const std::string &id)
{
    if (id.empty())
        return false;
    if (id == g_state.player || id == g_state.own_mate || g_state.protected_ids.count(id))
        return true;
    for (const ss::State::Standin &sb : g_state.standins)
        if (sb.in == id)
            return true; // a stand-in: the seat stays the regular driver's in the market, but he is the one racing
    const ss::Driver *d = g_state.driver(id);
    return d && !d->team.empty();
}

// g_lock held: this session's events from `from` on are dropped (a flashback, a restart)
int drop_track_events(float from)
{

    size_t before = g_track.size();
    g_track.erase(std::remove_if(g_track.begin(), g_track.end(),
                                 [&](const PendingTrack &t) { return t.session == g_track_session && t.time > from; }),
                  g_track.end());
    return static_cast<int>(before - g_track.size());
}

// g_lock held: what the market keeps of the events (one session, or all with session < 0). In a session, the same two
// cars within kTrackPairWindow are one fight (a collision told from either car, an overtake repeated), an overtake
// given back within it cancels both, and a driver retires once.
std::vector<PendingTrack> kept_track(int session, int *repeated = nullptr, int *undone = nullptr)
{
    std::vector<PendingTrack> kept;
    int rep = 0, und = 0;
    for (const PendingTrack &t : g_track) {
        if (session >= 0 && t.session != session)
            continue;
        bool skip = false;
        for (size_t i = kept.size(); i-- > 0;) {
            const PendingTrack &p = kept[i];
            if (p.session != t.session || t.time - p.time > kTrackPairWindow)
                break; // older events are further back still
            if (p.e.kind != t.e.kind)
                continue;
            bool same = p.e.a == t.e.a && p.e.b == t.e.b, mirrored = p.e.a == t.e.b && p.e.b == t.e.a;
            if (t.e.kind == "overtake" && mirrored) {
                kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(i)); // given back: neither overtake stands
                und += 2;
                skip = true;
                break;
            }
            if (same || (t.e.kind == "collision" && mirrored)) {
                ++rep;
                skip = true;
                break;
            }
        }
        if (!skip && t.e.kind == "retirement")
            for (const PendingTrack &p : kept)
                if (p.session == t.session && p.e.kind == "retirement" && p.e.a == t.e.a) {
                    ++rep;
                    skip = true;
                    break;
                }
        if (!skip)
            kept.push_back(t);
    }
    if (repeated)
        *repeated = rep;
    if (undone)
        *undone = und;
    return kept;
}

// the race is over (its session ended or unloaded): what the telemetry told and what the market keeps, once a race
// 1.1.3 (15 Nov): the feed tells who most of the cars are (the game's UDP output is on): a race read this way has data
// even when nothing happened in it
bool feed_tells_drivers()
{
    SwoftyTelemetryFeed f;
    if (!feed_snapshot(f) || !f.session.participant_count)
        return false;
    uint32_t n = std::min<uint32_t>(f.session.participant_count, 24), known = 0;
    for (uint32_t i = 0; i < n; ++i)
        known += track_driver(f, i).empty() ? 0 : 1;
    return known * 2 >= n;
}

void track_race_over(const char *where)
{
    if (!g_track_race.exchange(false))
        return;
    std::lock_guard lock(g_lock.quiet());
    TrackTally t = g_tally;
    g_tally = TrackTally{};
    if (!g_loaded || !settings().on_track)
        return;
    // 1.1.3 (15 Nov): a race the telemetry could read counts as one with data, events or not (a clean race: 0
    // incidents, not "no data yet"). A restart's session is dropped whole later, this mark with it.
    if (!g_track_restarted.count(g_track_session) && feed_tells_drivers()) {
        PendingTrack cov;
        cov.e.kind = "coverage";
        cov.session = g_track_session;
        cov.season = g_track_season_now;
        cov.round = g_track_round_now;
        cov.time = 0; // at the start: a flashback (events after a moment) never drops it, a restart (all) does
        g_track.push_back(cov);
    }
    if (!t.raw)
        return;
    int passes = 0, hits = 0, out = 0, repeated = 0, undone = 0;
    for (const PendingTrack &p : kept_track(g_track_session, &repeated, &undone)) {
        passes += p.e.kind == "overtake";
        hits += p.e.kind == "collision";
        out += p.e.kind == "retirement";
    }
    t.repeated += repeated;
    t.undone += undone;
    log("[silly-season] on track: race over (%s): %d event(s) from the game, %d kept (%d overtake(s), %d collision(s), "
        "%d retirement(s)); left out: %d repeated, %d given back, %d of cars with no race seat in the market, %d of cars "
        "it can't tell, %d while paused, %d before a flashback or restart, %d feed unread",
        where, t.raw - t.outside, passes + hits + out, passes, hits, out, t.repeated, t.undone, t.strangers, t.unknown,
        t.paused, t.rewound, t.unread);
}

void on_track_event(const char *kind, const SwoftyEvent *ev)
{
    std::lock_guard lock(g_lock.quiet()); // kept for the next Team HQ: nothing saved changes here
    if (!g_loaded || !settings().on_track)
        return;
    ++g_tally.raw;
    if (!g_track_race.load()) {
        ++g_tally.outside;
        if (!g_track_told_off) {
            g_track_told_off = true;
            log("[silly-season] on track: a %s outside a race is left out (practice, qualifying, before lights out or "
                "after the race)", kind);
        }
        return;
    }
    // nothing happens on track while the game is paused: an event then is the game's replay or flashback at work
    if (view::paused()) {
        ++g_tally.paused;
        return;
    }
    SwoftyTelemetryFeed f;
    if (!feed_snapshot(f)) {
        ++g_tally.unread;
        return;
    }
    float now = f.session.session_time;
    // the session clock went back (a flashback, a restart): what came after that moment never happened
    if (g_track_clock >= 0 && now + 1.0f < g_track_clock)
        g_track_clock = now;
    g_tally.rewound += drop_track_events(now + 1.0f);
    PendingTrack t;
    t.e.kind = kind;
    t.time = now;
    t.session = g_track_session;
    t.season = g_track_season_now;
    t.round = g_track_round_now;
    bool pair = t.e.kind != "retirement";
    t.e.a = track_driver(f, static_cast<uint32_t>(ev->a));
    if (pair)
        t.e.b = track_driver(f, static_cast<uint32_t>(ev->b));
    else
        t.e.reason = static_cast<int>(ev->b);
    if (t.e.a.empty() || (pair && (t.e.b.empty() || t.e.b == t.e.a))) {
        ++g_tally.unknown;
        return;
    }
    if (!races_now(t.e.a) || (pair && !races_now(t.e.b))) {
        ++g_tally.strangers;
        return;
    }
    // 1.1.3: an exact repeat of the pair's latest event within the window (the same overtake told again, a collision
    // told from either car, a second retirement) is left out at once; everything else is kept as it came and judged
    // when it is read (kept_track), so a flashback can undo a give-back as well as an overtake
    for (size_t i = g_track.size(); i-- > 0;) {
        const PendingTrack &p = g_track[i];
        if (p.session != g_track_session || now - p.time > kTrackPairWindow)
            break;
        if (p.e.kind != t.e.kind)
            continue;
        bool same = p.e.a == t.e.a && p.e.b == t.e.b, mirrored = p.e.a == t.e.b && p.e.b == t.e.a;
        if (!pair ? p.e.a == t.e.a : (same || mirrored)) {
            if (same || (t.e.kind == "collision" && mirrored) || !pair) {
                ++g_tally.repeated;
                return;
            }
            break; // the pair's latest is the other way round: a give-back, judged when read
        }
    }
    if (!pair) // a driver retires once a race (also beyond the window)
        for (const PendingTrack &p : g_track)
            if (p.session == g_track_session && p.e.kind == "retirement" && p.e.a == t.e.a) {
                ++g_tally.repeated;
                return;
            }
    g_track_clock = std::max(g_track_clock, now);
    if (g_track.size() < 4000)
        g_track.push_back(std::move(t));
}

// 1.1.3: a flashback with nothing after it. The SDK has no flashback event, and events only came to say the clock had
// gone back: a collision undone by a flashback with no other event in the rest of the race stayed (the test bench:
// Verstappen's incident). Twice a second during a race, out of the pause, the feed's clock is checked.
void watch_track_clock()
{
    if (!g_track_race.load() || view::paused())
        return;
    static std::chrono::steady_clock::time_point last;
    auto now_t = std::chrono::steady_clock::now();
    if (now_t - last < std::chrono::milliseconds(500))
        return;
    last = now_t;
    SwoftyTelemetryFeed f;
    if (!feed_snapshot(f))
        return;
    float now = f.session.session_time;
    std::lock_guard lock(g_lock.quiet()); // kept for the next Team HQ: nothing saved changes here
    if (g_track_clock >= 0 && now + 1.0f < g_track_clock) {
        float was = g_track_clock;
        int dropped = drop_track_events(now + 1.0f);
        g_tally.rewound += dropped;
        log("[silly-season] on track: the session clock went back from %.0f s to %.0f s (a flashback): %d event(s) after it "
            "are dropped", was, now, dropped);
        g_track_clock = now;
        return;
    }
    g_track_clock = std::max(g_track_clock, now);
}

int g_results_rounds = -1;       // rounds of this career's season the game's standings hold (-1: unread)
// 1.1.3 (15 Nov): of those, the ones this visit may take: the calendar has passed them, or their race ended since the
// last visit. A round of the table can hold only its qualifying for now (the reader takes the session with the most
// cars, and qualifying has as many as the race).
int g_results_confirmed = 0;
bool g_results_another = false;  // 1.1.3 (15 Nov): every round the market took (two or more) reads differently: another career's
std::string g_results_said;      // the last outcome logged
std::vector<game_data::Round> g_rounds; // 1.1.3: this visit's standings, this career's rows only
bool g_results_differ = false;   // 1.1.3: a round the market took reads differently now: another career's standings
bool g_results_short = false;    // 1.1.3: the standings hold fewer rounds than the market took (maybe only for now)
std::string g_results_differ_said, g_results_short_said;

// g_lock held, at an idle Team HQ, after the standings are read and before their results are taken: the races'
// events go to the market
// 1.1.3 (15 Nov): only the races of `season` (and those whose season wasn't known), each GP to its own round: two GPs
// before one Team HQ used to go to one round. The closing season's races go before it closes, the new one's after.
void take_track_events(const ss::Settings &s, int season)
{
    if (g_track.empty())
        return;
    std::vector<PendingTrack> mine, others;
    for (const PendingTrack &t : g_track) {
        if (g_track_restarted.count(t.session))
            continue; // a restarted session: it never happened
        (t.season < 0 || t.season == season ? mine : others).push_back(t);
    }
    std::swap(g_track, mine);
    std::vector<PendingTrack> kept = kept_track(-1);
    g_track = std::move(others);
    if (g_state.season_index < 0 || g_state.real)
        return;
    // a GP whose round wasn't known: the standings' newest row the market hasn't taken yet, else the next round
    int fallback = g_results_confirmed > g_state.results_read ? g_results_confirmed : std::max(g_state.results_read, g_state.round) + 1;
    std::map<int, std::vector<ss::TrackEvent>> by_round;
    for (const PendingTrack &t : kept)
        by_round[t.round > 0 ? t.round : fallback].push_back(t.e);
    for (const auto &[round, list] : by_round) {
        int passes = 0, hits = 0, failures = 0;
        bool covered = false;
        for (const ss::TrackEvent &e : list) {
            passes += e.kind == "overtake";
            hits += e.kind == "collision";
            failures += e.kind == "retirement" && e.reason == 8;
            covered = covered || e.kind == "coverage";
        }
        ss::apply_track_events(g_state, s, list, round);
        log("[silly-season] on track, round %d: %d overtake(s), %d collision(s), %d mechanical retirement(s)%s", round, passes,
            hits, failures, covered ? "" : " (the feed didn't tell the cars: no data for the drivers who raced)");
    }
}

// A round's fingerprint, never 0 (0: none kept). Scheme 1 (1.1.3 before 15 Nov): its winner, runner-up and how many
// cars it classified. Scheme 2: the whole classification, so two races with the same top two still differ.
uint32_t round_key(const game_data::Round &r, int scheme = 2)
{
    if (scheme == 1) {
        uint32_t first = 0x3fff, second = 0x3fff;
        for (const game_data::Finish &f : r) {
            if (f.position == 1)
                first = f.stat & 0x3fff;
            else if (f.position == 2)
                second = f.stat & 0x3fff;
        }
        uint32_t k = (first << 18) | (second << 4) | (static_cast<uint32_t>(r.size()) & 0xf);
        k ^= static_cast<uint32_t>(r.size()) << 28;
        return k ? k : 1;
    }
    std::vector<std::pair<int, uint32_t>> order;
    for (const game_data::Finish &f : r)
        order.push_back({f.position, f.stat});
    std::sort(order.begin(), order.end());
    uint32_t h = 2166136261u;
    auto mixin = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (i * 8)) & 0xff;
            h *= 16777619u;
        }
    };
    mixin(static_cast<uint32_t>(order.size()));
    for (const auto &[pos, stat] : order) {
        mixin(static_cast<uint32_t>(pos));
        mixin(stat);
    }
    return h ? h : 1;
}

// g_lock held, at an idle Team HQ: the game's standings for this season, read once a visit (g_rounds,
// g_results_rounds, g_results_confirmed). 1.1.3: rows of another championship found before this career raced
// (State::foreign_keys) stay out while the game still shows them first; rounds the market already took are checked
// against their fingerprints. 15 Nov: a round that reads differently is a correction unless every round taken does.
void read_season_results()
{
    g_results_rounds = -1;
    g_results_confirmed = 0;
    g_rounds.clear();
    g_results_differ = g_results_short = g_results_another = false;
    std::vector<game_data::Round> rounds;
    std::string why;
    if (!game_data::read_results(rounds, why)) {
        std::string said = "unread: " + why;
        if (said != g_results_said) {
            g_results_said = said;
            log("[silly-season] results: not read (%s); races are simulated", why.c_str());
        }
        return;
    }
    const int scheme = g_state.key_scheme;
    // the other championship's rows: still first in the table (the game added this career's races after them), or
    // gone (the game replaced the table with this career's own)
    size_t skip = 0;
    if (!g_state.foreign_keys.empty() && !rounds.empty()) {
        size_t same = 0, can = std::min(rounds.size(), g_state.foreign_keys.size());
        while (same < can && round_key(rounds[same], scheme) == g_state.foreign_keys[same])
            ++same;
        if (same == can) {
            skip = same;
        } else {
            log("[silly-season] results: the standings no longer start with the %zu round(s) of another championship "
                "left out before; every row is this career's", g_state.foreign_keys.size());
            g_state.foreign_keys.clear();
        }
    }
    // a save of the first scheme: its keys checked out (or are gone), so they are made again in the second
    if (scheme == 1) {
        for (size_t i = 0; i < skip; ++i)
            g_state.foreign_keys[i] = round_key(rounds[i], 2);
    }
    std::vector<game_data::Round> foreign(rounds.begin(), rounds.begin() + static_cast<std::ptrdiff_t>(skip));
    rounds.erase(rounds.begin(), rounds.begin() + static_cast<std::ptrdiff_t>(skip));
    // 1.1.3: a market's first visit, with the calendar at its first round: no race of this season has been run, so
    // standings that hold rounds belong to another championship (seen in an F2 Revamp career, whose F1 table showed a
    // race before the player's first F1 weekend). They are kept aside, never taken, and never count as rounds run.
    if (g_state.season_index < 0 && !rounds.empty())
        if (auto pos = career::current_round(); pos && pos->round <= 1 && g_weekend_raced == 0) {
            for (const game_data::Round &r : rounds)
                g_state.foreign_keys.push_back(round_key(r, 2));
            log("[silly-season] results: the standings hold %zu round(s) but the calendar hasn't run a race yet; left out "
                "(this career's races count from round 1 whether the game adds them after these or replaces the table)",
                rounds.size());
            rounds.clear();
        }
    // the rounds the market took, against what the standings say now
    size_t check = std::min({rounds.size(), g_state.result_keys.size(), static_cast<size_t>(std::max(0, g_state.results_read))});
    int checked = 0, differ = 0;
    for (size_t i = 0; i < check; ++i) {
        if (!g_state.result_keys[i])
            continue;
        ++checked;
        if (round_key(rounds[i], scheme) != g_state.result_keys[i]) {
            ++differ;
            if (differ == 1) {
                std::string said = "round " + std::to_string(i + 1);
                if (said != g_results_differ_said) {
                    g_results_differ_said = said;
                    log("[silly-season] results: round %zu in the standings isn't the round %zu the market took", i + 1, i + 1);
                }
            }
        }
    }
    g_results_differ = differ > 0;
    g_results_another = checked >= 2 && differ == checked;
    if (scheme == 1 && !g_results_another) {
        for (size_t i = 0; i < check; ++i)
            if (g_state.result_keys[i])
                g_state.result_keys[i] = round_key(rounds[i], 2);
        g_state.key_scheme = 2;
        log("[silly-season] results: the market's fingerprints move to the whole classification (%zu round(s))", check);
    }
    auto pos = career::current_round();
    g_results_short = static_cast<int>(rounds.size()) < g_state.results_read && (!pos || pos->season_index == g_state.season_index);
    if (g_results_short) {
        std::string said = std::to_string(rounds.size()) + "<" + std::to_string(g_state.results_read);
        if (said != g_results_short_said) {
            g_results_short_said = said;
            log("[silly-season] results: the standings show %zu round(s), fewer than the %d the market took", rounds.size(),
                g_state.results_read);
        }
    }
    // 1.1.3 (15 Nov): a round of the table counts once its GP is over: the calendar has moved past it, or its race
    // ended since the last visit (a race session said so). Practice and qualifying alone never make it a round run.
    int n = static_cast<int>(rounds.size()), confirmed = 0;
    if (pos) {
        bool this_season = g_weekend_season < 0 || g_weekend_season == pos->season_index;
        int raced = g_race_ended && this_season && g_weekend_raced > 0 ? g_weekend_raced : 0;
        confirmed = std::min(n, std::max(pos->round - 1, raced));
        if (confirmed < n)
            log("[silly-season] results: the standings hold %d round(s), round %d not over yet (the calendar is at round "
                "%d%s): it waits for its race", n, confirmed + 1, pos->round, raced ? "" : " and no race ended since the last visit");
    } else {
        // no calendar to read: the rounds the market has already gone past, or a race that ended
        int raced = g_race_ended && g_weekend_raced > 0 ? g_weekend_raced : 0;
        confirmed = std::min(n, std::max(g_state.round, raced));
    }
    g_results_rounds = n;
    g_results_confirmed = confirmed;
    g_rounds = std::move(rounds);
}

void take_season_results();

// g_lock held: the standings read and taken in one go (the engine's tests)
void take_season_data()
{
    read_season_results();
    take_season_results();
}

// g_lock held, at an idle Team HQ, after read_season_results and with the market at this season: the game's race
// results go into the market, each round once (State::counted). A round already behind the market is recorded now;
// a round still ahead waits in State::incoming for complete_round, which records it before that round's decisions.
// The player's secret meetings go into the news.
void take_season_results()
{
    if (g_results_rounds >= 0 && g_state.season_index >= 0) {
        int fed = 0, queued = 0, late = 0, corrected = 0;
        size_t unknown = 0;
        std::string unknown_list; // "80 (label)": what the log says of a driver the market can't tell
        for (int r = 0; r < g_results_confirmed; ++r) {
            const game_data::Round &round = g_rounds[static_cast<size_t>(r)];
            int at = r + 1;
            uint32_t key = round_key(round, 2);
            if (g_state.result_keys.size() <= static_cast<size_t>(r))
                g_state.result_keys.resize(static_cast<size_t>(r) + 1, 0);
            char how = g_state.counted_as(at);
            if (how == 't' || how == 'k') {
                // counted from the table already: a classification that changed since (a penalty after the flag) is
                // noted, and what was counted stays
                if (g_state.result_keys[static_cast<size_t>(r)] && g_state.result_keys[static_cast<size_t>(r)] != key) {
                    ++corrected;
                    log("[silly-season] results: round %d's classification changed since the market took it (a correction); "
                        "the market keeps what it counted", at);
                }
                g_state.result_keys[static_cast<size_t>(r)] = key;
                continue;
            }
            if (how == 's') {
                // the market simulated it before the game's results came: its count stays, nothing is added
                g_state.count_round(at, 'k');
                g_state.result_keys[static_cast<size_t>(r)] = key;
                ++late;
                continue;
            }
            std::vector<std::pair<std::string, int>> list;
            for (const game_data::Finish &f : round) {
                std::string id = f.stat == g_player_stat && g_player_stat && !g_state.player.empty() ? g_state.player : known_driver(f.stat);
                if (id.empty()) {
                    if (++unknown <= 4) {
                        std::string label = db_label(f.stat);
                        unknown_list += (unknown_list.empty() ? "" : ", ") + std::to_string(f.stat) + " (" +
                                        (label.empty() ? std::string("no label") : label) + ")";
                    }
                    continue;
                }
                list.push_back({id, f.position});
            }
            if (at <= g_state.round) {
                for (const auto &[id, pos] : list)
                    ss::record_result(g_state, id, pos, at); // the season's round, also when several are taken at once
                ++fed;
            } else {
                g_state.incoming[at] = std::move(list);
                ++queued;
            }
            // 1.1.3: the records' status word, per value: what a retirement looks like there is still to be seen in the
            // game (a breakdown, a crash and a manual retirement), so it decides nothing yet
            {
                std::map<uint32_t, std::string> by;
                for (const game_data::Finish &f : round) {
                    std::string &who = by[f.status];
                    if (std::count(who.begin(), who.end(), ',') < 3)
                        who += (who.empty() ? "" : ", ") + std::string("P") + std::to_string(f.position);
                }
                if (by.size() > 1) {
                    std::string line;
                    for (const auto &[st, who] : by)
                        line += (line.empty() ? "" : "; ") + std::to_string(st) + ": " + who;
                    log("[silly-season] results: round %d: status words %s", at, line.c_str());
                }
            }
            g_state.result_keys[static_cast<size_t>(r)] = key;
        }
        if (g_results_confirmed > g_state.results_read)
            g_state.results_read = g_results_confirmed;
        (void)fed;
        if (late)
            log("[silly-season] results: %d round(s) the market had simulated came in the standings later; the simulated "
                "count stays (nothing is counted twice)", late);
        char said[160];
        std::snprintf(said, sizeof said, "%d round(s) run, %d over", g_results_rounds, g_results_confirmed);
        if (fed || queued || said != g_results_said) {
            g_results_said = said;
            std::string winner;
            if (g_results_confirmed > 0)
                for (const game_data::Finish &f : g_rounds[static_cast<size_t>(g_results_confirmed - 1)])
                    if (f.position == 1)
                        winner = f.stat == g_player_stat && g_player_stat ? g_state.player : known_driver(f.stat);
            log("[silly-season] results: %s, %d round(s) taken now and %d with their round's decisions%s%s%s%s", said, fed, queued,
                winner.empty() ? "" : "; last winner ", winner.c_str(), unknown ? "; drivers the market can't tell: " : "",
                unknown_list.c_str());
        }
        (void)corrected;
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
    int game = -1; // 1.1.4: the seat in the game: -1 no write, 0 or 1 waiting for the game to show it, 2 confirmed
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

// 1.1.5: the short name of the series a driver left F1 for (Driver::away), for the panel's chips
inline std::string away_code(const std::string &series)
{
    return series == "wec" ? "WEC" : series == "indycar" ? "INDYCAR" : series == "formula_e" ? "FORMULA E" : "";
}

struct UiDriver {
    std::string id;
    std::string name, team, code, salary, kind, last_team;
    uint32_t col = 0;
    int age = 0, contract = 0;
    float value = 0, potential = 0, trend = 0;
    bool clause = false, feud = false, pressure = false;
    bool player = false; // 1.1.3: the player himself, listed with the others (marked YOU)
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
    int t_overtakes = 0, t_incidents = 0, t_failures = 0; // 1.1.3: this season on track (game telemetry)
    int t_rounds = 0;       // 1.1.3: his races this season that came with telemetry (0: no data, not "no incidents")
    bool t_on = false;      // 1.1.3: the "On-track reputation" option is on
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
    std::vector<std::string> headlines; // the round's other stories
    // 1.1.3: the round's main story, told big, with the panel's key to read it in full
    int lead_id = 0;
    std::string lead_kind, lead_title, lead_text;
};

// a team became keen on the player
struct UiAlert {
    std::string team, code;
    uint32_t col = 0;
    int interest = 0;
};

struct OverlayData {
    bool career = false, enabled = true, es = false, effects = true;
    bool waiting_year = false; // 1.1.3: a Progressive career whose real year isn't known yet: the panel says so
    bool real = false; // history mode: the real season is told
    int version = 0;
    int year = 0, round = 0, rounds = 24;
    bool finalised = false;
    int open_seats = 0; // 1.1.3 (15 Nov): the new season's seats the game hasn't shown yet (State::seat_checks)
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
    bool fresh = false; // 1.1.4: another career (or none): nothing that waited to be shown is kept
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

// 1.1.4: the flow thread builds the overlays' data in a copy of its own and hands it over under g_ov_lock (a pointer
// swap); the render thread takes it at the start of a frame (adopt_overlay) and draws from g_ov without any lock, so
// neither thread waits for the other. g_ov belongs to the render thread.
std::mutex g_ov_lock;
OverlayData g_ov;
std::unique_ptr<OverlayData> g_ov_next; // g_ov_lock: built, not taken yet
std::atomic<int> g_ov_built{0};         // the last version built

// what waits to be shown (the render thread takes these one at a time): the older first, the newest kept within cap
template <class T> void carry(std::deque<T> &into, std::deque<T> &older, size_t cap)
{
    for (auto it = older.rbegin(); it != older.rend(); ++it)
        into.push_front(std::move(*it));
    while (into.size() > cap)
        into.pop_front();
}

void carry_queues(OverlayData &into, OverlayData &older)
{
    carry(into.banners, older.banners, 6);
    carry(into.signings, older.signings, 3);
    carry(into.farewells, older.farewells, 4);
    carry(into.strips, older.strips, 1);
    carry(into.alerts, older.alerts, 3);
}

// render thread: the newest data built, if there is one
bool adopt_overlay()
{
    std::unique_ptr<OverlayData> next;
    {
        std::lock_guard lock(g_ov_lock);
        next = std::move(g_ov_next);
    }
    if (!next)
        return false;
    if (!next->fresh)
        carry_queues(*next, g_ov);
    g_ov = std::move(*next);
    return true;
}

// flow thread: hands the data over (what the last hand-over still holds waits on in this one)
void publish_overlay(OverlayData &&o)
{
    auto made = std::make_unique<OverlayData>(std::move(o));
    {
        std::lock_guard lock(g_ov_lock);
        if (g_ov_next) {
            if (g_ov_next->fresh)
                made->fresh = true;
            else if (!made->fresh)
                carry_queues(*made, *g_ov_next);
        }
        g_ov_next = std::move(made);
    }
#ifdef SILLY_SEASON_PREVIEW
    adopt_overlay(); // the tests and the previews have no render thread: the data is taken at once
#endif
}
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
    o.open_seats = static_cast<int>(std::count_if(st.seat_checks.begin(), st.seat_checks.end(),
                                                  [&](const ss::State::SeatCheck &k) { return k.year == st.year && k.state < 2; }));
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
            for (const ss::State::SeatCheck &sc : st.seat_checks)
                if (sc.year == st.year && sc.team == t.id && sc.driver == who && !who.empty())
                    next.seat[k].game = sc.state;
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
        u.salary = d.salary > 0.05f ? ss::text::salary_short(ss::text::lang_of(s.lang), d.salary) : std::string("-"); // the player's isn't known
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
        } else if (!d.away.empty()) {
            // 1.1.5: racing in another series for now (he may come back)
            u.role = "away";
            u.role_code = away_code(d.away);
            u.role_col = 0xFFB547;
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
    // 1.1.3: the player is listed too, marked YOU, under his own name when he is a driver of the game (a player asked
    // why only the other 19 showed on the Drivers and Stock pages)
    auto as_player = [&](UiDriver &u, const ss::Driver &d) {
        u.player = true;
        if (d.kind != "player" && !d.name.empty())
            u.name = d.name;
    };
    for (const ss::Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const ss::Driver *d = st.driver(who)) {
                UiDriver u = ui_driver(*d);
                if (d->id == st.player)
                    as_player(u, *d);
                else if (d->contract <= st.year && !d->retiring)
                    ++o.seats_in_play;
                o.drivers.push_back(std::move(u));
            }
    if (own_team)
        for (const std::string &who : {st.player, st.own_mate})
            if (const ss::Driver *d = st.driver(who)) {
                UiDriver u = ui_driver(*d);
                u.team = g_own_team_name;
                u.code = "MYT";
                u.col = 0x5E5CE6;
                if (who == st.player)
                    as_player(u, *d);
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
        std::string_view key = panel_key;
        for (int i = 0; i < static_cast<int>(std::size(kPanelKeys)); ++i)
            if (key == kPanelKeys[i].id)
                g_panel_key = i;
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
            c.t_overtakes = static_cast<int>(d.s_overtakes);
            c.t_incidents = static_cast<int>(d.s_incidents);
            c.t_failures = static_cast<int>(d.s_failures);
            c.t_rounds = d.s_track_rounds;
            c.t_on = s.on_track;
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
            // 1.1.0 phase 6: his role off the grid. 1.1.5: or the series he races in now
            if (d.team.empty() && !d.away.empty()) {
                c.role = "away";
                c.role_team = away_code(d.away);
            } else if (s.academies && d.team.empty()) {
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
        // 1.1.3: the main story of the round: a signing first (a bombshell above all), then what breaks a seat (a deal
        // off, a sacking, a retirement), then talks and rumours; the newest of equals
        auto weight = [&](const UiNews &n) {
            int w = n.kind == "official" ? 60 : n.kind == "collapse" || n.kind == "sacking" ? 50 : n.kind == "retirement" ? 45
                    : n.kind == "player" ? 40 : n.kind == "talks" ? 30 : n.kind == "rumour" ? 20 : 10;
            for (const ss::NewsItem &x : st.news)
                if (x.id == n.id && x.big)
                    w += 15;
            return w;
        };
        const UiNews *lead = nullptr;
        for (const UiNews &n : o.news)
            if (n.year == st.year && n.round == st.round && (!lead || weight(n) > weight(*lead)))
                lead = &n;
        if (lead) {
            r.lead_id = lead->id;
            r.lead_kind = lead->kind;
            r.lead_title = lead->title;
            std::string line = lead->body.substr(0, lead->body.find('\n'));
            size_t end = line.find(". ");
            r.lead_text = end == std::string::npos ? line : line.substr(0, end + 1);
        }
        for (const UiNews &n : o.news)
            if (n.year == st.year && n.round == st.round && (!lead || n.id != lead->id) && r.headlines.size() < 2)
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
    // 1.1.4: only the new ones; what still waits on screen is carried over when the render thread takes this data
    for (UiAlert &a : new_alerts)
        o.alerts.push_back(std::move(a));
    while (o.alerts.size() > 3)
        o.alerts.pop_front();
    o.career = g_loaded;
    // 1.1.3: until the first Team HQ tells the real year, a Progressive career's market is still the baseline's 2026
    // (a player saw the 2026 teams and a team keen on him in a 2015 career): the panel waits, no other overlay shows
    o.waiting_year = g_real_career && st.season_index < 0;
    o.enabled = headlines;
    o.effects = effects;
    o.version = ++g_ov_built;
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
    publish_overlay(std::move(o));
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
// g_weekend_raced, g_weekend_season, g_race_ended and g_session_typed are declared with the other counters above.
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
    // 1.1.3 (15 Nov): never without a copy of what was there: careers/backup/<id>.<revision>.json, the last three
    if (storage::available() && g_state.season_index >= 0) {
        std::string path = "careers/backup/" + safe_name(id) + "." + std::to_string(g_state.revision % 3) + ".json";
        storage::Result r = storage::write(path, ss::dump_save(ss::to_json(g_state)));
        log("[silly-season] career %s: the market before the reset is kept in %s (%s)", id.c_str(), path.c_str(),
            storage::name(r));
    }
    log("[silly-season] career %s: %s; a new market starts", id.c_str(), why);
    uint64_t revision = g_state.revision; // the new market's copies stay newer than the old one's
    g_state = ss::State{};
    g_pool_read = false;
    g_adopted_from.clear();
    std::string err;
    if (!ss::init(g_state, g_baseline, group, seed_for(id), &err)) {
        log("[silly-season] cannot start the market: %s", err.c_str());
        g_loaded = false;
        return;
    }
    g_state.revision = revision;
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
// 1.1.3: a new season's seat change, from the news to the game's rows. Each starts "announced" (the market's plan); it
// is "requested" when the game takes the write and "confirmed" when a later read of the rows shows the driver in that
// team. Until then the market follows the grid the game was read with, never its own plan.
// 1.1.3 (15 Nov): each driver's deal just before the season turned (start_new_season moves the market's plan in at
// once), so a move the game never takes gives his old deal back
struct PrevDeal {
    std::string team;
    int contract = 0;
    float salary = 0;
};
std::map<std::string, PrevDeal> g_turn_prev;

void note_check(const ss::SeatChange &c)
{
    ss::State::SeatCheck k{c.team, c.driver, c.seat, g_state.year, 0};
    if (auto it = g_turn_prev.find(c.driver); it != g_turn_prev.end()) {
        k.have_prev = true;
        k.prev_team = it->second.team;
        k.prev_contract = it->second.contract;
        k.prev_salary = it->second.salary;
    }
    for (ss::State::SeatCheck &x : g_state.seat_checks)
        if (x.year == g_state.year && x.team == c.team && x.seat == c.seat) {
            x = k;
            return;
        }
    g_state.seat_checks.push_back(k);
}

// a move the game never took: the driver's deal as it was before the season turned (his team follows the game's rows)
void give_back_deal(const ss::State::SeatCheck &k)
{
    ss::Driver *d = g_state.driver(k.driver);
    if (!d || !k.have_prev)
        return;
    d->contract = k.prev_contract;
    d->salary = k.prev_salary;
    log("[silly-season] %s: the move to %s never happened; his deal goes back to %s until %d", k.driver.c_str(),
        k.team.c_str(), k.prev_team.empty() ? "none" : k.prev_team.c_str(), k.prev_contract);
}

// 1.1.3 (15 Nov): a seat write the last valid read of this career's rows allows: the team is in the rows, and the seat
// isn't the human's or a protected driver's (the player's own team-mate, a team the market doesn't plan). nullptr: ok
const char *write_blocked(const ss::SeatChange &c)
{
    if (!g_grid_valid_now || g_last_seats_owner != rows_owner())
        return "no valid read of this career's rows in this visit";
    if (!g_row_team_stat.count(c.team) && !g_state.team(c.team))
        return "a team not in the rows";
    for (const RealSeat &s : g_last_seats) {
        if (team_by_stat(s.team) != c.team || s.seat != c.seat)
            continue;
        if (s.human)
            return "the human's seat";
        std::string who = driver_for_stat(s.driver);
        if (!who.empty() && g_state.protected_ids.count(who))
            return "a protected driver's seat";
    }
    return nullptr;
}

// what the last read of the rows says of a check: 2 the driver in that team and seat, 1 in that team's other seat,
// 0 not in that team
int seat_shows(const ss::State::SeatCheck &k)
{
    int best = 0;
    for (const RealSeat &s : g_last_seats) {
        if (s.human || team_by_stat(s.team) != k.team || driver_for_stat(s.driver) != k.driver)
            continue;
        best = std::max(best, s.seat == k.seat ? 2 : 1);
    }
    return best;
}

// g_lock held, with the rows read in this visit: this season's open checks are written (those the rows don't show yet),
// the rows are read again, and each is confirmed or taken back. When the rows can't be read again, the checks wait for
// the next Team HQ and the market goes back to the grid read before the writes.
void write_and_confirm(const char *why)
{
    g_state.seat_checks.erase(std::remove_if(g_state.seat_checks.begin(), g_state.seat_checks.end(),
                                             [](const ss::State::SeatCheck &k) { return k.year < g_state.year; }),
                              g_state.seat_checks.end());
    if (g_state.seat_checks.empty())
        return;
    // 1.1.3 (15 Nov): only on a valid read of this career's rows in this visit
    if (!g_grid_valid_now || g_last_seats_owner != rows_owner()) {
        log("[silly-season] %s: no valid read of this career's rows in this visit; %zu seat(s) wait for the next Team HQ", why,
            g_state.seat_checks.size());
        return;
    }
    std::vector<ss::SeatChange> writes;
    std::vector<size_t> which;
    int blocked = 0;
    for (size_t i = 0; i < g_state.seat_checks.size(); ++i) {
        ss::State::SeatCheck &k = g_state.seat_checks[i];
        if (seat_shows(k) == 2)
            continue; // the rows show it already: checked again with the rest after the writes
        ss::SeatChange c{k.team, k.seat, k.driver, "season"};
        if (const char *no = write_blocked(c)) {
            log("[silly-season] %s: %s to %s seat %d is not written (%s)", why, k.driver.c_str(), k.team.c_str(), k.seat, no);
            k.state = std::min(k.state, 0);
            ++blocked;
            continue;
        }
        writes.push_back(c);
        which.push_back(i);
    }
    std::vector<career::Result> results;
    if (!writes.empty())
        apply_changes(writes, why, &results);
    std::vector<bool> refused(g_state.seat_checks.size(), false);
    for (size_t j = 0; j < which.size(); ++j) {
        if (j < results.size() && results[j] == career::Result::ok)
            g_state.seat_checks[which[j]].state = std::max(g_state.seat_checks[which[j]].state, 1);
        else
            refused[which[j]] = true; // the game said no
    }
    std::vector<ss::GridCar> before = g_last_cars;
    // every check is read again after the batch, those the rows showed before it too: another write in it can have
    // moved a driver already in place
    bool read = !writes.empty() ? sync_real_grid() : true;
    int confirmed = 0, other_seat = 0, taken_back = 0, waiting = 0, unseen_requests = 0, slow = 0;
    if (!read) {
        // the game's grid as read before the writes: the market never takes its plan for the game's
        if (!before.empty())
            ss::sync_grid(g_state, before);
        for (const ss::State::SeatCheck &k : g_state.seat_checks)
            waiting += k.state < 2;
    } else {
        size_t i = 0;
        for (auto it = g_state.seat_checks.begin(); it != g_state.seat_checks.end(); ++i) {
            int shows = seat_shows(*it);
            if (shows) {
                ++confirmed;
                other_seat += shows == 1;
                if (shows == 1)
                    log("[silly-season] %s: %s is in %s's other seat (not seat %d): taken as confirmed", why, it->driver.c_str(),
                        it->team.c_str(), it->seat);
                it = g_state.seat_checks.erase(it);
                continue;
            }
            bool was_refused = i < refused.size() && refused[i];
            // the game said ok, but the rows don't show it: the game can be slow to show a seat, so it waits for
            // one more Team HQ before it is taken back
            if (!was_refused && it->state == 1 && ++it->misses < 2) {
                ++slow;
                ++it;
                continue;
            }
            if (!was_refused && it->state == 0 && blocked) {
                ++waiting; // not written in this visit (no valid target): it waits
                ++it;
                continue;
            }
            unseen_requests += it->state == 1;
            if (ss::season_change_refused(g_state, it->team, it->driver, false))
                ++taken_back;
            give_back_deal(*it);
            it = g_state.seat_checks.erase(it);
        }
    }
    log("[silly-season] %s: %zu seat(s) written; %d confirmed by the rows%s, %d taken back (%d the game took but never "
        "showed), %d waiting for the game to show them, %d waiting for the next read", why, writes.size(), confirmed,
        other_seat ? " (some in the other seat)" : "", taken_back, unseen_requests, slow, waiting);
}

void apply_new_season(const ss::Settings &s, const std::vector<ss::SeatChange> &changes)
{
    (void)s;
    if (g_state.observer) {
        log("[silly-season] new season: the game's grid stands (My Team: no seat is written)");
        return;
    }
    for (const ss::SeatChange &c : changes)
        note_check(c);
    // 1.1.3: only with the game's grid read in this visit, and only the seats that change against it. Before, every
    // seat was written whatever was read, so a grid the market couldn't read (other addons' teams, a game update) was
    // overwritten with the market's own, undoing what other addons had set (editors, driver managers). The changes
    // stay announced and are written at the next Team HQ that reads the rows; meanwhile the market follows the game.
    // 15 Nov: read and valid (a read rejected for unknown teams left the writes allowed before).
    if (!g_grid_valid_now) {
        if (!g_last_cars.empty())
            ss::sync_grid(g_state, g_last_cars);
        log("[silly-season] new season: no valid read of the game's grid in this visit, so no seat is written yet (%zu "
            "planned change(s) wait for the next Team HQ that reads it)", changes.size());
        return;
    }
    write_and_confirm("new season");
}

// g_lock held, at Team HQ with the rows read: the new season's changes still open (written but not seen, or not
// written yet) are checked against the rows, written if need be, and confirmed or taken back
void confirm_open_seats()
{
    if (g_state.observer || g_state.real || !g_grid_valid_now)
        return;
    bool open = std::any_of(g_state.seat_checks.begin(), g_state.seat_checks.end(),
                            [](const ss::State::SeatCheck &k) { return k.year == g_state.year && k.state < 2; });
    if (open)
        write_and_confirm("new season (checked again)");
    else if (!g_state.seat_checks.empty())
        g_state.seat_checks.clear();
}

void on_hub()
{
    Timing tm("team hq", 30);
    std::lock_guard lock(g_lock);
    if (!g_loaded)
        return;
    tm.mark("lock");
    ss::Settings s = settings();
    ss::localize(g_state, s.lang != "en");

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
    g_grid_read_now = g_grid_valid_now = false;
    // The career's rows say the grid. 1.1.3: the last session's cars are only the fallback when the rows can't be read.
    // They used to be taken first, so a move the game made before a session (My Team's own market, an editor) was
    // already in the market when the rows were read: the observer never told it, and the log never named it.
    bool rows_read = sync_real_grid();
    if (rows_read)
        confirm_open_seats(); // 1.1.3: last winter's seats the rows hadn't shown yet
    tm.mark("game grid");
    if (!rows_read && g_grid_dirty) {
        size_t known = 0;
        for (ss::GridCar &c : g_grid) {
            if (!c.human)
                c.driver = driver_label(c.driver);
            if (g_state.team(c.team) && (c.human || g_state.driver(c.driver)))
                ++known;
        }
        if (known * 2 >= g_grid.size()) {
            ss::sync_grid(g_state, g_grid);
            log("[silly-season] grid synced from the session's cars: %zu of %zu known, player %s (%s)", known, g_grid.size(),
                g_state.player.c_str(), g_state.player_team.c_str());
        } else {
            log("[silly-season] grid NOT synced: only %zu of %zu cars match known drivers and teams (first: %s / %s)",
                known, g_grid.size(), g_grid.empty() ? "" : g_grid[0].driver.c_str(),
                g_grid.empty() ? "" : g_grid[0].team.c_str());
        }
    }
    g_grid_dirty = false;
    if (!rows_read)
        sync_player();
    tm.mark("session grid");
    adopt_game_pool("team hq"); // the drivers other addons put in the career's lists
    tm.mark("driver lists");
    take_f2_promotions();       // F2 Revamp's promotions from F2, as signings
    tm.mark("f2");
    if (!g_adopted_from.empty() && !player_before.empty() && player_before != "player" && player_before != g_state.player) {
        reset_market("the market taken from a provisional id was another driver's", career::current_round() ? career::current_round()->season_index : 0);
        if (!g_loaded)
            return;
    }
    // 1.1.0's probe (probe::run) did its job during development: the readers it led to live in game_data.inl and
    // game_ratings.inl, and it no longer writes to the log of a released build
    take_game_ratings(); // before the market moves: it decides on what the game shows
    tm.mark("ratings");
    // 1.1.3 (15 Nov): where the game is, first. The first visit of a new season closes the year before with its own
    // races and only then reads the standings, which are the new year's: before, a new year's rounds also went into
    // the year that was closing and were read again after it, and a career's totals counted them twice.
    auto pos = career::current_round();
    bool turning = pos && g_state.season_index >= 0 && pos->season_index > g_state.season_index;
    if (!turning)
        read_season_results(); // this season's standings: the rounds run, and whether they are the ones the market took
    tm.mark("standings");
    // 1.1.3: whether the player owns the F1 2026 season content (logged once it is known, for reports)
    {
        static int told = -1;
        auto o = swofty::content::owned("f1_2026");
        int now = o == swofty::content::Ownership::owned ? 1 : o == swofty::content::Ownership::not_owned ? 0 : -1;
        if (now >= 0 && now != told) {
            told = now;
            log("[silly-season] F1 2026 season content: %s", now ? "owned" : "not owned");
        }
    }

    // the report after the race: rounds this visit completes in a market already running
    bool market_running = g_state.season_index >= 0;
    int round_before = g_state.round, year_before = g_state.year;

    // a saved market ahead of the game belongs to another career under the same id (two careers with the
    // same driver can share one on this core).
    // 1.1.3 (15 Nov): the standings only say "another career" with real evidence: every round the market took (two or
    // more) reads differently, or the market came from another id this visit and its rounds don't match. One round
    // read differently is a correction of that race (a penalty after the flag) and a shorter table is the game
    // emptying it for a moment: the market is kept, and what it counted stays. Before any reset, a copy is kept.
    bool same_season = !turning && pos && g_state.season_index == pos->season_index && g_results_rounds >= 0;
    bool another = same_season && (g_results_another || ((g_results_differ || g_results_short) && !g_adopted_from.empty()));
    if (same_season && g_results_short && !another)
        log("[silly-season] results: the standings are shorter than the market's %d round(s) for now; the market is kept",
            g_state.results_read);
    if (same_season && g_results_differ && !another)
        log("[silly-season] results: a round the market took reads differently now (a correction, not another career): the "
            "market is kept");
    if (pos && g_state.season_index >= 0 &&
        (g_state.season_index > pos->season_index || another ||
         (g_state.season_index == pos->season_index && g_state.round > pos->round + 1))) {
        char why[220];
        std::snprintf(why, sizeof why, "the saved market (season index %d, round %d, %d result round(s)) is %s the game "
                      "(season index %d, round %d, %d in its standings)", g_state.season_index, g_state.round,
                      g_state.results_read, another ? "another career's, by its results, than" : "ahead of",
                      pos->season_index, pos->round, g_results_rounds);
        reset_market(why, pos->season_index);
        if (!g_loaded)
            return;
        read_season_results(); // the new market reads the table as its own
    }
    if (!g_adopted_from.empty()) {
        // the adopted market checked out: it lives under the real id from now on. 1.1.3 (15 Nov): its old copy goes
        // once the new one reads back (save_state), not before
        log("[silly-season] career %s: the market from %s is this career's", g_state.career.c_str(), g_adopted_from.c_str());
        if (provisional_id(g_adopted_from))
            g_retire_after_save = g_adopted_from;
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
        if (turning) {
            // the closing year's races (their overtakes and incidents) go to it before it closes
            take_track_events(s, g_state.season_index);
            // each driver's deal before the market's plan moves in: given back for a move the game never takes
            g_turn_prev.clear();
            for (const auto &[id, d] : g_state.drivers)
                g_turn_prev[id] = {d.team, d.contract, d.salary};
            std::vector<ss::RatingChange> ratings;
            bool real_season = g_state.real; // the season that ends was a real one: Progressive sets the new grid
            auto changes = ss::start_new_season(g_state, s, &ratings);
            g_state.season_index = pos->season_index;
            log("[silly-season] season %d: %zu seat(s) change", g_state.year, changes.size());
            if (g_real_career && g_real_ok && g_state.year <= g_real_last)
                start_real_season(g_state.year, pos->rounds);
            if (real_season) {
                log("[silly-season] history: Progressive puts %d's real line-up on the grid", g_state.year);
                ratings.clear();
            } else {
                apply_new_season(s, changes);
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
            read_season_results(); // the new year's standings, read once, for the new year only
        }
        take_track_events(s, pos->season_index);
        tm.mark("track");
        int completed = std::min(pos->round - 1, pos->rounds);
        // a weekend whose race ended since the last visit counts, whatever the game's round says. 1.1.3 (15 Nov): only
        // a race that ended: a weekend whose sessions never said their type no longer counts on its own (practice and
        // qualifying looked the same); the calendar counts it once it moves on.
        bool this_season = g_weekend_season < 0 || g_weekend_season == pos->season_index;
        if (g_weekend_raced != 0 && g_state.season_index >= 0 && this_season) {
            int raced = g_weekend_raced > 0 ? g_weekend_raced : g_state.round + 1;
            if (!g_race_ended) {
                log("[silly-season] hub: sessions of round %d ran, but no race of it ended%s", raced,
                    g_session_typed ? "" : " (no session said its type): the calendar counts it once it moves on");
            } else if (raced > completed) {
                log("[silly-season] hub: counting round %d as raced (the game says round %d)", raced, pos->round);
                completed = std::min(raced, pos->rounds);
            }
        }
        // the standings: the rounds of the table that are over (read_season_results)
        if (g_results_confirmed > completed && this_season) {
            log("[silly-season] hub: the standings hold %d round(s) over (the game says round %d)", g_results_confirmed, pos->round);
            completed = std::min(g_results_confirmed, pos->rounds);
        }
        g_weekend_raced = 0;
        g_weekend_season = -1;
        g_race_ended = g_session_typed = false;
        if (g_state.season_index >= 0 && g_state.season_index == pos->season_index && realign_year(pos->season_index))
            ss::season_preview(g_state, s); // a market saved in the wrong year: its preview for the right one
        if (g_state.season_index < 0) {
            // first visit: take the career where it is, without replaying the rounds already raced
            if (g_real_career) {
                // 1.1.3: only rows of this career read in this visit tell the year. A first read that fails (the game
                // still filling the rows) left the career before's in g_last_seats, and their year was taken.
                int year = 0;
                if (!g_grid_read_now || g_last_seats_owner != rows_owner())
                    log("[silly-season] history: this career's rows weren't read in this visit; the real year waits for them");
                else
                    year = detect_real_year();
                if (year)
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
        // the game's results, each round once: those behind the market now, those ahead with their round
        take_season_results();
        if (g_results_rounds < 0 && !g_results.empty() && g_state.counted_as(g_state.round + 1) == '-') {
            // the telemetry's results, when no standings could be read: the next round's
            auto &list = g_state.incoming[g_state.round + 1];
            for (auto &[driver, p] : g_results)
                list.push_back({driver_label(driver), p});
        }
        g_results.clear();
        tm.mark("season data");
        if (completed > g_state.round)
            ss::complete_round(g_state, s, completed, pos->rounds);
        g_state.incoming.clear(); // a round not completed now is read again from the standings next time
    } else {
        // older core: count weekends and season ends ourselves
        if (g_results_rounds < 0 && g_state.counted_as(g_state.round + 1) == '-') // the telemetry's results
            for (auto &[driver, p] : g_results)
                ss::record_result(g_state, driver_label(driver), p);
        g_results.clear();
        if (g_weekends_ended > 0) {
            ss::complete_round(g_state, s, g_state.round + g_weekends_ended, g_state.rounds);
            g_weekends_ended = 0;
        }
        if (g_season_ended && !g_state.finalised)
            ss::finalise_season(g_state, s);
        if (g_moves_finalised) {
            auto changes = ss::start_new_season(g_state, s);
            apply_new_season(s, changes);
            g_season_ended = g_moves_finalised = false;
        }
    }
    tm.mark("market");
    // 1.1.3 (15 Nov): stand-ins, returns and mid-season moves only on a valid read of this career's rows, and never into
    // the human's or a protected seat. One that can't be written now: a return waits for the next Team HQ; a stand-in
    // is called off (its race is next) and a sacking or a swap is taken back before anyone reads it.
    {
        std::vector<ss::SeatChange> now;
        for (const ss::SeatChange &c : ss::take_pending(g_state)) {
            const char *no = write_blocked(c);
            if (!no) {
                now.push_back(c);
                continue;
            }
            log("[silly-season] mid-season: %s to %s seat %d (%s) isn't written: %s", c.driver.c_str(), c.team.c_str(), c.seat,
                c.reason.c_str(), no);
            if (c.reason == "return")
                g_state.pending.push_back(c);
            else if (c.reason == "standin")
                ss::cancel_standin(g_state, c.team, c.seat, c.driver);
            else if (c.reason == "midseason" || c.reason == "swap")
                ss::cancel_change(g_state, c.team, c.seat, c.driver);
        }
        apply_changes(now, "mid-season");
    }
    tm.mark("seats");
    g_state.player_name = s.lang == "en" ? "You" : "Tú";
    if (market_running && g_state.round > 0 && (g_state.round > round_before || g_state.year != year_before))
        ++g_report_seq; // a race since the last visit: the report after it
    ss::refresh_mails(g_state, s);
    tm.mark("mails");
    apply_texts(s);
    tm.mark("texts");
    push_talent_ratings();
    tm.mark("talent ratings");
    push_talent_numbers();
    tm.mark("talent numbers");
    update_overlay(s);
    tm.mark("overlay");
    g_popup_wait = 1.5; // let Team HQ settle before a popup
    save_state("team hq");
    tm.mark("save");
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
    Timing tm("frame", 16); // 1.1.3: which part of a slow frame was ours
    watch_track_clock();
    if (g_reset_press_row) {
        g_reset_press_row = false;
        hub::set_option(kAddon, "press_answer", "-"); // ready for the next question
        tm.mark("press row");
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
        tm.mark("press answer");
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
            tm.mark("awards or cover seen");
        }
    if (int year = g_reveal_done.exchange(0); year > 0) {
        // the player watched next season's grid revealed: once a season
        std::lock_guard lock(g_lock);
        if (g_loaded && g_state.year == year && g_state.reveal_year != year) {
            g_state.reveal_year = year;
            update_overlay(settings());
            save_state("grid reveal");
        }
        tm.mark("grid reveal seen");
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
        tm.mark("panel read");
    }
    {
        // the panel just closed: the game's texts (the tab title's unread count) refresh now; the refresh is a
        // short hitch, so it waited while the panel was open
        static bool was_open = false;
        bool open_now = g_board_open.load();
        if (was_open && !open_now) {
            std::lock_guard lock(g_lock.quiet()); // the game's texts only
            if (g_loaded && g_lang_refresh_due && lang::available()) {
                g_lang_refresh_due = false;
                lang::refresh();
            }
            tm.mark("texts refresh");
        }
        was_open = open_now;
    }
    if (!g_hub_idle)
        return;
    // quiet: taken every frame at Team HQ; on_hub() takes g_lock itself, and the news popup counts as a change below
    std::lock_guard lock(g_lock.quiet());
    if ((g_round_poll -= dt) <= 0) {
        g_round_poll = 2.0;
        if (g_loaded)
            if (auto pos = career::current_round(); pos && pos->season_index * 100 + pos->round != g_round_seen) {
                log("[silly-season] hub: the game's round changed to %d (season index %d) after Team HQ opened",
                    pos->round, pos->season_index);
                tm.mark("round check");
                on_hub();
                tm.mark("team hq again");
            }
        tm.mark("round check");
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
        tm.mark("report popup");
        return;
    }
    if (g_popup_wait < 0 || !g_loaded)
        return;
    g_popup_wait -= dt;
    if (g_popup_wait < 0) {
        g_lock.touch(); // the news shown are marked as shown
        show_news();
        tm.mark("news popup");
    }
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

std::string g_grid_extra_said; // the cars without a market seat last logged (take_grid)

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
    // 1.1.3: cars whose driver holds no race seat in the market's grid, which follows the career's rows since the
    // last Team HQ. A session with more cars than the career has seats (a 2025 career raced with 26: three 2024 cars
    // from another addon's entry list) explains odd races, and those cars' overtakes and crashes aren't counted.
    if (g_state.season_index >= 0) {
        std::string list;
        int extra = 0;
        for (const ss::GridCar &c : g_grid) {
            std::string id = driver_label(c.driver);
            if (c.human || races_now(id))
                continue;
            if (extra++ < 6)
                list += (list.empty() ? "" : ", ") + (id.empty() ? std::string("?") : id) + " (" + c.team + ")";
        }
        std::string said = std::to_string(extra) + ":" + list;
        if (extra && said != g_grid_extra_said) {
            g_grid_extra_said = said;
            log("[silly-season] grid seen at %s: %d car(s) hold no race seat in the market's grid: %s%s. What they do on "
                "track isn't counted; more cars than the career has seats usually come from another addon's entry list",
                where, extra, list.c_str(), extra > 6 ? ", ..." : "");
        }
    }
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
            // 1.1.3 (15 Nov): the panel's key as set (it said F7 whatever the setting)
            out += std::string(es ? "\nResponde en la fila \"Rueda de prensa\" de esta pestaña (1, 2 o 3) o con "
                                  : "\nAnswer on this tab's \"Press conference\" row (1, 2 or 3) or with ") +
                   panel_key_label() + ".";
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
        Timing tm("career created", 20);
        begin_career(ctx, true);
        tm.mark("market");
        career_ready();
        tm.mark("texts and panel");
    });
    events::on<career::events::Loaded>().after([](const career::events::Loaded &, const events::Context &ctx) {
        Timing tm("career loaded", 20);
        begin_career(ctx, false);
        tm.mark("market");
        career_ready();
        tm.mark("texts and panel");
    });
    events::on<career::events::Saving>().before([](career::events::Saving &, const events::Context &) {
        std::lock_guard lock(g_lock.quiet()); // the game saving changes nothing in the market
        if (g_loaded && g_state.career.empty())
            g_state.career = career::saves::current(); // set for this event
        save_state("profile save", true);
    });
    events::on<career::events::Unloaded>().after([](const career::events::Unloaded &, const events::Context &) {
        Timing tm("career unloaded", 20);
        std::lock_guard lock(g_lock);
        save_state("career unloaded");
        tm.mark("save");
        if (g_loaded)
            log("[silly-season] the game's saves for this career: %d written, %d unchanged, %d skipped without building "
                "the JSON", g_saves_written, g_saves_same, g_saves_skipped);
        g_saves_written = g_saves_same = g_saves_skipped = 0;
        g_loaded = false;
        slot_reset();
        g_f2_waiting = false;
        g_report_seq = 0;
        g_keen_known = false;
        g_keen_prev.clear();
        g_hub_idle = false;
        clear_texts();
        tm.mark("texts");
        g_talent_ratings_year = 0;
        g_own_team_name.clear();
        g_real_grid_said.clear();
        g_real_career = g_real_now = false;
        g_row_look.clear();
        g_last_seats.clear(); // 1.1.3: the next career never tells its year from these rows
        g_last_seats_owner.clear();
        g_last_cars.clear();
        g_results_said.clear();
        g_results_differ_said.clear();
        g_results_short_said.clear();
        g_player_stat = 0;
        g_track.clear();
        g_track_done.clear();
        g_track_restarted.clear();
        g_track_season_now = -1;
        g_track_round_now = 0;
        g_retire_after_save.clear(); // 1.1.3 (15 Nov): kept on disk; the next load decides with the copies it finds
        g_turn_prev.clear();
        g_tally = TrackTally{};
        g_track_race = false;
        g_track_started = -1;
        g_grid_extra_said.clear();
        // 1.1.3: nothing of this career waits to be shown in the next one. 1.1.4: an empty hand-over (no career)
        OverlayData blank;
        blank.fresh = true;
        blank.version = ++g_ov_built;
        publish_overlay(std::move(blank));
        g_ov_generation.fetch_add(1);
    });
    // no banner or panel while a session runs
    events::on("session.unload").after([](std::span<const std::byte>, const events::Context &) {
        g_in_session = false;
        track_race_over("session unloaded");
    });
    // 1.1.3: lights out: from here the session's overtakes, collisions and retirements count (races only)
    // (1.1.3: the handlers from here to the race's results only note what happened for the next Team HQ and change
    // nothing saved: they take g_lock.quiet(), and a game save in between skips the market's JSON)
    events::subscribe(SWOFTY_EVENT_RACE_START, [](const SwoftyEvent *, void *) {
        g_track_race = true;
        std::lock_guard lock(g_lock.quiet());
        if (g_loaded && settings().on_track) {
            open_feed();
            g_track_told_off = false;
            g_track_clock = -1;
            // 1.1.3 (15 Nov): the GP this race is: its events go to that round, whatever Team HQ they reach
            g_track_season_now = g_weekend_season;
            g_track_round_now = g_weekend_raced > 0 ? g_weekend_raced : 0;
            if (g_track_season_now < 0)
                if (auto pos = career::current_round())
                    g_track_season_now = pos->season_index;
            // 1.1.3 (15 Nov): an earlier attempt at this same GP that never ended (a restart that opened a new session)
            // never happened either
            size_t before = g_track.size();
            g_track.erase(std::remove_if(g_track.begin(), g_track.end(),
                                         [](const PendingTrack &t) {
                                             return t.session != g_track_session && !g_track_done.count(t.session) &&
                                                    t.round > 0 && t.round == g_track_round_now && t.season == g_track_season_now;
                                         }),
                          g_track.end());
            int abandoned = static_cast<int>(before - g_track.size());
            g_tally.rewound += abandoned;
            if (abandoned)
                log("[silly-season] on track: %d event(s) of an earlier attempt at this GP that never ended are dropped",
                    abandoned);
            // lights out again in the same session: the race was restarted, and its first start never happened
            int dropped = g_track_started == g_track_session ? drop_track_events(-1.0f) : 0;
            g_tally.rewound += dropped;
            g_track_started = g_track_session;
            if (dropped)
                log("[silly-season] on track: lights out again in this session (a restart): %d event(s) of the first "
                    "start are dropped", dropped);
            else
                log("[silly-season] on track: lights out, the race's overtakes and incidents count");
        }
    });
    // 1.1.3 (15 Nov): how a session ended (e->a: 0 over, 1 a restart, 2 unloaded with it open). A restart's race never
    // happened: its events go, whatever session the restart opens next.
    events::subscribe(SWOFTY_EVENT_SESSION_END, [](const SwoftyEvent *e, void *) {
        std::lock_guard lock(g_lock.quiet());
        if (e->a == 1) {
            g_track_restarted.insert(g_track_session);
            size_t before = g_track.size();
            g_track.erase(std::remove_if(g_track.begin(), g_track.end(),
                                         [](const PendingTrack &t) { return t.session == g_track_session; }),
                          g_track.end());
            int n = static_cast<int>(before - g_track.size());
            g_tally.rewound += n;
            log("[silly-season] on track: the session was restarted; its %d event(s) never happened", n);
        } else if (g_track_started == g_track_session) {
            g_track_done.insert(g_track_session);
        }
    });
    events::on<career::events::SeasonEnd>().after([](const career::events::SeasonEnd &, const events::Context &) {
        std::lock_guard lock(g_lock.quiet());
        g_season_ended = true;
    });
    events::on("career.moves.finalised").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard lock(g_lock.quiet());
        g_moves_finalised = true;
    });
    events::on("career.weekend.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard lock(g_lock.quiet());
        ++g_weekends_ended;
    });

    // the real grid at every session start
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &ctx) {
        g_in_session = true;
        track_race_over("another session started");
        ++g_track_session;
        {
            std::lock_guard lock(g_lock.quiet());
            g_track_clock = -1;
        }
        Timing tm("session start", 8);
        if (!in_career(ctx))
            return;
        tm.mark("career check");
        std::lock_guard lock(g_lock.quiet());
        if (!g_loaded)
            return;
        tm.mark("lock");
        note_weekend(ctx, "session start");
        tm.mark("weekend");
        if (!take_grid(ctx.participants(), "session start"))
            g_grid_wanted = true;
        tm.mark("grid");
    });
    // the lap event's context always has the participants: the first crossing reads the grid when the
    // session start could not
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &, const events::Context &ctx) {
        if (!g_grid_wanted.load(std::memory_order_relaxed))
            return;
        std::unique_lock lock(g_lock.quiet(), std::try_to_lock);
        if (!lock.owns_lock())
            return;
        if (take_grid(ctx.participants(), "first lap") || !g_loaded)
            g_grid_wanted = false;
    });

    // the race of the weekend ended; and its results, when the core's telemetry reads positions (the core of
    // 1.0.1's time leaves them at zero, so the race is simulated)
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &ctx) {
        g_grid_wanted = false;
        track_race_over("session ended"); // 1.1.3: nothing after the race counts (its replays, the results screens)
        if (!in_career(ctx))
            return;
        {
            std::lock_guard lock(g_lock.quiet());
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
        std::lock_guard lock(g_lock.quiet());
        if (g_loaded) {
            g_results.insert(g_results.end(), results.begin(), results.end());
            log("[silly-season] race result: %zu classified car(s)", results.size());
        }
    });

    // the ratings the game uses in this career
    events::on<drivers::events::RatingsRead>().after([](const drivers::events::RatingsRead &r, const events::Context &) {
        int stat = static_cast<int>(r.stat);
        if (stat < 0 || stat > 3)
            return;
        // the game reads ratings often: a change to the market only when the rating is new to it
        std::unique_lock lock(g_lock.quiet(), std::try_to_lock);
        if (!lock.owns_lock() || !g_loaded)
            return;
        std::string id = strip_game(r.driver().id());
        ss::Driver *d = g_state.driver(id);
        if (!d)
            return;
        const float *field[] = {&d->experience, &d->racecraft, &d->awareness, &d->pace};
        float before = *field[stat];
        bool seen = d->ratings_seen;
        ss::observe_rating(g_state, id, stat, r.value);
        if (*field[stat] != before || d->ratings_seen != seen)
            g_lock.touch();
    });

    events::subscribe(SWOFTY_EVENT_CAREER_HUB, on_hub_event);
    // 1.1.3: overtakes, collisions and retirements, from the game's telemetry (its UDP output must be on)
    events::subscribe(telemetry::event::overtake, [](const SwoftyEvent *e, void *) { on_track_event("overtake", e); });
    events::subscribe(telemetry::event::collision, [](const SwoftyEvent *e, void *) { on_track_event("collision", e); });
    events::subscribe(telemetry::event::retirement, [](const SwoftyEvent *e, void *) { on_track_event("retirement", e); });
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
    // 1.1.3: which build this is, for a player's log (the manifest's version, set by build.bat, and when it was built)
    log("[silly-season] ready: version %s (built %s %s), market %s, intensity %.*s", SS_VERSION, __DATE__, __TIME__,
        std::string_view(game_market) == "game" ? "shared with the game" : "run by Silly Season",
        int(std::string_view(intensity).size()), std::string_view(intensity).data());
    // 1.1.4: the game's build (the exe's PE timestamp and image size, as Swofty's build overlays name it), so a
    // player's log says which game update it ran on
    if (auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr))) {
        auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        log("[silly-season] game build: pe timestamp %u, image %u bytes", unsigned(nt->FileHeader.TimeDateStamp),
            unsigned(nt->OptionalHeader.SizeOfImage));
    }
}

SWOFTY_ADDON()
