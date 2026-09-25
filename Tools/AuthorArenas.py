"""Author Content/Data/Arenas.json: the randomised PvP arena pool (Docs/Arenas.md).

Pure Python. Every arena is mirror-symmetric across the centre line (X = 0): Ember spawns on
the west (-X) half, Dusk on the east, and every blocker is written together with its mirrored
twin, so neither team gets a spawn, cover or sight-line advantage. Decoration (not blocking)
may differ between halves. The runtime (CireArenas) and the native checks re-verify symmetry,
spawn clearance and walkable paths from the written JSON.

Slots list candidate assets in priority order: Eric's Fab packs once added to the project
(paths under /Game/Fab/Arenas/...), then this project's CC0 / original assets, then an engine
shape. Kit sizes come from Art/Arenas/Meshes/ArenaMeshes.json and prop sizes from
Art/Arenas/ImportReport.json so authored footprints match the art.

Usage: python Tools/AuthorArenas.py
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Content/Data/Arenas.json"
KIT = {m["name"]: m for m in json.loads((ROOT / "Art/Arenas/Meshes/ArenaMeshes.json").read_text())["meshes"]}
REPORT = json.loads((ROOT / "Art/Arenas/ImportReport.json").read_text())
PROPS = {m["asset"].split(".")[-1]: m for v in REPORT["props"].values() for m in v}

M = lambda n: f"/Game/Arenas/Materials/{n}.{n}"
SKY = lambda n: f"/Game/Arenas/Sky/{n}.{n}"
SLOTS: dict = {}

# Fab packs from Eric's library that are Unreal-format only (Docs/FAB-ADD-TO-PROJECT.md, "Arenas"). After "Add to
# Project", put the real object paths of the meshes you want here and rerun this script: they are tried before the
# CC0/original art, per slot, and the arena switches over automatically. Empty lists keep the current art.
FAB_OVERRIDES = {
    "well": [],              # e.g. Fab "Old Stone Well" (website download, fab-assets pipeline)
    "basalt_tall": [],       # Iceland Collections: basalt column assemblies
    "lava_large": [],        # Iceland Collections: mossy lava boulders
    "arch": [],              # Moab Desert Collections: arch / hoodoo assemblies
    "hoodoo": [],
    "canyon_wall": [],       # Moab Desert Collections: cliff assemblies
    "glade_tree": [],        # European Hornbeam: mature trees
    "glade_tree_young": [],
    "column": [],            # Underwater World: ruin columns
    "ruin_arch": [],
    "kelp": [],
    "container": [],         # Big Star Station: crates, containers, hangar walls
    "cargo_crate": [],
    "bulkhead": [],
}


def kit(name):
    return f"/Game/Arenas/Meshes/{name}.{name}"


def prop(name):
    return PROPS[name]["asset"]


def town(name):
    return f"/Game/Environment/Town/Meshes/{name}.{name}"


def town_prop(asset_id, mesh, variant="1k"):
    return f"/Game/Environment/Town/Props/{asset_id}/{asset_id}_{variant}/StaticMeshes/{mesh}.{mesh}"


def slot(sid, candidates=(), footprint=None, fallback="cube", fallback_material=None, fit="footprint", shape="box", **kw):
    """Register a slot. footprint defaults to the first kit/prop candidate's native size (scale 1 = native)."""
    if footprint is None:
        for c in candidates:
            name = c.split("|")[0].split(".")[-1]
            if name in KIT:
                footprint = KIT[name]["size"]
                break
            if name in PROPS:
                footprint = PROPS[name]["size"]
                break
    assert footprint, sid
    s = {"candidates": list(candidates), "fallback": fallback, "fallbackMaterial": fallback_material or M("MI_ArenaW_FieldStone"),
         "footprint": [round(float(v), 1) for v in footprint], "fit": fit, "shape": shape}
    for k, v in kw.items():
        s[{"materials": "materials", "shadow": "shadow", "essential": "essential", "cull": "cull", "spin": "spin", "wpo": "wpo",
           "hidden": "hidden", "offset": "offset", "yaw": "yaw", "wpo_distance": "wpoDistance"}[k]] = v
    s["candidates"] = list(FAB_OVERRIDES.get(sid, [])) + s["candidates"]
    SLOTS[sid] = s
    return sid


def mat_all(path, n=1):
    return {str(i): path for i in range(n)}


# ============================================================ slots
# generic collision-only proxies (visual art is placed separately)
slot("proxy_box", [], footprint=(100, 100, 100), hidden=True)
slot("proxy_round", [], footprint=(100, 100, 100), shape="round", hidden=True)
# flat ground patches (tracks, beaches): engine cube with a world-aligned material
for name, mi in (("patch_track", "MI_ArenaW_Track"), ("patch_hangar", "MI_ArenaW_HangarFloor")):
    slot(name, [], footprint=(100, 100, 2), fallback_material=M(mi), shadow=False)
for name, mi in (("ground_stubble", "MI_ArenaW_Stubble"), ("ground_blacksand", "MI_ArenaW_BlackSand"), ("ground_redsoil", "MI_ArenaW_RedSoil"),
                 ("ground_forest", "MI_ArenaW_ForestFloor"), ("ground_seabed", "MI_ArenaW_Seabed"), ("ground_deck", "MI_ArenaW_Deck")):
    slot(name, [], footprint=(100, 100, 2), fallback_material=M(mi), shadow=False)
slot("sea", [], footprint=(100, 100, 2), fallback_material=M("MI_Arena_Water"), shadow=False)
slot("glow_strip", [], footprint=(100, 100, 2), fallback_material=M("MI_ArenaF_Emissive"), shadow=False)
slot("glow_amber", [], footprint=(100, 100, 2), fallback_material=M("MI_ArenaF_EmissiveAmber"), shadow=False)

# ---- The Sunlit Fields
slot("hay_round", [kit("SM_Arena_HayBaleRound")], essential=True)
slot("hay_square", [kit("SM_Arena_HayBaleSquare")])
slot("hay_stack", [kit("SM_Arena_HayStack")], essential=True)
slot("hay_pyramid", [kit("SM_Arena_HayPyramid")], essential=True)
slot("stook", [kit("SM_Arena_Stook")], essential=True)
slot("scarecrow", [kit("SM_Arena_Scarecrow")])
slot("stone_wall", [kit("SM_Arena_StoneWall")])
slot("ruin_wall", [kit("SM_Arena_RuinWall")])
slot("hay_wagon", [kit("SM_Arena_HayWagon")], essential=True)
slot("well", [town("SM_Town_Well")], footprint=(286, 330, 439), shape="round")
slot("fence", [town("SM_Town_Fence")], footprint=(13, 413, 110))
slot("cart", [town("SM_Town_Cart")], footprint=(511, 189, 170))
slot("hay_sacks", [town("SM_Town_HaySacks")], footprint=(228, 182, 100))
slot("woodpile", [town("SM_Town_Woodpile")], footprint=(252, 370, 286))
slot("windmill", [kit("SM_Arena_Windmill")])
slot("windmill_sails", [kit("SM_Arena_WindmillSails")], fit="none", spin=-11.0, shadow=True)
slot("barn", [kit("SM_Arena_Barn")])
slot("wheat", [kit("SM_Arena_WheatClump")], footprint=(100, 100, 128), fit="uniform", wpo=True, wpo_distance=4500, essential=True)
slot("stubble", [kit("SM_Arena_StubbleTuft")], footprint=(40, 40, 20), fit="uniform", shadow=False, cull=9000)
TREE = lambda n: f"/Game/Arenas/Trees/{n}/SM_{n}.SM_{n}"
slot("oak", [TREE("island_tree_01")], footprint=(476, 482, 503), fit="uniform")
slot("young_tree", [TREE("tree_small_02")], footprint=(292, 429, 456), fit="uniform")
slot("bucket", [town_prop("wooden_bucket_01", "wooden_bucket_01") + "|" + town_prop("wooden_bucket_01", "wooden_bucket_01_handle")], footprint=(34, 34, 36), fit="uniform", shadow=True)
slot("barrels", [town_prop("wooden_barrels_01", "wooden_barrels_01_barrel01")], footprint=(74, 74, 92), fit="uniform")
slot("spade", [prop("rusted_spade_01_1k")], footprint=(17, 5, 110), fit="uniform")
slot("tree_stump", [town_prop("tree_stump_01", "tree_stump_01_1k")], footprint=(142, 160, 58), fit="uniform")
slot("field_rock", [town_prop("rock_moss_set_01", "rock_moss_set_01_rock02")], footprint=(266, 326, 126), fit="uniform")

# ---- The Black Shore (Iceland)
slot("basalt_tall", [kit("SM_Arena_BasaltTall")], essential=True, fit="footprint")
slot("basalt_steps", [kit("SM_Arena_BasaltSteps")])
slot("basalt_wall", [kit("SM_Arena_BasaltWall")], essential=True)
slot("basalt_stack", [kit("SM_Arena_BasaltStack")], shadow=True)
slot("basalt_cliff", [kit("SM_Arena_BasaltCliff")], shadow=True)
slot("lava_large", [kit("SM_Arena_LavaRockLarge")], essential=True)
slot("lava_medium", [kit("SM_Arena_LavaRockMedium")])
slot("lava_outcrop", [kit("SM_Arena_LavaOutcrop")], materials={"0": M("MI_Arena_Basalt")})
slot("lava_cluster", [prop("coast_rocks_05_1k")], footprint=(404, 375, 133), materials=mat_all(M("MI_ArenaB_Lava")))
slot("lava_ridge", [prop("coast_land_rocks_02_1k")], footprint=(489, 1045, 140), materials=mat_all(M("MI_ArenaB_Lava")))
slot("beach_pebbles", [prop("sand_rocks_small_01_1k")], footprint=(463, 388, 53), materials=mat_all(M("MI_ArenaW_BlackGravel")), shadow=False)
slot("lyme_grass", [prop("grass_medium_02_e")], footprint=(72, 86, 80), fit="uniform", shadow=False)
slot("driftwood", [prop("dead_tree_trunk_1k")], footprint=(610, 56, 58), fit="uniform", materials={"0": M("MI_Arena_Bark")})

# ---- Redrock Canyon (Moab)
slot("arch", [kit("SM_Arena_SandstoneArch")], essential=True)
slot("arch_leg", [], footprint=(330, 330, 540), shape="round", hidden=True)
slot("hoodoo", [kit("SM_Arena_Hoodoo")], essential=True)
slot("sandstone_block", [kit("SM_Arena_SandstoneBlock")], essential=True)
slot("canyon_wall", [kit("SM_Arena_CanyonWall")], shadow=True)
slot("red_boulder", [prop("namaqualand_boulder_03_1k")], footprint=(241, 307, 147), materials=mat_all(M("MI_ArenaB_Sandstone")))
slot("red_boulder_small", [prop("namaqualand_boulder_05_1k")], footprint=(136, 75, 54), materials=mat_all(M("MI_ArenaB_Sandstone")))
for k, name in enumerate(("wild_rooibos_bush_a", "wild_rooibos_bush_b", "wild_rooibos_bush_c")):
    slot(f"scrub_{k}", [prop(name)], footprint=PROPS[name]["size"], fit="uniform", shadow=True)
slot("dead_log", [prop("dead_tree_trunk_1k")], footprint=(305, 28, 29), fit="uniform")

# ---- Hornbeam Glade
slot("glade_tree", [TREE("island_tree_01")], footprint=(476, 482, 503), fit="uniform", essential=True)
slot("glade_tree_young", [TREE("tree_small_02")], footprint=(292, 429, 456), fit="uniform")
slot("trunk_proxy", [], footprint=(130, 130, 900), shape="round", hidden=True)
slot("fallen_log", [kit("SM_Arena_FallenLog")], essential=True)
slot("root_plate", [kit("SM_Arena_RootPlate")], essential=True)
slot("moss_boulder", [kit("SM_Arena_MossBoulder")], essential=True)
slot("stump", [prop("tree_stump_02_1k")], footprint=(152, 139, 52))
slot("root_bank", [prop("root_cluster_01_1k")], footprint=(412, 268, 151))
slot("forest_shrub", [town_prop("shrub_02", "shrub_02_c")], footprint=(176, 228, 132), fit="uniform")
slot("sapling", [prop("shrub_03_a")], footprint=(20, 15, 40), fit="uniform", shadow=False)
slot("grass_tuft", [prop("grass_medium_02_c")], footprint=(26, 25, 23), fit="uniform", shadow=False, cull=7000)
slot("forest_log", [prop("dead_tree_trunk_1k")], footprint=(610, 56, 58), fit="uniform")
slot("forest_rock", [town_prop("rock_moss_set_02", "rock_moss_set_02_rock10")], footprint=(220, 156, 76), fit="uniform")

# ---- The Drowned Sanctum (underwater)
slot("column", [kit("SM_Arena_Column")], shape="round", essential=True)
slot("column_broken", [kit("SM_Arena_ColumnBroken")], shape="round", essential=True)
slot("column_fallen", [kit("SM_Arena_ColumnFallen")])
slot("ruin_arch", [kit("SM_Arena_RuinArch")], essential=True)
slot("pier_proxy", [], footprint=(140, 150, 460), hidden=True)
slot("sunken_wall", [kit("SM_Arena_RuinWall")], materials={"0": M("MI_ArenaB_RuinBlock"), "1": M("MI_ArenaB_RuinBlock")})
slot("plinth", [kit("SM_Arena_Plinth")])
slot("statue", [town_prop("gothic_statue", "gothic_statue_1k")], footprint=(148, 156, 174), fit="uniform")
slot("bust", [prop("marble_bust_01_1k")], footprint=(54, 60, 102), fit="uniform")
slot("lion_head", [prop("lion_head_1k")], footprint=(64, 42, 88), fit="uniform")
slot("whale_statue", [prop("bronze_whale_statue_1k")], footprint=(186, 274, 214), fit="uniform")
slot("sea_rock", [kit("SM_Arena_SeaRock")])
slot("kelp", [kit("SM_Arena_Kelp")], footprint=(102, 70, 916), fit="uniform", wpo=True, wpo_distance=6000)
slot("coral", [kit("SM_Arena_CoralBranch")], fit="uniform")
slot("coral_violet", [kit("SM_Arena_CoralBranch")], fit="uniform", materials={"0": M("MI_ArenaF_CoralViolet")})
slot("coral_gold", [kit("SM_Arena_CoralBranch")], fit="uniform", materials={"0": M("MI_ArenaF_CoralGold")})
slot("motes", [kit("SM_Arena_Mote")], footprint=(2, 2, 2), fit="uniform", shadow=False, wpo=True, wpo_distance=8000, cull=6000)
slot("pollen", [kit("SM_Arena_Mote")], footprint=(2, 2, 2), fit="uniform", shadow=False, wpo=True, wpo_distance=6000, cull=5000,
     materials={"0": M("MI_Arena_Pollen")})
slot("light_shaft", [kit("SM_Arena_LightShaft")], shadow=False)
slot("chest", [prop("treasure_chest_bottom") + "|" + prop("treasure_chest_lid")], footprint=(96, 52, 60), fit="uniform", shadow=True)
slot("cannon", [prop("cannon_01_frame")], footprint=(94, 138, 59), fit="uniform")
slot("wreck", ["|".join(prop(n) for n in ("ship_pinnace_hull", "ship_pinnace_deck", "ship_pinnace_aft", "ship_pinnace_details", "ship_pinnace_rigging", "ship_pinnace_sails"))],
     footprint=(953, 3979, 2747), fit="uniform")
slot("shell", [prop("lambis_shell_1k")], footprint=(28, 14, 10), fit="uniform", shadow=False)

# ---- Star Station Hangar
slot("container", [kit("SM_Arena_Container")], essential=True)
slot("container_rust", [kit("SM_Arena_Container")], materials={"0": M("MI_Arena_ContainerRust")})
slot("cargo_crate", [kit("SM_Arena_CargoCrate")], essential=True)
slot("pylon", [kit("SM_Arena_Pillar")], essential=True)
slot("deck_barrier", [kit("SM_Arena_Barrier")])
slot("bulkhead", [kit("SM_Arena_Bulkhead")])
slot("dropship", [kit("SM_Arena_Dropship")], essential=True)
slot("station_tower", [kit("SM_Arena_StationTower")])
slot("generator", [prop("portable_generator")], footprint=(164, 112, 116), fit="uniform")
slot("tool_chest", [prop("metal_tool_chest_chest") + "|" + prop("metal_tool_chest_lid")], footprint=(69, 32, 70), fit="uniform")
slot("gas_bottle", [prop("propane_tank_1k")], footprint=(34, 34, 55), fit="uniform")
slot("cargo_cart", [prop("industrial_storage_cart_1k")], footprint=(160, 110, 138), fit="uniform")
slot("gantry", [prop("overhead_crane") + "|" + prop("overhead_crane_winch")], footprint=(1222, 400, 454), fit="uniform")
slot("jerrycan", [prop("metal_jerrycan_1k")], footprint=(35, 17, 46), fit="uniform")


# ============================================================ arena builder
class Arena:
    def __init__(self, aid, name, theme, subtitle, half=(2600, 2000)):
        self.d = {"id": aid, "name": name, "theme": theme, "subtitle": subtitle, "enabled": True, "weight": 1.0,
                  "halfExtents": list(half), "spawns": {}, "pieces": [], "scatter": []}
        self.half = half
        ember = [[-half[0] + 350, y] for y in (-600, -300, 0, 300, 600)]
        self.d["spawns"] = {"ember": ember, "dusk": [[-x, y] for x, y in ember]}

    def add(self, s, x, y, z=0.0, yaw=0.0, scale=1.0, blocker=False):
        assert s in SLOTS, s
        sc = scale if isinstance(scale, (list, tuple)) else (scale, scale, scale)
        self.d["pieces"].append([s, round(x, 1), round(y, 1), round(z, 1), round(yaw, 2), sc[0], sc[1], sc[2], 1 if blocker else 0])

    def pair(self, s, x, y, z=0.0, yaw=0.0, scale=1.0, blocker=False):
        """Piece plus its mirror twin across the centre line (x -> -x, yaw -> 180 - yaw)."""
        self.add(s, x, y, z, yaw, scale, blocker)
        self.add(s, -x, y, z, 180.0 - yaw, scale, blocker)

    def scatter(self, s, region, count, seed, scale=(1, 1), outside=False, margin=0, clearance=0, exclude=(), z=0, z_jitter=0, random_yaw=True, tilt=False):
        self.d["scatter"].append({"slot": s, "region": list(region), "count": count, "seed": seed, "scale": list(scale), "outside": outside,
                                  "margin": margin, "clearance": clearance, "exclude": [list(e) for e in exclude], "z": z, "zJitter": z_jitter,
                                  "randomYaw": random_yaw, "tilt": tilt})

    def set(self, **kw):
        self.d.update(kw)
        return self


ARENAS = []

# ------------------------------------------------------------ 1. THE SUNLIT FIELDS
a = Arena("sunlit_fields", "The Sunlit Fields", "Golden wheat field at harvest, golden hour",
          "A harvest field at golden hour. Bales, stooks and the old wagon break the sightlines.")
H = a.half
# Line-of-sight blockers (all mirrored)
a.pair("hay_stack", -1500, 850, blocker=True)
a.pair("hay_stack", -1500, -850, blocker=True)
a.add("hay_pyramid", 0, 460, yaw=90, blocker=True)
a.add("hay_pyramid", 0, -460, yaw=90, blocker=True)
a.add("hay_wagon", 0, 1330, yaw=90, blocker=True)
a.add("well", 0, -1340, blocker=True)
a.pair("ruin_wall", -800, 1620, yaw=90, blocker=True)
a.pair("hay_stack", -820, -1620, yaw=90, blocker=True)
# Cover (below eye height)
a.pair("hay_round", -1130, 380, yaw=12, blocker=True)
a.pair("hay_round", -1150, -400, yaw=-8, blocker=True)
a.pair("hay_round", -470, 1020, yaw=70, blocker=True)
a.pair("hay_round", -500, -980, yaw=-65, blocker=True)
a.pair("stook", -720, 640, yaw=10, blocker=True)
a.pair("stook", -690, -700, yaw=-15, blocker=True)
a.pair("stook", -1950, 1480, yaw=30, blocker=True)
a.pair("stook", -1950, -1480, yaw=-30, blocker=True)
a.pair("stook", -320, 1700, yaw=5, blocker=True)
a.pair("stone_wall", -2050, 1250, blocker=True)
a.pair("stone_wall", -2050, -1250, blocker=True)
# Decoration inside (no collision): scarecrow, tools, sacks, a bucket by the well
a.add("scarecrow", -1180, 1020, yaw=-20)
a.add("scarecrow", 1200, -1040, yaw=160)
a.add("spade", -1080, 470, yaw=40, z=0)
a.add("bucket", 150, -1180, yaw=10)
a.add("hay_sacks", 190, 1620, yaw=90)
a.add("hay_square", -1330, 1060, yaw=15)
a.add("hay_square", 1360, -1040, yaw=-20)
# Pollen and chaff drifting in the low sun
a.scatter("pollen", (-4200, -3600, 4200, 3600), 3200, 15, scale=(1.5, 3.0), z=330, z_jitter=320)
# Field boundary: split-rail fences behind the spawns, a low stone wall on the field side
for y in range(-1850, 1851, 412):
    a.add("fence", -H[0] - 120, y)
    a.add("fence", H[0] + 120, y, yaw=180)
# Farmstead and landmarks outside the fight
MILL = (7800, 3600, 200.0)  # seen from the Ember side over the wheat; the barn is seen from the Dusk side
a.add("windmill", MILL[0], MILL[1], yaw=MILL[2])
hub = (MILL[0] + math.cos(math.radians(MILL[2])) * 440, MILL[1] + math.sin(math.radians(MILL[2])) * 440)
a.add("windmill_sails", hub[0], hub[1], z=1460, yaw=MILL[2])
a.add("barn", -7400, -3800, yaw=20)
a.add("hay_pyramid", -5600, -2900, yaw=20)
a.add("hay_round", -5000, -3300, yaw=70)
a.add("hay_wagon", -6200, -1500, yaw=-30)
a.add("cart", 5200, -3600, yaw=35)
a.add("woodpile", -8600, -2600, yaw=70)
a.add("barrels", -6400, -3000, yaw=0)
for k, (x, y, s) in enumerate(((-8800, 3500, 3.4), (-9400, 1200, 3.0), (-9000, -2600, 3.6), (-9600, -5200, 2.8), (9000, 4200, 3.2),
                               (9600, 1500, 2.9), (9300, -1900, 3.5), (8800, -4500, 3.0), (-3600, 11200, 3.6), (1800, 11800, 3.3),
                               (6200, 10600, 3.8), (-7600, 9200, 3.0), (2500, -12000, 3.5), (-3800, -11200, 3.2), (7800, -10200, 3.6))):
    a.add("oak" if k % 3 else "young_tree", x, y, yaw=k * 47.0, scale=s)
a.add("field_rock", -3400, 2800, yaw=40)
a.add("field_rock", 3600, 2500, yaw=-20, scale=0.8)
a.add("tree_stump", -3900, -2300, yaw=10)
# Standing wheat all around the harvested arena, dense close in, thinner toward the horizon
a.scatter("wheat", (-7000, -6500, 7000, 6500), 21000, 11, scale=(0.85, 1.25), outside=True, margin=90, tilt=True,
          exclude=[(-3100, -2250, 3100, 2250), (-9200, -5400, -4600, -800), (7000, 2800, 8700, 4400)])
a.scatter("wheat", (-14000, -14000, 14000, 14000), 21000, 12, scale=(0.9, 1.3), outside=True, margin=90, tilt=True,
          exclude=[(-7000, -6500, 7000, 6500), (-9200, -5400, -4600, -800), (7000, 2800, 8700, 4400)])
a.add("patch_track", -6900, -3100, z=-1.3, yaw=20, scale=(42, 34, 1))  # the barnyard
a.scatter("wheat", (-26000, -26000, 26000, 26000), 9000, 16, scale=(1.5, 2.2), outside=True, margin=90,
          exclude=[(-14000, -14000, 14000, 14000)])  # sparse, larger clumps out to the horizon
# Stubble inside the harvested fight area and around the fences
a.scatter("stubble", (-H[0], -H[1], H[0], H[1]), 5200, 13, scale=(0.8, 1.4), clearance=40)
a.scatter("stubble", (-3100, -2250, 3100, 2250), 1800, 14, scale=(0.8, 1.4), outside=True)
a.set(ground="ground_stubble", groundSize=56000, ambience="arena_fields", music="MUS_Crusade",
      minimap={"ground": [0.30, 0.22, 0.09], "blocker": [0.86, 0.66, 0.30], "accent": [1.0, 0.82, 0.42]},
      lighting={"sunPitch": -10.0, "sunYaw": 90.0, "sunIntensity": 7.5, "sunColor": [1.0, 0.70, 0.42], "sunSourceAngle": 1.2,
                "lightShafts": True, "shaftBloomScale": 0.3, "shaftThreshold": 3.0, "volumetricScattering": 1.6,
                "skyMaterial": SKY("MI_Sky_Fields"), "skyYaw": 18.0, "skyBrightness": 0.7, "skyTint": [1.05, 0.95, 0.85],
                "skyHaze": [0.78, 0.56, 0.34], "skyHazeStrength": 0.8,
                "skyLightIntensity": 1.8, "skyLightColor": [1.0, 0.88, 0.74],
                "fogDensity": 0.006, "fogFalloff": 0.2, "fogColor": [0.50, 0.36, 0.22], "fogStart": 1500, "fogHeight": -100,
                "volumetricFog": True, "volumetricDistribution": 0.8, "volumetricExtinction": 0.25, "volumetricAlbedo": [1.0, 0.84, 0.62],
                "exposureBias": 0.0, "saturation": 1.1, "contrast": 1.05, "temperature": 5900, "bloom": 0.8, "vignette": 0.4,
                "gain": [1.03, 1.0, 0.95], "shadowTint": [1.03, 0.97, 0.92]})
ARENAS.append(a)

# ------------------------------------------------------------ 2. THE BLACK SHORE (Iceland)
a = Arena("black_shore", "The Black Shore", "Icelandic black-sand beach: basalt columns, moss-covered lava, sea mist",
          "Black sand, basalt columns and mossy lava under a North Atlantic overcast.")
H = a.half
a.pair("basalt_tall", -1450, 820, yaw=15, blocker=True)
a.pair("basalt_tall", -1450, -820, yaw=-20, blocker=True)
a.add("basalt_wall", 0, 1180, blocker=True)
a.add("basalt_wall", 0, -1180, blocker=True)
a.pair("lava_large", -520, 460, yaw=20, blocker=True)
a.pair("lava_large", -540, -470, yaw=-35, blocker=True)
a.pair("basalt_wall", -1000, 1620, yaw=90, blocker=True)
a.pair("basalt_steps", -900, -1560, yaw=10, blocker=True)
a.pair("lava_medium", -1260, 0, yaw=40, blocker=True)
a.pair("lava_medium", -2000, 1350, yaw=-30, blocker=True)
a.pair("lava_medium", -2000, -1350, yaw=60, blocker=True)
a.pair("basalt_steps", -300, 1650, yaw=-25, scale=0.7, blocker=True)
# moss patches and gravel inside (flat)
# The shore: black-sand beach, then the sea to the north; lava fields and sea cliffs to the south
a.add("sea", 0, 24400, z=4, scale=(700, 400, 1))
for k, (x, y, s, r) in enumerate(((-4200, 13000, 1.4, 20), (1500, 17000, 1.9, -35), (7800, 12500, 1.2, 70), (-10500, 16000, 1.6, 5), (12500, 20000, 2.2, 40))):
    a.add("basalt_stack", x, y, z=-60, yaw=r, scale=s)
for k, x in enumerate(range(-15000, 15001, 1950)):  # a black basalt column cliff behind the lava field to the south
    a.add("basalt_cliff", x, -6400 - (k % 3) * 180, z=-20, yaw=-90 + ((k * 7) % 11 - 5), scale=(1.0, 1.0, 0.9 + 0.15 * ((k * 5) % 4)))
for k, x in enumerate(range(-9000, 9001, 1500)):  # the waterline: low lava shelves half in the sea
    a.add("lava_cluster", x + (k % 2) * 400, 4250 + (k % 3) * 160, z=-20, yaw=k * 53.0, scale=(2.2, 1.8, 1.4))
a.pair("lava_ridge", -4200, -2600, yaw=25, scale=1.4)
a.pair("lava_cluster", -3300, 1600, yaw=10, scale=1.6)
a.add("lava_outcrop", -4600, 600, yaw=30, scale=1.5)
a.add("lava_outcrop", 4700, -900, yaw=-40, scale=1.7)
a.add("basalt_tall", -4200, -900, yaw=5, scale=1.6)
a.add("basalt_tall", 4400, 1200, yaw=40, scale=1.8)
a.add("driftwood", 1800, 3200, yaw=20)
a.add("driftwood", -2600, 3500, yaw=-60, scale=0.8)
a.scatter("lava_medium", (-9000, -8000, 9000, 2400), 180, 21, scale=(0.35, 1.2), outside=True, margin=300)
a.scatter("lava_outcrop", (-10000, -9000, 10000, 1800), 60, 22, scale=(0.5, 1.4), outside=True, margin=500)
a.scatter("lava_ridge", (-11000, -5800, 11000, -2600), 26, 27, scale=(0.8, 1.6), outside=True, margin=400)
a.scatter("beach_pebbles", (-8000, 2100, 8000, 4200), 70, 23, scale=(0.6, 1.2), outside=True, margin=60)
a.scatter("lyme_grass", (-9000, 2150, 9000, 3000), 900, 24, scale=(0.8, 1.6), outside=True, margin=80)
a.scatter("lyme_grass", (-9000, -6000, 9000, -2150), 500, 25, scale=(0.7, 1.3), outside=True, margin=80)
a.scatter("beach_pebbles", (-H[0], -H[1], H[0], H[1]), 26, 26, scale=(0.25, 0.5), clearance=80)
a.set(ground="ground_blacksand", groundSize=56000, ambience="arena_iceland", music="MUS_OppressiveGloom",
      minimap={"ground": [0.07, 0.08, 0.09], "blocker": [0.42, 0.48, 0.44], "accent": [0.62, 0.80, 0.58]},
      lighting={"sunPitch": -38.0, "sunYaw": 90.0, "sunIntensity": 4.2, "sunColor": [0.86, 0.92, 1.0], "sunSourceAngle": 6.0,
                "volumetricScattering": 0.6, "skyMaterial": SKY("MI_Sky_Iceland"), "skyYaw": 0.0, "skyBrightness": 0.95,
                "skyLightIntensity": 1.55, "skyLightColor": [0.86, 0.92, 1.0],
                "fogDensity": 0.022, "fogFalloff": 0.12, "fogColor": [0.44, 0.49, 0.54], "fogStart": 1400, "fogHeight": 0,
                "skyHaze": [0.62, 0.66, 0.70], "skyHazeStrength": 0.6,
                "volumetricFog": True, "volumetricDistribution": 0.2, "volumetricExtinction": 0.6, "volumetricAlbedo": [0.9, 0.95, 1.0],
                "exposureBias": 0.45, "saturation": 0.88, "contrast": 1.08, "temperature": 7200, "bloom": 0.6, "vignette": 0.5,
                "gain": [0.97, 1.0, 1.03]})
ARENAS.append(a)

# ------------------------------------------------------------ 3. REDROCK CANYON (Moab)
a = Arena("redrock_canyon", "Redrock Canyon", "Moab desert: red sandstone canyon, natural arch, harsh sun",
          "A red sandstone canyon under a hard desert sun. Fight beneath the arch.")
H = a.half
a.add("arch", 0, 0, yaw=90)
a.add("arch_leg", 0, 440, blocker=True)
a.add("arch_leg", 0, -440, blocker=True)
a.pair("hoodoo", -1350, 950, yaw=20, blocker=True)
a.pair("hoodoo", -1400, -1050, yaw=-40, scale=0.85, blocker=True)
a.pair("sandstone_block", -620, 1420, yaw=15, blocker=True)
a.pair("sandstone_block", -700, -1380, yaw=-25, blocker=True)
a.pair("sandstone_block", -1950, 1400, yaw=40, scale=0.8, blocker=True)
a.pair("sandstone_block", -1950, -1420, yaw=-10, scale=0.8, blocker=True)
a.pair("red_boulder", -1020, 330, yaw=30, blocker=True)
a.pair("red_boulder", -1060, -360, yaw=-50, blocker=True)
a.pair("red_boulder", -350, 1000, yaw=80, scale=0.8, blocker=True)
a.pair("red_boulder", -380, -1000, yaw=10, scale=0.8, blocker=True)
# Canyon walls to the north and south, open ends with distant mesas
for side in (1, -1):
    for k, x in enumerate((-7400, -4500, -1500, 1500, 4500, 7400)):
        a.add("canyon_wall", x, side * (H[1] + 850 + 90 * (k % 2)), yaw=90 * side, scale=(1.0, 1.02, 0.85 + 0.12 * ((k + side) % 3)))
for k, (x, y, r, s) in enumerate(((-12500, 1800, 180, 1.2), (-13500, -3800, 190, 0.9), (12500, -1200, 0, 1.1), (13800, 4200, -10, 0.8))):
    a.add("canyon_wall", x, y, yaw=r, scale=(1.0, 1.4, s))
a.add("hoodoo", -6200, 800, yaw=10, scale=1.4)
a.add("hoodoo", 6400, -900, yaw=70, scale=1.6)
a.add("arch", -9000, -600, yaw=60, scale=1.5)
a.add("dead_log", -1800, 1750, yaw=30, scale=1.2)
a.add("dead_log", 1700, -1760, yaw=200, scale=1.1)
a.scatter("red_boulder_small", (-H[0], -H[1], H[0], H[1]), 36, 31, scale=(0.5, 1.2), clearance=60)
a.scatter("scrub_0", (-9000, -2700, 9000, 2700), 160, 32, scale=(1.0, 1.8), clearance=100)
a.scatter("scrub_1", (-9000, -2700, 9000, 2700), 160, 33, scale=(1.0, 1.8), clearance=100)
a.scatter("scrub_2", (-9000, -2700, 9000, 2700), 140, 34, scale=(1.0, 1.8), clearance=100)
a.scatter("red_boulder", (-12000, -2700, 12000, 2700), 70, 36, scale=(0.4, 1.3), outside=True, margin=250)
a.set(ground="ground_redsoil", groundSize=56000, ambience="arena_moab", music="MUS_FiveArmies",
      minimap={"ground": [0.30, 0.13, 0.07], "blocker": [0.82, 0.42, 0.24], "accent": [1.0, 0.62, 0.35]},
      lighting={"sunPitch": -52.0, "sunYaw": 90.0, "sunIntensity": 11.0, "sunColor": [1.0, 0.93, 0.82], "sunSourceAngle": 0.55,
                "volumetricScattering": 0.8, "skyMaterial": SKY("MI_Sky_Moab"), "skyYaw": 0.0, "skyBrightness": 1.0,
                "skyLightIntensity": 1.0, "skyLightColor": [0.95, 0.92, 1.0],
                "fogDensity": 0.006, "fogFalloff": 0.3, "fogColor": [0.78, 0.55, 0.42], "fogStart": 1500, "fogHeight": -200,
                "volumetricFog": False, "exposureBias": 0.0, "saturation": 1.06, "contrast": 1.12, "temperature": 6100, "bloom": 0.7,
                "vignette": 0.4, "gain": [1.03, 1.0, 0.96]})
ARENAS.append(a)

# ------------------------------------------------------------ 4. HORNBEAM GLADE
a = Arena("hornbeam_glade", "Hornbeam Glade", "European hornbeam forest clearing, dappled light",
          "A clearing in the hornbeam wood. Fallen trunks and old roots hide the flanks.")
H = a.half
for (x, y, s, r) in ((-1350, 1250, 3.9, 20), (-1300, -1300, 4.2, -35)):
    a.pair("glade_tree", x, y, yaw=r, scale=s)
    a.pair("trunk_proxy", x, y, blocker=True)
a.pair("moss_boulder", -560, 480, yaw=25, blocker=True)
a.pair("moss_boulder", -600, -500, yaw=-40, scale=0.9, blocker=True)
a.add("root_plate", 0, 1260, yaw=90, blocker=True)
a.add("root_plate", 0, -1260, yaw=-90, blocker=True)
a.pair("fallen_log", -760, 1180, yaw=62, blocker=True)
a.pair("fallen_log", -800, -1200, yaw=-58, blocker=True)
a.pair("fallen_log", -1650, 20, yaw=0, scale=(1.0, 0.75, 1.0), blocker=True)
a.pair("stump", -1000, 650, yaw=20, blocker=True)
a.pair("stump", -1030, -700, yaw=70, blocker=True)
a.pair("moss_boulder", -2050, 1450, yaw=60, scale=0.55, blocker=True)
a.pair("moss_boulder", -2050, -1450, yaw=-10, scale=0.55, blocker=True)
a.add("forest_log", 250, 1500, yaw=100)
a.add("forest_log", -260, -1550, yaw=80)
a.add("root_bank", 0, 1650, yaw=0)
# The wood: a dense ring of trees, undergrowth and dead wood
a.scatter("glade_tree", (-10000, -10000, 10000, 10000), 240, 41, scale=(3.4, 5.0), outside=True, margin=900)
a.scatter("glade_tree_young", (-8000, -8000, 8000, 8000), 160, 42, scale=(2.6, 3.8), outside=True, margin=450)
a.scatter("forest_shrub", (-7000, -7000, 7000, 7000), 260, 43, scale=(0.8, 1.6), outside=True, margin=120)
a.scatter("forest_shrub", (-6000, -6000, 6000, 6000), 400, 44, scale=(0.5, 1.1), outside=True, margin=40)
a.scatter("grass_tuft", (-H[0], -H[1], H[0], H[1]), 900, 46, scale=(1.5, 3.0), clearance=80)
a.scatter("sapling", (-7000, -7000, 7000, 7000), 400, 47, scale=(2.0, 4.0), outside=True, margin=60)
a.scatter("forest_log", (-8000, -8000, 8000, 8000), 40, 48, scale=(0.7, 1.3), outside=True, margin=250)
a.scatter("forest_rock", (-8000, -8000, 8000, 8000), 60, 49, scale=(0.6, 1.5), outside=True, margin=200)
a.set(ground="ground_forest", groundSize=56000, ambience="arena_hornbeam", music="MUS_DeathandAxes",
      minimap={"ground": [0.09, 0.13, 0.06], "blocker": [0.40, 0.48, 0.30], "accent": [0.62, 0.86, 0.42]},
      lighting={"sunPitch": -40.0, "sunYaw": 90.0, "sunIntensity": 7.0, "sunColor": [1.0, 0.94, 0.80], "sunSourceAngle": 0.8,
                "lightShafts": True, "shaftBloomScale": 0.2, "shaftThreshold": 6.0, "volumetricScattering": 1.8,
                "skyMaterial": SKY("MI_Sky_Hornbeam"), "skyYaw": 0.0, "skyBrightness": 0.85, "skyLightIntensity": 1.0,
                "skyLightColor": [0.90, 1.0, 0.86], "fogDensity": 0.014, "fogFalloff": 0.2, "fogColor": [0.36, 0.44, 0.30],
                "fogStart": 400, "volumetricFog": True, "volumetricDistribution": 0.7, "volumetricExtinction": 0.45,
                "volumetricAlbedo": [0.85, 1.0, 0.8], "exposureBias": 0.55, "saturation": 1.05, "contrast": 1.05, "temperature": 6300,
                "bloom": 0.8, "vignette": 0.5, "lightFunction": M("MI_Arena_Dapple"), "lightFunctionScale": 900.0})
ARENAS.append(a)

# ------------------------------------------------------------ 5. THE DROWNED SANCTUM (underwater)
a = Arena("drowned_sanctum", "The Drowned Sanctum", "A sunken temple on the seabed: caustics, kelp and drifting motes",
          "A temple lost beneath the sea. Columns and arches break the sightlines; kelp sways overhead.")
H = a.half
a.add("ruin_arch", 0, 0, yaw=90)
a.add("pier_proxy", 0, 300, blocker=True)
a.add("pier_proxy", 0, -300, blocker=True)
a.pair("column", -1300, 900, blocker=True)
a.pair("column", -1300, -900, blocker=True)
a.pair("column", -620, 1520, blocker=True)
a.pair("column", -620, -1520, blocker=True)
a.pair("column_broken", -880, 420, blocker=True)
a.pair("column_broken", -880, -420, blocker=True)
a.pair("sunken_wall", -1650, 1580, yaw=90, blocker=True)
a.pair("sunken_wall", -1650, -1580, yaw=90, blocker=True)
a.pair("plinth", -1500, 0, blocker=True)
a.pair("statue", -1500, 0, z=180, yaw=0)
a.pair("sea_rock", -380, 1150, yaw=30, scale=0.85, blocker=True)
a.pair("sea_rock", -400, -1160, yaw=-50, scale=0.85, blocker=True)
a.pair("column_fallen", -950, 1150, yaw=35, blocker=True)
a.pair("column_fallen", -980, -1180, yaw=-30, blocker=True)
a.add("bust", -240, 700, yaw=40)
a.add("lion_head", 260, -720, yaw=200)
a.add("whale_statue", 0, 1650, yaw=0)
a.add("chest", 1800, 1700, yaw=-30)
a.add("cannon", -1900, -1780, yaw=70)
a.add("wreck", 3800, 7600, z=-420, yaw=25, scale=1.0)
a.add("wreck", -9000, -9500, z=-600, yaw=-70, scale=0.8)
for k, (x, y) in enumerate(((-3600, 3400), (4200, -3800), (-5200, -2600), (5600, 2200), (0, -5200), (-1500, 6000))):
    a.add("column" if k % 2 else "column_broken", x, y, yaw=k * 40.0, scale=1.3)
a.add("sunken_wall", -4200, 0, yaw=10, scale=1.4)
a.add("sunken_wall", 4400, 400, yaw=-15, scale=1.5)
for k, (x, y) in enumerate(((-700, -1700), (800, 1720), (-2100, 700), (2150, -650), (-150, 0), (1300, 300), (-1250, -300))):
    a.add("light_shaft", x, y, z=40, yaw=k * 30.0, scale=(1.0, 1.0, 1.0))
a.scatter("kelp", (-9000, -9000, 9000, 9000), 520, 51, scale=(0.6, 1.2), outside=True, margin=150)
a.scatter("kelp", (-H[0], -H[1], H[0], H[1]), 26, 52, scale=(0.35, 0.6), clearance=250, exclude=[(-2300, -800, 2300, 800)])
a.scatter("coral", (-7000, -7000, 7000, 7000), 260, 53, scale=(0.8, 1.8), clearance=120)
a.scatter("coral_violet", (-7000, -7000, 7000, 7000), 200, 54, scale=(0.8, 1.6), clearance=120)
a.scatter("coral_gold", (-7000, -7000, 7000, 7000), 160, 55, scale=(0.8, 1.6), clearance=120)
a.scatter("sea_rock", (-10000, -10000, 10000, 10000), 90, 56, scale=(0.4, 1.4), outside=True, margin=400)
a.scatter("shell", (-H[0], -H[1], H[0], H[1]), 60, 57, scale=(1.0, 2.0), clearance=60)
a.scatter("motes", (-5000, -5000, 5000, 5000), 6000, 58, scale=(1.2, 2.6), z=450, z_jitter=440)
a.set(ground="ground_seabed", groundSize=56000, ambience="arena_underwater", music="MUS_BlackVortex",
      minimap={"ground": [0.03, 0.10, 0.12], "blocker": [0.40, 0.58, 0.56], "accent": [0.45, 0.92, 0.90]},
      lighting={"sunPitch": -72.0, "sunYaw": 90.0, "sunIntensity": 5.5, "sunColor": [0.62, 0.92, 0.95], "sunSourceAngle": 2.0,
                "lightShafts": False, "volumetricScattering": 2.5, "skyMaterial": SKY("MI_Sky_Underwater"), "skyBrightness": 1.0,
                "skyLightIntensity": 1.1, "skyLightColor": [0.55, 0.85, 0.9], "fogDensity": 0.05, "fogFalloff": 0.02,
                "fogColor": [0.04, 0.22, 0.26], "fogStart": 700, "fogHeight": 0, "fogMaxOpacity": 0.97, "volumetricFog": True,
                "volumetricDistribution": 0.6, "volumetricExtinction": 1.2, "volumetricAlbedo": [0.55, 0.9, 0.95],
                "exposureBias": 0.6, "saturation": 0.95, "contrast": 1.06, "temperature": 7600, "bloom": 1.1, "vignette": 0.6,
                "gain": [0.88, 1.0, 1.02], "lightFunction": M("MI_Arena_Caustics"), "lightFunctionScale": 700.0,
                "lights": [{"at": [0, 0, 700], "color": [0.4, 0.9, 1.0], "intensity": 30000, "radius": 2600}]})
ARENAS.append(a)

# ------------------------------------------------------------ 6. STAR STATION HANGAR
a = Arena("star_station", "Star Station Hangar", "An orbital hangar deck above a blue planet",
          "Hangar Bay 7, high above a blue world. Cargo and dropships are your cover.")
H = a.half
a.add("container", 0, 660, blocker=True)
a.add("container", 0, -660, blocker=True)
a.add("container_rust", 0, 660, z=260, yaw=180)
a.pair("container", -950, 1520, yaw=90, blocker=True)
a.pair("container_rust", -950, -1520, yaw=90, blocker=True)
a.pair("dropship", -1500, 420, yaw=90, scale=0.62, blocker=True)
a.pair("proxy_box", -1500, -700, scale=(2.5, 2.5, 3.0), blocker=True)
a.pair("cargo_crate", -1540, -660, yaw=5)
a.pair("cargo_crate", -1460, -740, z=150, yaw=20)
a.pair("cargo_crate", -620, 170 + 800, yaw=10, blocker=True)
a.pair("cargo_crate", -680, -1000, yaw=-15, blocker=True)
a.pair("deck_barrier", -1000, -300, yaw=0, blocker=True)
a.pair("deck_barrier", -2050, 1350, yaw=90, blocker=True)
a.pair("deck_barrier", -2050, -1350, yaw=90, blocker=True)
a.pair("pylon", -400, 1880, blocker=True)
a.pair("pylon", -400, -1880, blocker=True)
a.add("generator", -1200, 1250, yaw=30)
a.add("tool_chest", 1250, -1250, yaw=200)
a.add("gas_bottle", -1100, 1300, yaw=0)
a.add("gas_bottle", 1150, -1320, yaw=0)
a.add("cargo_cart", 1300, 1250, yaw=-40)
a.add("jerrycan", -1250, -1150, yaw=60)
# landing pad markings and deck light strips
for x in range(-2600, 2601, 650):
    a.add("glow_strip", x, 0, z=0.3, scale=(2.2, 0.06, 1))
for k in range(24):  # landing ring around the centre containers
    ang = 2 * math.pi * (k + 0.5) / 24
    a.add("glow_amber", math.cos(ang) * 1150, math.sin(ang) * 1150, z=0.35, yaw=math.degrees(ang) + 90, scale=(2.6, 0.08, 1))
for y in (-1990, 1990):
    a.add("glow_strip", 0, y, z=0.3, scale=(52, 0.08, 1))
for x in (-2590, 2590):
    a.add("glow_amber", x, 0, z=0.3, scale=(0.08, 40, 1))
a.add("patch_hangar", -2200, 0, z=-0.5, scale=(9, 40, 1))
a.add("patch_hangar", 2200, 0, z=-0.5, scale=(9, 40, 1))
# The hangar: bulkheads behind the spawns, open sides onto space, gantry overhead, towers on the skyline
for y in (-2600, -1600, -600, 400, 1400, 2400):
    a.add("bulkhead", -H[0] - 380, y + 100, yaw=0)
    a.add("bulkhead", H[0] + 380, y + 100, yaw=180)
for x in range(-2400, 2401, 300):
    a.add("deck_barrier", x, H[1] + 150, yaw=90)
    a.add("deck_barrier", x, -H[1] - 150, yaw=90)
a.add("gantry", 0, 0, z=1250, yaw=0, scale=(4.6, 1.0, 1.0))
for k, (x, y, s, r) in enumerate(((-9000, 14000, 1.2, 0), (7000, 17000, 1.5, 30), (15000, 6000, 1.0, 60), (-16000, -3000, 1.3, 10), (4000, -16000, 1.6, 45))):
    a.add("station_tower", x, y, z=-3000, yaw=r, scale=s)
a.pair("pylon", -2400, 2350)
a.pair("pylon", -2400, -2350)
a.set(ground="ground_deck", groundSize=7400, ambience="arena_station", music="MUS_Killers",
      minimap={"ground": [0.06, 0.08, 0.11], "blocker": [0.35, 0.55, 0.75], "accent": [0.35, 0.85, 1.0]},
      lighting={"sunPitch": -34.0, "sunYaw": 90.0, "sunIntensity": 8.5, "sunColor": [1.0, 0.96, 0.9], "sunSourceAngle": 0.4,
                "volumetricScattering": 0.5, "skyMaterial": SKY("MI_Sky_Space"), "skyBrightness": 1.0, "skyLightIntensity": 0.5,
                "skyLightColor": [0.6, 0.72, 1.0], "fogDensity": 0.0, "fogFalloff": 0.2, "fogColor": [0.0, 0.0, 0.0],
                "fogStart": 30000, "volumetricFog": False, "exposureBias": 0.6, "saturation": 1.05, "contrast": 1.1, "temperature": 7000,
                "bloom": 1.2, "vignette": 0.5,
                "lights": [{"at": [0, 0, 900], "color": [0.55, 0.8, 1.0], "intensity": 60000, "radius": 3200},
                           {"at": [-1800, 0, 500], "color": [1.0, 0.7, 0.4], "intensity": 25000, "radius": 1800},
                           {"at": [1800, 0, 500], "color": [1.0, 0.7, 0.4], "intensity": 25000, "radius": 1800}]})
ARENAS.append(a)


# ============================================================ validation (mirrors CireArenas::Validate)
# ------------------------------------------------------------ world-dressing: light life in two arenas
# Decoration only (never blockers): collision proxies, symmetry and sight lines are unchanged. Assets are the CC0 /
# original world-dressing kit (Tools/ImportWorldDressing.py, /Game/Free/Dressing).
DRESS = "/Game/Free/Dressing"
slot("dress_crow_flock", [f"{DRESS}/Meshes/SM_Dress_CrowFlock.SM_Dress_CrowFlock"], footprint=(1511, 1210, 202), fit="none",
     shadow=True, wpo=True, wpo_distance=12000)
slot("dress_crow", [f"{DRESS}/Meshes/SM_Dress_CrowPerched.SM_Dress_CrowPerched"], footprint=(61, 14, 24), fit="none", cull=5000)
slot("dress_litter", [f"{DRESS}/Meshes/SM_Dress_LeafLitter.SM_Dress_LeafLitter"], footprint=(260, 260, 2), fit="none", shadow=False, cull=7000)
slot("dress_branches", [f"{DRESS}/Props/dry_branches_medium_01/dry_branches_medium_01_1k/StaticMeshes/dry_branches_medium_01_a.dry_branches_medium_01_a"],
     footprint=(32, 130, 34), fit="uniform", shadow=False, cull=6000)
slot("dress_haypile", [f"{DRESS}/Meshes/SM_Dress_HayPile.SM_Dress_HayPile"], footprint=(227, 214, 55), fit="none", cull=9000,
     materials=mat_all(M("MI_Arena_Straw")))
_by_id = {x.d["id"]: x for x in ARENAS}
if "sunlit_fields" in _by_id:
    a = _by_id["sunlit_fields"]
    a.add("dress_crow_flock", 0, 0, z=1900, yaw=30)                 # crows wheel over the centre bales
    a.add("dress_crow_flock", 5200, 2600, z=2300, yaw=200, scale=1.3)  # and over the windmill side
    a.pair("dress_crow", -1180, 1020, z=176, yaw=70)               # on the scarecrows' arms (mirrored twin)
    a.pair("dress_haypile", -2350, 300, yaw=40)                    # loose straw behind the spawns
    a.pair("dress_haypile", -2380, -420, yaw=-20, scale=0.8)
if "hornbeam_glade" in _by_id:
    a = _by_id["hornbeam_glade"]
    H = a.half
    a.scatter("dress_litter", (-H[0], -H[1], H[0], H[1]), 26, 81, scale=(0.8, 1.4), clearance=40)
    a.scatter("dress_litter", (-7000, -7000, 7000, 7000), 120, 82, scale=(1.0, 2.0), outside=True, margin=40)
    a.scatter("dress_branches", (-H[0], -H[1], H[0], H[1]), 40, 83, scale=(0.8, 1.6), clearance=60)
    a.pair("dress_crow", -760, 1180, z=62, yaw=150)                # on the fallen logs
    a.add("dress_crow_flock", 0, 0, z=2400, yaw=120, scale=0.8)


def footprints(arena):
    out = []
    for s, x, y, z, yaw, sx, sy, sz, blk in arena["pieces"]:
        if not blk:
            continue
        sl = SLOTS[s]
        fx, fy, fz = sl["footprint"][0] * sx, sl["footprint"][1] * sy, sl["footprint"][2] * sz
        rnd = sl["shape"] == "round"
        ext = (max(fx, fy) / 2,) * 2 if rnd else (fx / 2, fy / 2)
        out.append({"c": (x, y), "e": ext, "yaw": yaw, "round": rnd, "h": fz + z})
    return out


def dist(f, p):
    dx, dy = p[0] - f["c"][0], p[1] - f["c"][1]
    r = math.radians(-f["yaw"])
    lx, ly = dx * math.cos(r) - dy * math.sin(r), dx * math.sin(r) + dy * math.cos(r)
    if f["round"]:
        return max(0.0, math.hypot(lx, ly) - f["e"][0])
    return math.hypot(max(0.0, abs(lx) - f["e"][0]), max(0.0, abs(ly) - f["e"][1]))


def validate(arena):
    errs = []
    for key in ("ground", "underlay"):
        if arena.get(key) and arena[key] not in SLOTS:
            errs.append(f"unknown {key} slot {arena[key]}")
    hx, hy = arena["halfExtents"]
    fps = footprints(arena)
    for team in ("ember", "dusk"):
        for i, s in enumerate(arena["spawns"][team]):
            for f in fps:
                if dist(f, s) < 150:
                    errs.append(f"{team} spawn {i} crowded by blocker at {f['c']}")
    for f in fps:
        if abs(f["c"][0]) < 1 and (f["round"] or f["yaw"] % 90 == 0):
            continue
        want = (-f["yaw"]) % 180
        ok = any(g["round"] == f["round"] and abs(g["c"][0] + f["c"][0]) < 5 and abs(g["c"][1] - f["c"][1]) < 5 and abs(g["h"] - f["h"]) < 2 and
                 (f["round"] or (min(abs(want - g["yaw"] % 180), 180 - abs(want - g["yaw"] % 180)) < 1 and abs(g["e"][0] - f["e"][0]) < 2 and abs(g["e"][1] - f["e"][1]) < 2))
                 for g in fps)
        if not ok:
            errs.append(f"blocker at {f['c']} has no mirror twin")
    cell, rad = 25.0, 50.0
    w, h = int(math.ceil(2 * hx / cell)), int(math.ceil(2 * hy / cell))
    free = [[False] * w for _ in range(h)]
    count = 0
    near = lambda cx, cy: [f for f in fps if abs(f["c"][0] - cx) < max(f["e"]) * 1.5 + 200 and abs(f["c"][1] - cy) < max(f["e"]) * 1.5 + 200]
    for j in range(h):
        for i in range(w):
            c = (-hx + (i + .5) * cell, -hy + (j + .5) * cell)
            ok = abs(c[0]) <= hx - rad and abs(c[1]) <= hy - rad and all(dist(f, c) >= rad for f in near(*c))
            free[j][i] = ok
            count += ok
    def cellof(p):
        return (min(w - 1, max(0, int((p[0] + hx) // cell))), min(h - 1, max(0, int((p[1] + hy) // cell))))
    start = cellof(arena["spawns"]["ember"][0])
    seen = {start} if free[start[1]][start[0]] else set()
    queue = list(seen)
    while queue:
        i, j = queue.pop()
        for a2, b2 in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            if 0 <= a2 < w and 0 <= b2 < h and free[b2][a2] and (a2, b2) not in seen:
                seen.add((a2, b2))
                queue.append((a2, b2))
    for team in ("ember", "dusk"):
        for i, s in enumerate(arena["spawns"][team]):
            if cellof(s) not in seen:
                errs.append(f"{team} spawn {i} unreachable")
    frac = len(seen) / max(1, count)
    if frac < 0.9:
        errs.append(f"only {frac:.0%} reachable")
    # the network probe rolls from (-650, 0) toward +X: keep that lane open
    for f in fps:
        for x in range(-700, -149, 25):
            if dist(f, (x, 0)) < 46:
                errs.append(f"probe lane blocked near x={x} by {f['c']}")
                break
    tall = sum(1 for f in fps if f["h"] >= 190)
    if tall < 6:
        errs.append(f"only {tall} tall blockers")
    return errs, frac, tall


def main() -> int:
    bad = False
    for x in ARENAS:
        errs, frac, tall = validate(x.d)
        print(f"{x.d['id']}: blockers={len(footprints(x.d))} tall={tall} reachable={frac:.1%} errors={len(errs)}")
        for e in errs:
            print("   ", e)
        bad |= bool(errs)
    if bad and "--force" not in sys.argv:
        print("CIRE_ARENA_AUTHOR_FAIL validation errors (use --force to write anyway)")
        return 1
    doc = {"schemaVersion": 1,
           "notes": "Randomised PvP arena pool (CireArenas, Docs/Arenas.md). Written by Tools/AuthorArenas.py - edit that script, not this file. "
                    "All arenas share the footprint at 'origin'; arena-local X runs from Ember (west) to Dusk (east). Piece rows: "
                    "[slot, x, y, z, yaw, scaleX, scaleY, scaleZ, blocker]. Blockers get an invisible collision proxy matching the slot footprint "
                    "and must be mirror-symmetric; slots list candidates in priority order (Fab packs, then CC0/original, then an engine shape).",
           "origin": [0, 60000, 0], "slots": SLOTS, "arenas": [x.d for x in ARENAS]}
    OUT.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    total = sum(len(x.d["pieces"]) for x in ARENAS)
    print(f"CIRE_ARENA_AUTHOR_PASS arenas={len(ARENAS)} slots={len(SLOTS)} pieces={total} bytes={OUT.stat().st_size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
