// Writes every text the market makes over many careers (news, mails, press questions and answers, the Team HQ
// menu's reports) into a JSON file, for tools/texts.py to look for unfilled gaps, ids left in sight, texts too long
// for where they are shown and kinds that always say the same. Not shipped.
//
//   text_dump.exe <data/baseline.json> <out.json> [careers] [seasons]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/market.hpp"

using namespace ss;

namespace {

struct Lcg {
    uint64_t s;
    double next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
};

void race(State &st, Lcg &rng)
{
    std::vector<std::pair<double, std::string>> order;
    for (const Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const Driver *d = st.driver(who))
                order.push_back({t.performance * 0.7 + d->overall() * 0.5 + rng.next() * 18.0 - (rng.next() < 0.06 ? 100 : 0), who});
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); ++i)
        record_result(st, order[i].second, order[i].first < 0 ? 0 : static_cast<int>(i + 1));
    // some on-track events, so the stories they make are written too
    std::vector<TrackEvent> events;
    for (int i = 0; i < 14 && order.size() > 4; ++i) {
        const std::string &a = order[static_cast<size_t>(rng.next() * double(order.size()))].second;
        const std::string &b = order[static_cast<size_t>(rng.next() * double(order.size()))].second;
        if (a != b)
            events.push_back({i < 10 ? "overtake" : i < 13 ? "collision" : "retirement", a, i < 13 ? b : std::string(), 8});
    }
    Settings s;
    apply_track_events(st, s, events, st.round + 1);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("usage: text_dump <baseline.json> <out.json> [careers] [seasons]\n");
        return 2;
    }
    int careers = argc > 3 ? std::max(1, std::atoi(argv[3])) : 12, seasons = argc > 4 ? std::max(1, std::atoi(argv[4])) : 6;
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    swofty::json::Error err;
    Value baseline = swofty::json::parse(text.str(), &err);
    if (!baseline.is_object()) {
        std::printf("cannot read %s\n", argv[1]);
        return 2;
    }
    Value news = Value::array(), mails = Value::array(), press = Value::array(), reports = Value::array();
    std::set<std::string> names, teams;
    size_t items = 0;
    for (int c = 0; c < careers; ++c) {
        const char *group = c % 2 ? "f1_career_2025" : "f1_career_2026";
        Settings s;
        s.intensity = c % 3 == 2 ? Intensity::chaos : c % 3 == 1 ? Intensity::lively : Intensity::realistic;
        s.rumours = c % 2 ? Rumours::many : Rumours::normal;
        s.progression = true;
        s.lang = "en";
        State st;
        std::string error;
        if (!init(st, baseline, group, 9000 + static_cast<uint64_t>(c) * 131, &error)) {
            std::printf("init failed: %s\n", error.c_str());
            return 1;
        }
        // the player: a created driver in a seat (even careers), a real driver (odd careers)
        std::vector<GridCar> cars;
        const std::string human = c % 2 ? "george_russell" : "fernando_alonso";
        for (const Team &t : st.teams)
            for (const std::string &who : t.seats)
                if (!who.empty())
                    cars.push_back({who == human && c % 2 == 0 ? std::string("player") : who, t.id, who == human});
        sync_grid(st, cars);
        st.player_name = "You";
        localize(st, false);
        season_preview(st, s);
        Lcg rng{static_cast<uint64_t>(c) * 7 + 3};
        int last_news = 0, last_press = 0;
        auto take = [&](int season) {
            localize(st, false);
            for (const NewsItem &n : st.news)
                if (n.id > last_news) {
                    last_news = n.id;
                    news.push_back(Value::object({{"kind", n.kind}, {"title", n.title_en}, {"body", n.body_en}, {"career", c},
                                                  {"season", season}, {"official", n.official}, {"big", n.big}}));
                    ++items;
                }
            refresh_mails(st, s);
            for (const auto &[which, m] : {std::pair<const char *, const State::Mail &>{"preview", st.mail_preview},
                                           {"radar", st.mail_radar}, {"grid", st.mail_grid}})
                if (!m.subject.empty() || !m.body.empty())
                    mails.push_back(Value::object({{"which", which}, {"subject", m.subject}, {"body", m.body}, {"career", c}}));
            if (const State::Press *q = press_pending(st); q && q->id != last_press) {
                last_press = q->id;
                Value answers = Value::array();
                for (const std::string &a : q->answers_en)
                    answers.push_back(a);
                press.push_back(Value::object({{"topic", q->topic}, {"question", q->question_en}, {"answers", std::move(answers)},
                                               {"career", c}}));
                answer_press(st, s, static_cast<int>(rng.next() * 3));
            }
        };
        for (int season = 0; season < seasons; ++season) {
            for (int r = 1; r <= 24; ++r) {
                race(st, rng);
                complete_round(st, s, r, 24);
                take(season);
                if (r == 12 || r == 24)
                    for (const auto &[which, body] :
                         {std::pair<const char *, std::string>{"stock", report_stock(st, s)}, {"rumours", report_rumours(st, s)},
                          {"deals", report_deals(st, s)}, {"grid", report_grid(st, s)}, {"contracts", report_contracts(st, s)},
                          {"market", report_driver_market(st, s)}, {"history", report_history(st, s)}, {"f2", report_f2(st, s)},
                          {"staff", report_staff(st, s)}})
                        reports.push_back(Value::object({{"which", which}, {"text", body}, {"career", c}}));
            }
            start_new_season(st, s);
            take(season);
        }
        for (const auto &[id, d] : st.drivers)
            if (!d.name.empty())
                names.insert(d.name);
        for (const Team &t : st.teams) {
            teams.insert(t.name);
            if (!t.td_name.empty())
                names.insert(t.td_name);
            if (!t.tp_name.empty())
                names.insert(t.tp_name);
        }
    }
    Value out = Value::object();
    out["careers"] = careers;
    out["seasons"] = seasons;
    Value n = Value::array(), t = Value::array();
    for (const std::string &x : names)
        n.push_back(x);
    for (const std::string &x : teams)
        t.push_back(x);
    out["names"] = std::move(n);
    out["teams"] = std::move(t);
    out["news"] = std::move(news);
    out["mails"] = std::move(mails);
    out["press"] = std::move(press);
    out["reports"] = std::move(reports);
    std::ofstream o(argv[2], std::ios::binary);
    o << swofty::json::dump(out);
    std::printf("%d career(s) x %d season(s): %zu news item(s) written to %s\n", careers, seasons, items, argv[2]);
    return 0;
}
