// Silly Season: every text the player reads, in Spanish and English.
#pragma once
#include <string>
#include <string_view>

namespace ss::text {

struct Lang {
    bool es = true;
};

inline Lang lang_of(std::string_view code) { return Lang{code != "en"}; }

// news titles
std::string t_rumour(Lang l);
std::string t_talks(Lang l);
std::string t_official(Lang l);
std::string t_retirement(Lang l);
std::string t_sacking(Lang l);
std::string t_collapse(Lang l);
std::string t_bombshell(Lang l);
std::string t_grid(Lang l, int year);
std::string t_season(Lang l, int year);
std::string t_paddock(Lang l, size_t count);

// headlines that say who and where, in capitals ("OCON TO ALPINE?"); `pick` chooses among phrasings.
// Drivers go by their surname, teams by their name.
std::string h_rumour(Lang l, int pick, const std::string &driver, const std::string &team);
std::string h_talks(Lang l, int pick, const std::string &driver, const std::string &team);
std::string h_signing(Lang l, int pick, const std::string &driver, const std::string &team, bool bombshell);
std::string h_renewal(Lang l, int pick, const std::string &driver, const std::string &team);
std::string h_renewals(Lang l, int count);
std::string h_collapse(Lang l, int pick, const std::string &driver, const std::string &team, bool fake);
std::string h_left(Lang l, const std::string &driver);
std::string h_retirement(Lang l, int pick, const std::string &driver);
std::string h_sacking(Lang l, int pick, const std::string &driver, const std::string &team);
std::string h_pressure(Lang l, int pick, const std::string &driver);
std::string h_player_interest(Lang l, int pick, const std::string &team);
std::string h_player_renewal(Lang l, int pick, const std::string &team);
std::string h_standin(Lang l, const std::string &in, const std::string &out);
std::string h_standin_return(Lang l, const std::string &driver, const std::string &team);
std::string h_staff(Lang l, const std::string &director, const std::string &team);
std::string h_feud(Lang l, const std::string &a, const std::string &b);
std::string h_feud_exit(Lang l, const std::string &driver, const std::string &team);
std::string h_clause(Lang l, const std::string &driver);
// a press question (reply false) or the player's answer (reply true); topic: future, mate, wish, signing
std::string h_press(Lang l, const std::string &topic, const std::string &about, bool reply);
std::string h_press_silence(Lang l);
// the player's secret meeting with another team, as the paddock hears of it
std::string h_meeting(Lang l, const std::string &team);
std::string meeting(Lang l, int pick, const std::string &team);

// news bodies; `pick` chooses among phrasings
std::string rumour(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &from);
std::string fake_rumour(Lang l, int pick, const std::string &driver, const std::string &team);
std::string talks(Lang l, int pick, const std::string &driver, const std::string &team);
std::string collapse(Lang l, int pick, const std::string &driver, const std::string &team, bool fake);
std::string signing(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &from,
                    int until, const std::string &kind);
std::string renewal(Lang l, int pick, const std::string &driver, const std::string &team, int until);
std::string renewals_summary(Lang l, const std::string &list);
std::string retirement(Lang l, int pick, const std::string &driver, int age, int seasons);
std::string sacking(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &in);
std::string season_start(Lang l, int year, int newcomers, const std::string &list);
std::string left_grid(Lang l, const std::string &driver);

// early-season and player items
std::string t_pressure(Lang l);
std::string t_player(Lang l);
std::string t_preseason(Lang l, int year);
std::string pressure(Lang l, int pick, const std::string &driver, const std::string &team);
std::string player_interest(Lang l, int pick, const std::string &team);
std::string player_renewal(Lang l, int pick, const std::string &team);
std::string preseason(Lang l, int year, int count, const std::string &list);

// outlets: fictional media with a reliability, 0 (tabloid) .. 4 (very reliable)
constexpr int kOutlets = 5;
const char *outlet_name(int i);
int outlet_reliability(int i);           // 1..5
std::string outlet_label(Lang l, int i, int rating); // "The Pit Wall (high reliability)", rating 1..5 as seen now

// quotes from the team (by role, never by name), salaries, stand-ins, the season review
std::string boss_quote(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &kind);
std::string salary_line(Lang l, float millions);
std::string salary_short(Lang l, float millions);
std::string t_standin(Lang l);
std::string standin(Lang l, int pick, const std::string &out, const std::string &team, const std::string &in,
                    int race);
std::string standin_return(Lang l, const std::string &out, const std::string &team);
std::string t_review(Lang l, int year);
std::string review(Lang l, const std::string &signing, const std::string &revelation, const std::string &leap,
                   const std::string &retired, int moves, int rookies);

// F2, technical staff, sponsors, exit clauses, feuds, new talents
std::string t_f2(Lang l);
std::string f2_leader(Lang l, const std::string &driver, int age);
std::string f2_champion(Lang l, const std::string &champion, const std::string &second, const std::string &third);
std::string h_f2(Lang l, int year);
std::string t_staff(Lang l);
std::string staff_move(Lang l, int pick, const std::string &name, const std::string &from, const std::string &to);
std::string staff_hire(Lang l, const std::string &name, const std::string &team, const std::string &gone);
std::string h_staff(Lang l);
std::string backing_line(Lang l, float millions);
std::string clause_line(Lang l, int rank);
std::string t_clause(Lang l);
std::string clause_triggered(Lang l, const std::string &driver, const std::string &team, int rank);
std::string t_feud(Lang l);
std::string feud(Lang l, int pick, const std::string &a, const std::string &b, const std::string &team);
std::string feud_exit(Lang l, const std::string &driver, const std::string &team);
std::string t_talents(Lang l, int year);
std::string talents(Lang l, const std::string &list);

// the press: questions, answers and what the paddock makes of them
std::string t_press(Lang l);
std::string q_future(Lang l, const std::string &team);
std::string q_mate(Lang l, const std::string &mate, const std::string &team);
std::string q_wish(Lang l, const std::string &team);
std::string q_signing(Lang l, const std::string &driver, const std::string &team);
std::string a_future(Lang l, int i, const std::string &team);
std::string a_mate(Lang l, int i);
std::string a_signing(Lang l, int i);
std::string a_none(Lang l);
std::string r_future(Lang l, int i, const std::string &team);
std::string r_mate(Lang l, int i, const std::string &mate);
std::string r_wish(Lang l, const std::string &driver);
std::string r_signing(Lang l, int i, const std::string &driver);
std::string r_silence(Lang l);

// inbox mails (the game shows them under its own contact)
std::string mail_preview_subject(Lang l, int round);
std::string mail_radar_subject(Lang l);
std::string mail_grid_subject(Lang l, int year);
std::string mail_intro(Lang l);
std::string mail_quiet(Lang l);
std::string mail_sign(Lang l);
std::string h_negotiations(Lang l);
std::string h_stock(Lang l);
std::string h_up(Lang l);
std::string h_down(Lang l);
std::string h_pressure(Lang l);
std::string h_expiring(Lang l, int year);
std::string h_moves(Lang l, int year);
std::string h_news(Lang l);

// report headings and labels
std::string h_rumours(Lang l);
std::string h_deals(Lang l, int year);
std::string h_contracts(Lang l, int year);
std::string h_grid(Lang l, int year, bool final);
std::string h_history(Lang l);
std::string h_market(Lang l);
std::string none(Lang l);
std::string stage(Lang l, const char *stage);
std::string kind(Lang l, const std::string &kind);
std::string free_seat(Lang l);
std::string unknown_seat(Lang l);

// 1.1.0 phase 3: personality, dominoes, deadline day, strategies, awards, the outlets' record, the scout, numbers
std::string trait_line(Lang l, int pick, const std::string &trait, const std::string &driver);
std::string h_swoop(Lang l, const std::string &driver, const std::string &team);
std::string h_domino(Lang l, int pick, const std::string &next, const std::string &team);
std::string domino(Lang l, int pick, const std::string &leaver, const std::string &team, const std::string &next,
                   const std::string &next_from);
std::string h_refusal(Lang l, int pick, const std::string &driver, const std::string &team);
std::string refusal(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &from,
                    bool buyout, const std::string &trait);
std::string h_deadline(Lang l, int left);
std::string deadline(Lang l, int year, int left, int open, const std::string &list);
std::string h_wants_out(Lang l, const std::string &driver, const std::string &team);
std::string wants_out(Lang l, int pick, const std::string &driver, const std::string &team);
std::string strategy_name(Lang l, const std::string &key);
std::string h_strategy(Lang l, const std::string &team, const std::string &now);
std::string strategy_switch(Lang l, const std::string &team, const std::string &was, const std::string &now);
std::string h_awards(Lang l, int year);
std::string award_title(Lang l, const std::string &key);
std::string award_detail(Lang l, const std::string &key, int a, int b, float c);
std::string h_media(Lang l);
std::string media_scorecard(Lang l, const std::string &best, int bh, int bn, int br, const std::string &worst, int wh, int wn,
                            int wr);
std::string h_scout(Lang l, int year);
std::string scout_intro(Lang l, int year);
std::string scout_status(Lang l, const std::string &status);
std::string scout_interest(Lang l, int interest);
std::string h_number(Lang l, const std::string &driver, int number);
std::string number_line(Lang l, const std::string &driver, const std::string &team, int number);

// 1.1.0 phase 5: new press topics (deadline, a rival's bombshell, the player's renewal, last season's awards)
std::string q_deadline(Lang l, const std::string &team, int left);
std::string a_deadline(Lang l, int i);
std::string r_deadline(Lang l, int i, const std::string &team);
std::string q_rival(Lang l, const std::string &driver, const std::string &team);
std::string a_rival(Lang l, int i);
std::string r_rival(Lang l, int i, const std::string &driver);
std::string q_renewal(Lang l, const std::string &team);
std::string a_renewal(Lang l, int i, const std::string &team);
std::string r_renewal(Lang l, int i, const std::string &team);
std::string q_award(Lang l, const std::string &driver, const std::string &award, int year);
std::string a_award(Lang l, int i);
std::string r_award(Lang l, int i, const std::string &driver);
// sister-team swaps, sponsors, team principals, farewells
std::string h_swap(Lang l, const std::string &up, const std::string &senior);
std::string swap(Lang l, int pick, const std::string &up, const std::string &down, const std::string &senior,
                 const std::string &junior);
const char *sponsor_brand(int pick);
std::string h_sponsor(Lang l, const std::string &team, bool in);
std::string sponsor(Lang l, int pick, const std::string &team, const std::string &brand, int millions, bool in);
std::string h_principal(Lang l, const std::string &team);
std::string principal_out(Lang l, int pick, const std::string &old, const std::string &team, const std::string &fresh,
                          const std::string &plan);
std::string h_farewell(Lang l, const std::string &driver);
std::string farewell(Lang l, const std::string &driver, int seasons, int starts, int wins, int podiums, int titles);
std::string tribute(Lang l, const std::string &driver, int seasons, int wins); // "Name (12 seasons, 3 wins)"
// the inbox mails' new sections
std::string mail_open_seats(Lang l, const std::string &teams);
std::string h_mail_chains(Lang l);
std::string mail_media(Lang l, const std::string &outlet, int hits, int stories);
std::string h_mail_farewells(Lang l);
// 1.1.0 phase 6: academies and reserves
std::string h_academy(Lang l, const std::string &driver, const std::string &team);
std::string academy(Lang l, int pick, const std::string &driver, const std::string &team, int age);
std::string h_reserves(Lang l, int year);
std::string reserve_line(Lang l, const std::string &team, const std::string &driver, bool renewed);
// a driver another addon moved up from F2 (F2 Revamp)
std::string h_f2_promotion(Lang l, const std::string &driver, const std::string &team);
std::string f2_promotion(Lang l, const std::string &driver, const std::string &team, int until, const std::string &source);
// 1.1.0, from player reports: a signing the game refused at the new season, and My Team's market
std::string h_deal_off(Lang l, const std::string &driver, const std::string &team);
std::string deal_off(Lang l, const std::string &driver, const std::string &team, const std::string &now_team);
std::string h_observer(Lang l);
std::string observer(Lang l);

} // namespace ss::text
