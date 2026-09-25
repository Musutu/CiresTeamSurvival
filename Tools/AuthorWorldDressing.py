"""Author the world dressing overlay for the medieval town (world-dressing).

Writes two additive overlay files; the base TownAssetSlots.json / TownLayout.json (Tools/AuthorTownLayout.py)
are read, never modified:
  Content/Data/TownAssetSlots.dressing.json  new "dress_*" slots (priority 5, never replace a base slot)
  Content/Data/TownLayout.dressing.json      placements, appended to the base layout at runtime

Rules (mirrored from the runtime and AuthorTownLayout.py, every row is validated here so nothing relies on
runtime suppression):
  * Colliding pieces (benches, tables, display shelves, weapon racks, hay piles) keep the full route clearance
    (half lane width + 70 cm), the challenge bays and the breach spawn; they carve the navmesh.
  * Small clutter never collides (collision false) and only avoids the bays and spawn ("bays").
  * Flat ground cards (puddles, litter), smoke, crows and wall-hung pieces above head height have clearance
    "none" or "bays": they cannot block anyone.
  * Wall pieces are hung on base-kit buildings only (their facades are measured from the OBJ sources), never
    on slots that an overlay replaces (Tripo houses are fitted into footprints, so their walls are unknown).
Pure Python. Usage: python Tools/AuthorWorldDressing.py
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content/Data"
BASE_SLOTS = json.loads((DATA / "TownAssetSlots.json").read_text())["slots"]
BASE_LAYOUT = json.loads((DATA / "TownLayout.json").read_text())["placements"]
OVERRIDDEN = set()
for overlay in DATA.glob("TownAssetSlots.*.json"):
    if overlay.name.endswith(".dressing.json"):
        continue
    for sid, entry in json.loads(overlay.read_text())["slots"].items():
        if entry.get("status", "imported") == "imported":
            OVERRIDDEN.add(sid)
DRESS = json.loads((ROOT / "Art/Environment/Dressing/ImportReport.json").read_text())
TOWN_IMPORT = json.loads((ROOT / "Art/Environment/Town/ImportReport.json").read_text())
ARENA_IMPORT = json.loads((ROOT / "Art/Arenas/ImportReport.json").read_text())
TOWN_MESHES = {m["name"]: m for m in json.loads((ROOT / "Art/Environment/Town/Meshes/TownMeshes.json").read_text())["meshes"]}
ROUTES = json.loads((DATA / "BattlefieldRoutes.json").read_text())
ROUTE = [tuple(p) for p in ROUTES["lanes"][0]["points"]]
HW = ROUTES["bounds"]["halfWidth"]
ROUTE_MARGIN, BAY_MARGIN, SPAWN_MARGIN = ROUTES.get("laneWidth", 520) / 2 + 70, 450, 420
INNER_LIMIT = 2100 - 60

# ------------------------------------------------------------------ slots
SLOTS: dict = {}
BOXES: dict = {}  # slot -> local (x0, y0, x1, y1)


def box_of(meshes):
    xs0, ys0, xs1, ys1 = [], [], [], []
    for m in meshes:
        e, o = m["extent"], m["origin"]
        xs0.append(o[0] - e[0]); ys0.append(o[1] - e[1]); xs1.append(o[0] + e[0]); ys1.append(o[1] + e[1])
    return (min(xs0), min(ys0), max(xs1), max(ys1))


def slot(sid, mesh, parts=(), box=None, collision=False, clearance="route", cull=None, light=None, shadow=True, note=None):
    s = {"mesh": mesh, "fit": "none"}
    if parts:
        s["parts"] = list(parts)
    s["collision"] = collision
    if clearance != "route":
        s["clearance"] = clearance
    if not shadow:
        s["castShadow"] = False
    if cull:
        s["cullDistance"] = cull
    if light:
        s["light"] = light
    if note:
        s["note"] = note
    SLOTS[sid] = s
    BOXES[sid] = box


def ph(sid, asset_id, pick=None, **kw):
    """A Poly Haven prop imported by Tools/ImportWorldDressing.py; pick selects mesh names (default: all parts)."""
    meshes = DRESS["props"][asset_id]["meshes"]
    if pick:
        meshes = [m for m in meshes if any(m["asset"].endswith("." + p) for p in pick)]
    slot(sid, meshes[0]["asset"], [m["asset"] for m in meshes[1:]], box_of(meshes), **kw)


def kitmesh(sid, name, **kw):
    m = DRESS["meshes"][name]
    slot(sid, m["asset"], (), box_of([m]), **kw)


def town_ph(sid, asset_id, **kw):
    meshes = TOWN_IMPORT["props"][asset_id]["meshes"]
    slot(sid, meshes[0]["asset"], [m["asset"] for m in meshes[1:]], box_of(meshes), **kw)


def arena_prop(sid, asset_id, pick=None, **kw):
    meshes = ARENA_IMPORT["props"][asset_id]
    if pick:
        meshes = [m for m in meshes if any(m["asset"].endswith("." + p) for p in pick)]
    sx = max(m["size"][0] for m in meshes) / 2
    sy = max(m["size"][1] for m in meshes) / 2
    slot(sid, meshes[0]["asset"], [m["asset"] for m in meshes[1:]], (-sx, -sy, sx, sy), **kw)


NEAR, MID, FAR = [2500, 4200], [4500, 7500], [9000, 16000]
WARM = [1.0, 0.52, 0.2]
# market goods and pottery
for sid, aid in (("dress_cheese", "CheeseBox_01"), ("dress_bowl", "wooden_bowl_02"), ("dress_plate", "carved_wooden_plate"),
                 ("dress_board", "wooden_cutting_board"), ("dress_goblets", "brass_goblets"), ("dress_pot_brass", "brass_pot_01"),
                 ("dress_vase_a", "ceramic_vase_01"), ("dress_vase_b", "ceramic_vase_03"), ("dress_jar", "antique_ceramic_vase_01"),
                 ("dress_onion", "yellow_onion"), ("dress_pomegranate", "food_pomegranate_01"), ("dress_pears", "food_pears_asian_01"),
                 ("dress_bottles", "wine_bottles_01"), ("dress_flowerpot", "planter_pot_clay")):
    ph(sid, aid, cull=NEAR, shadow=False)
ph("dress_sweet_potato", "sweet_potato", cull=NEAR, shadow=False)
ph("dress_shelves", "wooden_display_shelves_01", collision=True, clearance="route", cull=FAR)
town_ph("dress_barrel", "wine_barrel_01", collision=True, clearance="route", cull=MID)
ph("dress_bucket", "wooden_bucket_02", cull=MID)
# yards and doorsteps
for sid, aid in (("dress_broom", "wooden_broom"), ("dress_sledge", "sledgehammer_01"), ("dress_hatchet", "hatchet"),
                 ("dress_saw", "handsaw_wood"), ("dress_axe", "wooden_axe_02"), ("dress_mallet", "wooden_hammer_01"),
                 ("dress_stool_fold", "folding_wooden_stool"), ("dress_stool", "wooden_stool_01"), ("dress_ladder", "wooden_ladder_02")):
    ph(sid, aid, cull=MID)
ph("dress_spinning_wheel", "spinning_wheel_01", cull=MID)
ph("dress_bench", "painted_wooden_bench", collision=True, clearance="route", cull=FAR)
ph("dress_round_table", "round_wooden_table_02", collision=True, clearance="route", cull=FAR)
ph("dress_chandelier", "lantern_chandelier_01", cull=FAR, light={"offset": [0, 0, -45], "color": WARM, "intensity": 2400, "radius": 700, "flicker": 0.25})
for sid, aid in (("dress_warhammer", "ornate_war_hammer"), ("dress_mace", "ornate_medieval_mace"), ("dress_estoc", "antique_estoc")):
    ph(sid, aid, cull=NEAR)
for i, aid in enumerate(("planter_box_01", "planter_box_02", "planter_box_03"), 1):
    ph(f"dress_flowerbox_{i}", aid, cull=MID)
ph("dress_nettles", "nettle_plant", cull=NEAR, shadow=False)
ph("dress_weeds", "weed_plant_02", cull=NEAR, shadow=False)
ph("dress_sorrel", "shrub_sorrel_01", cull=NEAR, shadow=False)
ph("dress_branches", "dry_branches_medium_01", cull=MID, shadow=False)
ph("dress_rat", "street_rat", clearance="bays", cull=[1500, 2600], shadow=False)
# original kit
kitmesh("dress_sign_boot", "SM_Dress_ShopSignBoot", clearance="bays", cull=MID)
kitmesh("dress_sign_tankard", "SM_Dress_ShopSignTankard", clearance="bays", cull=MID)
kitmesh("dress_sign_key", "SM_Dress_ShopSignKey", clearance="bays", cull=MID)
kitmesh("dress_banner_red", "SM_Dress_WallBannerRed", clearance="bays", cull=FAR)
kitmesh("dress_banner_pale", "SM_Dress_WallBannerPale", clearance="bays", cull=FAR)
kitmesh("dress_laundry", "SM_Dress_Laundry", clearance="bays", cull=MID)
kitmesh("dress_laundry_short", "SM_Dress_LaundryShort", clearance="bays", cull=MID)
kitmesh("dress_ivy_tall", "SM_Dress_IvyTall", clearance="bays", cull=FAR, shadow=False)
kitmesh("dress_ivy_low", "SM_Dress_IvyLow", clearance="bays", cull=FAR, shadow=False)
kitmesh("dress_puddle_a", "SM_Dress_PuddleA", clearance="none", cull=MID, shadow=False)
kitmesh("dress_puddle_b", "SM_Dress_PuddleB", clearance="none", cull=MID, shadow=False)
kitmesh("dress_grime", "SM_Dress_GrimeStreak", clearance="none", cull=MID, shadow=False)
kitmesh("dress_litter", "SM_Dress_LeafLitter", clearance="none", cull=MID, shadow=False)
kitmesh("dress_smoke", "SM_Dress_Smoke", clearance="none", shadow=False)
kitmesh("dress_crow", "SM_Dress_CrowPerched", clearance="none", cull=[3500, 5000])
kitmesh("dress_crow_flock", "SM_Dress_CrowFlock", clearance="none", shadow=True)
kitmesh("dress_torch", "SM_Dress_WallTorch", clearance="bays", cull=FAR, shadow=False,
        light={"offset": [24, 0, 30], "color": [1.0, 0.46, 0.14], "intensity": 3800, "radius": 780, "flicker": 0.35})
kitmesh("dress_weapon_rack", "SM_Dress_WeaponRack", collision=True, clearance="route", cull=FAR)
kitmesh("dress_haypile", "SM_Dress_HayPile", collision=True, clearance="route", cull=MID)
# CC0 content already in the repository (arena Poly Haven imports)
TREES = ARENA_IMPORT["treeMeshes"] if isinstance(ARENA_IMPORT.get("treeMeshes"), dict) else {}
arena_prop("dress_shrub", "shrub_03", pick=["shrub_03_a"], cull=MID)
arena_prop("dress_shrub_b", "shrub_03", pick=["shrub_03_c"], cull=MID)
arena_prop("dress_roots", "root_cluster_01", cull=MID)
arena_prop("dress_chest", "treasure_chest", collision=True, clearance="route", cull=MID)
town_ph("dress_basket", "wicker_basket_01", cull=NEAR, shadow=False)
town_ph("dress_apple", "food_apple_01", cull=NEAR, shadow=False)

# ------------------------------------------------------------------ geometry (runtime-equivalent checks)
def corners(sid, x, y, yaw, scale):
    x0, y0, x1, y1 = BOXES[sid]
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [(x + (px * c - py * s) * scale, y + (px * s + py * c) * scale) for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]


def inside(sid, x, y, yaw, scale, px, py, margin):
    x0, y0, x1, y1 = BOXES[sid]
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
                c = (max(500, min(ROUTES["bounds"]["maxX"] - 250, anchor[0] + off * 100)), side * (HW - 220))
                sc = route_dist(*c) - abs(off) * 25
                if sc > score:
                    best, score = c, sc
        out.append(best)
    return out


BAYS = bays()
SPAWN = ROUTE[0]
PLACEMENTS, DROPPED = [], []
OCCUPIED = []  # boxes of colliding base + dressing pieces (dressing never overlaps a colliding piece)


def base_box(row):
    s = BASE_SLOTS.get(row["slot"], {})
    if s.get("collision") is False:
        return None
    mesh = s.get("mesh", "").split(".")[-1]
    if s.get("fit") == "footprint" and "footprint" in s:
        fx, fy, _ = s["footprint"]
        b = (-fx / 2, -fy / 2, fx / 2, fy / 2)
    elif mesh in TOWN_MESHES:
        lo, hi = TOWN_MESHES[mesh]["boundsMin"], TOWN_MESHES[mesh]["boundsMax"]
        b = (lo[0], lo[1], hi[0], hi[1])
    else:
        return None
    x, y, yaw, sc = row["x"], row["y"], row.get("yaw", 0), row.get("scale", 1)
    c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    pts = [(x + (px * c - py * s_) * sc, y + (px * s_ + py * c) * sc) for px, py in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3]))]
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


for row in BASE_LAYOUT:
    if row.get("mode") is None and row["slot"] != "market_hall":  # the hall is open-sided: goods go inside
        b = base_box(row)
        if b:
            OCCUPIED.append(b)


def valid(sid, x, y, yaw, scale, mode):
    cl = SLOTS[sid].get("clearance", "route")
    ys = [y] if mode is None else [abs(y), -abs(y)]
    for yy in ys:
        yw = yaw if (mode is None or yy > 0) else -yaw
        cs = corners(sid, x, yy, yw, scale)
        if max(abs(c[1]) for c in cs) > INNER_LIMIT and mode != "outer":
            return "divider"
        if cl == "none":
            continue
        if inside(sid, x, yy, yw, scale, SPAWN[0], SPAWN[1], SPAWN_MARGIN):
            return "spawn"
        for b in BAYS:
            if inside(sid, x, yy, yw, scale, b[0], b[1], BAY_MARGIN):
                return "bay"
        if cl == "route":
            for (ax, ay), (bx, by) in zip(ROUTE, ROUTE[1:]):
                n = max(1, int(math.dist((ax, ay), (bx, by)) // 40))
                for k in range(n + 1):
                    if inside(sid, x, yy, yw, scale, ax + (bx - ax) * k / n, ay + (by - ay) * k / n, ROUTE_MARGIN):
                        return "route"
    return None


def aabb(sid, x, y, yaw, scale):
    cs = corners(sid, x, y, yaw, scale)
    return (min(c[0] for c in cs), min(c[1] for c in cs), max(c[0] for c in cs), max(c[1] for c in cs))


def overlaps(box, pad=0):
    return any(box[0] < o[2] + pad and box[2] > o[0] - pad and box[1] < o[3] + pad and box[3] > o[1] - pad for o in OCCUPIED)


def put(sid, x, y, yaw=0.0, scale=1.0, z=0.0, mode=None, avoid=None, why_tag=""):
    """Place a row if it passes the clearance rules. Colliding pieces also avoid other colliding pieces."""
    why = valid(sid, x, y, yaw, scale, mode)
    box = aabb(sid, x, y, yaw, scale)
    collide = SLOTS[sid]["collision"]
    if not why and mode is None and (avoid if avoid is not None else collide) and overlaps(box, 15):
        why = "overlap"
    if why:
        DROPPED.append(f"{sid} ({x:.0f},{y:.0f}) {why} {why_tag}")
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
    if collide and mode is None:
        OCCUPIED.append(box)
    return True


def local(anchor, lx, ly):
    """Realm-local point at (lx forward, ly left) in an anchor's frame (x, y, yaw)."""
    x, y, yaw = anchor
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return x + lx * c - ly * s, y + lx * s + ly * c


# ------------------------------------------------------------------ building facades from the OBJ sources
OBJ_CACHE: dict = {}
WALL_MATS = {"Stone", "Plaster", "Planks", "Castle"}


def obj_faces(name):
    """[(material, [(x, y, z), ...])] per triangle of a base-kit OBJ (vertices precede the usemtl/f blocks)."""
    if name not in OBJ_CACHE:
        verts, faces, cur = [], [], None
        for line in (ROOT / "Art/Environment/Town/Meshes" / f"{name}.obj").read_text().splitlines():
            if line.startswith("v "):
                _, a, b, c = line.split()
                verts.append((float(a), float(b), float(c)))
            elif line.startswith("usemtl "):
                cur = line.split()[1]
            elif line.startswith("f "):
                faces.append((cur, [verts[int(t.split("/")[0]) - 1] for t in line.split()[1:]]))
        OBJ_CACHE[name] = faces
    return OBJ_CACHE[name]


def obj_verts(name):
    return [(v[0], v[1], v[2], m) for m, pts in obj_faces(name) for v in pts]


def _facade(name, y, z, sign):
    """X of the outermost wall face (normal along sign*X) covering local (y, z)."""
    best = None
    for mat, pts in obj_faces(name):
        if mat not in WALL_MATS:
            continue
        xs = [p[0] for p in pts]
        if max(xs) - min(xs) > 1.0:
            continue
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = pts[:3]
        nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
        if nx * sign <= 0:
            continue
        # point-in-triangle in the YZ plane
        def side(p1, p2, q):
            return (q[0] - p2[0]) * (p1[1] - p2[1]) - (p1[0] - p2[0]) * (q[1] - p2[1])
        q = (y, z)
        t = [(p[1], p[2]) for p in pts[:3]]
        d1, d2, d3 = side(q, t[0], t[1]), side(q, t[1], t[2]), side(q, t[2], t[0])
        if (d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0):
            continue
        x = xs[0]
        if best is None or x * sign > best * sign:
            best = x
    return best


def facade_x(name, y0, y1, z0, z1):
    """Front (+X) wall face at the centre of the (y, z) window; None when no wall covers it."""
    return _facade(name, (y0 + y1) / 2, (z0 + z1) / 2, 1)


def ray_x(name, y, z, sign=1):
    """Outermost surface hit by a ray along -sign*X at (y, z), any material or orientation (towers, buttresses)."""
    best = None
    for mat, pts in obj_faces(name):
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = pts[:3]
        det = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
        if abs(det) < 1e-9:
            continue
        u = ((y - ay) * (cz - az) - (z - az) * (cy - ay)) / det
        v = ((by - ay) * (z - az) - (bz - az) * (y - ay)) / det
        if u < 0 or v < 0 or u + v > 1:
            continue
        x = ax + u * (bx - ax) + v * (cx - ax)
        if best is None or x * sign > best * sign:
            best = x
    return best


def back_x(name, y, z):
    return _facade(name, y, z, -1)


def chimney_top(name):
    stone = [v for v in obj_verts(name) if v[3] == "Stone"]
    if not stone:
        return None
    top = max(v[2] for v in stone)
    cap = [v for v in stone if v[2] > top - 30]
    cx, cy = sum(v[0] for v in cap) / len(cap), sum(v[1] for v in cap) / len(cap)
    whole = max(v[2] for v in obj_verts(name))
    if top < whole - 200 or abs(cx) > 600:  # no chimney above the roof
        return None
    return cx, cy, top


HOUSES = {"house_shop", "house_planks", "cottage_thatch", "cottage_slate", "townhouse_row", "townhouse_row_b", "warehouse",
          "house_a", "house_b", "house_c", "tavern"}
WALLSAFE = {sid for sid in HOUSES if sid not in OVERRIDDEN}
DISTRICTS = json.loads((DATA / "TownLayout.json").read_text())["districts"]


def district(x):
    for d in DISTRICTS:
        if d["minX"] <= x < d["maxX"]:
            return d["id"]
    return None


def buildings(mode_filter=(None,)):
    for row in BASE_LAYOUT:
        if row["slot"] in WALLSAFE and row.get("mode") in mode_filter:
            yield row, BASE_SLOTS[row["slot"]]["mesh"].split(".")[-1]


rng = random.Random(2026)
SIGNS = ["dress_sign_boot", "dress_sign_tankard", "dress_sign_key"]

# ---- facades of the street rows: ivy, grime, signs, banners, laundry, torches, doorstep life
for row, mesh in buildings((None, "outer")):
    x, y, yaw, mode = row["x"], row["y"], row.get("yaw", 0), row.get("mode")
    lo, hi = TOWN_MESHES[mesh]["boundsMin"], TOWN_MESHES[mesh]["boundsMax"]
    half_w = (hi[1] - lo[1]) / 2
    d = district(x)
    anchor = (x, y, yaw)
    used = []

    def slot_u(width):
        for _ in range(8):
            u = rng.uniform(-half_w + width / 2 + 30, half_w - width / 2 - 30)
            if all(abs(u - o) > (width + w) / 2 + 20 for o, w in used):
                used.append((u, width))
                return u
        return None

    def wall(sid, u, z, extra=1.0):
        fx = facade_x(mesh, u - 40, u + 40, max(0, z - 60), z + 60)
        if fx is None:
            return False
        px, py = local(anchor, fx + extra, u)
        return put(sid, px, py, yaw, z=z, mode=mode)

    if mode is None:  # street-facing rows: laundry first (widest), then trade signs and banners
        if d in ("residential", "market", "gate", "square") and rng.random() < .75 and (u := slot_u(390)) is not None:
            wall(rng.choice(["dress_laundry", "dress_laundry_short"]), u, 330)
        if d in ("market", "gate", "square", "residential") and rng.random() < .8 and (u := slot_u(40)) is not None:
            wall(rng.choice(SIGNS), u, 330)
        if d in ("market", "square", "gate") and rng.random() < .5 and (u := slot_u(110)) is not None:
            wall(rng.choice(["dress_banner_red", "dress_banner_pale"]), u, 520)
    # grime streaks under the eaves / windows and ivy climbing the ground floor
    for _ in range(2):
        if rng.random() < .75 and (u := slot_u(260)) is not None:
            wall("dress_grime", u, rng.uniform(260, 420), .5)
    if rng.random() < (.75 if d in ("residential", "square", "castle", "approach") else .5) and (u := slot_u(240)) is not None:
        wall(rng.choice(["dress_ivy_tall", "dress_ivy_low"]), u, 0, 1.5)
    if mode == "outer":
        continue  # the far rows only get weathering: nobody walks there
    # doorstep and wall-foot life (ground level, in front of the facade)
    fx = facade_x(mesh, -half_w, half_w, 0, 150) or hi[0]
    for _ in range(rng.randint(1, 3)):
        u = rng.uniform(-half_w + 60, half_w - 60)
        pick = rng.random()
        px, py = local(anchor, fx + 30, u)
        if pick < .25:
            put(rng.choice(["dress_flowerbox_1", "dress_flowerbox_2", "dress_flowerbox_3"]), px, py, yaw + 90)
        elif pick < .4:
            put("dress_bench", *local(anchor, fx + 45, u), yaw + 90)
        elif pick < .5:
            put("dress_broom", *local(anchor, fx + 12, u), yaw + rng.uniform(-20, 20))
        elif pick < .6 and d == "residential":
            put("dress_spinning_wheel", *local(anchor, fx + 70, u), yaw + rng.uniform(40, 140))
        elif pick < .7:
            put("dress_flowerpot", px, py, rng.uniform(0, 360), scale=1.6)
            put("dress_flowerpot", *local(anchor, fx + 30, u + 35), rng.uniform(0, 360), scale=1.3)
        elif pick < .85:
            put(rng.choice(["dress_nettles", "dress_weeds", "dress_sorrel"]), *local(anchor, fx + 25, u), rng.uniform(0, 360), scale=rng.uniform(1.6, 2.4))
        else:
            put("dress_barrel", *local(anchor, fx + 40, u), rng.uniform(0, 360))
            put("dress_bucket", *local(anchor, fx + 45, u + 70), rng.uniform(0, 360))
    if rng.random() < .15:
        put("dress_rat", *local(anchor, fx + 15, rng.uniform(-half_w, half_w)), yaw + rng.uniform(60, 120), scale=1.3)

# ---- houses an overlay replaced (Tripo, fitted into the base footprint): no wall pieces (their walls are unknown),
# but the ground in front of the footprint gets doorstep life, 60+ cm clear of where the facade can be.
for row in BASE_LAYOUT:
    if row["slot"] not in (HOUSES & OVERRIDDEN) or row.get("mode") is not None:
        continue
    foot = BASE_SLOTS[row["slot"]].get("footprint") or [700, 800, 900]
    anchor = (row["x"], row["y"], row.get("yaw", 0))
    for _ in range(rng.randint(1, 3)):
        u = rng.uniform(-foot[1] / 2 + 80, foot[1] / 2 - 80)
        pick = rng.random()
        if pick < .35:
            put(rng.choice(["dress_flowerbox_1", "dress_flowerbox_2", "dress_flowerbox_3"]), *local(anchor, foot[0] / 2 + 70, u), anchor[2] + 90)
        elif pick < .55:
            put("dress_bench", *local(anchor, foot[0] / 2 + 80, u), anchor[2] + 90)
        elif pick < .75:
            put("dress_barrel", *local(anchor, foot[0] / 2 + 75, u), rng.uniform(0, 360))
            put("dress_bucket", *local(anchor, foot[0] / 2 + 80, u + 75), rng.uniform(0, 360))
        else:
            put(rng.choice(["dress_nettles", "dress_weeds", "dress_sorrel"]), *local(anchor, foot[0] / 2 + 60, u), rng.uniform(0, 360), scale=rng.uniform(1.8, 2.6))

# ---- chimney smoke: every other base-kit house with a real chimney (both rows)
for row, mesh in buildings((None, "outer")):
    top = chimney_top(mesh)
    if not top or rng.random() < .45:
        continue
    px, py = local((row["x"], row["y"], row.get("yaw", 0)), top[0], top[1])
    put("dress_smoke", px, py, rng.uniform(-25, 25), scale=rng.uniform(.8, 1.15), z=top[2] - 5, mode=row.get("mode"))

# ---- the market: goods tables, pottery and produce, display shelves, the market hall filled with trade
TABLE_TOP = 54
GOODS = [["dress_onion"] * 6 + ["dress_sweet_potato"] * 4, ["dress_pomegranate"] * 5 + ["dress_pears"],
         ["dress_vase_a", "dress_vase_b", "dress_jar", "dress_vase_a"], ["dress_bowl", "dress_plate", "dress_board", "dress_bowl"],
         ["dress_cheese", "dress_cheese", "dress_board"], ["dress_goblets", "dress_bottles", "dress_pot_brass"]]


def goods_table(x, y, yaw, kind):
    if not put("table", x, y, yaw) if "table" in SLOTS else False:
        return False
    items = GOODS[kind % len(GOODS)]
    for k, item in enumerate(items):
        lx = -70 + 140 * (k + .5) / len(items) + rng.uniform(-6, 6)
        put(item, *local((x, y, yaw), lx, rng.uniform(-12, 12)), rng.uniform(0, 360), z=TABLE_TOP, scale=1.0)
    for s in (-1, 1):
        if rng.random() < .6:
            put(rng.choice(["dress_basket", "crate_basket", "dress_basket"]) if "crate_basket" in SLOTS else "dress_basket",
                *local((x, y, yaw), s * 110, -45), rng.uniform(0, 360))
    return True


# the base "table" slot (WoodenTable_01) is reused; register it so the validator knows its box
SLOTS["table"] = {"collision": True}
BOXES["table"] = box_of(TOWN_IMPORT["props"]["WoodenTable_01"]["meshes"])
SLOTS["barrel"] = {"collision": True}
BOXES["barrel"] = box_of(TOWN_IMPORT["props"]["wine_barrel_01"]["meshes"])
SLOTS["hay_sacks"] = {"collision": True}
BOXES["hay_sacks"] = (-150, -101, 78, 81)
SLOTS["woodpile"] = {"collision": True}
BOXES["woodpile"] = (-126, -185, 126, 185)
SLOTS["crate"] = {"collision": True}
BOXES["crate"] = box_of(TOWN_IMPORT["props"]["wooden_crate_01"]["meshes"])

stall_rows = [r for r in BASE_LAYOUT if r["slot"] in ("market_stall_a", "market_stall_b")]
kind = 0
for r in stall_rows:
    # a goods table beside each stall, turned toward the road, plus baskets and a crate stack
    yaw = r.get("yaw", 0)
    for side in (-1, 1):
        tx, ty = local((r["x"], r["y"], yaw), 40, side * 285)
        if goods_table(tx, ty, yaw + 90, kind):
            kind += 1
            break
    bx, by = local((r["x"], r["y"], yaw), -30, rng.choice((-1, 1)) * 250)
    put("dress_basket", bx, by, rng.uniform(0, 360))
    put("dress_apple", bx, by, 0, z=8)
    put("dress_apple", bx + 6, by + 4, 0, z=8)
# open ground in the market square: display shelves with pots, crates, hay and a cart's worth of goods
for x, y, yaw in ((6150, -350, 180), (7700, -380, 0), (8500, 250, 90), (5900, 1150, -90)):
    if put("dress_shelves", x, y, yaw):
        for k in range(3):
            put(rng.choice(["dress_vase_a", "dress_vase_b", "dress_jar", "dress_pot_brass"]), *local((x, y, yaw), 0, -35 + 35 * k), rng.uniform(0, 360), z=118)
            put(rng.choice(["dress_bowl", "dress_plate", "dress_goblets"]), *local((x, y, yaw), 0, -35 + 35 * k), rng.uniform(0, 360), z=74)
for x, y in ((7300, 1150), (6000, 1300), (8050, -250)):
    put("dress_haypile", x, y, rng.uniform(0, 360), scale=.9)
# the covered market hall (base kit, 6900,-820): tables of wares, barrels, sacks and a hanging lantern ring
hall = next((r for r in BASE_LAYOUT if r["slot"] == "market_hall"), None)
if hall:
    hx, hy = hall["x"], hall["y"]
    for k, lx in enumerate((-420, -140, 140, 420)):
        goods_table(hx + lx, hy - 120, 0, kind + k)
    for lx in (-520, 520):
        put("barrel", hx + lx, hy + 230, rng.uniform(0, 360))
        put("dress_barrel", hx + lx + 70, hy + 250, rng.uniform(0, 360))
    put("hay_sacks", hx - 250, hy + 250, 180)
    put("hay_sacks", hx + 250, hy + 260, 0)
    for lx in (-350, 350):
        put("dress_chandelier", hx + lx, hy, 0, z=560, scale=1.6)
# puddles and litter where carts churn the ground
for k in range(10):
    x = rng.uniform(5700, 8800)
    y = rng.uniform(-900, 1100)
    put(rng.choice(["dress_puddle_a", "dress_puddle_b"]), x, y, rng.uniform(0, 360), scale=rng.uniform(.7, 1.2), z=2.5)

# ---- residential lanes: yards with woodpiles, tools, hay, laundry poles, weeds, rats
for x, y in ((3350, 1150), (4550, 1150), (3600, -1150), (5050, -1000)):
    if put("woodpile", x, y, rng.choice((0, 90))):
        put("dress_hatchet", *local((x, y, 0), 150, 40), rng.uniform(0, 360), z=40)
        put("dress_axe", *local((x, y, 0), 145, -60), 90, z=0)
for x, y in ((3100, 650), (4800, 700), (4200, -700)):
    put("dress_haypile", x, y, rng.uniform(0, 360))
for x, y in ((3300, -900), (4600, 450), (5200, 700)):
    put("dress_ladder", x, y, rng.uniform(0, 360), z=0)
    put("dress_sledge", x + 60, y + 40, rng.uniform(0, 360))
    put("dress_saw", x - 40, y + 60, rng.uniform(0, 360), z=1)
for k in range(26):
    x, y = rng.uniform(3000, 5600), rng.uniform(-1200, 1200)
    put(rng.choice(["dress_nettles", "dress_weeds", "dress_sorrel", "dress_shrub", "dress_shrub_b", "dress_branches"]), x, y,
        rng.uniform(0, 360), scale=rng.uniform(1.5, 2.6))
for k in range(8):
    put("dress_litter", rng.uniform(3000, 5600), rng.uniform(-1150, 1150), rng.uniform(0, 360), scale=rng.uniform(.7, 1.2), z=1)
for k in range(6):
    put(rng.choice(["dress_puddle_a", "dress_puddle_b"]), rng.uniform(3000, 5600), rng.uniform(-900, 900), rng.uniform(0, 360),
        scale=rng.uniform(.6, 1.0), z=2.5)
for k in range(5):
    put("dress_rat", rng.uniform(3000, 5600), rng.choice((-1, 1)) * rng.uniform(900, 1150), rng.uniform(0, 360), scale=1.3)

# ---- town square: benches round the shrine, tavern tables outside, flowers, the chapel's ivy
shrine = next((r for r in BASE_LAYOUT if r["slot"] == "shrine"), {"x": 1650, "y": -280})
for k in range(8):
    a = 2 * math.pi * k / 8 + .2
    put("dress_bench", shrine["x"] + math.cos(a) * 560, shrine["y"] + math.sin(a) * 560, math.degrees(a) + 90)
for x, y in ((2350, 900), (2700, 1050), (900, 850), (1150, 1050)):
    if put("dress_round_table", x, y, rng.uniform(0, 360)):
        put("dress_goblets", x, y, rng.uniform(0, 360), z=75)
        put(rng.choice(["dress_bottles", "dress_bowl", "dress_plate"]), x + 12, y - 10, rng.uniform(0, 360), z=75)
        for k in range(3):
            a = rng.uniform(0, 2 * math.pi)
            put(rng.choice(["dress_stool", "dress_stool_fold"]), x + math.cos(a) * 75, y + math.sin(a) * 75, rng.uniform(0, 360))
for k in range(6):
    a = 2 * math.pi * (k + .5) / 6
    put(rng.choice(["dress_flowerbox_1", "dress_flowerbox_2", "dress_flowerbox_3"]), shrine["x"] + math.cos(a) * 480,
        shrine["y"] + math.sin(a) * 480, math.degrees(a) + 90)
chapel = next((r for r in BASE_LAYOUT if r["slot"] == "chapel"), None)
if chapel:
    for u in (-330, 20, 300):
        fx = facade_x("SM_Town_Chapel", u - 40, u + 40, 0, 200)
        if fx:
            put("dress_ivy_tall", *local((chapel["x"], chapel["y"], chapel.get("yaw", 0)), fx + 1.5, u), chapel.get("yaw", 0))
for k in range(6):
    put(rng.choice(["dress_puddle_a", "dress_puddle_b"]), rng.uniform(500, 2900), rng.uniform(-1000, 1000), rng.uniform(0, 360),
        scale=rng.uniform(.6, 1.0), z=2.5)
for statue in [r for r in BASE_LAYOUT if r["slot"] in ("statue", "gibbet")]:
    put("dress_crow", statue["x"] + rng.uniform(-20, 20), statue["y"] + rng.uniform(-20, 20), rng.uniform(0, 360),
        z=(200 if statue["slot"] == "statue" else 570) * statue.get("scale", 1))

# ---- castle bailey and approach: weapon racks, torches, chests, hay for the horses, banners on the curtain
for x, y, yaw in ((-1800, 700, 0), (-1800, -700, 180), (-2300, 1200, -90)):
    if put("dress_weapon_rack", x, y, yaw):
        for k, w in enumerate(["dress_estoc", "dress_warhammer", "dress_mace", "dress_estoc", "dress_mace"]):
            put(w, *local((x, y, yaw), 8, -80 + 40 * k), yaw + 90, z=110)
for x, y in ((-2150, 400), (-2150, -400), (-1650, 1300)):
    put("dress_chest", x, y, rng.uniform(-20, 20))
for x, y in ((-2500, 1250), (-2500, -1250)):
    put("dress_haypile", x, y, rng.uniform(0, 360))
    put("hay_sacks", x + 200, y, 90)
gate = next((r for r in BASE_LAYOUT if r["slot"] == "castle_gate"), None)
if gate and "castle_gate" not in OVERRIDDEN:
    for s in (-1, 1):
        for mesh_side, face_yaw in (("front", 0.0), ("back", 180.0)):
            u = s * 500
            if mesh_side == "front":
                fx = ray_x("SM_Castle_Gatehouse", s * 470, 300)  # the flanking towers' faces beside the passage
                if fx is not None:
                    put("dress_torch", *local((gate["x"], gate["y"], 0), fx + 2, s * 470), 0, z=300)
            else:
                bx = back_x("SM_Castle_Gatehouse", u, 300)
                if bx is not None and abs((ray_x("SM_Castle_Gatehouse", u, 300, -1) or bx) - bx) < 3:
                    put("dress_torch", *local((gate["x"], gate["y"], 0), bx - 1, u), 180, z=300)
for row in [r for r in BASE_LAYOUT if r["slot"] == "castle_wall" and r.get("yaw", 0) == 0 and r["x"] < -2500]:
    for u in (-250, 250):
        put("dress_torch", *local((row["x"], row["y"], 0), 247, u), 0, z=330)
        put("dress_banner_red", *local((row["x"], row["y"], 0), 247, u + 150), 0, z=900)
for k in range(8):
    put("dress_crow", rng.uniform(-3000, -1100), rng.choice((-1, 1)) * rng.uniform(1300, 1600), rng.uniform(0, 360), z=1175)

# ---- gate road and breach: puddles, litter, crows on the gibbet, torches on the town gate
for k in range(10):
    put(rng.choice(["dress_puddle_a", "dress_puddle_b"]), rng.uniform(8900, 13500), rng.uniform(-1100, 1100), rng.uniform(0, 360),
        scale=rng.uniform(.8, 1.4), z=2.5)
for k in range(12):
    put("dress_litter", rng.uniform(8900, 15000), rng.uniform(-1500, 1500), rng.uniform(0, 360), scale=rng.uniform(.8, 1.4), z=1.2)
for k in range(16):
    put(rng.choice(["dress_branches", "dress_roots", "dress_nettles", "dress_weeds"]), rng.uniform(11700, 15500), rng.uniform(-1800, 1800),
        rng.uniform(0, 360), scale=rng.uniform(1.2, 2.4))
tgate = next((r for r in BASE_LAYOUT if r["slot"] == "gatehouse"), None)
if tgate and "gatehouse" not in OVERRIDDEN:
    for s in (-1, 1):
        u = s * 440
        fx = ray_x("SM_Town_Gatehouse", s * 460, 300)
        if fx is not None:
            put("dress_torch", *local((tgate["x"], tgate["y"], 0), fx + 2, s * 460), 0, z=300)
        bx = back_x("SM_Town_Gatehouse", u, 300)
        if bx is not None and abs((ray_x("SM_Town_Gatehouse", u, 300, -1) or bx) - bx) < 3:
            put("dress_torch", *local((tgate["x"], tgate["y"], 0), bx - 1, u), 180, z=300)
for k in range(5):
    put("dress_crow", rng.uniform(11300, 11500), rng.uniform(-1900, 1900), rng.uniform(0, 360), z=860)
# circling crows: over the breach, the gibbet, the castle keep and the residential roofs
for x, y, z in ((12800, 0, 2100), (11900, 1150, 1500), (-3600, 0, 3600), (4300, 0, 2300), (8000, -600, 2700)):
    put("dress_crow_flock", x, y, rng.uniform(0, 360), scale=rng.uniform(.9, 1.2), z=z)

def route_y(px):
    for (ax, ay), (bx, by) in zip(ROUTE, ROUTE[1:]):
        if min(ax, bx) <= px <= max(ax, bx) and ax != bx:
            return ay + (by - ay) * (px - ax) / (bx - ax)
    return 0.0


# ---- fill pass: walk a jittered grid over each district and drop a small cluster wherever the rules allow
def cluster_market(x, y, yaw):
    r = rng.random()
    if r < .45:
        return goods_table(x, y, yaw, rng.randrange(len(GOODS)))
    if r < .65:
        ok = put("crate", x, y, yaw + rng.uniform(-10, 10))
        if ok:
            put("crate", x + rng.uniform(-6, 6), y + rng.uniform(-6, 6), yaw + rng.uniform(-20, 20), z=31, scale=.9)
            put(rng.choice(["dress_onion", "dress_pomegranate", "dress_apple"]), x, y, 0, z=62)
            put("dress_basket", *local((x, y, yaw), 0, 70), rng.uniform(0, 360))
        return ok
    if r < .8:
        ok = put("dress_barrel", x, y, rng.uniform(0, 360))
        if ok:
            put("barrel", *local((x, y, yaw), 0, 62), rng.uniform(0, 360))
            put("dress_bucket", *local((x, y, yaw), 55, 25), rng.uniform(0, 360))
        return ok
    if r < .9:
        return put("hay_sacks", x, y, yaw)
    return put("dress_haypile", x, y, rng.uniform(0, 360), scale=rng.uniform(.7, 1))


def cluster_yard(x, y, yaw):
    r = rng.random()
    if r < .25:
        ok = put("woodpile", x, y, yaw)
        if ok:
            put(rng.choice(["dress_hatchet", "dress_axe"]), *local((x, y, yaw), 150, 20), rng.uniform(0, 360), z=2)
        return ok
    if r < .45:
        return put("dress_haypile", x, y, rng.uniform(0, 360), scale=rng.uniform(.7, 1.1))
    if r < .6:
        ok = put("dress_barrel", x, y, rng.uniform(0, 360))
        if ok:
            put("dress_bucket", *local((x, y, yaw), 50, 30), rng.uniform(0, 360))
            put("dress_broom", *local((x, y, yaw), -10, 50), rng.uniform(0, 360))
        return ok
    if r < .75:
        ok = put("dress_bench", x, y, yaw)
        if ok:
            put(rng.choice(["dress_bowl", "dress_board", "dress_mallet"]), x, y, rng.uniform(0, 360), z=45)
        return ok
    if r < .9:
        for k in range(3):
            put(rng.choice(["dress_nettles", "dress_weeds", "dress_sorrel", "dress_shrub"]), x + rng.uniform(-90, 90), y + rng.uniform(-90, 90),
                rng.uniform(0, 360), scale=rng.uniform(1.6, 2.6))
        return True
    return put("dress_spinning_wheel", x, y, rng.uniform(0, 360))


def cluster_square(x, y, yaw):
    r = rng.random()
    if r < .4:
        ok = put("dress_round_table", x, y, rng.uniform(0, 360))
        if ok:
            put("dress_goblets", x, y, rng.uniform(0, 360), z=75)
            put(rng.choice(["dress_bottles", "dress_bowl", "dress_plate", "dress_pot_brass"]), x + 14, y - 12, rng.uniform(0, 360), z=75)
            for k in range(rng.randint(2, 3)):
                a = rng.uniform(0, 2 * math.pi)
                put(rng.choice(["dress_stool", "dress_stool_fold"]), x + math.cos(a) * 78, y + math.sin(a) * 78, rng.uniform(0, 360))
        return ok
    if r < .65:
        return put("dress_bench", x, y, yaw)
    if r < .85:
        ok = put(rng.choice(["dress_flowerbox_1", "dress_flowerbox_2", "dress_flowerbox_3"]), x, y, yaw)
        put("dress_flowerpot", x + 60, y + 30, 0, scale=1.6)
        return ok
    return put("dress_barrel", x, y, 0)


for district_id, (x0, x1), step, chance, fn in (("market", (5650, 8850), 380, .8, cluster_market),
                                                 ("residential", (3050, 5550), 430, .7, cluster_yard),
                                                 ("square", (350, 2950), 470, .55, cluster_square),
                                                 ("gate", (8950, 11000), 520, .5, cluster_market)):
    x = x0
    while x < x1:
        y = -1150.0
        while y < 1150:
            if rng.random() < chance:
                jx, jy = x + rng.uniform(-90, 90), y + rng.uniform(-90, 90)
                # face the road: yaw across the lane so tables and benches line up with the street
                fn(jx, jy, 90 if jy < route_y(jx) else -90)
            y += step
        x += step

# ---- planters: the Poly Haven boxes are empty troughs; plant each one (sorrel, weeds, a small shrub)
SOIL = {"dress_flowerbox_1": 36, "dress_flowerbox_2": 38, "dress_flowerbox_3": 76}
for row in [r for r in PLACEMENTS if r["slot"] in SOIL]:
    yaw = row.get("yaw", 0)
    for k, lx in enumerate((-26, 0, 26)):
        px, py = local((row["x"], row["y"], yaw), 0, lx)
        put(("dress_sorrel", "dress_weeds", "dress_sorrel")[k], px, py, rng.uniform(0, 360), scale=rng.uniform(2.6, 3.4), z=SOIL[row["slot"]])

PLACEMENTS.sort(key=lambda r: (r["x"], r["y"]))
out_slots = {k: v for k, v in SLOTS.items() if k.startswith("dress_")}
# The base "barrel" slot resolved to Poly Haven Barrel_01, a modern red hazard drum. This overlay (priority 5 > base 0)
# swaps in the upright wooden wine barrel; placements and collision rules are unchanged.
wine = TOWN_IMPORT["props"]["wine_barrel_01"]["meshes"]
out_slots["barrel"] = {"mesh": wine[0]["asset"], "parts": [m["asset"] for m in wine[1:]], "fit": "none",
                       "note": "world-dressing: wooden barrel instead of the Poly Haven Barrel_01 hazard drum"}
slots_doc = {
    "schemaVersion": 1,
    "priority": 5,
    "source": "dressing",
    "description": ("World dressing slots (world-dressing): CC0 Poly Haven props, ambientCG-textured cards and the original "
                    "dressing kit under /Game/Free/Dressing. New slot ids only (dress_*), so no base slot is replaced. "
                    "'cullDistance' [start, end] cm fades small clutter out; 'light.flicker' animates torches. "
                    "Written by Tools/AuthorWorldDressing.py."),
    "slots": out_slots,
}
layout_doc = {
    "schemaVersion": 1,
    "units": "centimeters",
    "description": ("World dressing placements (world-dressing), appended to TownLayout.json at runtime with the same "
                    "realm-local coordinates, modes and clearance checks. Written by Tools/AuthorWorldDressing.py."),
    "placements": PLACEMENTS,
}
(DATA / "TownAssetSlots.dressing.json").write_text(json.dumps(slots_doc, indent=1) + "\n", encoding="utf-8")
(DATA / "TownLayout.dressing.json").write_text(json.dumps(layout_doc, indent=1) + "\n", encoding="utf-8")
counts: dict = {}
for r in PLACEMENTS:
    counts[r["slot"]] = counts.get(r["slot"], 0) + 1
reasons: dict = {}
for d in DROPPED:
    key = d.split()[0] + " " + d.split()[2]
    reasons[key] = reasons.get(key, 0) + 1
print(f"overridden base slots (no wall dressing): {sorted(OVERRIDDEN & HOUSES)}")
print(f"slots={len(out_slots)} placements={len(PLACEMENTS)}")
print(", ".join(f"{k}:{v}" for k, v in sorted(counts.items())))
print(f"dropped={len(DROPPED)}: " + ", ".join(f"{k}x{v}" for k, v in sorted(reasons.items())))
print("CIRE_DRESSING_LAYOUT_PASS")
