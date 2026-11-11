# 1.1.2

- **No more stall at Team HQ.** With many drivers from other addons (More Driver Models, for example), the first Team HQ visit after loading a career could freeze for 10 to 15 seconds while Silly Season read the career's driver lists from the game database. It now reads them in the background as soon as the career loads, finds each driver in one pass instead of searching the whole table every time, and keeps them while the game runs. Team HQ no longer waits.
- **"You" in both seats of your team is fixed.** In some Driver Careers (2025 careers with your own driver), the hub could show you in both seats, because your car's row named the same driver as your team-mate's. Your team-mate now keeps his own name, and the hub never seats one driver twice.
- **Retired drivers in My Team (diagnostics):** when a driver leaves the grid, the log now records how the game holds him, to find out why a driver the game retired could still turn up in Silly Season's rumours. Send your logs if you see it.

# 1.1.1

- **Installs on every launcher that can run it.** 1.1.0 asked for launcher 1.8.16 or later, but its manifest links the source code, which launchers before 1.8.19 refuse. Silly Season now asks for launcher **1.8.36** or later.
- **Tagged "Steam" in the Addon Browser:** the copy of the game it was tested on, instead of "Not tested".
- **F7 panel and banner:** the README no longer says they need `SWOFTY_OVERLAY_FG=1` with Frame Generation (the launcher draws them since 1.8.25). If they never show on an NVIDIA card with DLSS, update the launcher to 1.8.38.

# 1.1.0

- **History mode for Progressive Career on the Swofty Seasons.** Silly Season now plays along with Progressive's real seasons (2015 to 2026): it tells next year's real moves through the season as rumours, talks and official signings, announces the real retirements and the real mid-season stand-ins at the right race, and closes each season with the real grid. It never touches a seat while the real years last: Progressive puts each line-up on the grid. Each year's team names, car pace and driver ratings come from Progressive's data, and the drivers of the season addons (Vettel, Räikkönen, Kubica, Schumacher...) are part of the market. From 2026 on, Silly Season's own market takes over.
- **The real season.** Race results come from your career's standings, round by round: form, points, pressure, sackings, the driver stock and the constructors' order (next year's cars, exit clauses) follow what really happens on track, and the standings count the rounds raced. If they can't be read, races are simulated as before.
- **Secret meetings:** when the game puts a secret meeting with another team on your calendar, the paddock hears of it.
- **The race count no longer falls behind** when the game reports every session as practice.
- **Stand-ins:** a reserve the game wouldn't seat is asked for by his id; if the game still refuses, the stand-in and its news are called off.
- **The breaking-news banner draws in full** in the game (it could show only its white strip).
- **Drivers have a character.** Each one is ambitious, loyal or after the money, to a degree. Loyal drivers renew more often and for longer, and are hard to buy out. Ambitious stars turn down seats below their level, sign with exit clauses and can ask to leave a slow car despite their contract. Money-minded drivers cost more and follow the budget. You see it in the news and on the Drivers page.
- **Teams have a plan:** title push, contender, rebuild, youth or survival, from their car, brand, budget and results. A title team signs proven drivers and has no patience; a rebuilding team bets on youth; a team fighting to survive loves a driver with sponsors. The plan can change at mid-season, and the season preview and the Grid page show it.
- **The domino effect.** A signing waits until the seat it goes to is free, so moves fall in order. When a transfer goes official, the seat it leaves opens at once, and the team's next choice goes straight into talks ("DOMINO EFFECT: X NEXT FOR Y").
- **Deadline day:** the last two races before the grid is set. Deals close fast, there can be a last-minute swoop nobody saw coming, and the F7 header, the ticker and the banner count down.
- **The media keep score.** Every outlet's reliability now moves with the stories it gets right or wrong, and newsrooms change a little each winter. At the season's close, a media scorecard names who got it right.
- **Season awards** from your career's results: champion, constructors, rookie of the year, beat the car and most improved, in the news and in a Hall of Fame on the History page.
- **Scouting page (F7):** a shortlist of the drivers available next season who fit your team, with how keen each one is. In My Team, your scout also sends a report twice a season for your second seat.
- **Konnersport joins the market** in 2025 careers that have the eleventh team: its seats are planned, Aiden Jackson and Callie Mayer can move, and other drivers can sign for it. In careers without it, nothing changes.
- **New talents get their own racing number** once they reach F1.
- The headline ticker shows only the first line of lists (the grid, the awards) instead of drawing them over each other.
- **Driver cards (F7):** click a driver, or pick him with Up/Down and press Enter/A, for his full card: value and potential, contract and salary, character, this season's points, wins and podiums, a race-by-race value chart (this season against the last), his career path, the stories about him and his honours.
- **Team cards (F7):** from the Grid and Engineers pages: the team's plan, car, prestige, budget and technical director, this season's and next season's line-ups, its academy and reserves, its market moves and honours.
- **Transfer Centre (F7):** a new page with every chain of moves, domino by domino, and the signings still waiting for their seat.
- **Search, sort and filter** on the Drivers, Contracts, Free agents and Scouting pages: type with / to search, S (X on a controller) to sort, F (Y) to filter by team or character.
- **Bombshell signing cards:** a star's big move gets a full card with both teams' colours instead of the usual banner.
- **The grid reveal:** once next season's grid is final, your next Team HQ visit reveals it team by team on a full screen. Press R on the Grid page to see it again.
- **Stacked news:** the updates of one story (rumour, talks, official) stack into one card on the News page, and when several stories arrive at once the banner shows how many are waiting and moves faster.
- **Team HQ card:** a small card in a corner of Team HQ for a few seconds, with unread news, the latest headline and the deadline countdown.
- **Drivers from other addons join the market.** Silly Season now reads your career's own driver lists from the game, so the free agents another addon adds (More Driver Models, for example) can be signed, show in Free agents and Scouting, and have their own cards. Icons of the past only come back with the Legends option.
- **Drivers it didn't know get their real ratings and names.** A driver of another season put on the grid with an editor (Team & Driver Editor, Career Mode Editor) now gets his ratings and his name from the game, instead of a default 70.
- **My Team's own team is the twelfth team** in the F7 hub: it shows on the Grid, Drivers and Contracts pages with you and your team-mate, has its own card, takes its place in the constructors' order and can win the constructors' award. The market still never touches its seats.
- **New options** to turn each block off: Driver personalities, Team plans, Deadline day, Bombshell signing cards, Grid reveal, Team HQ market card and Racing numbers for new talents.
- **Mid-season sackings really happen in the game.** In 1.0.1 a sacking could be announced ("X out, Y takes the seat from the next race") while the game kept the old driver: the game only seats race drivers and free agents, and the replacement was often a reserve or an F2 driver. Silly Season now reads who the game will seat and only brings in those drivers, mid-season and for stand-ins. If the game still turns a change down, the market takes it back before you see it: no news, no move, the driver keeps his seat. A driver sacked by 1.0.1 whom the game kept racing is no longer treated as a free agent.
- **Your Future (F7):** a new page about you: your value race by race, your contract, how each team rates you (keen, watching or cool, with the seats that open), how your team feels about you, and everything the paddock says about you. In My Team, your team's place in the constructors' order against the teams either side of it.
- **New press topics with consequences:** deadline day ("does your team need a signing?"), a rival's bombshell signing, your own renewal and last season's awards. Asking for reinforcements makes your team hunt a proven driver, and what you say about your contract changes how comfortable your team is with you, which brings renewal talk or suitors.
- **Standings (F7):** a new page with the drivers' and constructors' championships from the real results, My Team included. Click a line for the driver's or the team's card.
- **Career totals:** each driver's starts, wins, podiums, titles and teams across the seasons of your career, on his card, and a Records section on the History page.
- **Team-mate duel** on each driver's card: how many races he finished ahead of his current team-mate this season.
- **The odds of every story:** each rumour and each link of the Transfer Centre shows its chance of ending in a signing, from what the paddock can see (the stage, who reports it, whether the driver wants the seat, deadline day).
- **Sister-team swaps:** Red Bull can swap a struggling driver with Racing Bulls' driver in form mid-season, as in real life.
- **Money moves:** sponsors join a team doing better than its car and leave one doing worse, and the constructors' order pays prize money each winter. The budget shapes the team's plan and salaries, and the team card shows how it moved this season.
- **Team principals:** every team has one (invented). A team far below where its car belongs can sack him, and the new principal brings his own plan.
- **Farewells:** a driver who retires says goodbye at the season's last race with his career in numbers, and the season review pays tribute.
- **Richer mails:** the radar mail now covers deadline day and the seats still open, the chains of moves and the year's most reliable outlet; the grid mail adds the awards and the farewells.
- **New option, Living paddock:** turn off the sister-team swaps, the moving budgets and the team principals.
- The side menu of the F7 panel fits its 17 pages on any screen.
- **Academies and reserves:**
  - Every team has a reserve driver, starting with the game's own. Each winter, once the grid is final, the teams keep or sign theirs for next season (one news item lists them all). Racing Bulls shares Red Bull's, and Haas shares Ferrari's.
  - Once a season the academies sign young drivers from F2 and the free agents, and an academy driver gets a better shot at his team's seats.
  - Rookies come in through the second or partner team first (Racing Bulls before Red Bull, Haas before Ferrari).
  - For a stand-in, the team calls the better of its reserve and its academy drivers on the day.
  - Free agents show their role (reserve or academy, and whose) on the Free agents page and on their card; the team card names its reserve.
  - The reserves live in Silly Season's market: Swofty can set the race seats, not the game's own reserve slot.
  - New option, **Academies and reserves**, to turn it all off.
- **A better F7 panel:**
  - **Season bar** in the header: the round you're at, the mid-season window and deadline day, and the grid's close.
  - **Ratings radar** on every driver's card (pace, racecraft, awareness, experience, with the peak he can reach).
  - **Standings** show each driver's last five races in colour and his gap to the leader.
  - **Rumours** show each story's chance as a ring, and can be searched, sorted (by chance too) and filtered by team.
  - **Paddock:** the team keenest on you, and the top three of the championship.
  - Pages slide in from their side. The menu shows how many teams are keen on you and your place in the championship.
  - **Everything opens its card:** news, deals, moves, the stock, F2 (the podium too), the drivers on the Grid page.
  - **Step through cards:** a card opened from a list goes to the previous or next one with Left/Right (LB/RB), and shows "3 / 20".
  - **Find anyone:** / (Y on a pad) on any page without a table searches every driver and team.
  - The right stick scrolls, the scroll bar can be dragged with the mouse, and every page remembers where you left it.
  - **Press:** your answer stays on screen as "Answer sent" until the paddock reacts.
  - **New option, Panel size (F7):** small, normal or large.
  - The tables are sorted and filtered once when something changes, not every frame.
- **New and redesigned overlays:**
  - One overlay at a time, the most important first, and never during a session or while the F7 panel is open. Full-screen moments can be skipped with Esc or A.
  - **Breaking-news banner:** the driver's initials in his team's colour, the team's livery behind the story and an icon for each kind of news.
  - **Bombshell signing:** both teams crash together, gold sparks fly, the driver's value counts up and the new team's boss has his say.
  - **Grid reveal:** where each new driver comes from, who leaves each team and a summary at the end.
  - **Team HQ card:** the last three headlines, the deadline countdown as a ring and your place in the championship.
  - **Press questions on screen:** at Team HQ the question shows with its three answers; press 1, 2 or 3 to answer, or F7 to read it in the panel.
  - **Report after each race:** back at Team HQ, a card with your value, new rumours, done deals and the teams following you.
  - **Interest alerts:** a small alert when a team becomes keen on you.
  - **Awards ceremony:** when the season closes, the awards are handed out one by one on a full screen.
  - **Season cover:** the first Team HQ visit of each season opens with the year, every team's plan and the winter's moves.
  - **Farewell card** for each driver who retires, and a red strip when deadline day opens.
  - **New options** to turn off the press questions on screen, the race report, the awards ceremony and the season cover.
- **Fixes in 1.1.0's own blocks:**
  - The team-mate duel counted wrong when several races were read at once.
  - Simulated races were filed a round late.
  - A reserve called up in a sacking the game refused lost his reserve role.
  - In-season moves showed the next year on the career path.
  - A press question claimed your contract ends this year, which the game doesn't tell.
- **Progressive Career's 2015 to 2018 seasons:** the drivers of those years (Rosberg, Massa, Maldonado, Vandoorne...) have their real names and ages, and Rosberg retires as champion.
- **F2 Revamp support** (miguelvf3's career that starts in F2 2026):
  - Silly Season stays out of the way while you race F2.
  - It starts the market with your first F1 season (2027) and treats the drivers F2 Revamp promotes as signings with their two-year contracts.
  - The two addons talk through Swofty services: Silly Season now publishes `silly_season.market` for other addons.
- **Fixes from 1.0.1 reports:**
  - **My Team: the news no longer announces signings that never happen.** In My Team, Swofty can't change the race seats, so in 1.0.1 the winter's signings were announced but the grid stayed as the game had it. Silly Season now follows the market there: rumours, talks, odds and the press go on as before, but it never signs, sacks or retires anyone itself. It reports each move as official once the driver turns up in his new car, and the stories that predicted it come true. One news item explains this the first time.
  - **A signing the game refuses at the new season is taken back.** The market reads the game's grid again. If you hadn't seen the signing yet, it disappears; if you had, a "DEAL OFF" story says the move fell through. A driver whose successor the game refused races on instead of being retired.
  - **Next season's grid only uses drivers the game can seat** (race drivers and free agents, not reserves), as mid-season sackings already did.
  - **The hub shows the game's own ratings.** Silly Season now reads each driver's ratings from your career at every Team HQ: the overall rating the game shows (perks, specialists and the car included) and all five statistics, focus too. The market decides on them, so values, interest and who signs whom follow what the game shows instead of drifting from it over the seasons. Driver cards and Your Future show the game's **Rating** next to Silly Season's **Market value** (the rating plus current form), and the ratings radar has five axes. If the ratings can't be read, the market keeps its own numbers and says so in the log.
  - **A driver's results are no longer lost** when the game races him as a story team's copy (Konnersport's drivers in 2025 careers).
  - **Driver progression no longer overwrites the game's ratings.** After a few seasons it could swing a driver by 20 to 30 points, because the game develops its drivers on its own and Swofty can't tell Silly Season where it took them. The option now moves the market's own view of each driver, and only the new talents' ratings reach the game.

# 1.0.1

Fixes, and the market now follows your save.

- **The real grid from your save.** At every Team HQ, Silly Season reads who sits in each seat of your career. Transfers made with the Career Mode Editor, other addons' line-ups and a season already under way all reach the market, and drivers it didn't know join it with their game names.
- **Careers already in a later season** (2030 in a 2026 career) get the right year: the market follows the game's season, contracts move on with it and veterans no longer on the grid have retired. A market saved in the wrong year by 1.0.0 is mended at the next Team HQ.
- **Your driver is always found,** a created driver and 2026 careers included (no more "Who are you?").
- **My Team:** your own team shows in the F7 hub, and your team-mate is protected: no other team signs him.
- **Teams Silly Season doesn't know** (Konnersport, another addon's): their drivers stay where they are and aren't listed as free agents.
- **Your market survives Swofty's new career ids.** Swofty now names careers by their save slot; a career started with 1.0.0 finds the market it saved under its old id.
- **A new career keeps its market.** The game gives a new career a temporary id until its first save. The market now follows the change, and a career you left before that save finds its market again at the next load.
- **Careers don't mix.** Two careers with the same driver could share a saved market. If the saved market is ahead of the career you load, a fresh one starts.
- **Race count.** A weekend counts once its race is run. Practice or qualifying alone no longer moves the market a race forward.
- **New talents' ratings** reach the game once a talent gets an F1 seat, instead of being refused while he is a reserve.
- **Launcher warning:** a career that uses Silly Season is marked as needing it, so the launcher warns before Play if the addon is off.
- **Headlines that say who and where:** "OCON TO ALPINE?", "SAINZ-MERCEDES TALKS COOL OFF", "STROLL UNDER PRESSURE", "THE PRESS ASK ABOUT YOUR FUTURE"... instead of the same "MARKET RUMOUR" over and over, in the hub, the ticker, the banner and the reports.
- **New option, Panel effects (F7):** turn off the shadows, glows and animations for a lighter panel on slower PCs.
- **Lighter panel:** text sizes are measured once and remembered instead of every frame.
- **The Team HQ tab's unread count** updates as soon as you close the F7 panel.
- **Who runs the market** explains that "The game" lets the game's own retirements and sackings clash with Silly Season's plans.
- A career that meets Silly Season mid-season no longer counts the first race's points twice once race results can be read.
- More detail in the log: each session's type, and when a weekend counts as raced.

# 1.0.0

First release. A living driver market for F1 25's Driver Career and My Team (2025 and 2026 careers).

- **The market:** rumours from reliable and less reliable outlets, talks, signings, renewals, retirements and mid-season sackings. Teams pick by car level, prestige, form, age, potential and their junior academies. Your own seat is never touched.
- **The market hub (F7 / Back + Y):** a full-screen, broadcast-style panel with a live headline ticker, animated cards,
  gauges and charts, and 13 pages: a Paddock overview, news cards, press conferences, rumours with outlet reliability, done deals, next season's grid in team colours, a driver ranking, contracts, a driver stock chart, F2, technical directors, free agents and history.
- **News without menus:** weekly inbox mails (market preview, post-race radar, next year's grid) and a broadcast-style breaking-news banner.
- **Press conferences:** questions about your future, your team-mate and other drivers' moves. Your answers shape rumours, renewals and rivalries.
- **New talents:** young drivers with their own names join F2 every season. F2 is simulated, and its podium gets noticed by F1 teams.
- **More depth:** stand-ins for injured drivers, technical directors who change teams, drivers with sponsors, exit clauses, garage feuds, salaries and a season review.
- **Team HQ tab:** reports and the main settings, plus a count of unread news.
- **Settings:** intensity (realistic, lively, chaos), number of rumours, legends on the market, and more in ADDON SETTINGS.

Requires Swofty Launcher 1.8.16 or later. The banner and the hub use Swofty's experimental overlay.
