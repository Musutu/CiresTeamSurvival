"""jungle-packs: the challenge-pack monster inventory (Docs/JunglePacks.md).

Reads the same data the game merges (NPCArchetypes.json, Races.json, Bestiary.json, JunglePacks.json) and applies the
same rules as CireJunglePacks.cpp:
  * pool     = every combat archetype the wave system can spawn, bosses and non-combat bonus creatures excluded,
               grouped by race (creatures outside every race: their JunglePacks.json family, Mixed packs only);
  * role     = tank (archetype role "tank"), healer (owns a healAlly ability), else DPS;
  * kit fill = a unit whose own kit has fewer than kitFloor non-basic abilities borrows from its race (same pack role
               first, then the rest, in id order; never summons / constructs; heals only for healers).

Usage: python Tools/JunglePackInventory.py [--write]   (--write replaces the generated block in Docs/JunglePacks.md)
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content" / "Data"
DOC = ROOT / "Docs" / "JunglePacks.md"
BEGIN, END = "<!-- inventory:begin (Tools/JunglePackInventory.py --write) -->", "<!-- inventory:end -->"
ROLES = ("tank", "healer", "DPS")


def load(name: str) -> dict:
    return json.loads((DATA / name).read_text(encoding="utf-8"))


def archetypes() -> tuple[dict, list[str], dict]:
    npc, races, best = load("NPCArchetypes.json"), load("Races.json"), load("Bestiary.json")
    db: dict[str, dict] = {}
    for key, a in npc["archetypes"].items():
        db[key] = {"id": key, "role": a.get("role", "bruiser"), "cls": a.get("classification", "normal"),
                   "abilities": list(a.get("abilities", [])), "race": None, "creature": False}
    order = races.get("raceOrder") or list(races["races"].keys())
    names = {}
    for rid in order:
        race = races["races"][rid]
        names[rid] = race.get("name", rid)
        for uid, u in race["units"].items():
            if u.get("extends"):
                db[uid]["abilities"] += u.get("abilities", [])
                db[uid]["race"] = rid
            else:
                a = u["archetype"]
                db[uid] = {"id": uid, "role": a.get("role", "bruiser"), "cls": a.get("classification", "normal"),
                           "abilities": list(a.get("abilities", [])), "race": rid, "creature": False}
    for uid, u in best["units"].items():
        a = u["archetype"]
        db[uid] = {"id": uid, "role": a.get("role", "bruiser"), "cls": a.get("classification", "normal"),
                   "abilities": list(a.get("abilities", [])), "race": u.get("race"), "creature": True,
                   "kinds": u.get("kind", [])}
    return db, order, names


def role_of(a: dict) -> str:
    if a["role"] == "tank":
        return "tank"
    return "healer" if any(x.get("type") == "healAlly" for x in a["abilities"]) else "DPS"


def own_kit(a: dict) -> list[dict]:
    return [x for x in a["abilities"] if not x.get("basic")]


def build() -> dict:
    rules = load("JunglePacks.json")
    floor, families, excluded = rules.get("kitFloor", 6), rules.get("families", {}), set(rules.get("exclude", []))
    db, order, names = archetypes()

    def pack_race(a: dict):
        return a["race"] or families.get(a["id"])

    def eligible(a: dict) -> bool:
        return a["cls"] != "boss" and a["id"] not in excluded and pack_race(a) is not None

    ids = sorted(db)
    units, audit = [], []
    for uid in ids:
        a = db[uid]
        if not eligible(a):
            continue
        race, role, own = pack_race(a), role_of(a), own_kit(a)
        took = []
        if len(own) < floor:
            have = {x["id"] for x in a["abilities"]}
            def rank(o: dict) -> int:
                return 0 if o["role"] == a["role"] else 1 if role_of(o) == role else 2
            donors = [db[o] for p in range(3) for o in ids
                      if o != uid and db[o]["race"] == race and not db[o]["creature"] and o not in excluded and rank(db[o]) == p]
            for d in donors:
                for x in own_kit(d):
                    if len(own) + len(took) >= floor:
                        break
                    if x["id"] in have or x.get("type") in ("summon", "deploy"):
                        continue
                    if x.get("type") == "healAlly" and role != "healer":
                        continue
                    if x.get("type") == "disengage" and a["role"] not in ("caster", "ranged", "support"):
                        continue
                    have.add(x["id"])
                    took.append(f"{x['id']} ({d['id']})")
                if len(own) + len(took) >= floor:
                    break
            audit.append(f"`{uid}` [{race} {role}]: own kit {len(own)}, borrowed {len(took)} from "
                         f"{'its family race' if a['race'] is None else 'its race'} {race}"
                         f"{' (STILL SHORT)' if len(own) + len(took) < floor else ''}: {', '.join(took)}")
        units.append({"id": uid, "race": race, "role": role, "own": len(own), "kit": len(own) + len(took),
                      "creature": a["creature"], "rare": a["creature"] and a["race"] is None})
    stand = []
    for rid in order:
        for role in ROLES:
            if not any(u["race"] == rid and u["role"] == role and not u["rare"] for u in units):
                stand.append(f"{rid} has no {role}: that slot draws a {role} from the Mixed pool")
    bosses = sorted(uid for uid, a in db.items() if a["cls"] == "boss")
    skipped = sorted(uid for uid, a in db.items() if a["cls"] != "boss" and not eligible(a))
    return {"units": units, "audit": audit, "standins": stand, "order": order, "names": names, "floor": floor,
            "bosses": bosses, "skipped": skipped}


def markdown(inv: dict) -> str:
    units, order = inv["units"], inv["order"]
    lines = [BEGIN, "",
             f"**{len(units)} pack monsters** (non-boss combat units the wave system can spawn), "
             f"{len(inv['bosses'])} bosses kept out of packs, {len(inv['skipped'])} non-combat creatures excluded "
             f"({', '.join(inv['skipped']) or 'none'}).", "",
             "| Pack type | Tank | Healer | DPS | Total | Units (role, own kit -> complete kit) |",
             "| --- | ---: | ---: | ---: | ---: | --- |"]
    totals = {r: 0 for r in ROLES}
    for rid in order:
        mine = [u for u in units if u["race"] == rid and not u["rare"]]
        counts = {r: sum(1 for u in mine if u["role"] == r) for r in ROLES}
        for r in ROLES:
            totals[r] += counts[r]
        cells = ", ".join(f"{u['id']}{' *' if u['creature'] else ''} ({u['role']}, {u['own']}->{u['kit']})" for u in mine)
        lines.append(f"| {inv['names'].get(rid, rid)} (`{rid}`) | {counts['tank']} | {counts['healer']} | {counts['DPS']} | {len(mine)} | {cells} |")
    rares = [u for u in units if u["rare"]]
    rare_counts = {r: sum(1 for u in rares if u["role"] == r) for r in ROLES}
    for r in ROLES:
        totals[r] += rare_counts[r]
    lines.append(f"| Wild creatures (Mixed only) | {rare_counts['tank']} | {rare_counts['healer']} | {rare_counts['DPS']} | {len(rares)} | "
                 + ", ".join(f"{u['id']} * ({u['role']}, family {u['race']}, {u['own']}->{u['kit']})" for u in rares) + " |")
    lines.append(f"| **Mixed (all of the above)** | **{totals['tank']}** | **{totals['healer']}** | **{totals['DPS']}** | **{len(units)}** | |")
    lines += ["", "`*` = a Bestiary.json creature (race variant or rare). Bosses (never pack members): " + ", ".join(f"`{b}`" for b in inv["bosses"]) + ".", "",
              "### Role gaps and stand-ins", ""]
    lines += [f"- {s}" for s in inv["standins"]] or ["- None: every race fields at least one tank, one healer and one DPS, so no cross-race stand-in is needed today. "
                                                     "If a race ever loses a role, that slot draws the role from the Mixed pool and the game logs it (`STAND-IN` in the audit)."]
    lines += ["", f"### Kit fill audit (kit floor {inv['floor']})", "",
              f"Every pack unit needs 5 abilities for T3 and a complete kit bigger than that for T4. Units whose own kit has fewer than "
              f"{inv['floor']} non-basic abilities borrow the rest from their race (same pack role first). {len(inv['audit'])} units are filled:", ""]
    lines += [f"- {a}" for a in inv["audit"]]
    lines += ["", END]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    block = markdown(build())
    if not args.write:
        print(block)
        return
    text = DOC.read_text(encoding="utf-8")
    start, end = text.index(BEGIN), text.index(END) + len(END)
    DOC.write_text(text[:start] + block + text[end:], encoding="utf-8", newline="\n")
    print(f"updated {DOC}")


if __name__ == "__main__":
    main()
