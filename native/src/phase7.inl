// Silly Season 1.1.0, from player reports (2026-11-10): a seat of the new grid the game refuses is taken back, and
// careers whose seats only the game can change (My Team) get an observer market. Included by market.cpp after
// phase6.inl.

namespace {

// the player has seen this item: in a popup, the banner or the panel
bool news_seen(const State &st, const NewsItem &n)
{
    return n.shown || n.id <= st.seen_news || n.id <= st.banner_news;
}

// the grid item of the last winter, told again from the seats the market holds now (the game's), while unseen
void refresh_grid_news(State &st)
{
    for (auto it = st.news.rbegin(); it != st.news.rend(); ++it) {
        if (it->kind != "grid")
            continue;
        if (news_seen(st, *it) || it->year < st.year - 1)
            return;
        auto body = [&](text::Lang l) {
            std::string b;
            for (const Team &t : st.teams) {
                if (!t.active)
                    continue;
                b += t.name + ": " + (t.seats[0].empty() ? text::free_seat(l) : driver_name(st, t.seats[0])) + " / " +
                     (t.seats[1].empty() ? text::free_seat(l) : driver_name(st, t.seats[1])) + "\n";
            }
            return b;
        };
        it->body_en = body(kEn);
        it->body_es = body(kEs);
        it->body = st.view_es ? it->body_es : it->body_en;
        return;
    }
}

// the new season's opening item, its newcomers told again from the winter's moves that the game bore out, while unseen
void refresh_season_summary(State &st)
{
    for (auto it = st.news.rbegin(); it != st.news.rend(); ++it) {
        if (it->kind != "summary" || it->year != st.year || it->round != 0 || it->title_en != text::t_season(kEn, st.year))
            continue;
        if (news_seen(st, *it))
            return;
        std::vector<std::string> list;
        for (const Move &m : st.history)
            if (m.year == st.year - 1 && !m.to.empty() && (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback"))
                if (const Driver *d = st.driver(m.driver); d && d->team == m.to)
                    list.push_back(driver_name(st, m.driver) + " (" + team_name(st, m.to) + ")");
        std::string joined;
        for (size_t i = 0; i < list.size(); ++i)
            joined += (i ? ", " : "") + list[i];
        it->body_en = text::season_start(kEn, st.year, static_cast<int>(list.size()), joined);
        it->body_es = text::season_start(kEs, st.year, static_cast<int>(list.size()), joined);
        it->body = st.view_es ? it->body_es : it->body_en;
        return;
    }
}

} // namespace

bool season_change_refused(State &st, const std::string &team, const std::string &driver)
{
    const Driver *d = st.driver(driver);
    if (d && d->team == team)
        return false; // the game has him in that car after all
    bool told = false;
    for (auto it = st.news.end(); it != st.news.begin();) {
        --it;
        if (it->kind != "official" || it->driver != driver || it->to != team || it->year < st.year - 1)
            continue;
        told = true;
        if (!news_seen(st, *it)) {
            st.news.erase(it); // never read: as if it was never told
        } else {
            std::string dn = driver_name(st, driver), tn = team_name(st, team);
            std::string now = d && !d->team.empty() ? team_name(st, d->team) : std::string();
            add_news(st, st.round, "collapse", [&](text::Lang l) { return text::h_deal_off(l, dn, tn); },
                     [&](text::Lang l) { return text::deal_off(l, dn, tn, now); }, true);
            tag_news(st, driver, team, d ? d->team : std::string());
        }
        break;
    }
    st.history.erase(std::remove_if(st.history.begin(), st.history.end(),
                                    [&](const Move &m) {
                                        return m.driver == driver && m.to == team && m.year >= st.year - 1 &&
                                               (m.kind == "transfer" || m.kind == "promotion" || m.kind == "comeback");
                                    }),
                     st.history.end());
    refresh_grid_news(st);
    refresh_season_summary(st);
    return told;
}

void become_observer(State &st)
{
    if (st.observer)
        return;
    st.observer = true;
    st.pending.clear();  // nothing the market planned goes to the game from now on
    st.standins.clear();
    add_news(st, st.round, "summary", [](text::Lang l) { return text::h_observer(l); },
             [](text::Lang l) { return text::observer(l); }, true);
}

int observe_grid(State &st, const Settings &s, const std::map<std::string, std::string> &before, bool winter)
{
    if (!st.observer || st.real || st.season_index < 0 || before.empty())
        return 0;
    // a read that misses cars says nothing about who moved
    size_t seats = 0, seated = 0;
    for (const Team &t : st.teams)
        if (t.active)
            for (const std::string &who : t.seats) {
                ++seats;
                seated += who.empty() ? 0 : 1;
            }
    if (seated + 2 < seats)
        return 0;
    struct Seen {
        std::string driver, from, to;
    };
    std::vector<Seen> moves;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats) {
            if (who.empty() || who == st.player || who == st.own_mate)
                continue;
            auto it = before.find(who);
            std::string from = it == before.end() ? std::string() : it->second;
            if (from != t.id)
                moves.push_back({who, from, t.id});
        }
    // more than half the grid at once: another career's market, not a winter's moves. The market just follows.
    if (moves.empty() || moves.size() > seats / 2)
        return 0;
    StateRng sr(st);
    Rng &r = sr.r;
    int year = st.year + (winter ? 1 : 0); // the season the move is for
    int round = winter ? std::max(st.round, st.rounds) : st.round;
    for (const Seen &m : moves) {
        Driver *d = st.driver(m.driver);
        if (!d)
            continue;
        // the story that had it right comes true; his stories of other teams didn't
        Deal *deal = nullptr;
        for (Deal &x : st.deals) {
            if (x.driver != m.driver || x.stage == Stage::collapsed)
                continue;
            if (!deal && x.team == m.to && !x.fake && x.kind != "renewal")
                deal = &x;
            else
                x.stage = Stage::collapsed;
        }
        if (!deal) {
            std::string kind = !m.from.empty() ? "transfer" : d->seasons == 0 && d->c_starts == 0 ? "promotion" : "comeback";
            Rng lr(mix(st.seed, hash_str(m.driver) ^ static_cast<uint64_t>(year)));
            int until = year - 1 + contract_length(lr, *d, year, s.personality);
            deal = &add_deal(st, {m.driver, m.to, kind, year, until, Stage::agreed, false, st.round});
        }
        if (deal->until < year)
            deal->until = year;
        // the signing's story names the team he leaves: the read already moved him, so for the story he is where he was
        std::string now_team = d->team;
        int now_seat = d->seat;
        d->team = m.from;
        announce_official(st, s, r, *deal, round, true);
        d->team = now_team;
        d->seat = now_seat;
        d->contract = std::max(d->contract, deal->until);
        d->released = false;
        add_move(st, round, m.driver, m.from, m.to, deal->kind);
    }
    score_outlets(st);
    return static_cast<int>(moves.size());
}

bool observe_game_ratings(State &st, const std::string &driver, float overall, float experience, float racecraft, float focus,
                          float awareness, float pace)
{
    Driver *d = st.driver(driver);
    if (!d || d->regen)
        return false;
    auto ok = [](float v) { return std::isfinite(v) && v > 0 && v <= 100; };
    if (!ok(overall) || !ok(experience) || !ok(racecraft) || !ok(awareness) || !ok(pace))
        return false;
    d->game_overall = overall;
    d->experience = experience;
    d->racecraft = racecraft;
    d->awareness = awareness;
    d->pace = pace;
    d->focus = ok(focus) ? focus : 0;
    // the peaks are the market's guess at his ceiling: never below what he already shows
    d->experience_peak = std::max(d->experience_peak, experience);
    d->racecraft_peak = std::max(d->racecraft_peak, racecraft);
    d->awareness_peak = std::max(d->awareness_peak, awareness);
    d->pace_peak = std::max(d->pace_peak, pace);
    d->ratings_seen = true;
    return true;
}
