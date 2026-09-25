"""Write Content/Data/TownAssetSlots.fabkit.json: Medieval Kingdom (Hivemind, Fab) props over the town slots.

Only meshes present in the main checkout's local pack are written. The overlay has priority 25 (above the free
Fab pass at 20 and Tripo at 10); CireEnvironmentProps resolves each candidate with a quiet load, so a clone
without the pack keeps the lower-priority art. Props keep their authored real-world size ("fit": "none");
footprint slots (cart, market stall) are fitted to the slot footprint like every overlay.
Run: python Tools/BuildFabTownSlots.py
"""
import json
import os
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN = Path(subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout.splitlines()[0].split(" ", 1)[1])
KIT = MAIN / "Content" / "CastleTown" / "Static_Mesh"
PICK = {
    "crate": "SM_Crate_A_01", "crate_long": "SM_CargoCrate_01", "barrel": "SM_Barrel_01", "barrel_wine": "SM_Large_Keg",
    "bucket": "SM_Bucket_01", "stool": "SM_Stool", "fire_pit": "SM_FirePit_01",
    "hanging_lantern": "SM_Lantern_Hanging", "castle_door": "SM_Castle_Door", "shield": "SM_Shield_01", "axe": "SM_Axe_01",
    "jug": "SM_Flagon_01", "pot": "SM_Pot", "apple": "SM_Apple", "cart": "SM_Cart",
    "fence": "SM_Fence_01a",
}
FOOTPRINT_FIT = {"cart"}
# Reviewed on the environment gallery: the Fab market stand is a bare frame (the striped stalls read better) and the
# larger Fab table hides the goods placed on it, so market_stall_a and table keep their current art.


def main():
    index = {}
    for root, _, files in os.walk(KIT):
        for f in files:
            if f.endswith(".uasset"):
                index.setdefault(f[:-7], Path(root))
    slots, missing = {}, []
    for slot, name in PICK.items():
        folder = index.get(name)
        if not folder:
            missing.append(name)
            continue
        rel = folder.relative_to(MAIN / "Content").as_posix()
        row = {"kind": "mesh", "mesh": "/Game/%s/%s.%s" % (rel, name, name), "status": "imported",
               "source": "Medieval Kingdom (Hivemind, Fab)"}
        if slot not in FOOTPRINT_FIT:
            row["fit"] = "none"
        slots[slot] = row
    doc = {"schemaVersion": 1, "priority": 25, "source": "fab",
           "description": "Purchased Medieval Kingdom (Hivemind) props over the town's Tripo/procedural slots (Docs/FabIntegration.md). "
                          "Local-only pack: a slot resolves only when its package exists; otherwise the lower-priority art stays. "
                          "Written by Tools/BuildFabTownSlots.py.",
           "slots": slots}
    (REPO / "Content/Data/TownAssetSlots.fabkit.json").write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    print("slots %d, missing %s" % (len(slots), missing))


main()
