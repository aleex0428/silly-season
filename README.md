# Silly Season for F1 25 (Swofty Launcher)

A living driver market for F1 25's **Career mode**. Through the season rumours appear; some turn into talks and official
signings, others are denied. Veterans announce their retirement, impatient teams sack underperforming drivers and the
academies promote their F2 youngsters. When a new season starts, the game's grid really changes.

- **Addon:** `alexs.silly-season` 1.0.1
- **Requires:** Swofty Launcher 1.8.16 or later and F1 25 on Steam
- **Careers:** Driver Career and My Team for 2026 (`f1_career_2026`) and 2025 (`f1_career_2025`).
  Careers from other addons, such as Progressive Career, keep their own market.

## Install

1. Drag `dist/alexs.silly-season.swoftyaddon` onto the launcher window, or run:
   ```
   swofty addons install dist/alexs.silly-season.swoftyaddon
   swofty addons enable alexs.silly-season
   ```
2. Press **Play**. Start a new career or load one.

## In the game

The news comes to you without opening any menu:

- **Mails in your Team HQ inbox** from a paddock insider:
  - **Market preview** at the start of every race weekend (practice 1): what happened since the last race.
  - **Market radar** after every race: open talks, the driver stock (who is rising and who is falling), who is under
    pressure and whose contract is running out.
  - **The new grid** when the season's moves are settled: next year's grid and every change.
  - They are the game's own mails: Silly Season adds its mail groups to the database and fills their text live.
- **BREAKING NEWS banner** (overlay): a broadcast-style lower third with every official signing, retirement or sacking,
  outside sessions.
- **The market hub: F7** (or **Back + Y** on a controller), outside sessions. A full-screen, broadcast-style panel with a
  live headline ticker, the transfer window's status, a grouped side menu, animated cards, gauges and charts, and 13 pages:
  - **Paddock:** the market at a glance (seats in play, open talks, done deals, unread news), the top stories, your team
    and contract, and the drivers rising and falling.
  - **News:** every story as a card, grouped by race, with its kind and an unread marker.
  - **Press:** the journalist's question and your three answers.
  - **Rumours:** who goes where, at which stage, and which outlet says so with its reliability.
  - **Deals:** contracts signed for next season and this season's moves.
  - **Grid:** next season's projected (or final) grid in team colours, with new arrivals marked.
  - **Drivers:** a ranking with team, age, value, trend, contract and salary.
  - **Contracts:** who is out of contract this year, next year and later.
  - **Stock:** a chart of every driver's value change over the last race.
  - **Formula 2**, **Engineers** (technical directors and their level), **Free agents** and **History**.
  - Q/E, Left/Right or LB/RB change page; Up/Down, the wheel or the d-pad scroll; F7, Esc or B close it. The mouse
    works everywhere.
- **The Team HQ tab shows unread news:** `SILLY SEASON (3)`.
- **SILLY SEASON tab in Team HQ** (after STANDINGS). The manifest adds it to 2026 careers; for 2025 careers the plugin
  adds it with a `hub.tab` record that reuses the same template.
  - **Silly Season:** change the value with left or right to open each report as a pop-up.
  - Intensity, rumours, mid-season changes and protect your team-mate.
- **Pop-ups back at Team HQ:** off by default, because the mails already bring the news. Turn them on for all news or
  official news only.
- **ADDON SETTINGS** (main menu, under Career) holds every option: mails, banner and hub, panel effects (turn them off on slower PCs), pop-ups, driver progression and who runs the market (keep **Silly Season**: with **The game** the game also retires and sacks drivers on its own, which can clash with the market). Mails turn on or off after restarting the game.

## A season of Silly Season

| When | What happens |
|---|---|
| Season start | Preview: which drivers are out of contract and which seats are in play |
| From race 2 | Rumours, few at first and more as the season goes on; some true, some false |
| 15 % – 70 % | Drivers **under pressure** when their team-mate clearly beats them |
| From 30 % | **Your future:** teams watching you, or your team wanting to renew you |
| Any race | **Stand-ins:** a driver misses a race and the team's reserve steps in, then gives the seat back |
| From 20 % | Some rumours become talks; others cool off |
| 40 % – 65 % | Sacking window: a driver on a bad run can lose his seat straight away |
| From 55 % | Official renewals |
| From 60 % | Retirement announcements and official signings for next year |
| Last race | The market closes and the final grid is published |
| First Team HQ of the new season | Silly Season applies the seat changes in the game |

### How teams decide

- Teams pick in order of attraction (car performance and prestige). Ferrari, McLaren and Mercedes choose before Haas.
- Each candidate gets a score from his real ratings in the game (pace, racecraft, awareness and experience), recent
  form, age, room to grow and whether he is already in the team.
- **Academies:** Red Bull and Racing Bulls, Mercedes, Ferrari, McLaren, Alpine, Williams, Audi and Aston Martin prefer
  their youngsters. Racing Bulls works as the junior team and rarely signs veterans.
- **Contracts:** a driver signed for next year stays. A clearly better team can buy out a top driver's contract, which
  frees his old seat.
- **Retirements:** from 34, more likely the older and slower the driver. A veteran left without a seat usually retires.
- **Form:** the current Swofty core can't read race results yet, so Silly Season simulates every race from car and
  driver level. Real results will count once the core exposes them (telemetry).
- **When a race counts:** once the weekend's race is run. Practice or qualifying alone doesn't move the market.
- **Your seat is never touched.** Your team-mate can change unless you turn on **Protect my team-mate**.

### Press conferences

After a few races the press asks you about the market. The PRESS banner tells you, the hub's Press page gets a mark and
the question stays open for two races. Four kinds:

- **Your future:** commit to the team and your renewal is talked about; listen to offers and more teams show interest.
- **Rumours about your team-mate:** say "he should go now" and a feud starts; he will look for a way out even under
  contract.
- **Your ideal team-mate:** pick one of three available drivers and the team will consider him for the seat.
- **Other drivers' moves:** criticise a driver and a feud starts; he will never want to be your team-mate.

Answer in the F7 hub (1, 2, 3, click or controller) or on the **Press conference** row of the Team HQ tab, after reading
the question in the "Press conference" report. F1 25's own press conferences only exist in Braking Point and the My
Team intro, so this one is Silly Season's own.

### New talents, F2 and engineers

- **New talents:** every season 17 to 19 year-olds with their own names join F2. They are the game's fictional reserve
  drivers, renamed live (only those with a name key of their own). The game keeps their nationality, face and three
  initials, so the new surname starts with those letters. Two of them are women and get female names. Their new ratings
  drive the market at once and reach the game when they get an F1 seat (a reserve has no ratings row in the career
  until he races).
- **Simulated F2:** the leader is announced mid-season, the champion and podium at the end. The top three have a better
  chance of stepping up to F1.
- **Invented technical directors** with a level. Each season there can be a technical move: a good director at a small
  team leaves for a richer one, and next year's car shows it.

### Money, clauses and feuds

- **Drivers with sponsors:** some fictional drivers bring money, and teams with a small budget value them more.
- **Exit clauses:** some signings of top drivers include a clause tied to the team's position in the constructors'
  championship. If the team misses it, the driver is free even under contract.
- **Feuds:** team-mates of similar level can clash. If they still are at the end of the year, the slower one asks to
  leave, and two rivals never share a garage again.

### Outlets, money and the review

- **Every rumour comes from a fictional outlet with a reliability:** The Pit Wall and Paddock Confidential (high),
  Full Throttle (medium), Box Box Daily and Grid Gossip (low). True rumours mostly come from the reliable outlets and
  false ones from the tabloids.
- **Signings and sackings include quotes from team management**, always by role and never by a real person's name.
- **Estimated salaries** from the driver's level and the team's prestige.
- **Market review** when each season closes: signing of the year, biggest leap, bet for the future and retirements.

### Intensity

- **Realistic:** 4 to 8 changes per winter, few false rumours and at most one sacking per season.
- **Lively:** more signings, more false rumours, more buy-outs and up to 2 sackings.
- **Chaos:** anyone can end up anywhere, cars change a lot from year to year and up to 3 sackings.

## Files

```
silly-season/
├── swofty.json            manifest: options, the Team HQ tab and the DLL
├── data/baseline.json     grids, academies, contracts and ratings (editable)
├── data/ui/               the overlay's own soft-shadow and glow textures (made by tools/make_ui_textures.py)
├── db/emails.json         mail groups in the game's inbox (made by tools/make_emails.py)
├── assets/lang.json       the mails' default texts
├── native/
│   ├── silly_season.dll   the compiled plugin
│   ├── build.bat          builds with MSVC; "test" also runs the simulations, "preview" draws the hub to SVG
│   ├── include/           Swofty SDK (MIT), with a one-line MSVC patch
│   ├── src/market.*       the market engine (plain C++, no game calls)
│   ├── src/text.*         every text the player reads
│   ├── src/plugin.cpp     the link to F1 25: events, seats, the hub, pop-ups and saving
│   ├── src/hub.inl        the look of the F7 hub and the breaking-news banner
│   └── tests/             sim_test.cpp (76 careers of 10 seasons with checks), overlay_preview.cpp
└── tools/
    ├── extract_gamedb.py  reads a COPY of gamedatabases.2025.bin
    └── make_baseline.py   writes data/baseline.json
```

### Editing contracts, academies or team attraction

`data/baseline.json` is read every time the game starts. You can edit the contracts in
`careers.f1_career_2026.contracts`, the teams' `prestige` and `performance` or the drivers' `academy`. Changes apply to
**new** careers; careers already started keep their own state.

To rebuild it from your game, always work on a copy of the database:

```
python -I tools/extract_gamedb.py ../_swofty-ref/tools copy/gamedatabases.2025.bin gamedb.json
python -I tools/make_baseline.py gamedb.json data/baseline.json
```

## Building

You need Visual Studio 2022 or later with **Desktop development with C++**.

```
native\build.bat                       builds native\silly_season.dll
native\build.bat test                  also builds and runs the simulations
native\build.bat preview <dir> [en|es] draws every hub page and the banner into SVG files
```

To package:

```
python -I ../_swofty-ref/tools/pack_addon.py . dist
```

## Where the state lives

- `$DATA/addon-storage/alexs.silly-season/careers/<career>.json`: the launcher keeps it between sessions.
- Also in the core's per-career data (`$DATA/careers/data/<career>/alexs.silly-season.json`), which travels with the
  save in the save vault.
- Log: look for `[silly-season]` lines in `swofty/loader.log` in the game folder, or run `swofty logs`.

## Known limits

- If you load an older save, the market state doesn't go back with it.
- **The career needs Silly Season:** the launcher warns before Play if you turn it off for a career that uses it.
  Playing that career without it lets the game's grid drift away from the market.
- **Two careers with the same driver** can share one id on the current Swofty core. If a saved market is ahead of the
  career you load, Silly Season starts a fresh market for it rather than mixing them.
- **My Team:** your own team isn't part of the market, so Silly Season doesn't know who your team-mate is.
- **Who runs the market = Silly Season** empties the game's own retirement and sacking lists (the `career.market`
  record, experimental in Swofty). If something looks wrong, switch it to **The game** and restart.
- **Banner and hub:** they use Swofty's experimental overlay. With frame generation on (DLSS Frame Generation or FSR) it
  may not draw unless `SWOFTY_OVERLAY_FG=1`.
- **Stand-ins:** the Swofty core takes the regular driver off the grid while the reserve races and signs him again when
  he returns, so in the game's data his contract becomes a new one. Silly Season keeps the original contract. Turn off
  **Stand-ins** if you prefer.
- **New talents:** the new name is a live text change, applied only while the career is loaded. The age the game shows
  is still its row's, and the initials don't change.
- **Your future** is flavour: your own seat stays with the game's contract system, because Swofty doesn't move the
  player.
- The current Swofty core doesn't announce the end of each weekend or season, and doesn't expose the career's ratings.
  Everything is worked out at Team HQ from the current round.
