"""Read drivers, teams, entrant lists and driver statistics out of a COPY of gamedatabases.2025.bin.

Uso:
    python -I tools/extract_gamedb.py <ruta a _swofty-ref/tools> <copia de gamedatabases.2025.bin> <salida.json>

Solo lee el archivo. Nunca apuntes este script al archivo de la carpeta del juego: copia el .bin antes.
"""
import json
import sys

sys.path.insert(0, sys.argv[1])
import gamedb  # noqa: E402  (tools/gamedb.py del repositorio de Swofty)

db_path, out_path = sys.argv[2], sys.argv[3]
recs = gamedb.read(open(db_path, "rb").read())
by_key = {r.key: r for r in recs}


def get(path):
    r = by_key.get(gamedb.fnv1a64(path))
    return None if r is None else r.value.decode("utf-8", "replace")


DRIVER_FIELDS = ["_metadata_", "stat_id", "first_name", "last_name", "initials", "nationality", "car_number",
                 "f1_seasons_completed", "date_of_birth.year", "head"]
TEAM_FIELDS = ["_metadata_", "stat_id", "display_name_code", "team_boss", "ui_team_colour", "primary_colour"]
ENTRANT_FIELDS = ["_metadata_", "driver", "entrant_reference", "team", "team_position", "car_number"]

out = {"drivers": [], "teams": [], "lists": [], "stats": {}}
for i in range(int(get("entrants.drivers._driver_instancecount_") or 0)):
    out["drivers"].append({f: get(f"entrants.drivers.driver_{i}.{f}") for f in DRIVER_FIELDS})
for i in range(int(get("entrants.teams._team_instancecount_") or 0)):
    out["teams"].append({f: get(f"entrants.teams.team_{i}.{f}") for f in TEAM_FIELDS})
for i in range(int(get("entrants.entrant_lists._entrant_list_instancecount_") or 0)):
    base = f"entrants.entrant_lists.entrant_list_{i}"
    lst = {"index": i, "label": get(base + "._metadata_"), "refs": get(base + ".entrant_list_references"),
           "season": get(base + ".season"), "entrants": []}
    for j in range(int(get(base + "._entrant_instancecount_") or 0)):
        lst["entrants"].append({f: get(f"{base}.entrant_{j}.{f}") for f in ENTRANT_FIELDS})
    out["lists"].append(lst)
holders = "driver_statistics.driver_statistics"
for i in range(int(get(holders + "._DriverStatisticsHolder_instancecount_") or 0)):
    base = f"{holders}.DriverStatisticsHolder_{i}"
    drv = get(base + ".driver") or get(base + "._metadata_")
    sets = {}
    for s in range(int(get(base + "._StatisticsDataSet_instancecount_") or 0)):
        sb = f"{base}.StatisticsDataSet_{s}"
        stats = {}
        for q in range(int(get(sb + "._Statistic_instancecount_") or 0)):
            stats[get(f"{sb}.Statistic_{q}._metadata_")] = get(f"{sb}.Statistic_{q}.evolution")
        sets[get(sb + "._metadata_")] = stats
    out["stats"][drv] = sets
json.dump(out, open(out_path, "w", encoding="utf-8"), indent=1)
print(f'{len(out["drivers"])} drivers, {len(out["teams"])} teams, {len(out["lists"])} lists, '
      f'{len(out["stats"])} statistic holders -> {out_path}')
