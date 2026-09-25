"""Author the original world-dressing mesh kit as OBJ sources (world-dressing).

Pure Python; never launches Unreal. Reuses the Mesh builder of Tools/BuildTownMeshes.py and writes
Art/Environment/Dressing/Meshes/*.obj plus DressingMeshes.json. Centimetres, Z up.

Conventions: wall-mounted pieces (signs, banners, ivy, grime, torches, laundry) have their pivot on the wall
surface and stick out along +X, so the same yaw as the facade they hang on points them at the street.
Ground cards (puddles, litter) sit a few millimetres above Z=0. Material slot names map to material
instances in Tools/ImportWorldDressing.py (town instances are reused where they fit).

Special UV conventions read by the dressing materials:
  * Crow*: UV0.x = wing lever (0 body .. 1 wing tip), UV0.y = per-bird phase (0..1).
  * Smoke: UV0 = 0..1 across each card, 0 at the chimney .. 1 at the top.
  * Ivy: UV0 addresses one leaf cell of the 2 x 3 ambientCG LeafSet017 atlas.
These are original authored meshes (no third-party geometry).
"""
from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import BuildTownMeshes as kit  # noqa: E402  (module has no side effects on import)

ROOT = Path(__file__).resolve().parent.parent
kit.OUT = ROOT / "Art/Environment/Dressing/Meshes"
Mesh, add, mul, rot_x, rot_y, rot_z, mm, mv = kit.Mesh, kit.add, kit.mul, kit.rot_x, kit.rot_y, kit.rot_z, kit.mm, kit.mv


def quad(m, a, b, c, d, mat, uvs):
    m.poly([a, b, c, d], mat, uvs)


def two_sided_quad(m, a, b, c, d, mat, uvs):
    quad(m, a, b, c, d, mat, uvs)
    quad(m, d, c, b, a, mat, list(reversed(uvs)))


# ---------------------------------------------------------------- wall pieces
def shop_sign(name, emblem):
    m = Mesh(name)
    # wall plate and bracket arm out along +X, with a curled brace underneath
    m.box((2, 0, 0), (4, 16, 40), "Iron")
    m.beam((0, 0, 8), (120, 0, 8), 5, 5, "Iron")
    m.beam((0, 0, -30), (70, 0, 6), 3, 3, "Iron")
    for x in (40, 110):  # chains / rings
        m.beam((x, 0, 8), (x, 0, -16), 2, 2, "Iron")
    # board hangs in the XZ plane, readable from up and down the street
    m.box((75, 0, -52), (86, 5, 60), "Planks")
    m.box((75, 0, -21), (90, 7, 5), "Timber")
    m.box((75, 0, -83), (90, 7, 5), "Timber")
    if emblem == "boot":  # cobbler
        for s in (-1, 1):
            m.box((70, s * 3.5, -48), (14, 2, 30), "Iron")
            m.box((80, s * 3.5, -60), (34, 2, 10), "Iron")
    elif emblem == "tankard":  # tavern
        for s in (-1, 1):
            m.box((72, s * 3.5, -52), (22, 2, 32), "Iron")
            m.box((88, s * 3.5, -52), (8, 2, 18), "Iron")
            m.box((72, s * 3.5, -34), (26, 2, 5), "Iron")
    else:  # key: locksmith / smith
        for s in (-1, 1):
            m.box((62, s * 3.5, -52), (16, 2, 16), "Iron")
            m.box((82, s * 3.5, -52), (30, 2, 5), "Iron")
            m.box((94, s * 3.5, -58), (5, 2, 12), "Iron")
    return m


def wall_banner(name, cloth, width=90, height=230):
    m = Mesh(name)
    m.box((2, 0, 0), (4, 14, 20), "Iron")
    m.beam((0, 0, 0), (38, 0, 0), 4, 4, "Iron")
    m.beam((32, -width / 2 - 8, 0), (32, width / 2 + 8, 0), 4, 4, "Iron")
    rows = 8
    for i in range(rows):
        z0, z1 = -i * height / rows, -(i + 1) * height / rows
        x0 = 32 + 4 * math.sin(i * .9)
        x1 = 32 + 4 * math.sin((i + 1) * .9)
        w0 = width / 2
        w1 = width / 2
        if i == rows - 1:  # swallowtail
            pts = [(x0, -w0, z0), (x0, w0, z0), (x1, w1, z1), (x1, 0, z1 + height / rows * .7), (x1, -w1, z1)]
            m.poly(pts, cloth, [(p[1] / 100, p[2] / 100) for p in pts])
            m.poly(list(reversed(pts)), cloth, [(p[1] / 100, p[2] / 100) for p in reversed(pts)])
        else:
            two_sided_quad(m, (x0, -w0, z0), (x0, w0, z0), (x1, w1, z1), (x1, -w1, z1), cloth,
                           [(-w0 / 100, z0 / 100), (w0 / 100, z0 / 100), (w1 / 100, z1 / 100), (-w1 / 100, z1 / 100)])
    # an emblem stripe
    m.box((33.5 + 4 * math.sin(2.7), 0, -85), (1.5, width * .7, 16), "Iron")
    return m


def laundry(name, length=380, seed=3):
    rng = random.Random(seed)
    m = Mesh(name)
    for s in (-1, 1):  # wall hooks
        m.box((2, s * length / 2, 0), (4, 6, 12), "Iron")
        m.beam((0, s * length / 2, 0), (28, s * length / 2, 0), 2.5, 2.5, "Iron")
    sag = lambda y: -22 * (1 - (2 * y / length) ** 2)
    n = 12
    for i in range(n):
        y0, y1 = -length / 2 + length * i / n, -length / 2 + length * (i + 1) / n
        m.beam((28, y0, sag(y0)), (28, y1, sag(y1)), 1.5, 1.5, "Sack")
    y = -length / 2 + 30
    mats = ["Canvas", "CanvasAlt", "Sack", "Cloth", "CanvasAlt", "Cloth", "Sack"]
    k = 0
    while y < length / 2 - 60:
        w = rng.uniform(45, 85)
        h = rng.uniform(55, 110)
        mat = mats[k % len(mats)]
        k += 1
        top0, top1 = sag(y) - 1, sag(y + w) - 1
        rows = 4
        for r in range(rows):
            za0, zb0 = top0 - h * r / rows, top1 - h * r / rows
            za1, zb1 = top0 - h * (r + 1) / rows, top1 - h * (r + 1) / rows
            xo0 = 28 + 3 * math.sin(r * 1.3 + k)
            xo1 = 28 + 3 * math.sin((r + 1) * 1.3 + k)
            two_sided_quad(m, (xo0, y, za0), (xo0, y + w, zb0), (xo1, y + w, zb1), (xo1, y, za1), mat,
                           [(0, r / rows * h / 100), (w / 100, r / rows * h / 100), (w / 100, (r + 1) / rows * h / 100), (0, (r + 1) / rows * h / 100)])
        m.box((28, y + 4, top0 + 3), (3, 3, 8), "Timber")  # pegs
        m.box((28, y + w - 4, top1 + 3), (3, 3, 8), "Timber")
        y += w + rng.uniform(12, 30)
    return m


def ivy(name, width, height, count, seed):
    """Leaf cards on a wall patch, denser at the bottom, with a few stems."""
    rng = random.Random(seed)
    m = Mesh(name)
    cells = [(c, r) for c in range(2) for r in range(3)]
    for _ in range(count):
        # triangular-ish distribution: wide at the base, climbing in tongues
        z = height * (rng.random() ** 1.6)
        spread = width / 2 * (1 - .55 * z / height)
        y = rng.uniform(-spread, spread)
        s = rng.uniform(16, 30)
        a = math.radians(rng.uniform(-50, 50))
        c, r = rng.choice(cells)
        u0, v0 = c * .5, r / 3
        u1, v1 = u0 + .5, v0 + 1 / 3
        x = rng.uniform(1.5, 9)
        tilt = rng.uniform(.15, .55)  # leaves lean away from the wall at the bottom edge
        corners = [(-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)]
        pts = []
        for cu, cv in corners:
            dy = (cu * math.cos(a) - cv * math.sin(a)) * s
            dz = (cu * math.sin(a) + cv * math.cos(a)) * s
            pts.append((x + (.5 - cv) * s * tilt, y + dy, z + dz))
        uvs = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
        # front faces +X (towards the street); back side for grazing views
        m.poly(pts, "Ivy", uvs)
        m.poly(list(reversed(pts)), "Ivy", list(reversed(uvs)))
    for k in range(5):  # woody stems
        y = rng.uniform(-width * .35, width * .35)
        z = 0
        for _ in range(6):
            y2, z2 = y + rng.uniform(-35, 35), z + height / 6
            m.beam((1.5, y, z), (1.5, y2, z2), 2, 2, "Bark")
            y, z = y2, z2
    return m


def puddle(name, radius, seed):
    rng = random.Random(seed)
    m = Mesh(name)
    n = 22
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        r = radius * (1 + .28 * math.sin(3 * a + seed) + .15 * math.sin(7 * a + seed * 2) + rng.uniform(-.06, .06))
        pts.append((r * math.cos(a) * 1.4, r * math.sin(a), .6))
    centre = (0, 0, .6)
    for i in range(n):
        p, q = pts[i], pts[(i + 1) % n]
        m.poly([centre, p, q], "Puddle", [(0, 0), (p[0] / 100, p[1] / 100), (q[0] / 100, q[1] / 100)])
    return m


def card(name, w, h, mat, vertical=True, z=0.8):
    m = Mesh(name)
    if vertical:  # on a wall, pivot at the top centre of the streak
        pts = [(1.2, -w / 2, -h), (1.2, w / 2, -h), (1.2, w / 2, 0), (1.2, -w / 2, 0)]
    else:
        pts = [(-h / 2, -w / 2, z), (h / 2, -w / 2, z), (h / 2, w / 2, z), (-h / 2, w / 2, z)]
    m.poly(pts, mat, [(0, 1), (1, 1), (1, 0), (0, 0)])
    return m


def smoke(name, height=1100):
    m = Mesh(name)
    rows = 10
    for k in range(3):
        rot = rot_z(k * 60)
        for r in range(rows):
            t0, t1 = r / rows, (r + 1) / rows
            w0, w1 = 70 + 330 * t0 ** .8, 70 + 330 * t1 ** .8
            drift0, drift1 = 160 * t0 ** 1.5, 160 * t1 ** 1.5  # leans downwind (+X) as it rises
            a = add((drift0, 0, height * t0), mv(rot, (0, -w0 / 2, 0)))
            b = add((drift0, 0, height * t0), mv(rot, (0, w0 / 2, 0)))
            c = add((drift1, 0, height * t1), mv(rot, (0, w1 / 2, 0)))
            d = add((drift1, 0, height * t1), mv(rot, (0, -w1 / 2, 0)))
            quad(m, a, b, c, d, "Smoke", [(0, t0), (1, t0), (1, t1), (0, t1)])
    return m


def crow_into(m, origin, yaw, mat, spread, phase, scale=1.0):
    """A low-poly crow: body, head, beak, tail and two wings. Wings spread (flying) or folded."""
    R = rot_z(yaw)
    P = lambda x, y, z: add(origin, mv(R, (x * scale, y * scale, z * scale)))
    uv = lambda lever: (lever, phase)
    body = [P(18, 0, 4), P(0, 7, 2), P(-14, 0, 3), P(0, -7, 2), P(2, 0, 11), P(2, 0, -5)]
    top, bot = body[4], body[5]
    ring = body[:4]
    for i in range(4):
        a, b = ring[i], ring[(i + 1) % 4]
        m.poly([a, b, top], mat, [uv(0)] * 3)
        m.poly([b, a, bot], mat, [uv(0)] * 3)
    head = P(24, 0, 9)
    for s in (-1, 1):
        m.poly([P(15, s * 4, 6), head, P(15, 0, 12)] if s > 0 else [head, P(15, s * 4, 6), P(15, 0, 12)], mat, [uv(0)] * 3)
    m.poly([P(26, 0, 11), P(33, 0, 8), P(26, 0, 7)], mat, [uv(0)] * 3)  # beak
    m.poly([P(26, 0, 7), P(33, 0, 8), P(26, 0, 11)], mat, [uv(0)] * 3)
    tail = [P(-12, -3, 4), P(-12, 3, 4), P(-28, 7, 3), P(-28, -7, 3)]
    m.poly(tail, mat, [uv(0)] * 4)
    m.poly(list(reversed(tail)), mat, [uv(0)] * 4)
    for s in (-1, 1):
        if spread:
            root_f, root_b = P(8, s * 5, 7), P(-6, s * 5, 7)
            mid_f, mid_b = P(6, s * 26, 9), P(-10, s * 28, 8)
            tip = P(-8, s * 46, 7)
            m.poly([root_f, mid_f, mid_b, root_b], mat, [uv(0), uv(.55), uv(.6), uv(0)])
            m.poly([root_b, mid_b, mid_f, root_f], mat, [uv(0), uv(.6), uv(.55), uv(0)])
            m.poly([mid_f, tip, mid_b], mat, [uv(.55), uv(1), uv(.6)])
            m.poly([mid_b, tip, mid_f], mat, [uv(.6), uv(1), uv(.55)])
        else:
            wing = [P(10, s * 6.5, 7), P(-18, s * 6, 6), P(-22, s * 4, 3), P(6, s * 7, 3)]
            m.poly(wing if s > 0 else list(reversed(wing)), mat, [uv(0)] * 4)
    if not spread:  # legs
        for s in (-1, 1):
            m.beam(P(2, s * 3, -3), P(3, s * 3, -12), 1.2, 1.2, mat)


def crow_perched(name):
    m = Mesh(name)
    crow_into(m, (0, 0, 12), 0, "Crow", False, 0)
    return m


def crow_flock(name, seed=5):
    """Birds spaced round a ring about the pivot; M_DressCrow orbits them and flaps the wings."""
    rng = random.Random(seed)
    m = Mesh(name)
    for i in range(6):
        a = 2 * math.pi * i / 6 + rng.uniform(-.3, .3)
        r = rng.uniform(550, 950)
        z = rng.uniform(-120, 160)
        pos = (r * math.cos(a), r * math.sin(a), z)
        # tangent heading for counter-clockwise flight
        crow_into(m, pos, math.degrees(a) + 90, "CrowFlight", True, rng.random(), rng.uniform(1.0, 1.25))
    return m


def wall_torch(name):
    m = Mesh(name)
    m.box((2, 0, 0), (4, 14, 30), "Iron")
    m.beam((0, 0, -4), (22, 0, -4), 3, 3, "Iron")
    m.cylinder((24, 0, -14), 6, 14, "Iron", n=8, top=False, bottom=True)  # cup
    m.cylinder((24, 0, -40), 3, 40, "Timber", n=6, rot=rot_y(-12))
    m.cylinder((24, 0, 0), 5.5, 8, "Sack", n=8)  # pitch-soaked rag
    m.cone((24, 0, 8), 7, 26, "Flame", n=8)
    m.cone((24, 0, 8), 4, 36, "Flame", n=6, rot=rot_z(30))
    return m


def weapon_rack(name):
    m = Mesh(name)
    for s in (-1, 1):
        m.beam((0, s * 100, 0), (0, s * 100, 170), 12, 12, "Timber")
        m.beam((-35, s * 100, 0), (0, s * 100, 150), 8, 8, "Timber")
        m.beam((35, s * 100, 0), (0, s * 100, 150), 8, 8, "Timber")
    m.box((0, 0, 160), (10, 216, 10), "Timber")
    m.box((0, 0, 40), (50, 216, 6), "Planks")
    for k in range(7):
        y = -84 + k * 28
        m.box((6, y, 150), (6, 3, 18), "Iron")
    return m


def hay_pile(name, seed=2):
    rng = random.Random(seed)
    m = Mesh(name)
    for _ in range(9):
        c = (rng.uniform(-60, 60), rng.uniform(-45, 45), 0)
        m.cone(c, rng.uniform(35, 70), rng.uniform(25, 55), "Thatch", n=9, rot=rot_z(rng.uniform(0, 90)))
    for _ in range(14):  # stray stalks
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(60, 110)
        m.box((r * math.cos(a), r * math.sin(a), 1), (40, 3, 1.5), "Thatch", rot_z(rng.uniform(0, 180)))
    return m


BUILDERS = {
    "SM_Dress_ShopSignBoot": lambda: shop_sign("SM_Dress_ShopSignBoot", "boot"),
    "SM_Dress_ShopSignTankard": lambda: shop_sign("SM_Dress_ShopSignTankard", "tankard"),
    "SM_Dress_ShopSignKey": lambda: shop_sign("SM_Dress_ShopSignKey", "key"),
    "SM_Dress_WallBannerRed": lambda: wall_banner("SM_Dress_WallBannerRed", "BannerCloth"),
    "SM_Dress_WallBannerPale": lambda: wall_banner("SM_Dress_WallBannerPale", "CanvasAlt", 80, 200),
    "SM_Dress_Laundry": lambda: laundry("SM_Dress_Laundry"),
    "SM_Dress_LaundryShort": lambda: laundry("SM_Dress_LaundryShort", 260, seed=8),
    "SM_Dress_IvyTall": lambda: ivy("SM_Dress_IvyTall", 240, 420, 360, 4),
    "SM_Dress_IvyLow": lambda: ivy("SM_Dress_IvyLow", 380, 190, 280, 9),
    "SM_Dress_PuddleA": lambda: puddle("SM_Dress_PuddleA", 70, 1),
    "SM_Dress_PuddleB": lambda: puddle("SM_Dress_PuddleB", 110, 4),
    "SM_Dress_GrimeStreak": lambda: card("SM_Dress_GrimeStreak", 260, 130, "Grime", vertical=True),
    "SM_Dress_LeafLitter": lambda: card("SM_Dress_LeafLitter", 260, 260, "Litter", vertical=False),
    "SM_Dress_Smoke": lambda: smoke("SM_Dress_Smoke"),
    "SM_Dress_CrowPerched": lambda: crow_perched("SM_Dress_CrowPerched"),
    "SM_Dress_CrowFlock": lambda: crow_flock("SM_Dress_CrowFlock"),
    "SM_Dress_WallTorch": lambda: wall_torch("SM_Dress_WallTorch"),
    "SM_Dress_WeaponRack": lambda: weapon_rack("SM_Dress_WeaponRack"),
    "SM_Dress_HayPile": lambda: hay_pile("SM_Dress_HayPile"),
}


def main():
    rows = [BUILDERS[name]().save() for name in BUILDERS]
    (kit.OUT / "DressingMeshes.json").write_text(json.dumps({"generator": "Tools/BuildDressingMeshes.py", "units": "cm", "up": "Z",
                                                             "front": "+X", "meshes": rows}, indent=2) + "\n", encoding="utf-8")
    for r in rows:
        print(f"{r['name']:28s} tris={r['triangles']:6d} mats={','.join(r['materials'])} bounds={r['boundsMin']}..{r['boundsMax']}")
    print(f"CIRE_DRESSING_MESHES_PASS meshes={len(rows)} triangles={sum(r['triangles'] for r in rows)}")


if __name__ == "__main__":
    main()
