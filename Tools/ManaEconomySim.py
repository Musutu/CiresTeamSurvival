"""Small, bounded mana-economy simulation (items-v2). Not a balance matrix.

Compares the old mana model (1.5% of max mana per second, flat costs) with the items-v2 model
(flat + percent regen, mana costs that grow with champion level) for three concrete kits at four
levels, casting either on cooldown ("spam") or with a measured rotation (every other opportunity).
Reads the regen/cost numbers from Content/Data/Items.json -> manaEconomy so the doc and the game
agree. Usage: python Tools/ManaEconomySim.py [--markdown]
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ECON = json.loads((ROOT / "Content/Data/Items.json").read_text(encoding="utf-8")).get("manaEconomy", {})
GCD = 0.9          # ACireHero global cooldown after a cast
WINDOW = 120.0     # seconds simulated per case
STEP = 0.1

# (name, base mana cost, cooldown) from Content/Data/Abilities.json (level-1 base numbers).
KITS = {
    "caster (Wizard-like)": [("ember_lance", 40, 6), ("chain_spark", 45, 10), ("cinder_cone", 35, 8),
                             ("blight_sigil", 35, 8), ("grave_line", 35, 8), ("purge", 40, 14)],
    "healer (Scholar-like)": [("restoring_light", 45, 6), ("aether_mend", 40, 6), ("purify", 25, 8),
                              ("sanctuary", 70, 16), ("protection_dome", 55, 18), ("chain_spark", 45, 10)],
    "constructor (Artificer)": [("photon_turret", 55, 14), ("arc_mine", 35, 9), ("overcharge", 40, 20),
                                ("skitter_swarm", 45, 15), ("disruption_pylon", 50, 20), ("phase_lance", 40, 6)],
}
SKILLS_AT = {1: 2, 6: 4, 12: 6, 18: 6}    # active slots a champion typically owns at that level
INT_AT = lambda level: 20 + 2 * (level - 1)  # INT primary: 20 + 2 per level


def old_model(level, item_mana=0, item_regen=0):
    pool = 30 * INT_AT(level) + item_mana
    return pool, pool * 0.015 + item_regen, 1.0


def new_model(level, item_mana=0, item_regen=0):
    pool = 30 * INT_AT(level) + item_mana
    regen = ECON.get("regenFlat", 2.0) + pool * ECON.get("regenPercent", 0.01) + item_regen
    return pool, regen, 1.0 + ECON.get("costPerLevel", 0.05) * (level - 1)


def simulate(kit, level, model, cadence=1, item_mana=0, item_regen=0):
    """Returns (seconds until the first failed cast or None, casts landed, casts refused)."""
    pool, regen, cost_scale = model(level, item_mana, item_regen)
    skills = kit[:SKILLS_AT[level]]
    mana, t, gcd = pool, 0.0, 0.0
    ready = [0.0] * len(skills)
    skip = [0] * len(skills)
    first_dry, landed, refused = None, 0, 0
    while t < WINDOW:
        mana = min(pool, mana + regen * STEP)
        gcd = max(0.0, gcd - STEP)
        for i, (_, cost, cd) in enumerate(skills):
            if gcd > 0 or t < ready[i]:
                continue
            skip[i] += 1
            if skip[i] % cadence:
                ready[i] = t + cd        # measured play lets this window pass
                continue
            price = cost * cost_scale
            if mana < price:
                refused += 1
                first_dry = first_dry if first_dry is not None else t
                ready[i] = t + 1.0       # the player retries a second later
                continue
            mana -= price
            ready[i] = t + cd
            gcd = GCD
            landed += 1
            break
        t += STEP
    return first_dry, landed, refused


def fmt(dry):
    return "never" if dry is None else f"{dry:.0f}s"


def main():
    md = "--markdown" in sys.argv
    print(f"manaEconomy: {ECON}")
    rows = []
    for kit_name, kit in KITS.items():
        for level in SKILLS_AT:
            o_pool, o_regen, _ = old_model(level)
            n_pool, n_regen, n_scale = new_model(level)
            o = simulate(kit, level, old_model)
            n = simulate(kit, level, new_model)
            m = simulate(kit, level, new_model, cadence=2)
            item = simulate(kit, level, new_model, item_mana=250, item_regen=5)
            rows.append((kit_name, level, f"{o_pool:.0f}", f"{o_regen:.1f}", fmt(o[0]), f"{n_regen:.1f}", f"x{n_scale:.2f}",
                         fmt(n[0]), f"{n[1]}/{n[2]}", fmt(m[0]), fmt(item[0])))
    head = ("kit", "lvl", "pool", "old regen", "old spam dry", "new regen", "cost", "new spam dry", "casts ok/refused",
            "measured dry", "spam + mana item")
    if md:
        print("| " + " | ".join(head) + " |")
        print("|" + "---|" * len(head))
        for r in rows:
            print("| " + " | ".join(str(x) for x in r) + " |")
    else:
        print("  ".join(head))
        for r in rows:
            print("  ".join(str(x) for x in r))


if __name__ == "__main__":
    main()
