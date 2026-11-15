// The fake career the host tests read: a career entity whose entrant rows, standings and contracts manager are laid
// out as the game's (the SDK's game headers), behind a SwoftyCareer table. Included after ../src/plugin.cpp by
// grid_test.cpp and, through fake_game.hpp, by plugin_test.cpp.
#pragma once
#include <cstdarg>

namespace fake {

std::vector<uint8_t> entity(0x1100), rows;

void put32(std::vector<uint8_t> &b, size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); }
void put16(std::vector<uint8_t> &b, size_t off, uint16_t v) { std::memcpy(b.data() + off, &v, 2); }

// one entrant row: driver and team by stat id (team -1: none; Mercedes is team 0), seat 0/1 (-1: no seat), and the
// look he wears, by its own stat id (-1: none), as the game holds it: the driver at +0x50, the look at +0x54
void add_row(uint32_t driver, int64_t team, int seat, bool human, int64_t look = -1)
{
    size_t off = rows.size();
    rows.resize(off + 0x88);
    put32(rows, off + 0x48, human ? 0x10000000u : 7u);
    put32(rows, off + 0x50, (driver << 16) | 0x00);
    put32(rows, off + 0x54, look < 0 ? 0x3fff0000u : static_cast<uint32_t>(look) << 16);
    put32(rows, off + 0x58, team < 0 ? 0x3fff0000u : static_cast<uint32_t>(team) << 16);
    put32(rows, off + 0x5c, seat < 0 ? 0x3fff0000u : 0x00120000u);
    put16(rows, off + 0x80, seat < 0 ? 0 : static_cast<uint16_t>(0x100 | seat));
}

void publish()
{
    uintptr_t b = reinterpret_cast<uintptr_t>(rows.data()), e = b + rows.size();
    std::memcpy(entity.data() + 0x2e0, &b, 8);
    std::memcpy(entity.data() + 0x2e8, &e, 8);
    entity[0x2f8] = 0;
}

int available() { return 1; }
// the career state: unavailable (grid_test) until a test sets it (plugin_test: the mode, the key, the player)
SwoftyCareerState career_state{};
bool have_state = false;
int state(SwoftyCareerState *out)
{
    *out = have_state ? career_state : SwoftyCareerState{};
    return have_state ? SWOFTY_ASSET_OK : SWOFTY_ASSET_UNAVAILABLE;
}
void *get_entity() { return entity.data(); }

void putp(std::vector<uint8_t> &b, size_t off, const void *p)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(b.data() + off, &v, 8);
}

// the season's standings: a championship whose season 0 holds a weekend and results rounds
std::vector<uint8_t> champ(0x5b0), season_list(0x10), season(0x550), weekends(8), results_rounds;
std::vector<std::vector<uint8_t>> sessions, records;

// a round whose race classified these drivers (stat ids) in this order
void add_round(const std::vector<uint32_t> &order)
{
    records.emplace_back(order.size() * 0x10);
    std::vector<uint8_t> &rec = records.back();
    for (size_t i = 0; i < order.size(); ++i) {
        put32(rec, i * 0x10, order[i] << 16);
        put32(rec, i * 0x10 + 4, static_cast<uint32_t>(i)); // the game counts from 0
    }
    sessions.emplace_back(0x28);
    std::vector<uint8_t> &ses = sessions.back();
    putp(ses, 8, rec.data());
    putp(ses, 16, rec.data() + rec.size());
}

void publish_standings()
{
    results_rounds.assign(sessions.size() * 0x20, 0);
    for (size_t r = 0; r < sessions.size(); ++r) {
        putp(results_rounds, r * 0x20 + 8, sessions[r].data());
        putp(results_rounds, r * 0x20 + 16, sessions[r].data() + sessions[r].size());
    }
    putp(season, 0x288, weekends.data());
    putp(season, 0x290, weekends.data() + weekends.size());
    putp(season, 0x2a8 + 0xd0, results_rounds.data());
    putp(season, 0x2a8 + 0xd8, results_rounds.data() + results_rounds.size());
    putp(season_list, 8, season.data());
    putp(champ, 0x548, season_list.data());
    putp(champ, 0x550, season_list.data() + season_list.size());
    put32(champ, 0x560, 0);
    putp(entity, 0x1058, champ.data());
}

// the contracts manager: one driver's secret meetings, the current one with a team
std::vector<uint8_t> manager(0x400), meeting_list(0x10), meetings_of(0x50), meeting(0x24);
void set_meeting(uint32_t driver, uint32_t id, uint32_t team)
{
    put32(meeting, 0, id);
    meeting[0x14] = 2;
    put32(meeting, 0x20, team << 16);
    putp(meetings_of, 0x18, meeting.data());
    put32(meetings_of, 0x40, driver << 16);
    putp(meeting_list, 8, meetings_of.data());
    putp(manager, 0x3a8, meeting_list.data());
    putp(manager, 0x3b0, meeting_list.data() + meeting_list.size());
}

void *component(const char *name)
{
    return std::string_view(name) == "DriverCareerContractsManagerComponent" ? manager.data() : nullptr;
}

const SwoftyCareer career = {available, state, nullptr, nullptr, get_entity, component, nullptr, nullptr, nullptr, nullptr};

// every line the addon logs, kept for the tests to read; printed unless quiet
std::vector<std::string> log_lines;
bool log_quiet = false;
void log_(const char *fmt, ...)
{
    char buf[4096];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    log_lines.emplace_back(buf);
    if (!log_quiet)
        std::printf("%s\n", buf);
}

} // namespace fake
