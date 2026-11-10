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
#include <set>
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
    bool personality = true;        // drivers' characters (ambition, loyalty, money) shape their choices
    bool strategies = true;         // each team follows a plan (title push, rebuild...)
    bool deadline = true;           // deadline day: the last two races, deals close fast
    bool living = true;             // 1.1.0 phase 5: sister-team swaps, budgets and sponsors, team principals
    bool academies = true;          // 1.1.0 phase 6: reserve drivers, academies that sign and promote, partner teams
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
    // 1.1.0: what the game shows for him in this career (read at Team HQ): its overall rating, with the perks,
    // specialists and car the game counts in, and his focus, a statistic the market doesn't model. 0: not read
    float game_overall = 0, focus = 0;

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

    // personality, 0..1, drawn once per career (-1 until drawn): ambition wants a fast car, loyalty stays put,
    // money follows the budget
    float ambition = -1, loyalty = -1, money = -1;
    int wins = 0, podiums = 0;  // this season
    float value_start = 0;      // market value at the season's first race (the most improved award)
    int number = 0;             // racing number Silly Season gave a new talent (0: none given)
    int number_tries = 0;       // numbers the game refused
    std::string home;           // the only team he races for (Konnersport's drivers); empty: any
    std::vector<float> values;      // market value after each round of this season (index round - 1; 0: no value)
    std::vector<float> values_last; // the same for last season

    // 1.1.0 phase 5: his career in this market (seasons before this one; this season's are added when shown)
    int c_starts = 0, c_wins = 0, c_podiums = 0, c_points = 0, titles = 0;
    // this season, race by race (index round - 1): his position (0 not raced, -1 not classified) and his team
    std::vector<int> race_pos;
    std::vector<std::string> race_team;

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
    std::string strategy = "contend"; // title, contend, rebuild, youth, survival
    bool optional = false;  // a team only some careers have (Konnersport): planned once seen on the grid
    bool active = true;     // an optional team the game shows
    // 1.1.0 phase 5: the team principal (invented) and the money
    std::string tp_name;
    int tp_since = 0;       // the season he took over
    float budget_start = 0; // the budget at the season's start (its trend this season)
    // 1.1.0 phase 6: the reserve driver Silly Season's market gives the team (first in line for a stand-in). A second
    // team (Racing Bulls) and a partner team (Haas) share their senior team's.
    std::string reserve;
    int reserve_until = 0;  // his deal's last season

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
    int source = -1;      // the outlet that reported it (text::outlet), -1: none
    int clause = 0;       // exit clause of the new contract: the team must finish in this top (0: none)
    int id = 0;           // increasing, never reused
    int chain = 0;        // the deal whose move opened this seat (the domino), 0: none
    std::string waits;    // the driver whose exit must be official before this deal can be
    bool scored = false;  // its outlet's record took the outcome (it came true or fell through)
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
    // what the story is about, for stacking the updates of one story and for the signing card
    std::string driver, from, to; // driver id, team ids
    int until = 0;                // a signing's last contract season
    bool big = false;             // a bombshell signing (a star to a car at least as good)
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
    int results_read = 0;   // rounds of this season whose real results the market has taken from the game
    uint32_t last_meeting = 0; // the player's last secret meeting reported (the game's id)
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
    // drivers the market leaves alone: the team-mate in the player's own My Team team, whom the game
    // shows outside every team the market knows. Set from the game's grid at each Team HQ; not saved.
    std::set<std::string> protected_ids;

    // History mode: a career whose line-ups follow the real seasons (Progressive Career on the Swofty Seasons).
    // The market doesn't plan next season's grid: the real one is told through the season, move by move.
    bool real = false;
    struct RealMove {
        std::string driver, from, to, kind; // transfer, promotion, comeback, retirement, left
        int until = 0;                      // the new contract's last season, from the data
        int rumour_at = 0, talks_at = 0, official_at = 0;
        int done = 0;                       // 1 rumoured, 2 talks, 3 official
    };
    std::vector<RealMove> real_moves;
    struct RealChange { // a mid-season stand-in or swap of the real season, told before its race
        std::string team, out, in, kind;
        int seat = 0, round = 0, until = 0;
        bool told = false;
    };
    std::vector<RealChange> real_changes;
    std::map<std::string, std::array<std::string, 2>> real_next; // next season's real grid
    // left alone by the market: protected, or a driver whose only team isn't in this career
    bool shielded(const std::string &id) const;

    std::vector<std::string> f2_table; // this season's F2 order, best first
    int f2_reported = 0;         // 1: mid-season leader reported, 2: champion reported
    int talents_year = 0;
    bool staff_done = false;     // this season's technical market happened

    bool retirements_drawn = false;   // this season
    int midseason_moves = 0;          // this season
    bool midseason_done = false;
    bool finalised = false;           // next season's grid is final
    std::map<std::string, std::array<std::string, 2>> next_grid;

    int next_deal_id = 1;
    // the outlets' record: a hidden accuracy that drifts each winter, and the stories that came true or didn't
    struct Outlet {
        float accuracy = 0.5f;
        int hits = 0, misses = 0;               // all seasons
        int season_hits = 0, season_misses = 0; // this season
    };
    std::vector<Outlet> outlets;      // one per text::outlet, made at init and load
    // the season awards, newest last
    struct Award {
        int year = 0;
        std::string key;  // champion, constructors, rookie, overachiever, improved
        std::string who;  // driver or team id
        std::string name; // as shown that year
        std::string line; // the detail ("412 pts, 9 wins")
    };
    std::vector<Award> awards;
    std::set<std::string> refusals_told; // "driver|team" refusals already told this season
    bool strategy_reviewed = false;   // this season's mid-season look at the teams' strategies
    bool ambition_checked = false;    // this season's look at ambitious drivers stuck in slow cars
    bool deadline_told = false;       // this season's deadline day was announced
    int scout_told = 0;               // scout reports this season (My Team)
    int reveal_year = 0;              // the season whose final grid the player has seen revealed (F7)
    std::string own_mate;             // My Team: the player's team-mate in his own team; set by the plugin, not saved

    // 1.1.0 phase 5
    // the drivers the game takes for a race seat now (race drivers and free agents; not reserves, not drivers without a
    // single career row). Set by the plugin from the career's rows at each Team HQ; not saved. Empty and unknown: anyone.
    bool game_rows_known = false;
    std::set<std::string> game_seatable;
    bool seatable(const std::string &id) const { return !game_rows_known || game_seatable.count(id) > 0; }
    // a mid-season change the game must still confirm: undone when it refuses it
    struct Change {
        std::string kind;            // sacking, swap
        std::string team, other;     // the team, and for a swap the sister team
        int seat = 0, other_seat = 0;
        std::string out, in;         // sacking: the driver out and the one in; swap: senior's driver out, junior's in
        int out_contract = 0, in_contract = 0;
        int news = 0;                // the news item that told it
        int round = 0;
        std::vector<std::pair<int, int>> deals; // deals it called off: id, their stage before
    };
    std::vector<Change> changes;     // this season's, newest last
    int team_comfort = 0;            // -2..+2: how happy the player's team is with him (the press)
    bool push_signing = false;       // the player asked his team for reinforcements (this season)
    int sponsor_news = 0;            // sponsor stories this season
    int principals_out = 0;          // team principals sacked this season
    int farewells_year = 0;          // the season whose farewells were told
    // signings another addon made in the game (F2 Revamp's promotions from F2): the market treats them as its own
    struct Outside {
        std::string driver, team, source;
        int until = 0;               // the contract's last season
        int year = 0;                // the season it was signed in
        bool told = false;           // the driver was seen in that team's car: contract set, news and move told
    };
    std::vector<Outside> outside;
    // 1.1.0 phase 6
    int academy_year = 0;            // the season the academies last signed young drivers
    int reserves_year = 0;           // the season whose reserves were signed (for the next one)
    // the overlays the player has seen, once a season each
    int awards_shown = 0;            // the awards ceremony of this season
    int cover_shown = 0;             // the cover of this season
    // 1.1.0, from player reports: a career whose race seats only the game can change (My Team: the core can't write
    // seats there). The market tells rumours and talks, never signs or sacks anyone itself, and reports as official
    // the moves the game makes (observe_grid).
    bool observer = false;

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

// A race result (1-based position; 0 = no classification) of round `round` (0: the round after the last completed).
void record_result(State &st, std::string_view driver, int position, int round = 0);

// The round `round` of `rounds` is done: rumours, talks, deals, retirements, mid-season moves.
void complete_round(State &st, const Settings &s, int round, int rounds);

// The season's last race is done: settle every seat for next season.
void finalise_season(State &st, const Settings &s);

// The new season has started in the game: returns the seat changes to apply, advances the year.
std::vector<SeatChange> start_new_season(State &st, const Settings &s, std::vector<RatingChange> *ratings = nullptr);

// Seat changes waiting for the plugin (mid-season replacements). Clears them.
std::vector<SeatChange> take_pending(State &st);
// the game put a secret meeting with another team on the player's calendar: the paddock hears of it
void report_meeting(State &st, const std::string &team);
// History mode: one season of the real data, as the plugin reads it (Progressive's data/years.json)
struct RealSeason {
    int year = 0;
    std::map<std::string, std::array<std::string, 2>> grid;           // team id -> its two drivers ("" for none)
    std::map<std::string, std::pair<std::string, std::string>> names; // team id -> its name and code that year
    std::map<std::string, float> gap;                                 // team id -> pace gap to the fastest car, %
    struct Change {
        int round = 0, rounds = 0; // the real round, of the real calendar's rounds
        std::string team, out, in, kind;
        int seat = 0, until = 0;
    };
    std::vector<Change> changes;
    std::map<std::string, std::array<float, 4>> ratings; // driver -> experience, racecraft, awareness, pace
};
struct RealPerson {
    std::string name, nat;
    int born = 0;
};
struct RealContext {
    std::map<std::string, RealPerson> people; // drivers the baseline doesn't have
    std::map<std::string, int> until_now;     // driver -> his last season with this year's team
    std::map<std::string, int> until_next;    // driver -> his last season with next year's team
    std::set<std::string> earlier, later;     // drivers on a grid before this year / after next year
};
// a season of the real data begins (call it once the season's round count is known). next: the year after,
// whose grid the season's moves lead to; null when there is none, and the market plans freely from then on.
void real_season(State &st, const RealSeason &now, const RealSeason *next, const RealContext &ctx);

// a stand-in the game refused: the regular driver races after all, and the news of it goes
void cancel_standin(State &st, const std::string &team, int seat, const std::string &in);

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

// --- 1.1.0 phase 3 -----------------------------------------------------------------------------------

// how a team is building ("title", "contend", "rebuild", "youth", "survival") and its label for the hub
const char *strategy_label(const std::string &strategy); // "TITLE PUSH"
// how much a team appeals to a driver, by his personality (roughly the team's attraction, 50..100)
float appeal(const State &st, const Driver &d, const Team &t);
// the outlet's reliability as the paddock sees it now, 1..5
int outlet_rating(const State &st, int outlet);
// deadline day: the last two races before the grid is final. Races left (1 or 2), 0 when it isn't
int deadline_races_left(const State &st);

// the scout's shortlist for a team's seat next season: the player's team (My Team: his own, which the model
// doesn't hold) when `team` is empty
struct ScoutPick {
    std::string driver;
    float fit = 0;        // how well he suits the team, best first
    int interest = 0;     // 0 hard sell, 1 open, 2 keen
    std::string status;   // free, expiring, f2
};
std::vector<ScoutPick> scout(const State &st, const Settings &s, const std::string &team, size_t max);

// racing numbers for new talents with an F1 seat: the numbers to try, best first
struct NumberWish {
    std::string driver;
    std::vector<int> numbers;
};
std::vector<NumberWish> talent_numbers(const State &st);
// the game took (ok) or refused a number; after a few refusals the talent keeps the game's number
void number_result(State &st, const std::string &driver, int number, bool ok);

// --- drivers of the game the baseline doesn't have ------------------------------------------------------

// a driver as the game database holds him (another addon's: More Driver Models, an editor's line-up)
struct GameDriver {
    std::string id;       // the game's driver label
    std::string name;     // as the plugin could tell it
    std::string nat, initials;
    std::string kind = "pool"; // pool, reserve, f2: the career list he came from
    int stat = 0;
    int born = 0;         // 0 or 1900: unknown
    bool rated = false;   // the game has career statistics for him
    bool legend = false;  // an icon of the past (Prost, Mansell): signed only with the Legends option
    float pace = 0, racecraft = 0, awareness = 0, experience = 0;
    float pace_peak = 0, racecraft_peak = 0, awareness_peak = 0, experience_peak = 0;
};
// the career's driver lists hold drivers the market doesn't know: they join it as free agents (returns how
// many). A driver the market added earlier without ratings (seen on the grid) takes the game's.
int adopt_game_drivers(State &st, const std::vector<GameDriver> &list);

// --- 1.1.0 phase 4 -----------------------------------------------------------------------------------

// this season's chains of moves: each chain the deal ids in order, the move that opened a seat first
std::vector<std::vector<int>> transfer_chains(const State &st);
const Deal *deal_with_id(const State &st, int id);

// --- 1.1.0 phase 5 -----------------------------------------------------------------------------------

// a mid-season change (a sacking, a sister-team swap) the game refused: the market takes it back, its news
// goes. `team`, `seat` and `driver` are the refused seat change's. True when a change was undone.
bool cancel_change(State &st, const std::string &team, int seat, const std::string &driver);

// how likely a story is to end in a signing, 0..100, from what the paddock can see (the stage, who reports it)
int deal_odds(const State &st, const Settings &s, const Deal &d);

// the player's market: each team's interest in him
struct Suitor {
    std::string team;
    int interest = 0;       // 0..100
    std::string status;     // keen, watching, cool; "own" for his team (how keen it is to keep him)
    bool seat_open = false; // a seat there runs out this season
};
std::vector<Suitor> player_suitors(const State &st, const Settings &s);

// a driver's career in this market, this season included
struct CareerTotals {
    int seasons = 0, starts = 0, wins = 0, podiums = 0, points = 0, titles = 0, teams = 0;
};
CareerTotals career_totals(const State &st, const Driver &d);

// the season so far against his team-mate (the rounds both raced for the same team): his wins and the mate's
struct HeadToHead {
    std::string mate;
    int ahead = 0, behind = 0;
};
HeadToHead head_to_head(const State &st, const Driver &d);

// another addon put `driver` in a `team` car with a contract to `until` (F2 Revamp: source "F2 Revamp"). The same
// signing told again changes nothing; a longer contract replaces a shorter one. False for an unknown driver or team.
bool outside_signing(State &st, const std::string &driver, const std::string &team, int until, const std::string &source);
// after the grid is read: each outside signing whose driver is now in that team's car gets its contract, once with
// a news item and a move. Returns how many were told now.
int apply_outside(State &st);

// --- 1.1.0, from player reports -----------------------------------------------------------------------------

// a seat of the new season's grid the game refused, after the plugin read the grid again (the market now holds the
// game's grid): the signing it told falls through. Its news goes if the player hasn't seen it; if he has, a story
// says the deal is off. True when the market had told such a signing.
bool season_change_refused(State &st, const std::string &team, const std::string &driver);
// observer careers (My Team): `before` is each driver's team before the grid was read again (driver -> team id).
// The moves the game made are told as official signings, the deals that predicted them come true. `winter`: the
// game's new season has begun and the market hasn't turned yet. Returns the moves told (0 when the read looks
// partial or too different to be this career's: the market just follows it).
int observe_grid(State &st, const Settings &s, const std::map<std::string, std::string> &before, bool winter);
// the career turns out to be one only the game can change seats in: the market becomes an observer, and says so once
void become_observer(State &st);
// what the game shows for a driver now (its ratings screen): the overall and experience, racecraft, focus, awareness
// and pace. The market's model takes them, so it never drifts from the game. A new talent keeps the market's own
// numbers (his game row still holds the old reserve's until the plugin writes his). False: unknown driver or skipped.
bool observe_game_ratings(State &st, const std::string &driver, float overall, float experience, float racecraft, float focus,
                          float awareness, float pace);

// --- 1.1.0 phase 6: academies and reserves -------------------------------------------------------------

// the family whose academy a team also brings on: Haas for Ferrari's ("" for none). A second team (Racing Bulls) shares
// its senior team's family already.
std::string partner_family(const std::string &family);
// the team whose reserve and academy this one shares: Red Bull for Racing Bulls, Ferrari for Haas, else itself
const Team *lead_team(const State &st, const Team &t);
// the reserve driver a team can call on: its own, else its lead team's ("" for none)
std::string reserve_for(const State &st, const Team &t);
// the driver comes from the academy this team brings on (its own family's, or its partner's)
bool in_academy(const Team &t, const Driver &d);
// the team whose reserve the driver is, or null
const Team *reserve_team_of(const State &st, const std::string &driver);

} // namespace ss
