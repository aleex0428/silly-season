// SPDX-License-Identifier: MIT
//
// Copied from miguelvf3's F2 Revamp (https://github.com/miguelvf3/miguel-s-f1-25-mods, f2-feeder-career/api), which
// publishes it under the MIT licence for other addons to include.
//
// The F2 Revamp career service (miguelvf3.f2-revamp), for other addons to include. Look it up with
//
//     swofty::services::when<F2RevampCareer>([](const F2RevampCareer &c) { g_f2 = &c; });
//
// The callback runs only when F2 Revamp is installed and on in the profile. Its career is the data group
// f1_career_f2_26: an F2 2026 season, then F1 from 2027 (the stock 2026 F1 data, raced as 2027).
// Call the functions on the game thread (in an event handler, for example career.hub). Stat ids are
// entrants.drivers / entrants.teams stat ids. This header is MIT so any addon can copy it.
#pragma once
#include <cstdint>

struct F2RevampPromotion {
    uint32_t driver_stat;   // the F2 driver who moved up
    uint32_t team_stat;     // the F1 team that took him
    uint32_t replaced_stat; // the driver whose seat he took (Ocon, Lawson, Lindblad)
    uint32_t f2_place;      // his place in the final F2 table (1-based)
};

struct F2RevampCareer {
    static constexpr const char *name = "f2_revamp.career";
    static constexpr uint32_t version = 1;

    uint32_t size;
    // 1 while the loaded career is an F2 Revamp career whose player is still in his F2 season, else 0.
    int (*in_f2)(void);
    // The F2 drivers' order, best first, into driver_stat (up to cap); returns how many there are. While
    // in_f2() is 1 it is the order after the last F2 session; after that, the final F2 table. 0 outside an
    // F2 Revamp career, or one that skipped F2. The player's own driver is in it.
    uint32_t (*standings)(uint32_t *driver_stat, uint32_t cap);
    // The F2 drivers this addon moved up to F1 at the first F1 Team HQ, into out (up to cap); returns how
    // many. 0 before that Team HQ, outside an F2 Revamp career, or in one that skipped F2.
    uint32_t (*promoted)(F2RevampPromotion *out, uint32_t cap);
};
