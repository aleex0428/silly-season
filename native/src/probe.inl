// 1.1.0 phase 0: a probe of the career data Silly Season wants to read next (race results and standings,
// the drivers' contracts, the player's secret meetings and the moves the game itself has planned). It only
// reads, through the guarded copies of the real-grid reader, and writes what it finds to the log at an idle
// Team HQ when it changed. Included by plugin.cpp inside its anonymous namespace, after the rows reader.
// Layouts: the SDK's game headers (swofty/game/career.hpp, game.hpp), marked experimental there.

namespace probe {

uintptr_t ptr(uintptr_t at)
{
    uintptr_t v = 0;
    return rows::copy(&v, at, sizeof v) ? v : 0;
}
uint32_t u32(uintptr_t at)
{
    uint32_t v = 0;
    rows::copy(&v, at, sizeof v);
    return v;
}
int32_t i32(uintptr_t at) { return static_cast<int32_t>(u32(at)); }
uint16_t u16(uintptr_t at)
{
    uint16_t v = 0;
    rows::copy(&v, at, sizeof v);
    return v;
}
uint8_t u8(uintptr_t at)
{
    uint8_t v = 0;
    rows::copy(&v, at, sizeof v);
    return v;
}

// a [begin, end) array of `stride` elements, at most `max`; 0 when it doesn't look like one
size_t count(uintptr_t begin, uintptr_t end, size_t stride, size_t max)
{
    if (!begin || end < begin || (end - begin) % stride || (end - begin) / stride > max)
        return 0;
    return (end - begin) / stride;
}

std::string hex(uintptr_t at, size_t n)
{
    std::vector<uint8_t> b(n);
    if (!rows::copy(b.data(), at, n))
        return "(unreadable)";
    std::string out;
    char t[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(t, sizeof t, "%02x", b[i]);
        out += t;
        if (i % 4 == 3 && i + 1 < n)
            out += ' ';
    }
    return out;
}

// a driver key ("stat << 16 | domain") as the market's driver
std::string who(uint32_t key)
{
    std::string id = known_driver(key >> 16);
    return id.empty() ? "stat " + std::to_string(key >> 16) : id;
}

std::string g_said; // the last report, so an unchanged one isn't logged again

void standings(std::string &out)
{
    char line[256];
    uintptr_t e = reinterpret_cast<uintptr_t>(career::entity());
    uintptr_t champ = e ? ptr(e + 0x1058) : 0;
    if (!champ) {
        out += "standings: no current championship\n";
        return;
    }
    uintptr_t sb = ptr(champ + 0x548), se = ptr(champ + 0x550);
    uint32_t index = u32(champ + 0x560);
    size_t seasons = count(sb, se, 0x10, 64);
    std::snprintf(line, sizeof line, "standings: championship 0x%llx, %zu season(s), season index %u\n",
                  static_cast<unsigned long long>(champ), seasons, index);
    out += line;
    if (!seasons || index >= seasons)
        return;
    // an entry is {allocator, Season *}: the season sits at +8 (+0 is the allocator every list shares)
    uintptr_t season = ptr(sb + index * 0x10 + 8);
    if (!season || !count(ptr(season + 0x288), ptr(season + 0x290), 8, 64))
        season = ptr(sb + index * 0x10);
    if (!season)
        return;
    size_t weekends = count(ptr(season + 0x288), ptr(season + 0x290), 8, 64);
    std::snprintf(line, sizeof line, "standings: season 0x%llx, %zu weekend(s), current weekend %u, completion %u\n",
                  static_cast<unsigned long long>(season), weekends, u32(season + 0x2a0), u32(season + 0x538));
    out += line;
    uintptr_t st = season + 0x2a8; // F1SeasonStandings, embedded
    // results (+0xd0): rounds of 0x20 {_, sessions begin, end, _}; sessions of 0x28 {_, records begin, end, _, id};
    // records {u32 key, i32 position, i32 fastest-lap rank, u32 status}
    for (int which = 0; which < 2; ++which) {
        size_t field = which == 0 ? 0xd0 : 0x20, record = which == 0 ? 0x10 : 8;
        const char *name = which == 0 ? "results" : "points";
        size_t rounds = count(ptr(st + field), ptr(st + field + 8), 0x20, 64);
        std::snprintf(line, sizeof line, "%s: %zu round(s)\n", name, rounds);
        out += line;
        for (size_t r = 0; r < rounds; ++r) {
            uintptr_t round = ptr(st + field) + r * 0x20;
            uintptr_t ssb = ptr(round + 8), sse = ptr(round + 16);
            size_t sessions = count(ssb, sse, 0x28, 64);
            std::string ids;
            for (size_t k = 0; k < sessions; ++k) {
                uintptr_t s = ssb + k * 0x28;
                size_t recs = count(ptr(s + 8), ptr(s + 16), record, 128);
                ids += (k ? ", " : "") + std::to_string(u32(s + 0x20)) + "/" + std::to_string(recs);
            }
            std::snprintf(line, sizeof line, "%s: round %zu: %zu session(s) [id/records: %s]\n", name, r + 1, sessions, ids.c_str());
            out += line;
            // the last session of the last round in full (first 22 records)
            if (r + 1 == rounds && sessions) {
                uintptr_t s = ssb + (sessions - 1) * 0x28, rb = ptr(s + 8);
                size_t recs = count(rb, ptr(s + 16), record, 128);
                std::string list;
                for (size_t i = 0; i < recs && i < 22; ++i) {
                    uintptr_t at = rb + i * record;
                    uint32_t key = u32(at);
                    char one[96];
                    if (which == 0)
                        std::snprintf(one, sizeof one, "%s%s P%d fl%d st%u", i ? "; " : "", who(key).c_str(), i32(at + 4), i32(at + 8),
                                      u32(at + 12));
                    else
                        std::snprintf(one, sizeof one, "%s%s %dpt", i ? "; " : "", who(key).c_str(), i32(at + 4));
                    list += one;
                }
                out += std::string(name) + ": last session: " + list + "\n";
                if (recs)
                    out += std::string(name) + ": first record raw: " + hex(rb, record) + "\n";
            }
        }
    }
}

void contracts(std::string &out)
{
    char line[256];
    void *c = career::component("DriverCareerContractsManagerComponent");
    if (!c)
        c = career::component("k_Career_DriverCareerContractsManagerComponentDesc");
    if (!c) {
        out += "contracts: no DriverCareerContractsManagerComponent\n";
        return;
    }
    uintptr_t comp = reinterpret_cast<uintptr_t>(c);
    uintptr_t owner = ptr(comp + 0x350);
    uintptr_t cb = owner ? ptr(owner + 0x298) : 0, ce = owner ? ptr(owner + 0x2a0) : 0;
    std::snprintf(line, sizeof line, "contracts: component 0x%llx, owner 0x%llx, contracts span %lld byte(s)\n",
                  static_cast<unsigned long long>(comp), static_cast<unsigned long long>(owner),
                  static_cast<long long>(ce >= cb ? ce - cb : -1));
    out += line;
    // entries {allocator, DriverContract *}: driver key +0x50, team key +0x0, signed day +0xc, kind +0xe,
    // end day +0x10, state +0x12, end value +0x14, active +0x2c
    size_t n = count(cb, ce, 0x10, 512);
    std::snprintf(line, sizeof line, "contracts: %zu contract(s)\n", n);
    out += line;
    for (size_t i = 0; i < n && i < 30; ++i) {
        uintptr_t c = ptr(cb + i * 0x10 + 8);
        if (!c)
            continue;
        uint32_t team = u32(c), driver = u32(c + 0x50);
        std::string tid = team_by_stat(team >> 16);
        std::snprintf(line, sizeof line, "contracts: %s -> %s (team %u): signed day %u, end day %u, kind %u, state %u, end value %u, active %u\n",
                      who(driver).c_str(), tid.empty() ? "?" : tid.c_str(), team >> 16, u16(c + 0xc), u16(c + 0x10), u8(c + 0xe),
                      u8(c + 0x12), u32(c + 0x14), u8(c + 0x2c));
        out += line;
    }
    if (n)
        out += "contracts: first raw: " + hex(ptr(cb + 8), 0x58) + "\n";
    // secret meetings (+0x3a8/+0x3b0)
    uintptr_t mb = ptr(comp + 0x3a8), me = ptr(comp + 0x3b0);
    std::snprintf(line, sizeof line, "meetings: span %lld byte(s)\n", static_cast<long long>(me >= mb ? me - mb : -1));
    out += line;
    // entries {allocator, DriverSecretMeetings *}: current meeting +0x18 ({id, reason +0x14, team +0x20}),
    // previous meetings +0x28/+0x30, driver +0x40
    size_t m = count(mb, me, 0x10, 64);
    for (size_t i = 0; i < m; ++i) {
        uintptr_t dm = ptr(mb + i * 0x10 + 8);
        if (!dm)
            continue;
        uintptr_t cur = ptr(dm + 0x18);
        long long prev = static_cast<long long>(ptr(dm + 0x30)) - static_cast<long long>(ptr(dm + 0x28));
        std::snprintf(line, sizeof line, "meetings: driver %s (raw %u): current %s, previous span %lld\n", who(u32(dm + 0x40)).c_str(),
                      u32(dm + 0x40), cur ? "yes" : "none", prev);
        out += line;
        if (cur) {
            uint32_t team = u32(cur + 0x20);
            std::string tid = team_by_stat(team >> 16);
            if (tid.empty())
                tid = team_by_stat(team);
            std::snprintf(line, sizeof line, "meetings: current: id %u, reason %u, team raw %u (%s)\n", u32(cur), u8(cur + 0x14), team,
                          tid.empty() ? "?" : tid.c_str());
            out += line;
            out += "meetings: current raw: " + hex(cur, 0x24) + "\n";
        }
    }
}

void moves(std::string &out)
{
    char line[256];
    void *c = career::component("CareerDriverContractRenewalComponent");
    if (!c)
        c = career::component("k_Career_CareerDriverContractRenewalComponentDesc");
    if (!c) {
        out += "moves: no CareerDriverContractRenewalComponent\n";
        return;
    }
    uintptr_t comp = reinterpret_cast<uintptr_t>(c);
    uintptr_t mb = ptr(comp + 0x3b0), me = ptr(comp + 0x3b8);
    size_t n = count(mb, me, 0x18, 256);
    std::snprintf(line, sizeof line, "moves: %zu planned move(s), driver moves %s\n", n, u8(comp + 0x3a1) ? "disabled" : "enabled");
    out += line;
    for (size_t i = 0; i < n && i < 24; ++i) {
        uintptr_t at = mb + i * 0x18;
        uint32_t old_team = u32(at + 4), new_team = u32(at + 8);
        std::snprintf(line, sizeof line, "moves: %s: team %u -> %u, reason %u, weekend %u, day %u\n", who(u32(at)).c_str(),
                      old_team >> 16, new_team >> 16, u8(at + 0xc), u32(at + 0x10), u16(at + 0x14));
        out += line;
    }
}

// g_lock held, at an idle Team HQ
void run()
{
    std::string out;
    standings(out);
    contracts(out);
    moves(out);
    if (out == g_said)
        return;
    g_said = out;
    size_t at = 0;
    while (at < out.size()) {
        size_t nl = out.find('\n', at);
        std::string one = out.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        log("[silly-season] probe: %s", one.c_str());
        if (nl == std::string::npos)
            break;
        at = nl + 1;
    }
}

} // namespace probe
