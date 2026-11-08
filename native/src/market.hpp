// Silly Season: the driver market engine.
//
// Plain C++ with no game calls, so tests run it on the host for whole careers. The plugin (plugin.cpp)
// feeds it what the game shows (the real grid, ratings, race results, the round) and applies the seat
// changes it returns through the core's career operations.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <swofty/json.hpp>

namespace ss {

using swofty::json::Value;

// ---------------------------------------------------------------------------------------------------
// Settings (the addon's options)

enum class Intensity { realistic, lively, chaos };
enum class Rumours { off, few, normal, many };
enum class NewsFilter { all, official, none };

struct Settings {
    Intensity intensity = Intensity::realistic;
    Rumours rumours = Rumours::normal;
    NewsFilter news = NewsFilter::all;
    bool midseason = true;          // mid-season sackings and replacements
    bool protect_teammate = false;  // the player's team-mate keeps his seat
    bool legends = false;           // icons (Senna, Schumacher...) can be signed
    bool progression = false;       // young drivers grow, veterans fade (writes ratings)
    bool standins = true;           // a driver can miss a race (illness, injury) and a reserve steps in
    bool talents = true;            // new young drivers appear every season (renamed fictional reserves)
    bool press = true;              // the press asks the player about the market
    std::string lang = "en";        // "en" or "es"
};

Intensity intensity_of(std::string_view s);
Rumours rumours_of(std::string_view s);
NewsFilter news_of(std::string_view s);

// ---------------------------------------------------------------------------------------------------
// The model

struct Driver {
    std::string id;       // the game's driver label: "lando_norris"
    std::string name;     // "Lando Norris"
    std::string nat;
    std::string kind;     // f1, reserve, f2, pool, legend
    std::string academy;  // junior family ("redbull") or empty
    std::string reserve_of; // the team whose reserve driver he is, or empty
    float salary = 0;     // estimated salary, millions a season
    std::string initials; // the game's three letters (timing screens)
    bool female = false;
    std::vector<std::string> name_keys; // the game's first and last name keys, when only he uses them
    std::string alias_first, alias_last; // a new talent's invented name (written over name_keys)
    bool regen = false;   // a new talent made by Silly Season
    float backing = 0;    // sponsor money he brings, millions a season
    int clause_rank = 0;  // exit clause: he may leave if his team finishes below this place (0: none)
    bool wants_out = false; // clause triggered or a feud: he leaves at the season's end if he can
    int f2_pos = 0;       // this season's F2 position (0: not in F2)
    std::vector<std::string> feuds; // drivers he won't share a garage with
    int born = 2000;
    int stat = 0;         // the game's stat id
    int seasons = 0;      // F1 seasons completed
    float pace = 70, racecraft = 70, awareness = 70, experience = 50;
    float pace_peak = 70, racecraft_peak = 70, awareness_peak = 70, experience_peak = 70;

    // career state
    std::string team;     // team label while on the grid, else empty
    int seat = -1;        // 0 or 1 on the grid
    int contract = 0;     // last season of the contract (inclusive), 0 = none
    float form = 0;       // -1 (awful) .. +1 (on fire)
    int points = 0;       // this season
    int starts = 0;       // this season, races with a result
    bool retiring = false;     // decided this season
    bool retired = false;      // left F1 for good
    bool released = false;     // sacked or bought out this season, now free
    bool ratings_seen = false; // the game reported this driver's career ratings
    bool pressured = false;    // "under pressure" was reported this season
    float last_value = 0;      // market value at the last round, for the driver stock
    float trend = 0;           // value change over the last round

    float overall() const;
    float potential() const;
    int age(int year) const { return year - born; }
};

struct Team {
    std::string id;       // "mclaren_26"
    std::string name;     // "McLaren"
    std::string code;     // "MCL"
    std::string family;   // "mclaren", "redbull" (Red Bull and Racing Bulls share it)
    int stat = 0;
    float prestige = 70;    // how attractive the brand is
    float performance = 70; // how good the car is (drifts each season)
    float patience = 0.5f;  // 0 = sacks quickly, 1 = very patient
    bool junior = false;    // a family's second team (Racing Bulls): brings on young drivers
    uint32_t colour = 0x808080; // 0xRRGGBB
    float budget = 100;     // money for drivers; poor teams like drivers with sponsors
    std::string td_name;    // technical director (invented)
    float td_rating = 70;   // how good he is: moves the car's level each winter
    std::array<std::string, 2> seats;
    int points = 0;         // this season

    float attraction() const { return 0.55f * performance + 0.45f * prestige; }
};

enum class Stage { rumour, talks, agreed, official, collapsed };
const char *stage_name(Stage s);

// A deal for next season: a renewal, a transfer, a promotion or a comeback.
struct Deal {
    std::string driver;
    std::string team;
    std::string kind;     // renewal, transfer, promotion, comeback
    int year = 0;         // the season it starts
    int until = 0;        // last season of the new contract
    Stage stage = Stage::rumour;
    bool fake = false;    // a rumour with nothing behind it
    int opened = 0;       // round it surfaced
    int source = 0;       // the outlet that reported it (text::outlet)
    int clause = 0;       // exit clause of the new contract: the team must finish in this top (0: none)
};

struct NewsItem {
    int id = 0;           // increasing, never reused
    int year = 0, round = 0;
    std::string kind;     // rumour, talks, official, retirement, sacking, collapse, grid, summary, pressure,
                          // player, preseason
    std::string title, body;          // in the language shown now (localize)
    std::string title_en, body_en, title_es, body_es;
    bool official = false;
    bool shown = false;
};

struct Move {
    int year = 0, round = 0;
    std::string driver, from, to;
    std::string kind;     // transfer, renewal, promotion, comeback, retirement, sacked, replaced, left
};

// A seat change for the plugin to apply in the game, in order.
struct SeatChange {
    std::string team;
    int seat = 0;
    std::string driver;
    std::string reason;
};

struct RatingChange {
    std::string driver;
    float experience, racecraft, awareness, pace;
};

struct State {
    int schema = 1;
    std::string group;      // career data group: f1_career_2026
    std::string career;     // the core's career id
    int year = 2026;
    int season_index = 0;
    int round = 0;          // last completed round
    int rounds = 24;
    int round_base = 0;     // rounds of this season raced before the market met the career (no results for them)
    uint64_t seed = 1;
    uint64_t rng = 1;
    std::string player;     // the player's driver label, if known
    std::string player_team;
    std::string player_name; // how texts name the player ("Tú", "You"); set by the plugin, not saved
    bool view_es = false;    // the language news and press are shown in (localize); not saved
    int legacy_news = 0;     // news items from_json dropped (an old save's texts, maybe not in English); not saved

    std::map<std::string, Driver> drivers;
    std::vector<Team> teams;          // in the game's order
    std::vector<Deal> deals;          // for next season
    std::vector<NewsItem> news;
    std::vector<Move> history;
    std::vector<SeatChange> pending;  // seat changes for the plugin

    // a driver missing one race: `in` drives `race`, then `out` gets the seat back. The model keeps
    // `out` in the seat throughout.
    struct Standin {
        std::string out, in, team;
        int seat = 0;
        int race = 0;
    };
    std::vector<Standin> standins;

    // the press: one question for the player at a time
    struct Press {
        int id = 0;
        std::string topic;    // future, mate, wish, signing
        std::string subject;  // the driver it is about
        std::string question;              // shown text (localize)
        std::vector<std::string> answers;  // shown text
        std::string question_en, question_es;
        std::vector<std::string> answers_en, answers_es;
        std::vector<std::string> options;  // per answer: what it means (driver ids for "wish")
        int asked = 0;        // round
    };
    std::optional<Press> press;
    int next_press_id = 1;
    int player_loyalty = 0;      // -2 (wants out) .. +2 (committed), from the player's answers
    std::string teammate_wish;   // the driver the player asked for

    std::vector<std::string> f2_table; // this season's F2 order, best first
    int f2_reported = 0;         // 1: mid-season leader reported, 2: champion reported
    int talents_year = 0;
    bool staff_done = false;     // this season's technical market happened

    bool retirements_drawn = false;   // this season
    int midseason_moves = 0;          // this season
    bool midseason_done = false;
    bool finalised = false;           // next season's grid is final
    std::map<std::string, std::array<std::string, 2>> next_grid;

    int next_news_id = 1;
    int seen_news = 0;                // the player has read up to this id (menu, panel)
    int banner_news = 0;              // the overlay banner has shown up to this id
    int preview_year = 0;             // the season preview was written for this year

    // the texts of the game's inbox mails (lng keys the plugin overrides)
    struct Mail {
        std::string subject, body;
    };
    Mail mail_preview, mail_radar, mail_grid;

    Team *team(std::string_view id);
    const Team *team(std::string_view id) const;
    Driver *driver(std::string_view id);
    const Driver *driver(std::string_view id) const;
    bool locked(const Team &t, int seat, const Settings &s) const;
};

// ---------------------------------------------------------------------------------------------------
// Engine

// A new career from data/baseline.json. False when the baseline has no such career group.
bool init(State &st, const Value &baseline, std::string_view group, uint64_t seed, std::string *error = nullptr);

// Merge drivers the baseline knows that the state lacks (after an addon update), keep everything else.
void refresh_from_baseline(State &st, const Value &baseline);

// The grid as the game shows it: driver label + team label per car, and the player's car.
struct GridCar {
    std::string driver, team;
    bool human = false;
};
void sync_grid(State &st, const std::vector<GridCar> &cars);

// A rating the game reported for this career (0 experience, 1 racecraft, 2 awareness, 3 pace).
void observe_rating(State &st, std::string_view driver, int stat, float value);

// A race result (1-based position; 0 = no classification).
void record_result(State &st, std::string_view driver, int position);

// The round `round` of `rounds` is done: rumours, talks, deals, retirements, mid-season moves.
void complete_round(State &st, const Settings &s, int round, int rounds);

// The season's last race is done: settle every seat for next season.
void finalise_season(State &st, const Settings &s);

// The new season has started in the game: returns the seat changes to apply, advances the year.
std::vector<SeatChange> start_new_season(State &st, const Settings &s, std::vector<RatingChange> *ratings = nullptr);

// Seat changes waiting for the plugin (mid-season replacements). Clears them.
std::vector<SeatChange> take_pending(State &st);

// Next season's grid as planned right now (final, or a projection with the deals so far).
std::map<std::string, std::array<std::string, 2>> projected_grid(const State &st, const Settings &s);

// Persistence.
Value to_json(const State &st);
bool from_json(State &st, const Value &v);

// Texts for the in-game menu (news.cpp).
std::string report_rumours(const State &st, const Settings &s);
std::string report_deals(const State &st, const Settings &s);
std::string report_contracts(const State &st, const Settings &s);
std::string report_grid(const State &st, const Settings &s);
std::string report_history(const State &st, const Settings &s);
std::string report_driver_market(const State &st, const Settings &s);

std::string report_news(const State &st, const Settings &s);
std::string report_stock(const State &st, const Settings &s);

// Unseen news items allowed by the filter, oldest first; marks them shown. Joined into one popup body.
std::vector<NewsItem> take_news(State &st, const Settings &s, size_t max);

// Breaking news for the overlay banner (official signings, retirements, sackings), oldest first.
std::vector<NewsItem> take_breaking(State &st, size_t max);

// Shows every news item and the press question in this language (each is stored in both).
void localize(State &st, bool es);

// News the player hasn't read in the menu or the panel.
int unread(const State &st);
void mark_seen(State &st);

// The season's opening item: seats in play, contract years. Once per season.
void season_preview(State &st, const Settings &s);

// Rebuild the inbox mail texts from the current state.
void refresh_mails(State &st, const Settings &s);

// The press question waiting for the player, if any; answer it with 0, 1 or 2.
const State::Press *press_pending(const State &st);
bool answer_press(State &st, const Settings &s, int choice);

// New talents' names, as the game's name keys and the text to show: (key, text).
std::vector<std::pair<std::string, std::string>> name_overrides(const State &st);
// Ratings the plugin writes into the career for new talents (their game rows hold someone else's).
std::vector<RatingChange> talent_ratings(const State &st);

std::string report_f2(const State &st, const Settings &s);
std::string report_staff(const State &st, const Settings &s);
// a driver's teams over the years: "McLaren 2026-27, Williams 2028"
std::string trajectory(const State &st, const std::string &driver);
int team_rank(const State &st, const std::string &team); // 1 = leads the constructors' points
float driver_value(const Driver &d);

} // namespace ss
