// 1.1.0: the game's own ratings of each driver in this career, read at an idle Team HQ (a 2026-11-10 report: the
// hub showed the player 88 where the game's ratings screen said 91). The game works them out from five statistics
// (experience, racecraft, focus, awareness, pace) on curves through the career, plus perks, specialists and the car;
// the market modelled four from the baseline and never saw where the game took them.
// Read only, through the game's own getters, as the core's career_ops/stats.cpp reads them before a write:
// DriverStatisticsComponent (the Driver Career player's subclass when there is one), its driver table at +0x310
// (entries of 0x50: kind +0x0, stat id +0x2), the overall from Career_Get_DriverOverallRating, each statistic from
// the component's vtable getter (+0x280) at the career's progress (+0x278: a driver whose statistics don't move with
// time reads at 0). Layouts: the SDK's game headers, marked experimental. Anything that doesn't resolve or doesn't
// look right leaves the market's own numbers. Included by plugin.cpp after game_data.inl, inside its anonymous
// namespace.

namespace game_ratings {

using probe::count;
using probe::ptr;
using probe::u16;
using probe::u32;
using probe::u8;

// statistic ids of the game's driver statistics (the info stat ids)
constexpr uint16_t kExperience = 0, kRacecraft = 1, kFocus = 2, kAwareness = 3, kPace = 4;

struct Reading {
    uint32_t stat = 0;  // the driver's stat id
    uint8_t kind = 0;   // the entry's kind byte
    float overall = 0;
    float v[5] = {0, 0, 0, 0, 0}; // by statistic id
};

using OverallFn = float (*)(void *self, void *data, const uint32_t *driver, bool round);
using GetStatFn = float (*)(void *self, void *data, const uint32_t *driver, const uint16_t *info, const uint8_t *ctx, float x,
                            bool patched, bool clamp, bool round);
using FixedFn = bool (*)(void *self, const uint32_t *driver);
using CtxFn = void (*)(void *self, uint8_t *out);
using LengthFn = uint32_t (*)(void *time);

constexpr uintptr_t kCompCtx = 0x278, kCompTime = 0x290, kCompData = 0x2a8, kCompGate = 0x2a9, kCompApplied = 0x2ac,
                    kCompDrivers = 0x310;
constexpr uintptr_t kTimeLength = 0x290, kTimeDay = 0x2b0;
constexpr uintptr_t kEntrySize = 0x50, kEntryKind = 0x0, kEntryStat = 0x2;
constexpr uintptr_t kVtFixed = 0x278, kVtGet = 0x280, kCtxVtFill = 0xc8;

uintptr_t resolve(const char *id)
{
    return swofty::api && swofty::api->resolve ? swofty::api->resolve(id) : 0;
}

// the game's calls, each behind its own guard: a fault reads as "not read", never as a crash
float call_overall(OverallFn f, void *self, void *data, const uint32_t *key)
{
    __try {
        return f(self, data, key, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NAN;
    }
}
float call_get(GetStatFn f, void *self, void *data, const uint32_t *key, const uint16_t *info, const uint8_t *ctx, float x,
               bool patched)
{
    __try {
        return f(self, data, key, info, ctx, x, patched, true, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NAN;
    }
}
int call_fixed(FixedFn f, void *self, const uint32_t *key)
{
    __try {
        return f(self, key) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
bool call_ctx(CtxFn f, void *self, uint8_t *out)
{
    __try {
        f(self, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uint32_t call_length(LengthFn f, void *time)
{
    __try {
        return f(time);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void *component_by(const char *desc_id)
{
    uintptr_t desc = resolve(desc_id);
    return desc ? career::component_by_desc(reinterpret_cast<void *>(desc)) : nullptr;
}

// every driver of the career's statistics table; false (and why) when it can't be read. The flow thread only.
bool read_all(std::vector<Reading> &out, std::string &why)
{
    out.clear();
    auto overall = reinterpret_cast<OverallFn>(resolve("Career_Get_DriverOverallRating"));
    auto length = reinterpret_cast<LengthFn>(resolve("Career_Get_CareerLengthDays_Thunk"));
    if (!overall || !length || !swofty::mem::in_image(reinterpret_cast<uintptr_t>(overall)))
        return why = "Career_Get_DriverOverallRating or Career_Get_CareerLengthDays_Thunk doesn't resolve", false;
    void *comp = component_by("k_Career_DriverCareerPlayerDriverStatisticsComponentDesc");
    if (!comp)
        comp = component_by("k_Career_DriverStatisticsComponentDesc");
    if (!comp)
        return why = "no DriverStatisticsComponent on the career", false;
    uintptr_t at = reinterpret_cast<uintptr_t>(comp), vt = ptr(at);
    uintptr_t get_at = vt ? ptr(vt + kVtGet) : 0, fixed_at = vt ? ptr(vt + kVtFixed) : 0;
    if (!swofty::mem::in_image(get_at) || !swofty::mem::in_image(fixed_at))
        return why = "the statistics component's getters aren't the game's", false;
    auto get = reinterpret_cast<GetStatFn>(get_at);
    auto fixed = reinterpret_cast<FixedFn>(fixed_at);
    uintptr_t b = ptr(at + kCompDrivers), e = ptr(at + kCompDrivers + 8);
    size_t n = count(b, e, kEntrySize, 1024);
    if (!n)
        return why = "the driver table is empty or unreadable", false;
    // the statistics' context (the game fills it per call) and the patched-curve gate, as the game's overall uses them
    uint8_t ctx[16] = {};
    if (uintptr_t obj = ptr(at + kCompCtx))
        if (uintptr_t ovt = ptr(obj); ovt)
            if (uintptr_t fill = ptr(ovt + kCtxVtFill); swofty::mem::in_image(fill))
                call_ctx(reinterpret_cast<CtxFn>(fill), reinterpret_cast<void *>(obj), ctx);
    bool patched = u8(at + kCompGate) == 0 && u32(at + kCompApplied) >= 2;
    // the career's progress, 0..1: today against the career's length in days
    float progress = 0;
    if (uintptr_t time = ptr(at + kCompTime); time && swofty::mem::readable(time + kTimeLength, kTimeDay - kTimeLength + 4))
        if (uint32_t days = call_length(length, reinterpret_cast<void *>(time + kTimeLength)))
            progress = std::min(1.0f, static_cast<float>(u32(time + kTimeDay)) / static_cast<float>(days));
    void *self = comp, *data = reinterpret_cast<void *>(at + kCompData);
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t entry = b + i * kEntrySize;
        Reading r;
        r.kind = u8(entry + kEntryKind);
        r.stat = u16(entry + kEntryStat);
        uint32_t key = static_cast<uint32_t>(r.kind) | (r.stat << 16);
        int f = call_fixed(fixed, self, &key);
        if (f < 0) {
            ++bad;
            continue;
        }
        float x = f ? 0.0f : progress;
        r.overall = call_overall(overall, self, data, &key);
        bool ok = std::isfinite(r.overall) && r.overall > 0 && r.overall <= 100;
        for (uint16_t info = kExperience; info <= kPace && ok; ++info) {
            r.v[info] = call_get(get, self, data, &key, &info, ctx, x, patched);
            ok = std::isfinite(r.v[info]) && r.v[info] >= 0 && r.v[info] <= 100;
        }
        if (ok)
            out.push_back(r);
        else
            ++bad;
    }
    if (out.empty())
        return why = "no driver's ratings read (" + std::to_string(bad) + " refused)", false;
    return true;
}

} // namespace game_ratings
