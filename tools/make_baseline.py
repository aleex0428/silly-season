"""Build data/baseline.json for Silly Season from the extract of the game database.

Uso:
    python -I tools/make_baseline.py <gamedb.json de extract_gamedb.py> data/baseline.json

The game supplies the grids, the career driver pools, stat ids and ratings. This script adds what the
database does not hold: team names and attractiveness, junior academies, real birth years and the
contract end years (estimates, edit them freely in the JSON: the plugin reads the file at every start).
"""
import hashlib
import json
import sys

src, dst = sys.argv[1], sys.argv[2]
db = json.load(open(src, encoding="utf-8"))
drivers_db = {d["_metadata_"]: d for d in db["drivers"]}
teams_db = {t["_metadata_"]: t for t in db["teams"]}
lists = {l["label"]: l for l in db["lists"]}

# --- Careers the addon knows: the grid list and the lists whose drivers have career rows -------------
CAREERS = {
    "f1_career_2026": {"year": 2026, "grid": "f1_2026",
                       "pools": ["f1_2026_driver_pool", "f1_career_2026_reserve_drivers", "career_f2_2025_for_f1_2026"]},
    "f1_career_2025": {"year": 2025, "grid": "f1_2025",
                       "pools": ["f1_2025_driver_pool", "f1_career_2025_reserve_drivers", "career_f2_2024_for_f1_2025"]},
}
# entrant references of the 2025 grid that are entrant labels, not driver labels
ENTRANT_FIX = {"kimi_antonelli": "andrea-kimi_antonelli", "jr": "carlos_sainz-jr"}
# My Team / story drivers that never race in Driver Career (Konnersport's two race drivers come in through
# OPTIONAL_TEAMS below)
SKIP = {"aiden_jackson", "callie_mayer", "arron_barnes_konnersport", "noah_visser_konnersport",
        "arron_barnes_zenith", "noah_visser_zenith", "default_driver", "default_player"}
LEGENDS = {"ayrton_senna", "micheal_schumacher", "jacques_villeneuve", "jenson_button", "mika_hakkinen",
           "felipe_massa", "mark_webber", "david_coulthard"}

# --- Teams: display name, attractiveness (prestige), car level, family, patience -------------------------
TEAMS = {
    # 2026
    "mclaren_26": ("McLaren", 92, 92, "mclaren", 0.55),
    "mercedes_26": ("Mercedes", 93, 90, "mercedes", 0.60),
    "ferrari_26": ("Ferrari", 96, 86, "ferrari", 0.35),
    "redbull_26": ("Red Bull Racing", 90, 85, "redbull", 0.25),
    "williams_26": ("Williams", 72, 75, "williams", 0.70),
    "forceindia_26": ("Aston Martin", 82, 78, "astonmartin", 0.55),
    "tororosso_26": ("Racing Bulls", 64, 68, "redbull", 0.30),
    "haas_26": ("Haas", 60, 66, "haas", 0.60),
    "sauber_26": ("Audi", 76, 66, "audi", 0.60),
    "renault_26": ("Alpine", 66, 63, "alpine", 0.35),
    "gm_cadillac_26": ("Cadillac", 63, 55, "cadillac", 0.70),
    # 2025
    "mclaren": ("McLaren", 92, 95, "mclaren", 0.55),
    "mercedes": ("Mercedes", 93, 86, "mercedes", 0.60),
    "ferrari": ("Ferrari", 96, 86, "ferrari", 0.35),
    "redbull": ("Red Bull Racing", 90, 87, "redbull", 0.25),
    "williams": ("Williams", 72, 74, "williams", 0.70),
    "forceindia": ("Aston Martin", 80, 70, "astonmartin", 0.55),
    "tororosso": ("Racing Bulls", 64, 70, "redbull", 0.30),
    "haas": ("Haas", 60, 70, "haas", 0.60),
    "sauber": ("Kick Sauber", 66, 66, "audi", 0.60),
    "renault": ("Alpine", 66, 62, "alpine", 0.35),
    # the eleventh team a 2025 Driver Career can have (entrant list f1_2025_with_konnersport)
    "konnersport": ("Konnersport", 55, 64, "konnersport", 0.45),
}
# teams only some careers have: the market plans them once the game shows them on the grid. The game races
# Konnersport in the custom team's slot (stat 104, seen in the career's entrant rows), not under its own stat.
OPTIONAL_TEAMS = {"f1_career_2025": {"konnersport": ["aiden_jackson", "callie_mayer"]}}
TEAM_STAT = {"konnersport": 104}
TEAM_CODE = {"konnersport": "KON"}
# junior academies (driver -> family). A family's teams prefer their own juniors.
ACADEMY = {
    "isack_hadjar": "redbull", "liam_lawson": "redbull", "arvid_lindblad": "redbull", "yuki_tsunoda": "redbull",
    "max_verstappen": "redbull", "andrea-kimi_antonelli": "mercedes", "george_russell": "mercedes",
    "oliver_bearman": "ferrari", "charles_leclerc": "ferrari", "dino_beganovic": "ferrari",
    "franco_colapinto": "alpine", "lando_norris": "mclaren", "oscar_piastri": "mclaren", "alexander_dunne": "mclaren",
    "gabriel_bortoleto": "audi", "luke_browning": "williams", "paul_aron": "alpine", "victor_martins": "williams",
    "zane_maloney": "audi", "gabriele_mini": "alpine", "jak_crawford": "astonmartin", "pepe_marti": "redbull",
    "leonardo_fornaroli": "mclaren", "sebastian_montoya": "redbull", "callum_voisin": "mclaren",
}
BORN = {
    "lando_norris": 1999, "oscar_piastri": 2001, "george_russell": 1998, "andrea-kimi_antonelli": 2006,
    "max_verstappen": 1997, "isack_hadjar": 2004, "charles_leclerc": 1997, "lewis_hamilton": 1985,
    "alex_albon": 1996, "carlos_sainz-jr": 1994, "liam_lawson": 2002, "arvid_lindblad": 2007,
    "fernando_alonso": 1981, "lance_stroll": 1998, "oliver_bearman": 2005, "esteban_ocon": 1996,
    "nico_hulkenberg": 1987, "gabriel_bortoleto": 2004, "pierre_gasly": 1996, "franco_colapinto": 2003,
    "sergio_perez": 1990, "valtteri_bottas": 1989, "yuki_tsunoda": 2000, "leonardo_fornaroli": 2004,
    "roman_stanek": 2004, "pepe_marti": 2005, "oliver_goethe": 2004, "richard_verschoor": 2000,
    "luke_browning": 2002, "dino_beganovic": 2004, "sebastian_montoya": 2005, "gabriele_mini": 2005,
    "jak_crawford": 2005, "kush_maini": 2000, "victor_martins": 2001, "ritomo_miyata": 1999,
    "amaury_cordeel": 2002, "alexander_dunne": 2005, "joshua_duerksen": 2003, "cian_shields": 2005,
    "sami_meguetounif": 2003, "max_esterson": 2006, "john_bennett": 2005, "rafael_villagomez": 2001,
    "zane_maloney": 2003, "dennis_hauger": 2003, "enzo_fittipaldi": 2001, "paul_aron": 2004,
    "taylor_barnard": 2004, "callum_voisin": 2006, "matias_zagazeta": 2003, "nikola_tsolov": 2006,
    "tim_tramnitz": 2004, "ayrton_senna": 1960, "micheal_schumacher": 1969, "jacques_villeneuve": 1971,
    "jenson_button": 1980, "mika_hakkinen": 1968, "felipe_massa": 1981, "mark_webber": 1976,
    "david_coulthard": 1971,
}
# contract end season (inclusive); estimates of the real deals, per career start year
CONTRACTS = {
    2026: {"max_verstappen": 2028, "isack_hadjar": 2026, "lando_norris": 2027, "oscar_piastri": 2028,
           "george_russell": 2026, "andrea-kimi_antonelli": 2026, "charles_leclerc": 2029, "lewis_hamilton": 2026,
           "alex_albon": 2027, "carlos_sainz-jr": 2026, "liam_lawson": 2026, "arvid_lindblad": 2026,
           "fernando_alonso": 2026, "lance_stroll": 2027, "oliver_bearman": 2027, "esteban_ocon": 2026,
           "nico_hulkenberg": 2026, "gabriel_bortoleto": 2027, "pierre_gasly": 2028, "franco_colapinto": 2026,
           "sergio_perez": 2027, "valtteri_bottas": 2026},
    2025: {"aiden_jackson": 2026, "callie_mayer": 2026, "max_verstappen": 2028, "yuki_tsunoda": 2025, "lando_norris": 2027, "oscar_piastri": 2026,
           "george_russell": 2025, "andrea-kimi_antonelli": 2025, "charles_leclerc": 2029, "lewis_hamilton": 2026,
           "alex_albon": 2026, "carlos_sainz-jr": 2026, "liam_lawson": 2025, "isack_hadjar": 2025,
           "fernando_alonso": 2026, "lance_stroll": 2026, "oliver_bearman": 2026, "esteban_ocon": 2026,
           "nico_hulkenberg": 2026, "gabriel_bortoleto": 2026, "pierre_gasly": 2028, "franco_colapinto": 2025},
}
NAME_FIX = {"carlos_sainz-jr": "Carlos Sainz", "andrea-kimi_antonelli": "Kimi Antonelli",
            "micheal_schumacher": "Michael Schumacher", "alex_albon": "Alex Albon", "nico_hulkenberg": "Nico Hülkenberg",
            "sergio_perez": "Sergio Pérez", "sami_meguetounif": "Sami Meguetounif"}


def name_of(label):
    if label in NAME_FIX:
        return NAME_FIX[label]
    return " ".join(p.capitalize() for p in label.replace("-", " ").replace("_", " ").split())


def curve(c):
    """'@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.66,0:0.66,0.9:0.8,1:0.8' -> (now, peak) out of 100"""
    if not c:
        return None
    parts = c.split("@")[-1].split(",")
    maxy = 100.0
    pts = []
    for p in parts:
        k, _, v = p.partition(":")
        if k == "maxy":
            maxy = float(v)
        elif k.replace(".", "", 1).isdigit():
            pts.append((float(k), float(v)))
    if not pts:
        return None
    return round(pts[0][1] * maxy, 1), round(pts[-1][1] * maxy, 1)


def jitter(label, lo, hi):
    h = int(hashlib.sha1(label.encode()).hexdigest()[:8], 16)
    return round(lo + (h % 1000) / 1000.0 * (hi - lo), 1)


def ratings(label, kind):
    sets = db["stats"].get(label) or {}
    pick = sets.get("2026") or sets.get("default") or next(iter(sets.values()), None)
    out = {}
    for stat in ("pace", "racecraft", "awareness", "experience"):
        v = curve((pick or {}).get(stat))
        if v:
            out[stat], out[stat + "_peak"] = v
    if len(out) < 8:  # fictional reserves and drivers without statistics
        base = {"reserve": (60, 70), "f2": (58, 68), "pool": (60, 72)}.get(kind, (60, 70))
        for stat in ("pace", "racecraft", "awareness"):
            out.setdefault(stat, jitter(label + stat, *base))
            out.setdefault(stat + "_peak", min(95.0, out[stat] + jitter(label + stat + "p", 4, 14)))
        out.setdefault("experience", jitter(label + "exp", 25, 45))
        out.setdefault("experience_peak", 75.0)
    return out


def ref_label(e):
    lab = e.get("driver") or (e.get("entrant_reference") or "").split("-", 1)[-1]
    if e.get("entrant_reference") and "-" in e["entrant_reference"] and e["entrant_reference"].split("-", 1)[0].startswith(("f1_", "f2_")):
        lab = e["entrant_reference"].split("-", 1)[1]
    return ENTRANT_FIX.get(lab, lab)


out = {"schema": 1, "careers": {}, "teams": {}, "drivers": {}}
for group, c in CAREERS.items():
    grid = lists[c["grid"]]
    seats = {}
    for e in grid["entrants"]:
        team, pos = e["team"], int(e["team_position"])
        seats.setdefault(team, ["", ""])[0 if pos <= 0 else 1] = ref_label(e)
    pool = []
    reserves = {}
    for lname in c["pools"]:
        kind = "pool" if "driver_pool" in lname else "reserve" if "reserve" in lname else "f2"
        for e in lists[lname]["entrants"]:
            lab = ref_label(e)
            if not lab or lab in SKIP or not drivers_db.get(lab):
                continue
            pool.append(lab)
            out["drivers"].setdefault(lab, {"kind": "legend" if lab in LEGENDS else kind})
            if kind == "reserve" and e.get("team"):
                reserves[lab] = e["team"]  # the team whose reserve driver he is in this career
    for team, pair in OPTIONAL_TEAMS.get(group, {}).items():
        seats[team] = list(pair)
        for lab in pair:
            out["drivers"].setdefault(lab, {})["home"] = team  # races only while his team is in the career
    for team, pair in seats.items():
        for lab in pair:
            out["drivers"].setdefault(lab, {})["kind"] = "f1"
    out["careers"][group] = {"year": c["year"], "grid": seats, "pool": sorted(set(pool)),
                             "contracts": CONTRACTS[c["year"]], "reserves": reserves}
JUNIOR_TEAMS = {"tororosso_26", "tororosso"}  # a family's second team, which brings on its juniors


def colour(v):
    """the database's signed ARGB int -> 0xRRGGBB"""
    try:
        return int(v) & 0xFFFFFF
    except (TypeError, ValueError):
        return 0x808080


for label, (name, prestige, perf, family, patience) in TEAMS.items():
    t = teams_db[label]
    out["teams"][label] = {"name": name, "code": t["display_name_code"] or TEAM_CODE.get(label, ""),
                           "stat": TEAM_STAT.get(label, int(t["stat_id"])),
                           "prestige": prestige, "performance": perf, "family": family, "patience": patience,
                           "junior": label in JUNIOR_TEAMS,
                           "colour": colour(t.get("ui_team_colour") or t.get("primary_colour"))}
    if any(label in teams for teams in OPTIONAL_TEAMS.values()):
        out["teams"][label]["optional"] = True

# name keys used by exactly one driver: Silly Season can rename those drivers (new talents) without
# renaming anyone else
from collections import Counter  # noqa: E402

uses = Counter()
for row in db["drivers"]:
    for k in ("first_name", "last_name"):
        uses[row.get(k)] += 1
for label, d in out["drivers"].items():
    row = drivers_db[label]
    born = BORN.get(label) or int(row.get("date_of_birth.year") or 1900)
    if born <= 1900:
        born = int(jitter(label + "born", 1999, 2006))
    d.update({"name": name_of(label), "nat": row.get("nationality") or "", "stat": int(row["stat_id"]),
              "born": born, "seasons": int(row.get("f1_seasons_completed") or 0),
              "academy": ACADEMY.get(label, ""), "initials": row.get("initials") or "",
              "female": (row.get("head") or "").startswith("female")})
    first, last = row.get("first_name"), row.get("last_name")
    if d["kind"] == "reserve" and first and last and uses[first] == 1 and uses[last] == 1:
        d["name_keys"] = [first, last]  # a fictional driver Silly Season may turn into a new talent
    d.update(ratings(label, d["kind"]))
json.dump(out, open(dst, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(f'{len(out["drivers"])} drivers, {len(out["teams"])} teams, careers: {", ".join(out["careers"])} -> {dst}')
