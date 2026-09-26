"""champion-hq: write Content/Data/ChampionArt.hq.json rows from the integration report (Saved/ChampionHQIntegration.json).

Only champions whose HQ body is integrated get a row; the rest keep their current art. Row shape = ChampionArt.tripo.json
("champions" rows), so Tools/BuildTripoChampionMotion.py and Tools/RetargetChampionAttacks.py -CireChampionAttacksAdd
consume it (CIRE_CHAMPION_ART_FILES=ChampionArt.hq.json), then Tools/WriteChampionHQBindings.py publishes the bindings.
Plain Python. Usage: python Tools/WriteChampionHQArtRows.py [--status ready|review]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "Saved" / "ChampionHQIntegration.json"
OUT = ROOT / "Content" / "Data" / "ChampionArt.hq.json"

# key: (profileIds, folder, heightCm, preferred attack clip, ChampionAttacks02 extras)
CHAMPIONS = {
    "ranger": (["ranger"], "Ranger", 178, "attack_bow", ["attack_bow", "attack_crossbow"]),
    "scholar": (["scholar"], "Scholar", 176, "cast_a_spell", []),
    "lancer": (["lancer"], "Lancer", 182, "slash", []),
    "summoner": (["summoner"], "Summoner", 178, "cast_a_spell", []),
    "wizard": (["wizard"], "Wizard", 178, "cast_a_spell", []),
    "gunblade": (["gunblade"], "Gunblade", 182, "slash", ["attack_crossbow"]),
    "witch_slayer": (["witch_slayer"], "WitchSlayer", 180, "slash", ["attack_crossbow"]),
    "huntress": (["huntress"], "Huntress", 172, "slash", ["attack_bow"]),
    "aetheri_artificer": (["aetheri_artificer"], "AetheriArtificer", 184, "cast_a_spell", []),
    "aetheri_warden": (["aetheri_warden"], "AetheriWarden", 192, "slash", []),
    "dryad": (["dryad"], "Dryad", 180, "cast_a_spell", []),
    "dwarf_miner": (["dwarf_miner"], "DwarfMiner", 132, "slash", []),
    "orc_chieftain": (["orc_chieftain"], "OrcChieftain", 198, "slash", []),
    "drakish_footman": (["drakish_footman"], "DrakishFootman", 185, "slash", []),
    "keeper_of_light": (["keeper_of_light"], "KeeperOfLight", 178, "cast_a_spell", []),
    "troll_berserker": (["troll_berserker_melee", "troll_berserker_ranged"], "TrollBerserker", 210, "slash", []),
    "ether_golem_tank": (["ether_golem_tank"], "GolemGranite", 245, "slash", []),
    "ether_golem_support": (["ether_golem_support"], "GolemVerdant", 235, "cast_a_spell", []),
    "ether_golem_bruiser": (["ether_golem_bruiser"], "GolemFelfire", 240, "slash", []),
    "totemic_behemoth": (["totemic_behemoth"], "TotemicBehemoth", 285, "slash", []),
}
# champion-hq summons: key -> (summon id, folder, heightCm, attack clip). Rows carry "summonId" (SummonArt.json) instead of
# a playable profile; BuildTripoChampionMotion / RetargetChampionAttacks treat them like champions.
SUMMONS = {
    "oathbound_guardian": ("oathbound_guardian", "SummonGuardian", 200, "slash"),
    "spectral_companion": ("spectral_companion", "SummonSpectral", 176, "slash"),
    "mechanical_tank": ("mechanical_tank", "SummonMechTank", 190, "slash"),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--status", default="ready")
    args = parser.parse_args()
    units = json.loads(REPORT.read_text(encoding="utf-8"))["units"]
    previous = {r["profileId"]: r for r in json.loads(OUT.read_text(encoding="utf-8"))["champions"]} if OUT.exists() else {}
    rows = []
    for key, (profiles, folder, height, attack, extra) in CHAMPIONS.items():
        unit = units.get(key)
        if not unit or not unit.get("skeletal"):
            continue
        base = "/Game/Tripo/ChampionsHQ/" + folder
        name = unit["export"]
        anims = dict(unit["animations"])
        for need in ("idle", "walk", "run"):
            if need not in anims:
                raise SystemExit("%s: missing %s clip" % (key, need))
        anims.update({"hit": anims.get("hit_to_body_01"), "death": anims.get("fall"),
                      "attack": anims.get(attack) or anims.get("slash") or anims.get("cast_a_spell"),
                      "walkInPlace": "%s/Animations/%s_walk_inplace.%s_walk_inplace" % (base, name, name),
                      "runInPlace": "%s/Animations/%s_run_inplace.%s_run_inplace" % (base, name, name)})
        anims = {k: v for k, v in anims.items() if v}
        row = {"profileId": profiles[0], "profileIds": profiles, "displayName": folder,
               "status": previous.get(profiles[0], {}).get("status", args.status),
               "mesh": unit["mesh"], "skeleton": unit["skeleton"], "material": unit["material"],
               "locomotion": "%s/Animations/BS_Idle_Walk_Run_%s.BS_Idle_Walk_Run_%s" % (base, folder, folder),
               "attack": anims["attack"], "heightCm": height, "yaw": -90,
               "attacksFolder": "HQ" + folder, "attacksExtra": extra, "animations": anims}
        rows.append(row)
    for key, (summon, folder, height, attack) in SUMMONS.items():
        unit = units.get(key)
        if not unit or not unit.get("skeletal"):
            continue
        base = "/Game/Tripo/ChampionsHQ/" + folder
        name = unit["export"]
        anims = dict(unit["animations"])
        if not all(k in anims for k in ("idle", "walk", "run")):
            continue
        anims.update({"attack": anims.get(attack) or anims.get("slash") or anims.get("cast_a_spell"),
                      "walkInPlace": "%s/Animations/%s_walk_inplace.%s_walk_inplace" % (base, name, name),
                      "runInPlace": "%s/Animations/%s_run_inplace.%s_run_inplace" % (base, name, name)})
        anims = {k: v for k, v in anims.items() if v}
        rows.append({"profileId": "summon:" + summon, "summonId": summon, "displayName": folder, "status": args.status,
                     "mesh": unit["mesh"], "skeleton": unit["skeleton"], "material": unit["material"],
                     "locomotion": "%s/Animations/BS_Idle_Walk_Run_%s.BS_Idle_Walk_Run_%s" % (base, folder, folder),
                     "attack": anims["attack"], "heightCm": height, "yaw": -90, "attacksFolder": "HQ" + folder,
                     "attacksExtra": [], "animations": anims})
    data = {"schemaVersion": 1,
            "description": "Champion HQ bodies (feat/champion-hq): Tripo H3.1 multi-view bodies from the Art/ChampionHQ reference sheets, "
                           "rigged on the UE5-Mannequin 61-bone preset, on the M_CireHero_PBR master. Row shape = ChampionArt.tripo.json; "
                           "Tools/WriteChampionHQBindings.py turns ready rows into ChampionArtBindings.json rows that keep the previous body "
                           "as 'fallback'. Generated by Tools/WriteChampionHQArtRows.py; source log Art/ChampionHQ/TripoChampionHQ.json.",
            "champions": rows}
    OUT.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8", newline="\n")
    print("rows", len(rows), [r["profileId"] for r in rows])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
