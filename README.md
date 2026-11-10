# Silly Season for F1 25 (Swofty Launcher)

A living driver market for F1 25's **Career mode**. Through the season rumours appear; some turn into talks and official
signings, others are denied. Veterans announce their retirement, impatient teams sack underperforming drivers and the
academies promote their F2 youngsters. When a new season starts, the game's grid really changes.

- **Addon:** `alexs.silly-season` 1.1.1
- **Requires:** Swofty Launcher 1.8.36 or later and F1 25 on Steam (tested on the Steam copy)
- **Careers:** Driver Career and My Team (where the game makes the moves: see Known limits) for 2026 (`f1_career_2026`) and 2025 (`f1_career_2025`), and Progressive
  Career on the Swofty Seasons (`swofty.swofty-seasons-progressive`) in history mode. Careers from other addons keep
  their own market.

## New in 1.1.0

The biggest update so far. In short (every change is in [CHANGELOG.md](CHANGELOG.md)):

- **The real season:** race results, standings and secret meetings come from your career, and the hub shows the game's
  own driver ratings.
- **A deeper market:** driver personalities, team plans, the domino effect, deadline day, outlets that keep score,
  season awards, academies and reserve drivers, sister-team swaps, sponsors and team principals.
- **History mode** for Progressive Career on the Swofty Seasons (2015 to 2026).
- **A new F7 hub** with 17 pages, driver and team cards, search, and new overlays.
- **Other addons:** their drivers join the market, My Team's own team is the twelfth team, and F2 Revamp is supported.
- **Fixes from your reports:** sackings and signings the game refuses are taken back, My Team no longer announces moves
  that never happen, and driver progression no longer overwrites the game's ratings.

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
  live headline ticker, a season bar, the transfer window's status, a grouped side menu, animated cards, gauges and
  charts, and 17 pages:
  - **Paddock:** the market at a glance (seats in play, open talks, done deals, unread news), the top stories, your team
    and contract, the team keenest on you, the championship's top three and the drivers rising and falling.
  - **News:** every story as a card, grouped by race, with the updates of one story stacked together.
  - **Press:** the journalist's question and your three answers.
  - **Your Future:** your game rating, market value and potential, your value race by race, how keen each team is on
    you, how your team feels about you and what the paddock says about you.
  - **Rumours:** who goes where, at which stage, which outlet says so with its reliability, and each story's odds.
  - **Deals:** contracts signed for next season and this season's moves.
  - **Transfer Centre:** every chain of moves, domino by domino, and the signings still waiting for their seat.
  - **Grid:** next season's projected (or final) grid in team colours, with new arrivals marked. R replays the reveal.
  - **Standings:** the drivers' and constructors' championships from your career's real results.
  - **Drivers:** a ranking with team, age, value, trend, contract and salary.
  - **Contracts:** who is out of contract this year, next year and later.
  - **Stock:** a chart of every driver's value change over the last race.
  - **Formula 2**, **Scouting** (who fits your team next season), **Engineers**, **Free agents** (with reserves and
    academy drivers) and **History** (with the Hall of Fame and records).
  - **Driver and team cards:** click a driver or a team, or pick one with Up/Down and press Enter/A. Left/Right (LB/RB)
    steps through the list.
  - **Search, sort and filter:** / (Y) searches every driver and team; S (X) sorts and F (Y) filters the tables.
  - Q/E or LB/RB change page; Up/Down, the wheel, the right stick or the d-pad scroll; F7, Esc or B close it. The mouse
    works everywhere.
- **Overlays at Team HQ, one at a time:** the breaking-news banner, bombshell signing cards, the grid reveal, the awards
  ceremony, the season cover, farewells, the deadline-day strip, the report after each race, interest alerts, press
  questions you answer with 1, 2 or 3, and a small market card. None shows during a session or over the F7 panel.
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
- **Form:** from your career's real race results, read from the game's standings at every Team HQ. If they can't be
  read, Silly Season simulates the race from car and driver level.
- **When a race counts:** once the weekend's race is run. Practice or qualifying alone doesn't move the market.
- **Your seat is never touched.** Your team-mate can change unless you turn on **Protect my team-mate**.
- **The grid comes from your save:** at every Team HQ Silly Season reads who sits in each seat, so transfers made with
  the Career Mode Editor, other addons' line-ups and a season already under way are part of the market. In My Team your
  own team shows in the hub and your team-mate is never signed by another team.

### History mode: Progressive Career on the Swofty Seasons

Progressive Career moves through the real seasons from 2015 to 2026 and puts each year's real line-up on the grid.
In its careers Silly Season tells that story instead of writing its own:

- Next year's real moves come out through the season: rumours from the outlets, talks, then the official signing,
  each at its own moment. Debuts are settled late, as rookies' seats usually are.
- Real retirements are announced mid-season, and drivers who lose their seat are off the grid when it closes.
- The real mid-season stand-ins and swaps are announced before their race, scaled to your calendar.
- The tabloids still print stories that won't happen, and deny them later.
- Team names, car pace and driver ratings are each year's real ones (from Progressive's data). The season addons'
  drivers (Vettel, Räikkönen, Kubica, Schumacher, Mazepin...) are part of the market.
- Silly Season never writes a seat while the real years last: Progressive does. Stand-ins, new talents and sackings
  of its own stay off.
- From 2026 on, Progressive leaves the grid free and Silly Season's own market takes over.
- Silly Season tells the year from the drivers' looks on your grid at the first Team HQ, and reads Progressive's
  `data/years.json` from the game folder. Without it, Silly Season stays out of the career.

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
- **Reading the grid** uses the game's entrant rows as Swofty's experimental game headers lay them out. If a game update
  moves them, Silly Season notices, keeps its own grid and says so in the log (`real grid: not read`).
- **My Team:** Swofty can't change race seats in My Team, so there the game's own market makes the moves. Silly Season
  tells the rumours, talks and odds, never signs, sacks or retires anyone itself, and reports each move as official once
  the driver turns up in his new car. Your own team is never touched. With **Who runs the market = Silly Season** the
  game's retirement and sacking lists are emptied in every mode (the `career.market` record can't tell My Team apart), so
  in My Team nobody retires or is sacked; choose **The game** if you want the game to do both there.
- **Driver progression** moves Silly Season's own view of each driver (value, interest, cards). Only the new talents'
  ratings are written into the game: the game develops its own drivers, and Swofty can't read where it took them.
- **A seat the game refuses at the new season** is taken back: the market follows the game's grid, and a signing you
  had already read about turns into a "DEAL OFF" story.
- **Who runs the market = Silly Season** empties the game's own retirement and sacking lists (the `career.market`
  record, experimental in Swofty). If something looks wrong, switch it to **The game** and restart.
- **Banner and hub:** they use Swofty's experimental overlay. It draws with Frame Generation on since launcher 1.8.25.
  If the F7 panel or the banner never shows on an NVIDIA card with DLSS, update the launcher to 1.8.38 or later.
- **Stand-ins:** the Swofty core takes the regular driver off the grid while the reserve races and signs him again when
  he returns, so in the game's data his contract becomes a new one. Silly Season keeps the original contract. Turn off
  **Stand-ins** if you prefer.
- **New talents:** the new name is a live text change, applied only while the career is loaded. The age the game shows
  is still its row's, and the initials don't change.
- **Your future** is flavour: your own seat stays with the game's contract system, because Swofty doesn't move the
  player.
- The current Swofty core doesn't announce the end of each weekend or season: everything is worked out at Team HQ from
  the current round.
- **Ratings:** Silly Season reads each driver's ratings from the career with the game's own functions (Swofty's
  experimental game headers), so cards show the game's **Rating** next to the **Market value**. If a game update moves
  them, it keeps its own numbers and says so in the log (`game ratings: not read`).
- **Your contract's end year** is Silly Season's estimate: the game doesn't give its contracts an end date it can read.
