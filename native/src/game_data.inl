// 1.1.0 phase 1: the season the game really ran. Race results come from the current season's standings
// (F1SeasonStandings, embedded in Season at +0x2a8: results rounds at +0xd0), the player's secret meetings
// from the contracts manager. Layouts: the SDK's game headers, marked experimental; the core's own standings
// code reads the same chain (CareerEntity +0x1058 -> Championship, its seasons as {allocator, Season *}).
// Every read is guarded and validated; anything that doesn't look right is left out and the market goes on
// as before (a simulated race, the round from the game's calendar). Included by plugin.cpp after probe.inl.

namespace game_data {

using probe::count;
using probe::i32;
using probe::ptr;
using probe::u32;
using probe::u8;

struct Finish {
    uint32_t stat = 0; // the driver's stat id
    int position = 0;  // 1.., 0 when not classified
};
using Round = std::vector<Finish>;

// the current season of the loaded career's championship
uintptr_t current_season()
{
    uintptr_t e = reinterpret_cast<uintptr_t>(career::entity());
    uintptr_t champ = e ? ptr(e + 0x1058) : 0;
    if (!champ)
        return 0;
    uintptr_t sb = ptr(champ + 0x548), se = ptr(champ + 0x550);
    size_t seasons = count(sb, se, 0x10, 64);
    uint32_t index = u32(champ + 0x560);
    if (!seasons || index >= seasons)
        return 0;
    uintptr_t season = ptr(sb + index * 0x10 + 8);
    // a season holds its weekends (+0x288/+0x290): one without any is not a season
    if (!season || !count(ptr(season + 0x288), ptr(season + 0x290), 8, 64))
        return 0;
    return season;
}

// one round's race: the session of the round with the most classified drivers (the race; qualifying, when
// the round holds one too, has the same count, so the last of equals wins). 1: read, 0: not run yet, -1: bad
int read_round(uintptr_t round, Round &out, std::string &why)
{
    out.clear();
    uintptr_t sb = ptr(round + 8), se = ptr(round + 16);
    size_t sessions = sb == se ? 0 : count(sb, se, 0x28, 16);
    if (!sessions)
        return sb == se ? 0 : (why = "a round without readable sessions", -1);
    uintptr_t best = 0;
    size_t best_n = 0;
    for (size_t k = 0; k < sessions; ++k) {
        uintptr_t s = sb + k * 0x28;
        size_t n = count(ptr(s + 8), ptr(s + 16), 0x10, 64);
        if (n >= best_n) {
            best_n = n;
            best = s;
        }
    }
    if (best_n == 0)
        return 0;
    if (best_n < 10 || best_n > 30) {
        why = std::to_string(best_n) + " classified cars";
        return -1;
    }
    // the game counts positions from 0: the winner's record says 0 (seen in game, 9 Nov: Norris 0 with 25 points)
    std::set<int> seen;
    uintptr_t rb = ptr(best + 8);
    for (size_t i = 0; i < best_n; ++i) {
        uintptr_t at = rb + i * 0x10;
        uint32_t key = u32(at);
        int pos = i32(at + 4);
        if (pos < 0 || pos >= 30 || !seen.insert(pos).second) {
            why = "positions that don't add up";
            out.clear();
            return -1;
        }
        out.push_back({key >> 16, pos + 1});
    }
    return 1;
}

// every round the season has run so far; false when the standings can't be read or don't look right
bool read_results(std::vector<Round> &rounds, std::string &why)
{
    rounds.clear();
    uintptr_t season = current_season();
    if (!season) {
        why = "no current season";
        return false;
    }
    uintptr_t st = season + 0x2a8; // F1SeasonStandings
    uintptr_t rb = ptr(st + 0xd0), re = ptr(st + 0xd8);
    size_t n = re == rb ? 0 : count(rb, re, 0x20, 64);
    if (re != rb && !n) {
        why = "the results don't look like rounds";
        return false;
    }
    for (size_t r = 0; r < n; ++r) {
        Round round;
        std::string w;
        int got = read_round(rb + r * 0x20, round, w);
        if (got == 0)
            break; // the first round not run yet: the season so far ends here
        if (got < 0) {
            why = "round " + std::to_string(r + 1) + ": " + w;
            rounds.clear();
            return false;
        }
        rounds.push_back(std::move(round));
    }
    return true;
}

struct Meeting {
    uint32_t id = 0, team = 0, reason = 0; // team: the stat id
};

// the player's current secret meeting with another team, when the game has one on his calendar
bool read_meeting(uint32_t player_stat, Meeting &out)
{
    void *c = career::component("DriverCareerContractsManagerComponent");
    if (!c)
        c = career::component("k_Career_DriverCareerContractsManagerComponentDesc");
    if (!c)
        return false;
    uintptr_t comp = reinterpret_cast<uintptr_t>(c);
    uintptr_t mb = ptr(comp + 0x3a8), me = ptr(comp + 0x3b0);
    size_t m = count(mb, me, 0x10, 64);
    for (size_t i = 0; i < m; ++i) {
        uintptr_t dm = ptr(mb + i * 0x10 + 8); // DriverSecretMeetings
        if (!dm || (u32(dm + 0x40) >> 16) != player_stat)
            continue;
        uintptr_t cur = ptr(dm + 0x18); // SecretMeeting {id, reason +0x14, team +0x20}
        if (!cur)
            return false;
        uint32_t team = u32(cur + 0x20);
        out = {u32(cur), team >= 0x10000 ? team >> 16 : team, u8(cur + 0x14)};
        if (!out.team || out.team >= 0x3fff)
            return false;
        // the game's meeting ids can be 0 (seen in game, 9 Nov): the meeting is told apart by its team,
        // its reason and the two words before the team (+0x18, +0x1c), never 0
        out.id = (out.team << 20) ^ (out.reason << 16) ^ (u32(cur + 0x18) << 4) ^ u32(cur + 0x1c) ^ u32(cur) ^ 0x5e5e0001u;
        return true;
    }
    return false;
}

} // namespace game_data
