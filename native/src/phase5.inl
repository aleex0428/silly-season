// Silly Season 1.1.0 phase 5: a market more alive (sister-team swaps, money, team principals, farewells), the
// player's future, the season's numbers and the odds of each story. Included by market.cpp after its season
// helpers, so it shares their helpers (Rng, add_news, value...).

namespace {

// the active teams by their car, best first: where each one "should" finish
std::vector<const Team *> by_car(const State &st)
{
    std::vector<const Team *> out;
    for (const Team &t : st.teams)
        if (t.active)
            out.push_back(&t);
    std::stable_sort(out.begin(), out.end(), [](const Team *a, const Team *b) { return a->performance > b->performance; });
    return out;
}

int car_rank(const State &st, const Team &t)
{
    std::vector<const Team *> order = by_car(st);
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == &t)
            return static_cast<int>(i + 1);
    return static_cast<int>(order.size());
}

bool anyone_scored(const State &st)
{
    return std::any_of(st.teams.begin(), st.teams.end(), [](const Team &t) { return t.points > 0; });
}

// I1. Sister teams (Red Bull and Racing Bulls): the senior team's driver in trouble and the junior team's
// driver in form swap seats mid-season. Both are race drivers, so the game takes it as one seat change.
void sister_swap(State &st, const Settings &s, Rng &r, int round, double progress)
{
    if (!s.living || !s.midseason || progress < 0.35 || progress > 0.75 || st.midseason_moves >= midseason_cap(s.intensity))
        return;
    if (std::any_of(st.changes.begin(), st.changes.end(), [](const State::Change &c) { return c.kind == "swap"; }))
        return; // once a season
    for (Team &senior : st.teams) {
        if (senior.junior || !senior.active || senior.family.empty())
            continue;
        for (Team &junior : st.teams) {
            if (!junior.junior || !junior.active || junior.family != senior.family || &junior == &senior)
                continue;
            bool covered = std::any_of(st.standins.begin(), st.standins.end(), [&](const State::Standin &sb) {
                return sb.team == senior.id || sb.team == junior.id;
            });
            if (covered)
                continue; // a reserve is in one of the cars: not now
            for (int k = 0; k < 2; ++k) {
                if (st.locked(senior, k, s))
                    continue;
                Driver *d = st.driver(senior.seats[static_cast<size_t>(k)]);
                const Driver *mate = st.driver(senior.seats[static_cast<size_t>(1 - k)]);
                if (!d || !mate || d->kind == "legend" || d->id == st.player || d->kind == "player")
                    continue;
                // out of form, or far behind his team-mate without a good run to show for it
                float gap = value(*mate) - value(*d);
                if (!((d->form <= -0.15f && gap >= 2.0f) || (d->form <= 0.05f && gap >= 7.0f)))
                    continue;
                // the junior team's driver who has earned it: in form, and doing more with less
                int pick_seat = -1;
                float best = -1e9f;
                for (int j = 0; j < 2; ++j) {
                    if (st.locked(junior, j, s))
                        continue;
                    const Driver *c = st.driver(junior.seats[static_cast<size_t>(j)]);
                    if (!c || c->id == st.player || c->kind == "player" || c->kind == "legend")
                        continue;
                    // in clearly better form for his car, and not far below in level
                    if (c->form < 0.15f || c->form < d->form + 0.20f || c->overall() < d->overall() - 9.0f || feuding(*c, *mate))
                        continue;
                    float sc = value(*c) + 3.0f * c->form;
                    if (sc > best) {
                        best = sc;
                        pick_seat = j;
                    }
                }
                if (pick_seat < 0)
                    continue;
                double p = (s.intensity == Intensity::chaos ? 0.30 : s.intensity == Intensity::lively ? 0.18 : 0.08) *
                           (1.0 - 0.5 * senior.patience);
                if (!r.chance(p))
                    continue;
                Driver *up = st.driver(junior.seats[static_cast<size_t>(pick_seat)]);
                State::Change ch;
                ch.kind = "swap";
                ch.team = senior.id;
                ch.seat = k;
                ch.other = junior.id;
                ch.other_seat = pick_seat;
                ch.out = d->id;
                ch.in = up->id;
                ch.out_contract = d->contract;
                ch.in_contract = up->contract;
                ch.round = round;
                std::string down_id = d->id, up_id = up->id, dn = d->name, un = up->name, sn = senior.name, jn = junior.name;
                add_move(st, round, up_id, junior.id, senior.id, "promoted");
                add_move(st, round, down_id, senior.id, junior.id, "demoted");
                int pick = r.range(0, 9);
                add_news(st, round, "swap", [&](text::Lang l) { return text::h_swap(l, un, sn); },
                         [&](text::Lang l) { return text::swap(l, pick, un, dn, sn, jn); }, true);
                tag_news(st, up_id, senior.id, junior.id);
                ch.news = st.news.back().id;
                set_seat(st, senior, k, up_id);
                set_seat(st, junior, pick_seat, down_id);
                d->form *= 0.5f;
                up->form *= 0.5f;
                st.pending.push_back({senior.id, k, up_id, "swap"});
                ++st.midseason_moves;
                st.changes.push_back(std::move(ch));
                return;
            }
        }
    }
}

// I2. Money during the season: a team doing better than its car promised draws a sponsor, one doing worse loses one
void sponsor_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    int cap = s.intensity == Intensity::chaos ? 4 : s.intensity == Intensity::lively ? 3 : 2;
    if (!s.living || progress < 0.2 || progress > 0.85 || st.sponsor_news >= cap || !anyone_scored(st))
        return;
    if (!r.chance(s.intensity == Intensity::chaos ? 0.16 : s.intensity == Intensity::lively ? 0.11 : 0.08))
        return;
    std::vector<Team *> teams;
    for (Team &t : st.teams)
        if (t.active)
            teams.push_back(&t);
    if (teams.empty())
        return;
    Team &t = *teams[static_cast<size_t>(r.range(0, static_cast<int>(teams.size()) - 1))];
    int gap = car_rank(st, t) - rank_of(st, t); // > 0: ahead of where the car belongs
    bool in = gap > 0 || (gap == 0 && r.chance(0.5));
    if (in && t.budget >= 155)
        in = false;
    if (!in && t.budget <= 52)
        return;
    int amount = in ? r.range(4, 10) : r.range(4, 9);
    t.budget = clampf(t.budget + static_cast<float>(in ? amount : -amount), 50, 160);
    ++st.sponsor_news;
    std::string team = t.name, brand = text::sponsor_brand(static_cast<int>(r.next() % 1000));
    int pick = r.range(0, 9);
    add_news(st, round, "sponsor", [&](text::Lang l) { return text::h_sponsor(l, team, in); },
             [&](text::Lang l) { return text::sponsor(l, pick, team, brand, amount, in); }, false);
    tag_news(st, "", t.id);
}

// I3. A team far below where its car belongs loses patience with its principal; the new one brings a plan
void principal_round(State &st, const Settings &s, Rng &r, int round, double progress)
{
    int cap = s.intensity == Intensity::realistic ? 1 : 2;
    if (!s.living || progress < 0.5 || progress > 0.9 || st.principals_out >= cap || !anyone_scored(st))
        return;
    for (Team &t : st.teams) {
        if (!t.active || t.tp_since >= st.year)
            continue; // a principal in his first season keeps his job
        int behind = rank_of(st, t) - car_rank(st, t);
        if (behind < 3)
            continue;
        double p = 0.20 * (1.0 - t.patience) + (behind >= 5 ? 0.10 : 0.0);
        if (s.intensity == Intensity::chaos)
            p *= 1.5;
        if (!r.chance(p))
            continue;
        std::string old = t.tp_name;
        t.tp_name = invent_person(r, 0.12);
        t.tp_since = st.year;
        t.patience = clampf(0.35f + static_cast<float>(r.uniform()) * 0.35f, 0.1f, 0.9f);
        // the new principal's plan
        std::string was = t.strategy;
        std::string now = t.junior                                     ? "youth"
                          : t.performance >= 80                        ? "title"
                          : t.prestige >= 78 && t.performance < 74      ? "rebuild"
                          : t.budget < 80 && t.performance < 68         ? "survival"
                                                                       : "contend";
        if (now == was && !t.junior)
            now = was == "rebuild" ? "contend" : "rebuild";
        if (s.strategies)
            t.strategy = now;
        ++st.principals_out;
        std::string team = t.name, fresh = t.tp_name;
        bool plans = s.strategies;
        int pick = r.range(0, 9);
        add_news(st, round, "principal", [&](text::Lang l) { return text::h_principal(l, team); },
                 [&](text::Lang l) {
                     return text::principal_out(l, pick, old, team, fresh, plans ? text::strategy_name(l, now) : std::string());
                 },
                 true);
        tag_news(st, "", t.id);
        return;
    }
}

// I4. The drivers who retire say goodbye at the season's last race
void farewells(State &st, int round)
{
    if (st.farewells_year == st.year)
        return;
    st.farewells_year = st.year;
    for (auto &[id, d] : st.drivers) {
        if (!d.retiring || d.kind == "legend" || d.kind == "player" || id == st.player)
            continue;
        CareerTotals c = career_totals(st, d);
        if (c.seasons == 0 && c.starts == 0)
            continue;
        std::string name = d.name;
        add_news(st, round, "farewell", [&](text::Lang l) { return text::h_farewell(l, name); },
                 [&](text::Lang l) { return text::farewell(l, name, c.seasons, c.starts, c.wins, c.podiums, c.titles); }, true);
        tag_news(st, id, "", d.team);
    }
}

// I2. The season's prize money by the constructors' order; then each budget drifts back towards its brand's level
void season_money(State &st, const Settings &s)
{
    if (!s.living || !anyone_scored(st))
        return;
    int n = static_cast<int>(std::count_if(st.teams.begin(), st.teams.end(), [](const Team &t) { return t.active; }));
    for (Team &t : st.teams) {
        if (!t.active)
            continue;
        int rank = rank_of(st, t);
        float prize = 14.0f - 26.0f * static_cast<float>(rank - 1) / static_cast<float>(std::max(1, n - 1));
        float base = std::max(50.0f, t.prestige * 1.4f - 20.0f);
        t.budget = clampf(t.budget + prize * 0.6f + (base - t.budget) * 0.15f, 50, 160);
    }
}

// H2. The season's numbers go into each driver's career
void bank_season(State &st)
{
    for (auto &[id, d] : st.drivers) {
        d.c_starts += d.starts;
        d.c_wins += d.wins;
        d.c_podiums += d.podiums;
        d.c_points += d.points;
        d.race_pos.clear();
        d.race_team.clear();
    }
}

} // namespace

bool cancel_change(State &st, const std::string &team, int seat, const std::string &driver)
{
    for (auto it = st.changes.rbegin(); it != st.changes.rend(); ++it) {
        State::Change &ch = *it;
        if (ch.team != team || ch.seat != seat || ch.in != driver)
            continue;
        Team *t = st.team(ch.team);
        Driver *out = st.driver(ch.out), *in = st.driver(ch.in);
        if (!t || !out || !in)
            return false;
        if (ch.kind == "swap") {
            Team *o = st.team(ch.other);
            if (!o)
                return false;
            set_seat(st, *t, ch.seat, ch.out);
            set_seat(st, *o, ch.other_seat, ch.in);
        } else {
            set_seat(st, *t, ch.seat, ch.out);
            out->released = false;
            out->contract = ch.out_contract;
            in->contract = ch.in_contract;
            for (const auto &[id, stage] : ch.deals)
                if (Deal *d = deal_by_id(st, id))
                    d->stage = static_cast<Stage>(stage);
            if (Team *r = st.team(ch.other); r && r->reserve.empty())
                r->reserve = ch.in; // the reserve called up goes back to being one
        }
        if (st.midseason_moves > 0)
            --st.midseason_moves;
        int year = st.year, round = ch.round, news = ch.news;
        std::string out_id = ch.out, in_id = ch.in;
        st.history.erase(std::remove_if(st.history.begin(), st.history.end(),
                                        [&](const Move &m) {
                                            return m.year == year && m.round == round &&
                                                   ((m.driver == out_id && (m.kind == "sacked" || m.kind == "demoted")) ||
                                                    (m.driver == in_id && (m.kind == "replaced" || m.kind == "promoted")));
                                        }),
                         st.history.end());
        st.news.erase(std::remove_if(st.news.begin(), st.news.end(), [&](const NewsItem &n) { return n.id == news; }),
                      st.news.end());
        st.changes.erase(std::next(it).base());
        return true;
    }
    return false;
}

int deal_odds(const State &st, const Settings &s, const Deal &d)
{
    if (d.stage == Stage::official)
        return 100;
    if (d.stage == Stage::collapsed)
        return 0;
    int rel = d.source >= 0 ? outlet_rating(st, d.source) : 0;
    float p = d.stage == Stage::rumour ? (rel > 0 ? 12.0f + 10.0f * static_cast<float>(rel) : 45.0f)
              : d.stage == Stage::talks ? (rel > 0 ? 52.0f + 4.0f * static_cast<float>(rel) : 62.0f)
                                        : 80.0f; // agreed: the paddock calls it talks, but it's close
    const Driver *drv = st.driver(d.driver);
    const Team *t = st.team(d.team);
    // what the paddock can read: does he want that seat, is he free to take it
    if (drv && t && s.personality && drv->ambition >= 0 && d.kind != "renewal")
        p += clampf(interest(st, *drv, *t, s), -12.0f, 10.0f) * 0.8f;
    if (drv && drv->contract > st.year && d.kind == "transfer" && !drv->wants_out)
        p -= 6.0f;
    if (drv && drv->wants_out)
        p += 6.0f;
    if (!d.waits.empty())
        p -= 8.0f;
    if (s.deadline && deadline_races_left(st) > 0 && d.stage != Stage::rumour)
        p += 8.0f;
    return static_cast<int>(std::lround(clampf(p, 5.0f, 95.0f)));
}

std::vector<Suitor> player_suitors(const State &st, const Settings &s)
{
    std::vector<Suitor> out;
    const Driver *me = st.driver(st.player);
    if (!me)
        return out;
    float mine = value(*me);
    int age = me->age(st.year + 1);
    for (const Team &t : st.teams) {
        if (!t.active)
            continue;
        Suitor x;
        x.team = t.id;
        if (t.id == st.player_team) {
            float keep = 55.0f + 14.0f * static_cast<float>(st.team_comfort) + 12.0f * me->form + (mine - 80.0f) * 1.5f +
                         5.0f * static_cast<float>(st.player_loyalty);
            x.interest = std::clamp(static_cast<int>(std::lround(keep)), 2, 98);
            x.status = "own";
            out.push_back(x);
            continue;
        }
        // the seat he'd take: the weaker of the two, easier when its contract runs out
        float weakest = 1e9f;
        bool open = false;
        for (const std::string &who : t.seats) {
            const Driver *d = st.driver(who);
            if (!d) {
                open = true;
                weakest = std::min(weakest, 60.0f);
                continue;
            }
            bool leaving = d->contract <= st.year || d->retiring || d->wants_out || d->released;
            open = open || leaving;
            weakest = std::min(weakest, value(*d) + (leaving ? -3.0f : 3.0f));
        }
        float score = 45.0f + (mine - weakest) * 3.0f + (open ? 15.0f : -15.0f) + me->form * 10.0f +
                      (st.player_loyalty < 0 ? 8.0f : st.player_loyalty > 0 ? -6.0f : 0.0f);
        if (s.strategies) {
            if (t.strategy == "youth" && age > 25)
                score -= 15.0f;
            if (t.strategy == "title")
                score += mine >= 85.0f ? 6.0f : -6.0f;
            if (t.strategy == "survival")
                score -= 5.0f;
        }
        // seats already promised to someone else
        int promised = 0;
        for (const Deal &d : st.deals)
            if (d.team == t.id && !d.fake && d.kind != "renewal" && (d.stage == Stage::official || d.stage == Stage::agreed))
                ++promised;
        score -= promised >= 2 ? 30.0f : promised == 1 && open ? 10.0f : 0.0f;
        x.interest = std::clamp(static_cast<int>(std::lround(score)), 2, 98);
        x.seat_open = open;
        x.status = x.interest >= 66 ? "keen" : x.interest >= 40 ? "watching" : "cool";
        out.push_back(x);
    }
    std::stable_sort(out.begin(), out.end(), [](const Suitor &a, const Suitor &b) {
        if ((a.status == "own") != (b.status == "own"))
            return a.status == "own";
        return a.interest > b.interest;
    });
    return out;
}

CareerTotals career_totals(const State &st, const Driver &d)
{
    CareerTotals c;
    c.starts = d.c_starts + d.starts;
    c.wins = d.c_wins + d.wins;
    c.podiums = d.c_podiums + d.podiums;
    c.points = d.c_points + d.points;
    c.titles = d.titles;
    c.seasons = d.seasons + (d.starts > 0 ? 1 : 0);
    std::set<std::string> teams;
    for (const Move &m : st.history)
        if (m.driver == d.id && !m.to.empty() && m.kind != "renewal")
            teams.insert(m.to);
    if (!d.team.empty())
        teams.insert(d.team);
    c.teams = static_cast<int>(teams.size());
    return c;
}

bool outside_signing(State &st, const std::string &driver, const std::string &team, int until, const std::string &source)
{
    if (!st.driver(driver) || !st.team(team) || until < st.year)
        return false;
    for (State::Outside &o : st.outside)
        if (o.driver == driver && o.team == team) {
            if (until > o.until) { // a longer deal: the contract follows (a repeat changes nothing)
                o.until = until;
                if (Driver *d = st.driver(driver); d && o.told && d->team == team)
                    d->contract = std::max(d->contract, until);
            }
            return true;
        }
    st.outside.push_back({driver, team, source, until, st.year, false});
    if (st.outside.size() > 40)
        st.outside.erase(st.outside.begin());
    return true;
}

int apply_outside(State &st)
{
    int told = 0;
    for (State::Outside &o : st.outside) {
        Driver *d = st.driver(o.driver);
        Team *t = st.team(o.team);
        if (!d || !t || d->team != o.team)
            continue; // not in that car (yet): the next read of the grid tells
        if (d->contract < o.until)
            d->contract = o.until;
        if (o.told)
            continue;
        o.told = true;
        ++told;
        d->released = false;
        if (d->salary <= 0)
            d->salary = estimate_salary(*d, t);
        // a deal the market can see, so it never plans that seat for someone else
        for (Deal &x : st.deals)
            if (x.driver == o.driver && x.stage != Stage::official)
                x.stage = Stage::collapsed;
        add_move(st, st.round, o.driver, "", o.team, "promoted"); // a move of this season, like a sister-team promotion
        std::string name = d->name, team = t->name, source = o.source;
        int until = o.until;
        add_news(st, st.round, "official", [&](text::Lang l) { return text::h_f2_promotion(l, name, team); },
                 [&](text::Lang l) { return text::f2_promotion(l, name, team, until, source); }, true);
        tag_news(st, o.driver, o.team, "", until);
    }
    return told;
}

HeadToHead head_to_head(const State &st, const Driver &d)
{
    HeadToHead h;
    std::string mate;
    if (const Team *t = st.team(d.team)) {
        for (const std::string &who : t->seats)
            if (!who.empty() && who != d.id)
                mate = who;
    } else if (!st.player.empty() && d.id == st.player && !st.own_mate.empty()) {
        mate = st.own_mate;
    } else if (!st.own_mate.empty() && d.id == st.own_mate) {
        mate = st.player;
    }
    const Driver *m = st.driver(mate);
    if (!m)
        return h;
    h.mate = mate;
    size_t n = std::min(d.race_pos.size(), m->race_pos.size());
    for (size_t i = 0; i < n; ++i) {
        if (d.race_pos[i] == 0 || m->race_pos[i] == 0 || i >= d.race_team.size() || i >= m->race_team.size() ||
            d.race_team[i] != m->race_team[i])
            continue;
        int a = d.race_pos[i] > 0 ? d.race_pos[i] : 99, b = m->race_pos[i] > 0 ? m->race_pos[i] : 99;
        if (a < b)
            ++h.ahead;
        else if (b < a)
            ++h.behind;
    }
    return h;
}
