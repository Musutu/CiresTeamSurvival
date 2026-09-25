"""progression-shop: expected gold per player by wave, against item and Skill Shop prices.

Reads Content/Data/Waves.json, LootTables.json (economy, packSchedule), NPCArchetypes.json
(challenge packs), Items.json and SkillShop.json, and prints the curve that Docs/Progression.md
("Economy curve") quotes. Two income lines: waves only (a team that never clears a challenge
pack) and waves + every open challenge bay cleared once per round. Loot-table bonus gold and
arena wins are left out, so both lines are conservative.

  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/EconomyCurve.py [--rounds 6] [--json]

`curve()` is also used by Tools/RunProgressionChecks.py --only data to assert the price bands.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
START_GOLD = 120  # ACireHero::Gold default (CireGame.h)


def load(name):
    return json.loads((ROOT / "Content/Data" / name).read_text(encoding="utf-8"))


def mob_value(e, wave):
    return max(0, e["mobBase"] + e["mobStep"] * (max(1, wave) // max(1, e["stepEveryWaves"])))


def wave_gold(e, wave_def, cycle, extra_units, mv):
    """Gold one player earns from one lane wave (wave kills pay every teammate the full bounty)."""
    total = 0.0
    armored_type = wave_def["type"] in ("armored", "armored_escort")
    for unit in wave_def["units"]:
        count = unit["count"]
        if not unit.get("boss") and not unit.get("escortee"):
            count = max(1, min(20, count + extra_units * cycle))
        if unit.get("boss"):
            mult = e["bossMultiplier"]
        elif unit.get("nonAttacking") or (armored_type and unit.get("escortee")):
            mult = e["armoredMultiplier"]
        else:
            mult = 1
        total += count * mult
    return total * mv * wave_def.get("rewardMultiplier", 1)


def pack_gold(e, npcs, mv):
    packs = npcs["challengePacks"]
    return (len(packs["members"]) * e["packUnitMultiplier"] + e["packLeaderMultiplier"]) * mv


def bays_open(loot, round_number, wave_in_round):
    return sum(1 for bay in loot["packSchedule"]["bays"] if round_number >= bay["unlockRound"] and wave_in_round >= bay["unlockWave"])


def curve(rounds=6):
    waves, loot, npcs = load("Waves.json"), load("LootTables.json"), load("NPCArchetypes.json")
    e = loot["economy"]
    per_cycle = waves["wavesPerCycle"]
    extra = waves.get("cycleScaling", {}).get("extraUnits", 0)
    rows, only_waves, with_packs = [], START_GOLD, START_GOLD
    for r in range(1, rounds + 1):
        cleared_bays = 0
        for i in range(per_cycle):
            wave = (r - 1) * per_cycle + i + 1
            mv = mob_value(e, wave)
            g = wave_gold(e, waves["waves"][i % len(waves["waves"])], r - 1, extra, mv)
            # Each bay opened by now is cleared once per round, at the wave it opens.
            open_now = bays_open(loot, r, i + 1)
            p = (open_now - cleared_bays) * pack_gold(e, npcs, mv)
            cleared_bays = open_now
            only_waves += g
            with_packs += g + p
            rows.append({"round": r, "wave": wave, "mobValue": mv, "waveGold": round(g), "packGold": round(p),
                         "cumulativeWaves": round(only_waves), "cumulativeWithPacks": round(with_packs)})
    return rows


def prices():
    items = load("Items.json")
    by_id = {item["id"]: item for item in items["items"]}

    def total(item_id):
        item = by_id[item_id]
        return item["cost"] + sum(total(c) for c in item.get("components", []))
    tiers = {}
    for item in items["items"]:
        if item["cost"] > 0:
            tiers.setdefault(item["tier"], []).append(total(item["id"]))
    return {tier: (min(v), round(sum(v) / len(v)), max(v)) for tier, v in tiers.items()}


def skill_prices(wave):
    shop = load("SkillShop.json")["prices"]
    mv = mob_value(load("LootTables.json")["economy"], wave)
    return {"active(1st bought)": round(shop["active"] * (1 + shop["activeOwnedGrowth"]) * mv),
            "passive": round(shop["passive"] * mv), "ultimate": round(shop["ultimate"] * mv),
            "level 1->2": round(shop["levelUpBase"] * mv)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    rows = curve(args.rounds)
    if args.json:
        print(json.dumps({"curve": rows, "itemTotals": prices()}, indent=1))
        return
    print("round wave  mob  wave+  pack+   total(waves)  total(+packs)   skill prices at this wave")
    for row in rows:
        s = skill_prices(row["wave"])
        print(f"{row['round']:>5} {row['wave']:>4} {row['mobValue']:>4} {row['waveGold']:>6} {row['packGold']:>6} {row['cumulativeWaves']:>14} "
              f"{row['cumulativeWithPacks']:>14}   active {s['active(1st bought)']}, passive {s['passive']}, ult {s['ultimate']}, lvl {s['level 1->2']}")
    print("item totals (min / mean / max, recipe parts included):")
    for tier, (lo, mean, hi) in prices().items():
        print(f"  {tier:<11} {lo:>5} {mean:>5} {hi:>5}")


if __name__ == "__main__":
    main()
