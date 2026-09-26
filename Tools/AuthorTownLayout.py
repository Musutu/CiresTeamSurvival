"""Author Content/Data/TownAssetSlots.json and Content/Data/TownLayout.json.

The medieval town both teams defend (identical realm-local layout in each private realm):

  breach field -> town gatehouse -> gate road -> market -> residential lanes -> town square
  -> castle approach -> castle gatehouse (the defended leak zone) -> inner bailey -> keep

Coordinates are realm-local centimetres: X along the lane (castle at -1850, breach at 12500),
Y relative to the team's realm centre. Pure Python; validates every placement against the
authored route, challenge bays, spawn and realm divider with the same rules the runtime uses
and prints anything it had to drop, so the data never relies on runtime suppression.
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content/Data"
MESHES = json.loads((ROOT / "Art/Environment/Town/Meshes/TownMeshes.json").read_text())["meshes"]
IMPORT = json.loads((ROOT / "Art/Environment/Town/ImportReport.json").read_text())
ROUTES = json.loads((DATA / "BattlefieldRoutes.json").read_text())
# world-scale (September 25): the realm is three times as long. The castle end (goal, hero base, bailey) and the
# original market / lanes / square keep their coordinates; the old gate road, town wall and breach move out by
# GATE_SHIFT and seven new districts fill the gap. This script now owns the route and writes BattlefieldRoutes.json
# (CireLanePath::TownDefaults must match it: the native checks compare the two).
GATE_SHIFT = 30700
ROUTE = [(12500 + GATE_SHIFT, 0), (10800 + GATE_SHIFT, 0), (10100 + GATE_SHIFT, -550),   # breach -> outer gate -> gate road
         (39300, -550), (38000, 450), (36500, 700), (35200, -250),                       # Brookfield hamlet
         (33600, -700), (32000, -700), (30600, 300),                                      # the outer farmsteads
         (29200, 650), (27700, 650), (26300, -450), (25000, -450),                         # tanners' yard
         (24300, 0), (22700, 0),                                                           # through the old wall
         (21300, -600), (19700, -600), (18300, 550),                                       # weavers' lanes
         (16900, 550), (15500, -250), (14000, -250),                                       # the temple green
         (12900, 500), (11400, 500), (10000, -450),                                        # guildhall row
         (8700, -550), (7400, 500), (5800, 500), (4700, -550), (3300, -550), (2400, 500), (1000, 500), (0, 0), (-1850, 0)]
ROUTES["bounds"]["maxX"] = 13000 + GATE_SHIFT
for _lane in ROUTES["lanes"]:
    _lane["points"] = [list(p) for p in ROUTE]
HW = ROUTES["bounds"]["halfWidth"]
MAXX = ROUTES["bounds"]["maxX"]
# nav-paths: the route margin follows the editable lane width (half the road + 70 cm; 330 at the default 520).
ROUTE_MARGIN, BAY_MARGIN, SPAWN_MARGIN = ROUTES.get("laneWidth", 520) / 2 + 70, 450, 420
INNER_LIMIT = 2100 - 60  # local |y| that stays clear of the realm divider

G = "/Game/Environment/Town/Meshes/"
LEGACY = "/Game/Art/Environment/Props01/"


def gen(name):
    return f"{G}{name}.{name}"


def ph(asset_id, mesh):
    for m in IMPORT["props"][asset_id]["meshes"]:
        if m["asset"].split(".")[-1] == mesh:
            return m["asset"]
    raise KeyError(f"{asset_id}/{mesh}")


def ph_all(asset_id):
    return [m["asset"] for m in IMPORT["props"][asset_id]["meshes"]]


def ph_extent(asset_id, mesh=None):
    ms = IMPORT["props"][asset_id]["meshes"]
    m = next(x for x in ms if mesh is None or x["asset"].endswith("." + mesh))
    return m["extent"]


BOUNDS = {m["name"]: (m["boundsMin"], m["boundsMax"]) for m in MESHES}

# ------------------------------------------------------------------ slots
SLOTS = {}


def mesh_slot(sid, mesh, fallback=None, footprint=None, collision=True, clearance="route", light=None, shadow=True, parts=None,
              fit=None, note=None, fallback_parts=None, cull=None):
    # "fallback" keeps the legacy/engine mesh as the last resort; overlays add higher-priority candidates.
    s = {"mesh": mesh}
    if cull:
        s["cullDistance"] = cull  # world-scale: HISM cull distance [start, end] cm
    if parts:
        s["parts"] = parts
    if fit:
        s["fit"] = fit
    if fallback:
        s["fallback"] = fallback
    if fallback_parts:
        s["fallback_parts"] = fallback_parts
    if footprint:
        s["footprint"] = footprint
    if not collision:
        s["collision"] = False
    if not shadow:
        s["castShadow"] = False
    if clearance != "route":
        s["clearance"] = clearance
    if light:
        s["light"] = light
    if note:
        s["note"] = note
    SLOTS[sid] = s


def gen_footprint(name):
    lo, hi = BOUNDS[name]
    return [round(hi[0] - lo[0]), round(hi[1] - lo[1]), round(hi[2] - lo[2])]


WARM = [1.0, 0.56, 0.24]
LAMP_LIGHT = {"offset": [70, 0, 262], "color": WARM, "intensity": 5200, "radius": 950}

# Landmarks (fit-to-footprint lets Tripo/Fab replacements of any scale drop in).
mesh_slot("gatehouse", gen("SM_Town_Gatehouse"), footprint=gen_footprint("SM_Town_Gatehouse"), clearance="none",
          note="Town gate over the march road. Overlay meshes are used only with \"passage\": true: an 8 m wide, 5 m high open passage along local X at Y=0.")
mesh_slot("castle_gate", gen("SM_Castle_Gatehouse"), footprint=gen_footprint("SM_Castle_Gatehouse"), clearance="none",
          note="Castle gatehouse = defended leak zone entrance. Overlay meshes are used only with \"passage\": true: a 9 m wide, 5 m high open passage along local X at Y=0.")
SLOTS["gatehouse"]["requiresPassage"] = True
SLOTS["castle_gate"]["requiresPassage"] = True
mesh_slot("castle_keep", gen("SM_Castle_Keep"), footprint=gen_footprint("SM_Castle_Keep"), clearance="none")
mesh_slot("castle_wall", gen("SM_Castle_Curtain"), footprint=gen_footprint("SM_Castle_Curtain"), clearance="none")
mesh_slot("castle_tower", gen("SM_Castle_Tower"), footprint=gen_footprint("SM_Castle_Tower"), clearance="bays")
mesh_slot("wall_section", gen("SM_Town_WallSegment"), footprint=gen_footprint("SM_Town_WallSegment"), clearance="none")
mesh_slot("wall_tower", gen("SM_Town_WallTower"), footprint=gen_footprint("SM_Town_WallTower"), clearance="bays")
mesh_slot("watchtower", gen("SM_Town_Watchtower"), footprint=gen_footprint("SM_Town_Watchtower"))
mesh_slot("shrine", gen("SM_Town_Fountain"), fallback=LEGACY + "SM_OathObelisk.SM_OathObelisk", footprint=gen_footprint("SM_Town_Fountain"),
          note="Town-square centrepiece; the Tripo Dawnwell shrine drops in here.")
mesh_slot("chapel", gen("SM_Town_Chapel"), footprint=gen_footprint("SM_Town_Chapel"))
mesh_slot("market_hall", gen("SM_Town_MarketHall"), footprint=gen_footprint("SM_Town_MarketHall"))
# Houses (distinct silhouettes).
for sid, name in (("house_a", "SM_Town_HouseTimberA"), ("house_b", "SM_Town_HouseTimberB"), ("house_c", "SM_Town_HouseTimberC"),
                  ("house_shop", "SM_Town_HouseShop"), ("tavern", "SM_Town_Tavern"), ("house_planks", "SM_Town_HousePlanks"),
                  ("cottage_thatch", "SM_Town_CottageStone"), ("cottage_slate", "SM_Town_CottageStoneSlate"),
                  ("townhouse_row", "SM_Town_TownhouseRow"), ("townhouse_row_b", "SM_Town_TownhouseRowB"), ("warehouse", "SM_Town_Warehouse")):
    mesh_slot(sid, gen(name), footprint=gen_footprint(name))
# Street furniture.
mesh_slot("market_stall_a", gen("SM_Town_MarketStallRed"), footprint=gen_footprint("SM_Town_MarketStallRed"))
mesh_slot("market_stall_b", gen("SM_Town_MarketStallBlue"), footprint=gen_footprint("SM_Town_MarketStallBlue"))
mesh_slot("fountain", gen("SM_Town_Fountain"), footprint=gen_footprint("SM_Town_Fountain"))
mesh_slot("well", gen("SM_Town_Well"), footprint=gen_footprint("SM_Town_Well"))
mesh_slot("cart", gen("SM_Town_Cart"), footprint=gen_footprint("SM_Town_Cart"))
mesh_slot("lamp", gen("SM_Town_LampPost"), fallback=LEGACY + "SM_WatchLantern.SM_WatchLantern", footprint=gen_footprint("SM_Town_LampPost"),
          light=LAMP_LIGHT)
mesh_slot("wall_lantern", gen("SM_Town_WallLantern"), collision=False, clearance="bays",
          light={"offset": [40, 0, -26], "color": WARM, "intensity": 2600, "radius": 650})
mesh_slot("banner", gen("SM_Town_BannerPole"), footprint=gen_footprint("SM_Town_BannerPole"))
mesh_slot("barricade", gen("SM_Town_Barricade"), footprint=gen_footprint("SM_Town_Barricade"))
mesh_slot("fence", gen("SM_Town_Fence"), clearance="route")
mesh_slot("low_wall", gen("SM_Town_LowWall"))
mesh_slot("woodpile", gen("SM_Town_Woodpile"))
mesh_slot("hay_sacks", gen("SM_Town_HaySacks"))
mesh_slot("gibbet", gen("SM_Town_Gibbet"))
mesh_slot("castle_stairs", gen("SM_Castle_Stairs"), clearance="bays")
# Poly Haven CC0 props (multi-part meshes share one pivot).
mesh_slot("crate", ph("wooden_crate_01", "wooden_crate_01"), parts=[ph("wooden_crate_01", "wooden_crate_01_lid"), ph("wooden_crate_01", "wooden_crate_01_latch")],
          fallback=LEGACY + "SM_BracedSupplyCrate.SM_BracedSupplyCrate")
mesh_slot("crate_long", ph("wooden_crate_02", "wooden_crate_02_crate"), parts=[ph("wooden_crate_02", "wooden_crate_02_lid")],
          fallback=LEGACY + "SM_BracedSupplyCrate.SM_BracedSupplyCrate")
mesh_slot("barrel", ph("Barrel_01", "Barrel_01_1k"), fallback=LEGACY + "SM_CooperedBarrel.SM_CooperedBarrel")
mesh_slot("barrel_wine", ph("wine_barrel_01", "wine_barrel_01"), parts=[ph("wine_barrel_01", n) for n in ("wine_barrel_01_base", "wine_barrel_01_bung", "wine_barrel_01_Lid")],
          fallback=LEGACY + "SM_CooperedBarrel.SM_CooperedBarrel")
stack = ph_all("wooden_barrels_01")
mesh_slot("barrel_stack", stack[0], parts=stack[1:], fallback=LEGACY + "SM_CooperedBarrel.SM_CooperedBarrel")
mesh_slot("bucket", ph("wooden_bucket_01", "wooden_bucket_01"), parts=[ph("wooden_bucket_01", "wooden_bucket_01_handle")], collision=False)
mesh_slot("basket", ph("wicker_basket_01", "wicker_basket_01_1k"), collision=False)
mesh_slot("basket_lidded", ph("wicker_basket_02", "wicker_basket_02_base"), parts=[ph("wicker_basket_02", "wicker_basket_02_lid")], collision=False)
mesh_slot("table", ph("WoodenTable_01", "WoodenTable_01_1k"))
mesh_slot("stool", ph("wooden_stool_02", "wooden_stool_02_1k"), collision=False)
mesh_slot("fire_pit", ph("stone_fire_pit", "stone_fire_pit_1k"), fallback=LEGACY + "SM_ForgedBrazier.SM_ForgedBrazier",
          light={"offset": [0, 0, 80], "color": [1.0, 0.45, 0.16], "intensity": 9000, "radius": 1100})
mesh_slot("statue", ph("gothic_statue", "gothic_statue_1k"), fit="footprint", footprint=[150, 160, 175])
mesh_slot("hanging_lantern", ph("wooden_lantern_01", "wooden_lantern_01"), parts=[ph("wooden_lantern_01", "wooden_lantern_01_door"), ph("wooden_lantern_01", "wooden_lantern_01_handle")],
          collision=False, clearance="bays", light={"offset": [0, 0, 22], "color": WARM, "intensity": 1600, "radius": 500})
mesh_slot("castle_door", ph("large_castle_door", "large_castle_door_frame"),
          parts=[ph("large_castle_door", "large_castle_door_left"), ph("large_castle_door", "large_castle_door_right")], clearance="bays")
mesh_slot("shield", ph("kite_shield", "kite_shield_body"), parts=[ph("kite_shield", "kite_shield_clamps_and_bolts"), ph("kite_shield", "kite_shield_metal_trim")],
          collision=False, clearance="bays")
mesh_slot("axe", ph("wooden_axe", "wooden_axe_1k"), collision=False)
mesh_slot("jug", ph("jug_01", "jug_01_1k"), collision=False)
mesh_slot("pot", ph("ceramic_pot", "ceramic_pot_1k"), collision=False)
mesh_slot("apple", ph("food_apple_01", "food_apple_01_1k"), collision=False)
mesh_slot("ladder", ph("wooden_ladder", "wooden_ladder_steps"), parts=[ph("wooden_ladder", "wooden_ladder_supports")], collision=False)
mesh_slot("fallen_log", ph("dead_tree_trunk_02", "dead_tree_trunk_02_1k"))
mesh_slot("stump", ph("tree_stump_01", "tree_stump_01_1k"))
mesh_slot("boulder", ph("boulder_01", "boulder_01_1k"), fit="footprint", footprint=[128, 184, 100])
for i in range(1, 7):
    e = ph_extent("rock_moss_set_01", f"rock_moss_set_01_rock0{i}")
    mesh_slot(f"rock_{i}", ph("rock_moss_set_01", f"rock_moss_set_01_rock0{i}"), fit="footprint", footprint=[e[0] * 2, e[1] * 2, e[2] * 2])
# world-scale (vibrant): the scaled backdrop hills and field rocks wear the world-aligned mossy-rock blend (grey rock, green
# moss on up-facing slopes) instead of the small moss-rock scan texture that read as orange sandstone when scaled up 7-16x.
MOSSY = "/Game/Arenas/Materials/MI_ArenaB_ForestRock.MI_ArenaB_ForestRock"
for _sid in ["boulder"] + [f"rock_{i}" for i in range(1, 7)]:
    SLOTS[_sid]["materialOverride"] = MOSSY
for i, sub in enumerate(("a", "b", "c", "d"), 1):
    e = ph_extent("shrub_02", f"shrub_02_{sub}")
    mesh_slot(f"shrub_{i}", ph("shrub_02", f"shrub_02_{sub}"), fit="footprint", footprint=[e[0] * 2, e[1] * 2, e[2] * 2], collision=False, clearance="bays")
for i, sub in enumerate(("b", "c"), 1):
    e = ph_extent("fern_02", f"fern_02_{sub}")
    mesh_slot(f"fern_{i}", ph("fern_02", f"fern_02_{sub}"), fit="footprint", footprint=[e[0] * 2, e[1] * 2, e[2] * 2], collision=False, clearance="bays")
mesh_slot("tree", ph("island_tree_02", "island_tree_02_1k"), fit="footprint", footprint=[420, 410, 340], collision=False, clearance="bays")
SLOTS["tree"]["materialOverride"] = "/Game/Environment/Town/Materials/MI_Town_Bark.MI_Town_Bark"  # leafless, bark-textured: a dead town tree
# world-scale: countryside, farm and park slots for the new districts. Every slot has committed CC0 / original art;
# Tools/BuildFabWorldSlots.py adds the purchased Medieval Kingdom meshes over them (local-only overlay).
AK = "/Game/Arenas/Meshes/"
AP = "/Game/Arenas/Props/"
ATREE = lambda n: f"/Game/Arenas/Trees/{n}/SM_{n}.SM_{n}"


def arena_kit(sid, name, size, collision=True, clearance="route", shadow=True, cull=None, note=None):
    mesh_slot(sid, f"{AK}{name}.{name}", footprint=[round(v) for v in size], fit="footprint", collision=collision, clearance=clearance,
              shadow=shadow, cull=cull, note=note)


mesh_slot("tree_leafy", ATREE("island_tree_01"), fit="footprint", footprint=[760, 760, 1150], collision=False, clearance="bays",
          cull=[30000, 36000], note="Leafy broadleaf tree (CC0 Poly Haven island_tree_01); the Fab overlay swaps in a European beech.")
mesh_slot("tree_young", ATREE("tree_small_02"), fit="footprint", footprint=[480, 560, 820], collision=False, clearance="bays", cull=[26000, 32000])
mesh_slot("tree_fir", ATREE("tree_small_02"), fit="footprint", footprint=[520, 560, 1300], collision=False, clearance="bays", cull=[30000, 36000],
          note="Conifer slot; the CC0 fallback is the small broadleaf, the Fab overlay swaps in a silver fir.")
mesh_slot("grass_clump", f"{AP}grass_medium_02/grass_medium_02_1k/StaticMeshes/grass_medium_02_e.grass_medium_02_e", collision=False,
          shadow=False, cull=[5000, 6500], fit="footprint", footprint=[40, 44, 40])
mesh_slot("wildflowers", f"{AP}wild_rooibos_bush/wild_rooibos_bush_1k/StaticMeshes/wild_rooibos_bush_a.wild_rooibos_bush_a", collision=False,
          shadow=False, cull=[5000, 6500], fit="footprint", footprint=[70, 84, 54])
arena_kit("wheat", "SM_Arena_WheatClump", (104, 110, 139), collision=False, shadow=False, cull=[7000, 9000])
arena_kit("hay_round", "SM_Arena_HayBaleRound", (161, 156, 161))
arena_kit("hay_bale_stack", "SM_Arena_HayStack", (122, 340, 228))
arena_kit("stook", "SM_Arena_Stook", (81, 121, 153), collision=False, cull=[9000, 12000])
arena_kit("scarecrow", "SM_Arena_Scarecrow", (68, 190, 236), collision=False, cull=[9000, 12000])
arena_kit("hay_wagon", "SM_Arena_HayWagon", (557, 240, 276))
arena_kit("barn", "SM_Arena_Barn", (1500, 1040, 998))
arena_kit("windmill", "SM_Arena_Windmill", (870, 860, 1700))
arena_kit("windmill_sails", "SM_Arena_WindmillSails", (60, 2226, 2226), collision=False, clearance="none")
arena_kit("field_wall", "SM_Arena_StoneWall", (76, 401, 148))
_rock = ph("rock_moss_set_01", "rock_moss_set_01_rock02")
mesh_slot("mountain", _rock, fit="footprint", footprint=[30000, 30000, 9000], collision=False, clearance="none",
          note="Backdrop mountain; CC0 fallback is a scaled moss rock, the Fab overlay swaps in the Medieval Kingdom mountains.")
SLOTS["mountain"]["component"] = "static"  # plain mesh components: the Fab mountain materials cannot draw on instanced meshes
SLOTS["mountain"]["materialOverride"] = "/Game/Arenas/Materials/MI_ArenaB_ForestRock.MI_ArenaB_ForestRock"
# Material slots. World-space surfaces (road, plazas, ground) are drawn on scaled engine cubes: replacements
# must be world-aligned or they will stretch. Mesh-slot materials swap that material slot on every town mesh.
MAT = "/Game/Environment/Town/Materials/"
for sid, mi, mesh_slot_name in (
        ("cobblestone_material", "MI_TownW_Cobble", None), ("plaza_material", "MI_TownW_Plaza", None),
        ("ground_material", "MI_TownW_Ground", None), ("field_material", "MI_TownW_Field", None), ("meadow_material", "MI_TownW_Meadow", None),
        ("flagstone_material", "MI_TownW_Flagstone", None), ("castle_material", "MI_TownW_CastleW", None),
        ("stone_material", "MI_TownW_StoneW", None),
        ("cliff_material", "MI_TownW_CliffW", None),
        ("plaster_material", "MI_Town_Plaster", "Plaster"), ("timber_material", "MI_Town_Timber", "Timber"),
        ("planks_material", "MI_Town_Planks", "Planks"), ("roof_slate_material", "MI_Town_RoofSlate", "RoofSlate"),
        ("roof_clay_material", "MI_Town_RoofClay", "RoofClay"), ("thatch_material", "MI_Town_Thatch", "Thatch"),
        ("masonry_material", "MI_Town_Stone", "Stone"), ("castle_masonry_material", "MI_Town_Castle", "Castle")):
    s = {"kind": "material", "material": f"{MAT}{mi}.{mi}"}
    if mesh_slot_name:
        s["meshSlot"] = mesh_slot_name
    else:
        s["note"] = "world-aligned surface on scaled cubes"
    SLOTS[sid] = s

# ------------------------------------------------------------------ geometry helpers (runtime-equivalent checks)
def slot_box(sid):
    s = SLOTS[sid]
    if "footprint" in s and s.get("fit") == "footprint":
        fx, fy, _ = s["footprint"]
        return (-fx / 2, -fy / 2, fx / 2, fy / 2)
    mesh = s["mesh"].split(".")[-1]
    if mesh in BOUNDS:
        lo, hi = BOUNDS[mesh]
        return (lo[0], lo[1], hi[0], hi[1])
    for asset in IMPORT["props"].values():
        for m in asset["meshes"]:
            if m["asset"] == s["mesh"]:
                ex, org = m["extent"], m["origin"]
                boxes = [(org[0] - ex[0], org[1] - ex[1], org[0] + ex[0], org[1] + ex[1])]
                for part in s.get("parts", []):
                    for m2 in asset["meshes"]:
                        if m2["asset"] == part:
                            e2, o2 = m2["extent"], m2["origin"]
                            boxes.append((o2[0] - e2[0], o2[1] - e2[1], o2[0] + e2[0], o2[1] + e2[1]))
                return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    return (-50, -50, 50, 50)


def corners(sid, x, y, yaw, scale):
    x0, y0, x1, y1 = slot_box(sid)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [(x + (px * c - py * s) * scale, y + (px * s + py * c) * scale) for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]


def inside(sid, x, y, yaw, scale, px, py, margin):
    x0, y0, x1, y1 = slot_box(sid)
    c, s = math.cos(math.radians(-yaw)), math.sin(math.radians(-yaw))
    lx, ly = ((px - x) * c - (py - y) * s) / scale, ((px - x) * s + (py - y) * c) / scale
    m = margin / scale
    return x0 - m < lx < x1 + m and y0 - m < ly < y1 + m


def route_dist(px, py):
    best = 1e9
    for (ax, ay), (bx, by) in zip(ROUTE, ROUTE[1:]):
        dx, dy = bx - ax, by - ay
        t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
        best = min(best, math.hypot(px - ax - dx * t, py - ay - dy * t))
    return best


def bays():
    length = sum(math.dist(a, b) for a, b in zip(ROUTE, ROUTE[1:]))
    out = []
    for tier in (1, 2, 3):
        rem = length * (1 - tier * .25)
        anchor = ROUTE[-1]
        for a, b in zip(ROUTE, ROUTE[1:]):
            seg = math.dist(a, b)
            if rem <= seg:
                anchor = (a[0] + (b[0] - a[0]) * rem / seg, a[1] + (b[1] - a[1]) * rem / seg)
                break
            rem -= seg
        best, score = None, -1e18
        for side in (-1, 1):
            for off in range(-4, 5):
                c = (max(500, min(MAXX - 250, anchor[0] + off * 100)), side * (HW - 220))
                sc = route_dist(*c) - abs(off) * 25
                if sc > score:
                    best, score = c, sc
        out.append(best)
    return out


BAYS = bays()
SPAWN = ROUTE[0]
PLACEMENTS = []
DROPPED = []
OCCUPIED = []  # (x0,y0,x1,y1) of raw placements that must not overlap each other


def valid(sid, x, y, yaw, scale, mode):
    cl = SLOTS[sid].get("clearance", "route")
    ys = [y] if mode is None else [abs(y), -abs(y)]  # outer/inner rows land on both sides across the two teams
    for yy in ys:
        cs = corners(sid, x, yy, yaw if (mode is None or yy > 0) else -yaw, scale)
        if max(abs(c[1]) for c in cs) > INNER_LIMIT and mode != "outer":
            return "divider"
        if cl == "none":
            continue
        if inside(sid, x, yy, yaw if (mode is None or yy > 0) else -yaw, scale, SPAWN[0], SPAWN[1], SPAWN_MARGIN):
            return "spawn"
        for b in BAYS:
            if inside(sid, x, yy, yaw if (mode is None or yy > 0) else -yaw, scale, b[0], b[1], BAY_MARGIN):
                return "bay"
        if cl == "route":
            x0, y0, x1, y1 = slot_box(sid)
            reach = max(math.hypot(px, py) for px in (x0, x1) for py in (y0, y1)) * scale
            if route_dist(x, yy) > reach + ROUTE_MARGIN + 5:  # world-scale: cheap reject before sampling the long route
                continue
            for (ax, ay), (bx, by) in zip(ROUTE, ROUTE[1:]):
                n = max(1, int(math.dist((ax, ay), (bx, by)) // 40))
                for k in range(n + 1):
                    if inside(sid, x, yy, yaw if (mode is None or yy > 0) else -yaw, scale, ax + (bx - ax) * k / n, ay + (by - ay) * k / n, ROUTE_MARGIN):
                        return "route"
    return None


def aabb(sid, x, y, yaw, scale):
    cs = corners(sid, x, y, yaw, scale)
    return (min(c[0] for c in cs), min(c[1] for c in cs), max(c[0] for c in cs), max(c[1] for c in cs))


def overlaps(box, pad=0):
    return any(box[0] < o[2] + pad and box[2] > o[0] - pad and box[1] < o[3] + pad and box[3] > o[1] - pad for o in OCCUPIED)


def put(sid, x, y, yaw=0.0, scale=1.0, z=0.0, mode=None, reserve=False, check_overlap=False, quiet=False):
    why = valid(sid, x, y, yaw, scale, mode)
    box = aabb(sid, x, y, yaw, scale)
    if not why and check_overlap and overlaps(box, 20):
        why = "overlap"
    if why:
        if not quiet:
            DROPPED.append(f"{sid} ({x:.0f},{y:.0f}) {why}")
        return False
    row = {"slot": sid, "x": round(x, 1), "y": round(y, 1)}
    if yaw:
        row["yaw"] = round(((yaw + 180) % 360) - 180, 1)
    if scale != 1:
        row["scale"] = round(scale, 3)
    if z:
        row["z"] = round(z, 1)
    if mode:
        row["mode"] = mode
    PLACEMENTS.append(row)
    if reserve:
        OCCUPIED.append(box)
    return True


def width_of(sid):
    x0, y0, x1, y1 = slot_box(sid)
    return y1 - y0, x1, x1 - x0  # facade width, front offset, depth


def facing_street(y_side):
    """Yaw that turns a +X-front building toward the lane centre from the given side."""
    return -90.0 if y_side > 0 else 90.0


def street_row(x_from, x_to, side, pool, seed, facade, gap=(40, 140), mode=None, lanterns=True, clutter=True):
    rng = random.Random(seed)
    x = x_from
    k = 0
    while x < x_to:
        sid = pool[k % len(pool)] if seed < 0 else rng.choice(pool)
        k += 1
        w, front, depth = width_of(sid)
        if x + w > x_to + 150:
            break
        cx = x + w / 2
        yaw = facing_street(side)
        cy = side * (facade + front)
        # with yaw -90 the mesh's local -Y side maps to +X; keep centre on the facade width
        ok = put(sid, cx, cy if mode is None else abs(cy), yaw if mode is None else -90.0, 1.0, mode=mode, reserve=mode is None,
                 check_overlap=mode is None, quiet=True)
        if not ok:  # blocked by a landmark, bay or the road: slide along and try again
            x += 160
            continue
        if ok and mode is None:
            fy = side * (facade - 8)
            if lanterns and rng.random() < .45:
                put("wall_lantern", cx + rng.uniform(-w * .3, w * .3), side * (facade + 4), 90 if side > 0 else -90, 1, z=250, quiet=True)
            if clutter:
                r = rng.random()
                cx2 = cx + rng.uniform(-w * .35, w * .35)
                if r < .25:
                    put("barrel", cx2, fy - side * 45, rng.uniform(0, 360), quiet=True)
                    put("barrel", cx2 + 70, fy - side * 40, rng.uniform(0, 360), quiet=True)
                elif r < .45:
                    put("crate", cx2, fy - side * 40, facing_street(side) + rng.uniform(-8, 8), quiet=True)
                    put("crate", cx2 + 5, fy - side * 40, facing_street(side) + rng.uniform(-15, 15), z=35, scale=.9, quiet=True)
                elif r < .55:
                    put("barrel_wine", cx2, fy - side * 55, rng.uniform(0, 360), quiet=True)
                elif r < .65:
                    put("bucket", cx2, fy - side * 30, 0, quiet=True)
                elif r < .72:
                    put("woodpile", cx2, fy - side * 140, facing_street(side), .8, quiet=True)
        x += w + rng.uniform(*gap)


def along(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


# ================================================================== LAYOUT
# "style" drives the dressing fill pass (Tools/AuthorWorldDressing.py); the runtime reads id/name/minX/maxX only.
S = GATE_SHIFT
DISTRICTS = [
    {"id": "breach", "name": "The Breach Fields", "minX": 11550 + S, "maxX": 80000, "style": "breach"},
    {"id": "gate", "name": "Gate Road", "minX": 8900 + S, "maxX": 11550 + S, "style": "market"},
    {"id": "hamlet", "name": "Brookfield Hamlet", "minX": 35500, "maxX": 8900 + S, "style": "yard"},
    {"id": "farms", "name": "The Outer Farmsteads", "minX": 29500, "maxX": 35500, "style": "farm"},
    {"id": "tanners", "name": "Tanners' Yard", "minX": 24500, "maxX": 29500, "style": "yard"},
    {"id": "oldwall", "name": "The Old Wall", "minX": 22500, "maxX": 24500, "style": "wall"},
    {"id": "weavers", "name": "Weavers' Lanes", "minX": 17500, "maxX": 22500, "style": "yard"},
    {"id": "green", "name": "The Temple Green", "minX": 13500, "maxX": 17500, "style": "square"},
    {"id": "guild", "name": "Guildhall Row", "minX": 8900, "maxX": 13500, "style": "market"},
    {"id": "market", "name": "Market District", "minX": 5600, "maxX": 8900, "style": "market"},
    {"id": "residential", "name": "Cooper's Lanes", "minX": 3000, "maxX": 5600, "style": "yard"},
    {"id": "square", "name": "Town Square", "minX": 300, "maxX": 3000, "style": "square"},
    {"id": "approach", "name": "Castle Approach", "minX": -1450, "maxX": 300},
    {"id": "castle", "name": "Castle Bailey", "minX": -30000, "maxX": -1450},
]

# ---- castle: gatehouse is the defended leak zone entrance; bailey behind; keep beyond the realm edge
put("castle_gate", -1050, 0, 0, reserve=True)
for s in (-1, 1):
    put("castle_wall", -1040, s * 1540, 0, reserve=True)                 # front curtain to the realm edge
    put("castle_wall", -1980, s * 1580, 90, reserve=True)                # bailey side walls
    put("castle_wall", -2920, s * 1580, 90, reserve=True)
    put("castle_tower", -2600, s * 1560, 0, reserve=True)
    put("castle_wall", -3000, s * 1000, 0, reserve=True)                 # rear curtain
    put("banner", -1500, s * 1150, 90 * -s)
    put("fire_pit", -1950, s * 900, 0)
    put("barrel_stack", -2250, s * 1150, 90 * s, quiet=True)
    put("crate", -1750, s * 1250, 10)
    put("crate_long", -1640, s * 1260, 90)
    put("shield", -1460, s * 1300, -90 * s, z=240, quiet=True)
put("castle_wall", -3000, 0, 0, reserve=True)
put("castle_keep", -4050, 0, 0, reserve=True)
put("castle_stairs", -2800, 0, 0)
put("castle_door", -2830, 0, 90, scale=1.6)
put("statue", -2450, -700, 60, scale=1.2)
put("statue", -2450, 700, -60, scale=1.2)
# ---- castle approach: banner-lined causeway, braziers and barricades
for x in (-100, 250):
    for s in (-1, 1):
        put("banner", x, s * 760, 90 * -s)
for s in (-1, 1):
    put("fire_pit", -350, s * 900, 0)
    put("barricade", 150, s * 1050, 90 + s * 15)
    put("low_wall", -500, s * 1100, 0)
    put("low_wall", 0, s * 1150, 0)
    put("shrub_1", -200, s * 1250, 30)
    put("shrub_2", 250, s * 1250, 60 * s, scale=1.4)
# ---- town square: shrine centrepiece, chapel, well, statue, trees, benches (tables)
put("shrine", 1650, -280, 0, scale=.85, reserve=True)
put("chapel", 1700, 1360, 180, reserve=True)
put("statue", 2250, -650, 150, scale=1.3)
put("well", 600, -700, 30)
for x, y in ((400, -900), (2900, -1100)):
    put("tree", x, y, 40, scale=2.6)
for x in (450, 2900):
    put("banner", x, 1000, 180)
for x, y, yaw in ((2600, -850, 20), (800, -300, -10), (3150, 450, 70)):
    put("table", x, y, yaw)
    put("stool", x + 60, y + 60, 0)
    put("jug", x, y, 0, z=55)
put("gibbet", 350, -1100, 45)
put("cart", 3150, -1150, 180)
put("hay_sacks", 2850, -1150, 30)
# ---- residential lanes: houses inside the realm form an S-bend; gardens behind
lane_front = [("house_c", 4050)]
for sid, x in lane_front:
    w, front, depth = width_of(sid)
    put(sid, x, 150 + front, -90, reserve=True)
put("house_planks", 5150, 1250, -90, reserve=True, check_overlap=True)
for x in (3150, 3950, 4900):
    put("fence", x, 800, 90)
put("woodpile", 3900, 350, 0)
put("tree", 3900, 950, 10, scale=2.2)
put("shrub_2", 4200, 900, 0)
put("shrub_3", 3200, 950, 0)
put("fern_1", 4000, 650, 0)
put("ladder", 3400, 550, 90, quiet=True)
put("hay_sacks", 4400, -1100, 0)
put("fence", 4700, -1150, 0)
put("stump", 3700, -1150, 0)
put("axe", 3720, -1150, 0, z=55)
# ---- market: covered hall, stalls lining the road, well, carts, goods
put("market_hall", 6900, -820, 0, reserve=True)
rng = random.Random(7)
for i, x in enumerate(range(5950, 7200, 430)):
    for side in (-1, 1):
        sid = "market_stall_a" if (i + (side > 0)) % 2 else "market_stall_b"
        y = 500 + side * 540
        if put(sid, x + (120 if (side < 0 and x < 6000) else 0), y - (60 if side < 0 else 0), 90 if side < 0 else -90, check_overlap=True):
            put(rng.choice(["basket", "basket_lidded", "pot", "crate"]), x + 140, y + side * 60, rng.uniform(0, 360), quiet=True)
            put("barrel", x - 150, y + side * 80, rng.uniform(0, 360), quiet=True)
put("well", 8350, 750, 15, reserve=True)
for x, y, yaw in ((8000, 1150, 160), (7550, 1050, 20)):
    put("market_stall_a" if x > 7700 else "market_stall_b", x, y, -90, check_overlap=True)
put("cart", 8250, 1250, 170)
put("cart", 6450, 1250, 10)
put("barrel_stack", 8900, 600, 20, quiet=True)
for x, y in ((7800, 700), (7700, 1250), (6200, 1250), (6700, 1260)):
    put(rng.choice(["crate", "crate_long", "barrel_wine", "hay_sacks"]), x, y, rng.uniform(0, 360), quiet=True)
for k in range(10):
    put("apple", 6100 + k * 23, 1000 + (k % 3) * 9, 0, z=108, quiet=True)
for x in (5750, 8550):
    for s in (-1, 1):
        put("banner", x, 500 + s * 520 if x < 7000 else -550 + s * 520, 90 * -s, quiet=True)
# ---- gate road (world-scale: moved out by GATE_SHIFT with the town wall and the breach): watchtower, barricades
put("watchtower", 10450 + S, 1050, -90, reserve=True)
put("barricade", 10500 + S, -1150, 90)
put("barricade", 9300 + S, -1150, 80)
put("fire_pit", 10850 + S, 1000, 0)
put("crate", 10000 + S, -1100, 0)
put("crate_long", 10100 + S, -1150, 90)
put("barrel_stack", 9500 + S, 1150, 180, quiet=True)
# ---- town wall + gatehouse; breach fields beyond
put("gatehouse", 11400 + S, 0, 0, reserve=True)
for s in (-1, 1):
    put("wall_section", 11400 + S, s * 1500, 0, reserve=True)
for y in (2500, 3500):
    put("wall_section", 11400 + S, y, 0, mode="outer")
put("wall_tower", 11400 + S, 3000, 0, mode="outer")
fr = random.Random(99)
for k in range(46):
    x = fr.uniform(11800, 15600)
    y = fr.uniform(-HW + 100, HW - 100) if x + S < MAXX else fr.uniform(-1900, 1900)
    kind = fr.choice(["rock_1", "rock_2", "rock_3", "rock_4", "rock_5", "rock_6", "boulder", "fallen_log", "stump", "shrub_4", "rock_2", "boulder"])
    scale = fr.uniform(1.0, 2.2) if x + S < MAXX else fr.uniform(2.2, 4.5)
    put(kind, x + S, y, fr.uniform(0, 360), scale, quiet=True)
for x, y, yaw in ((12000, -900, 30), (12200, 1050, -20)):
    put("barricade", x + S, y, yaw)
put("gibbet", 11900 + S, 1150, 200)
put("cart", 12300 + S, -1150, 150)
put("fire_pit", 11800 + S, -1100, 0)
put("fire_pit", 11850 + S, 1000, 0)

# ================================================================== world-scale: the seven new districts
wr = random.Random(2509)


def around(x0, x1, y0, y1, n, pool, scale=(1, 1), seed=0, mode=None, yaw=None):
    """Scatter n pieces from pool in a rectangle (every row still passes the clearance rules)."""
    r = random.Random(seed)
    for _ in range(n):
        x, y = r.uniform(x0, x1), r.uniform(y0, y1)
        if mode is None and route_dist(x, y) < ROUTE_MARGIN + 80:
            continue  # never on the march road, even for pieces whose slot only keeps bay clearance (shrubs, ferns)
        put(r.choice(pool), x, y, r.uniform(0, 360) if yaw is None else yaw, r.uniform(*scale), mode=mode, quiet=True)


def lane_house(sid, x, side, inset=150):
    """A house pushed into the lane on one side (its facade toward the road, like Cooper's Lanes)."""
    w, front, depth = width_of(sid)
    return put(sid, x, side * (inset + front), facing_street(side), reserve=True, check_overlap=True, quiet=True)


# ---- Guildhall Row (8900..13500): guild houses, a smithy yard and a flagstone guild plaza
put("tavern", 12050, -1420, 90, reserve=True, check_overlap=True)      # the guildhall on the plaza
put("market_hall", 10600, -1050, 0, reserve=True, check_overlap=True, quiet=True)
for x, y in ((9600, 700), (9900, 900)):
    put("fire_pit", x, y, 0, quiet=True)                                   # the smithy forges
put("woodpile", 9300, 1100, 0, quiet=True)
put("barrel_stack", 9500, -1100, 90, quiet=True)
for x, y in ((10300, 1150), (12600, -900), (13200, -1100)):
    put(wr.choice(["crate", "crate_long", "barrel_wine", "hay_sacks"]), x, y, wr.uniform(0, 360), quiet=True)
put("cart", 12400, 1150, 190, quiet=True)
put("statue", 11200, -1000, 20, scale=1.15)
for x in (10700, 12700):
    for s in (-1, 1):
        put("banner", x, s * 1100, 90 * -s, quiet=True)
put("well", 12950, 1150, 30, quiet=True)
# ---- The Temple Green (13500..17500): a park with the fountain, leafy trees, flower beds and statues
put("fountain", 15300, 850, 0, reserve=True)
put("chapel", 16800, -1350, 0, reserve=True, check_overlap=True, quiet=True)
for x, y, sc in ((13900, 1000, 1.2), (14600, 1250, 1.0), (16200, 1200, 1.1), (14200, -1150, 1.15), (15200, -1200, .95), (17200, -700, 1.0)):
    put("tree_leafy", x, y, wr.uniform(0, 360), sc, quiet=True)
for x, y in ((14700, 700), (16000, 700), (15300, 1350)):
    put("statue", x, y, wr.uniform(0, 360), scale=1.1, quiet=True)
around(13600, 17400, -1300, 1300, 90, ["shrub_1", "shrub_2", "shrub_3", "shrub_4", "fern_1", "fern_2", "wildflowers", "wildflowers"], (1.0, 1.8), 31)
for x in (13700, 17300):
    for s in (-1, 1):
        put("banner", x, s * 1150, 90 * -s, quiet=True)
for x, y in ((14300, 300), (16400, -700)):
    put("table", x, y, wr.uniform(0, 360), quiet=True)
    put("stool", x + 60, y + 60, 0, quiet=True)
# ---- Weavers' Lanes (17500..22500): a tight S-bend between houses pushed into the lane
for x in (19200, 20000, 20800, 21600):
    lane_house(wr.choice(["cottage_thatch", "cottage_slate", "house_planks"]), x, 1, inset=60)
for x in (17800, 18500):
    lane_house(wr.choice(["cottage_slate", "house_planks"]), x, -1, inset=100)
for x in (19000, 20400, 21700):
    put("fence", x, -1150, 0, quiet=True)
around(17600, 22400, -1300, 1300, 40, ["woodpile", "hay_sacks", "barrel", "crate", "bucket", "basket", "stump", "shrub_2", "fern_1"], (.8, 1.1), 32)
put("well", 19600, 900, 15, quiet=True)
put("tree_leafy", 18100, -1100, 20, 1.0, quiet=True)
put("tree_young", 22100, 1150, 50, 1.0, quiet=True)
# ---- The Old Wall (22500..24500): the original town wall, its gate long gone: towers flank the road
for s in (-1, 1):
    put("wall_tower", 23500, s * 1050, 0, reserve=True)
    put("wall_section", 23500, s * 1560, 0, reserve=True, quiet=True)
    put("banner", 23150, s * 700, 90 * -s, quiet=True)
    put("banner", 23850, s * 700, 90 * -s, quiet=True)
    put("barricade", 22900, s * 1250, 90 + s * 20, quiet=True)
    put("fire_pit", 24050, s * 1000, 0, quiet=True)
for y in (2500, 3500):
    put("wall_section", 23500, y, 0, mode="outer")
put("wall_tower", 23500, 3000, 0, mode="outer")
around(22600, 24400, -1300, 1300, 16, ["rock_2", "rock_5", "crate", "fallen_log", "shrub_4"], (.8, 1.3), 33)
# ---- Tanners' Yard (24500..29500): sheds, drying racks (fences), hides and hay, workshops
for x, s in ((25300, 1), (26500, 1), (28200, -1), (29000, -1)):
    lane_house(wr.choice(["warehouse", "house_planks"]), x, s, inset=250)
for x in (25600, 26600, 27600, 28500):
    put("fence", x, -1150, 0, quiet=True)
    put("fence", x + 150, 1200, 0, quiet=True)
around(24600, 29400, -1300, 1300, 55, ["hay_sacks", "woodpile", "barrel", "barrel_wine", "crate", "crate_long", "bucket", "ladder", "cart"], (.9, 1.1), 34)
for x, y in ((25900, -1000), (28800, 1050)):
    put("fire_pit", x, y, 0, quiet=True)
put("tree_leafy", 27200, -1150, 70, 1.1, quiet=True)
# ---- The Outer Farmsteads (29500..35500): farmhouses, a barn, wheat fields, bales and stooks, dry-stone walls
put("barn", 31200, 1250, 0, reserve=True, check_overlap=True, quiet=True)
put("windmill", 34300, 1150, 200, reserve=True, quiet=True)
put("windmill_sails", 34300 + math.cos(math.radians(200)) * 440, 1150 + math.sin(math.radians(200)) * 440, 200, z=1460, quiet=True)
for x, s in ((30000, -1), (33000, 1), (35000, -1)):
    lane_house(wr.choice(["cottage_thatch", "cottage_slate"]), x, s, inset=300)
for x in range(29700, 35400, 420):
    put("field_wall", x, -1350, 0, quiet=True)
    put("field_wall", x + 200, 1380, 0, quiet=True)
around(29600, 35400, -1300, 1300, 36, ["hay_round", "hay_round", "stook", "stook", "hay_bale_stack", "scarecrow"], (.9, 1.2), 35)
put("hay_wagon", 32600, -1100, 20, quiet=True)
for x, y in ((29900, 1150), (32300, -1250), (34800, -1200)):
    put("tree_leafy", x, y, wr.uniform(0, 360), 1.2, quiet=True)
# ---- Brookfield Hamlet (35500..39600): cottages round a green with a well, gardens and an orchard
for x, s in ((36000, 1), (36900, -1), (37600, 1), (38400, -1), (39100, 1)):
    lane_house(wr.choice(["cottage_thatch", "cottage_slate", "house_c", "house_a"]), x, s, inset=250)
put("well", 37200, -900, 0, quiet=True)
for x in range(35700, 39500, 520):
    put("tree_young", x, 1250 if (x // 520) % 2 else -1250, wr.uniform(0, 360), wr.uniform(.8, 1.1), quiet=True)
around(35600, 39500, -1300, 1300, 40, ["shrub_1", "shrub_3", "fern_1", "wildflowers", "wildflowers", "woodpile", "hay_sacks", "bucket", "stump"], (.9, 1.6), 36)
for x in (36300, 38000):
    put("fence", x, 1000, 90, quiet=True)
    put("fence", x + 420, 1000, 90, quiet=True)

# ---- rocky hills and forests beyond the walls, mountains on the horizon (backdrop only, outer side)
hr = random.Random(1234)
for k in range(90):
    x = hr.uniform(-6500, 47500)
    y = hr.uniform(5200, 9000)
    put(hr.choice(["rock_1", "rock_2", "rock_4", "rock_6", "boulder"]), x, y, hr.uniform(0, 360), hr.uniform(7, 16), mode="outer", quiet=True)
for k in range(10):
    put(hr.choice(["rock_1", "rock_4", "boulder"]), hr.uniform(-7500, -5200), hr.uniform(-1800, 1800), hr.uniform(0, 360), hr.uniform(6, 12), quiet=True)
    put(hr.choice(["rock_2", "rock_6", "boulder"]), hr.uniform(16200, 19000) + S, hr.uniform(-1800, 1800), hr.uniform(0, 360), hr.uniform(6, 12), quiet=True)
for k in range(14):
    put("tree", hr.uniform(-2500, 15000), hr.uniform(4400, 5200), hr.uniform(0, 360), hr.uniform(2.0, 3.2), mode="outer", quiet=True)
# the outer ward and the countryside beyond the town wall: leafy woods and firs (outer side)
for k in range(150):
    x = hr.uniform(15000, 46000)
    put(hr.choice(["tree_leafy", "tree_leafy", "tree_fir", "tree_young"]), x, hr.uniform(4400, 8200), hr.uniform(0, 360), hr.uniform(.9, 1.6), mode="outer", quiet=True)
for k in range(16):  # a mountain range well beyond the woods, on the horizon of each realm's outer side
    put("mountain", -14000 + k * 4600 + hr.uniform(-800, 800), hr.uniform(22000, 30000), hr.uniform(0, 360), hr.uniform(1.0, 1.5), mode="outer", quiet=True)
for x in (-16000, 60000):  # and behind the castle and past the breach
    for y in (3000, 16000):
        put("mountain", x + hr.uniform(-2000, 2000), y, hr.uniform(0, 360), hr.uniform(1.0, 1.3), mode="outer", quiet=True)
# ---- street rows along both realm edges (shallow enough for the divider side)
SHALLOW = ["house_a", "house_c", "house_planks", "cottage_thatch", "cottage_slate"]
for side, seed in ((1, 11), (-1, 23)):
    for (a, b) in ((300, 3000), (3000, 5600), (5600, 8900), (8900, 11000)):
        street_row(a, b, side, SHALLOW, seed + a, facade=1250)
    # world-scale: the new town districts inside the old wall get their street fronts too
    for (a, b) in ((11000, 13500), (17500, 22400)):
        street_row(a, b, side, SHALLOW, seed + a, facade=1250)
    street_row(8900 + S, 11000 + S, side, SHALLOW, seed + 8900, facade=1250)   # the shifted gate road keeps its houses
    street_row(35600, 39500, side, ["cottage_thatch", "cottage_slate", "house_c", "house_a"], seed + 35600, facade=1250, gap=(260, 700))  # hamlet
    street_row(24600, 29400, side, ["warehouse", "house_planks", "cottage_slate"], seed + 24600, facade=1250, gap=(300, 900))  # tanners
# Deeper, taller second rows on each team's outer side form the skyline (now all the way to the old wall).
DEEP = ["tavern", "house_b", "house_shop", "townhouse_row", "townhouse_row_b", "warehouse"]
street_row(300, 22400, 1, DEEP, 5, facade=2150, mode="outer", gap=(20, 90), lanterns=False, clutter=False)
street_row(-2000, 22400, 1, ["townhouse_row", "townhouse_row_b", "house_b", "tavern"], 6, facade=3150, mode="outer", gap=(0, 60), lanterns=False, clutter=False)
# beyond the old wall the outer side opens into farmsteads: scattered cottages and barns
for x in range(25000, 41000, 1900):
    put(wr.choice(["cottage_thatch", "cottage_slate", "house_planks"]), x + wr.uniform(-300, 300), 2900 + wr.uniform(-200, 400), -90, mode="outer", quiet=True)
for x in range(-2000, 11400 + S, 1000):
    put("wall_section", x, 4150, 90, mode="outer", quiet=True)
for x in range(3000, 11400 + S, 5000):
    put("wall_tower", x, 4150, 0, mode="outer", quiet=True)
# ---- lamps along the march road (alternating sides, skipping gate passages)
dist = 0.0
side = 1
for a, b in zip(ROUTE, ROUTE[1:]):
    seg = math.dist(a, b)
    t = (400 - dist) if dist < 400 else 0
    while t < seg:
        px, py = along(a, b, t / seg)
        nx, ny = -(b[1] - a[1]) / seg, (b[0] - a[0]) / seg
        lx, ly = px + nx * side * 430, py + ny * side * 430
        if not (10900 + S < lx < 12150 + S or 23000 < lx < 24000 or -1700 < lx < -150):
            put("lamp", lx, ly, math.degrees(math.atan2(-ny * side, -nx * side)), quiet=True)
        side = -side
        t += 900
    dist = 0
PLACEMENTS.sort(key=lambda r: (r["x"], r["y"]))

# ---- world-scale: ground foliage (grass, wildflowers, wheat fields) as an additive layout overlay, so the base layout
# stays readable. Same validation as every other row; nothing here collides.
BASE_ROWS = PLACEMENTS
PLACEMENTS = []
gr = random.Random(77)
for x0, x1, n in ((13500, 17500, 420), (17500, 22500, 160), (24500, 29500, 260), (29500, 35500, 420), (35500, 39600, 380),
                  (39600, 42250, 90), (-1450, 300, 40), (300, 3000, 60)):
    for _ in range(n):
        put(gr.choice(["grass_clump", "grass_clump", "grass_clump", "wildflowers"]), gr.uniform(x0, x1), gr.uniform(-1330, 1330),
            gr.uniform(0, 360), gr.uniform(1.4, 2.6), quiet=True)
# the farmsteads' wheat fields between the road and the dry-stone walls (both sides)
for fx0, fx1, fy0, fy1 in ((29700, 31900, 500, 1300), (32500, 35300, -1300, -700), (32800, 35300, 250, 1300), (29700, 31700, -1300, -600)):
    x = fx0
    while x < fx1:
        y = fy0
        while y < fy1:
            put("wheat", x + gr.uniform(-25, 25), y + gr.uniform(-25, 25), gr.uniform(0, 360), gr.uniform(.85, 1.2), quiet=True)
            y += 95
        x += 95
# the countryside beyond the town wall (outer side): meadow grass and wildflowers
for _ in range(700):
    put(gr.choice(["grass_clump", "grass_clump", "wildflowers"]), gr.uniform(13000, 46000), gr.uniform(1500, 4000), gr.uniform(0, 360),
        gr.uniform(1.6, 2.8), mode="outer", quiet=True)
FOLIAGE_ROWS = sorted(PLACEMENTS, key=lambda r: (r["x"], r["y"]))
PLACEMENTS = BASE_ROWS

# ---- world-scale: world-aligned ground surfaces per district (read by ACireWorld; realm-local centre X / length / width,
# width 0 = the full realm floor)
FULL = 2 * HW - 60
SURFACES = [
    {"slot": "plaza_material", "x": 7250, "length": 3300, "width": FULL},        # market square
    {"slot": "plaza_material", "x": 1650, "length": 2700, "width": FULL},        # town square
    {"slot": "flagstone_material", "x": -500, "length": 1600, "width": 1900},    # castle approach
    {"slot": "flagstone_material", "x": -2600, "length": 3000, "width": 2 * HW + 500},  # inner bailey
    {"slot": "flagstone_material", "x": 11700, "length": 2600, "width": FULL},   # guild plaza
    {"slot": "plaza_material", "x": 23500, "length": 1500, "width": 1600},       # paving through the old wall
    {"slot": "meadow_material", "x": 15500, "length": 4000, "width": FULL},      # the temple green
    {"slot": "meadow_material", "x": 34550, "length": 10100, "width": 0},        # farmsteads + hamlet (full realm floor)
    {"slot": "meadow_material", "x": 27000, "length": 5000, "width": 0, "y": 0},  # tanners' yard outskirts
    {"slot": "field_material", "x": (11550 + S + MAXX + 2800) / 2, "length": MAXX + 2800 - 11550 - S, "width": 0},  # breach fields
]

slots_doc = {
    "schemaVersion": 1,
    "priority": 0,
    "source": "base",
    "description": ("Slot id -> mesh/material for the medieval town. Overlay manifests Content/Data/TownAssetSlots.<source>.json "
                    "(same schema, higher 'priority'; defaults tripo=10, fab=20) are merged at load: the highest-priority candidate "
                    "that loads wins, then this base mesh, then 'fallback'. Use \"fit\": \"footprint\" for meshes of unknown scale; "
                    "they are centred and uniformly scaled into this slot's footprint [depth X, width Y, height Z] cm. "
                    "Buildings face +X (their street facade). Written by Tools/AuthorTownLayout.py."),
    "slots": SLOTS,
}
layout_doc = {
    "schemaVersion": 1,
    "units": "centimeters",
    "description": ("Medieval town placements in realm-local coordinates (Y relative to the team realm centre), applied to both "
                    "private PvE realms. mode 'outer'/'inner' mirrors a row (authored on +Y) onto each team's outer/divider side. "
                    "Placements that touch the live route, challenge bays, breach spawn or the realm divider are omitted at runtime. "
                    "Written by Tools/AuthorTownLayout.py."),
    "districts": DISTRICTS,
    "gateShift": GATE_SHIFT,
    "surfaces": SURFACES,
    "placements": PLACEMENTS,
}
foliage_doc = {
    "schemaVersion": 1,
    "units": "centimeters",
    "description": ("world-scale ground foliage (grass, wildflowers, wheat fields), appended to TownLayout.json at runtime with the "
                    "same clearance checks. Written by Tools/AuthorTownLayout.py."),
    "placements": FOLIAGE_ROWS,
}
(DATA / "TownAssetSlots.json").write_text(json.dumps(slots_doc, indent=1) + "\n", encoding="utf-8")
(DATA / "TownLayout.json").write_text(json.dumps(layout_doc, indent=1) + "\n", encoding="utf-8")
(DATA / "TownLayout.foliage.json").write_text(json.dumps(foliage_doc, separators=(",", ":")).replace("},{", "},\n{") + "\n", encoding="utf-8")
# same compact layout as the hand-authored file: one line per lane
_lanes = ",\n".join('    { "team": %d, "points": %s }' % (l["team"], json.dumps(l["points"], separators=(",", ":"))) for l in ROUTES["lanes"])
_head = ",\n".join(f'  {json.dumps(k)}: {json.dumps(v)}' for k, v in ROUTES.items() if k not in ("lanes", "armoredEscort"))
(DATA / "BattlefieldRoutes.json").write_text("{\n" + _head + ',\n  "lanes": [\n' + _lanes + '\n  ],\n  "armoredEscort": ' +
                                            json.dumps(ROUTES["armoredEscort"]) + "\n}\n", encoding="utf-8")
print(f"route points={len(ROUTE)} length={sum(math.dist(a, b) for a, b in zip(ROUTE, ROUTE[1:])):.0f} cm foliage={len(FOLIAGE_ROWS)}")
counts = {}
for r in PLACEMENTS:
    counts[r["slot"]] = counts.get(r["slot"], 0) + 1
print(f"bays={BAYS}")
print(f"slots={len(SLOTS)} placements={len(PLACEMENTS)}")
print(", ".join(f"{k}:{v}" for k, v in sorted(counts.items())))
print(f"dropped ({len(DROPPED)}):")
for d in DROPPED:
    print("  ", d)
