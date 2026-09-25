"""Write Content/Data/TownAssetSlots.fabworld.json: Medieval Kingdom (Hivemind, Fab) foliage and backdrop over the
world-scale town slots (Docs/EnvironmentProps.md, "World scale").

Only meshes present in the main checkout's local pack are written (object paths only; the pack itself is never
committed). The overlay has priority 30 (above the Fab prop kit at 25); CireEnvironmentProps resolves each candidate
with a quiet load and fits it into the slot footprint, so a clone without the pack, or a run with -CireNoFab, keeps
the committed CC0 art. Every chosen mesh was checked with Tools/MeasureFabWorld.py: Nanite, and base materials that
support instanced static meshes (the Fab large rocks and mountains do not, so the mountain slot is drawn with plain
mesh components: "component": "static" in the base slot).
Run: python Tools/BuildFabWorldSlots.py
"""
import json
import os
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN = Path(subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout.splitlines()[0].split(" ", 1)[1])
PACK = MAIN / "Content" / "CastleTown"
PICK = {
    # slot: mesh name (searched anywhere in the pack)
    "tree_leafy": "SM_EuropeanBeech_L_02", "tree_young": "SM_EuropeanBeech_M_01", "tree_fir": "SM_SilverFir_L_02",
    "tree": "SM_EuropeanBeech_L_01",  # the old leafless town tree becomes a leafy beech
    "grass_clump": "SM_WildGrass_M_02", "wildflowers": "SM_WildCarrot_M_01",
    "fern_1": "SM_EagleFern_M_01", "fern_2": "SM_EagleFern_S_01",
    "shrub_1": "SM_EuropeanBeech_S_01", "shrub_2": "SM_EuropeanBeech_XS_01", "shrub_3": "SM_EuropeanBeech_S_02", "shrub_4": "SM_EuropeanBeech_XS_01",
    "stump": "SM_EuropeanBeech_Stump_01", "fallen_log": "SM_EuropeanBeech_Log_01",
    "mountain": "SM_Mountain_02@Hills",
}
NATIVE = {"stump", "fallen_log"}  # keep the scan's real-world size (their base slots have no footprint)


def main():
    index = {}
    for root, _, files in os.walk(PACK / "Static_Mesh"):
        for f in files:
            if f.endswith(".uasset"):
                index.setdefault(f[:-7] + "@" + Path(root).name, Path(root))
                index.setdefault(f[:-7], Path(root))
    for root, _, files in os.walk(PACK / "Scanned_Foliage"):
        for f in files:
            if f.endswith(".uasset"):
                index.setdefault(f[:-7], Path(root))
    slots, missing = {}, []
    for slot, key in PICK.items():
        folder = index.get(key)
        name = key.split("@")[0]
        if not folder:
            missing.append(key)
            continue
        rel = folder.relative_to(MAIN / "Content").as_posix()
        row = {"kind": "mesh", "mesh": f"/Game/{rel}/{name}.{name}", "status": "imported", "source": "Medieval Kingdom (Hivemind, Fab)"}
        if slot in NATIVE:
            row["fit"] = "none"
        if slot == "mountain":
            # the scan's own sand-coloured material reads as a yellow blob under the town grade; the CC0 arena blend (rock
            # with moss on up-facing slopes, world-aligned) gives green-topped mountains at any scale.
            row["materialOverride"] = "/Game/Arenas/Materials/MI_ArenaB_ForestRock.MI_ArenaB_ForestRock"
        slots[slot] = row
    doc = {"schemaVersion": 1, "priority": 30, "source": "fab",
           "description": "world-scale: purchased Medieval Kingdom (Hivemind) scanned foliage and backdrop mountains over the town's CC0 slots "
                          "(Docs/EnvironmentProps.md). Local-only pack: a slot resolves only when its package exists; otherwise the committed "
                          "art stays. Written by Tools/BuildFabWorldSlots.py.",
           "slots": slots}
    (REPO / "Content/Data/TownAssetSlots.fabworld.json").write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    print("slots %d, missing %s" % (len(slots), missing))


main()
