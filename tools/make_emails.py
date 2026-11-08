"""Writes db/emails.json: Silly Season's mail groups in the game's inbox triggers.

Uso: python -I tools/make_emails.py db/emails.json
"""
import json
import sys

# trigger, source group, new group, condition, stat id, key stem, importance, extra message fields
ROWS = [
    ("practice_1", "mt_drivermarket_retired", "silly_season_preview_dc", "email_dc_weekendpreview", 254, "preview", "eMedium", {}),
    ("practice_1", "mt_drivermarket_retired", "silly_season_preview_mt", "email_mt_weekendpreview", 255, "preview", "eMedium", {}),
    ("post_race", "dc_rivalry_teammate_intensified", "silly_season_radar_dc", "email_dc_weekendpreview", 256, "radar", "eLow",
     {"attachment": "none", "extra2_button": "none", "rivalry_email_type": "eNone", "rivalry_type": "eNone"}),
    ("post_race", "dc_rivalry_teammate_intensified", "silly_season_radar_mt", "email_mt_weekendpreview", 257, "radar", "eLow",
     {"attachment": "none", "extra2_button": "none", "rivalry_email_type": "eNone", "rivalry_type": "eNone"}),
    ("driver_moves_finalised", "dc_drivermoves", "silly_season_grid_dc", "email_dc_weekendpreview", 258, "grid", "eHigh", {"attachment": "none"}),
    ("driver_moves_finalised", "dc_drivermoves", "silly_season_grid_mt", "email_mt_weekendpreview", 259, "grid", "eHigh", {"attachment": "none"}),
]
MESSAGE_FIELDS = ["_metadata_", "accolade_category", "attachment", "extra1_button", "extra2_button", "from_contact",
                  "message_body", "message_body_type", "message_deletion", "message_importance", "rivalry_email_type",
                  "rivalry_type", "subject", "team_accolade_category"]
GROUP_FIELDS = ["_metadata_", "conditions", "laptop_opened_vo", "num_days_cooldown", "receiver", "stat_id",
                "messages._message_instancecount_"] + [f"messages.message_0.{f}" for f in MESSAGE_FIELDS]
ops = []
fields = {}
for trig, src, new, cond, stat, stem, imp, extra in ROWS:
    t = f"emails.triggers.trigger[_metadata_={trig}]"
    fields[f"{t}.group"] = GROUP_FIELDS
    msg = {"messages._message_instancecount_": "1", "messages.message_0._metadata_": f"{new}_01",
           "messages.message_0.from_contact": "marc_priestley",
           "messages.message_0.subject": f"lng_silly_season_mail_{stem}_subject",
           "messages.message_0.message_body": f"lng_silly_season_mail_{stem}_body",
           "messages.message_0.message_importance": imp}
    msg.update({f"messages.message_0.{k}": v for k, v in extra.items()})
    ops.append({"op": "add-row", "when": {"emails": True}, "table": t, "from": f"{t}.group[_metadata_={src}]", "stat_id": "keep",
                "set": {"_metadata_": new, "conditions": cond, "stat_id": str(stat), "num_days_cooldown": "0",
                        "receiver": "Driver", **msg}})
json.dump({"schema_version": 1, "fields": fields, "ops": ops}, open(sys.argv[1], "w", encoding="utf-8"), indent=1)
print(len(ops), "ops")
