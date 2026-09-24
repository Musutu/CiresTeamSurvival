"""Author the original arena mesh kit as OBJ sources (Art/Arenas/Meshes).

Pure Python; never launches Unreal. Every mesh is centred in XY with its base at Z=0
and faces +X, so Arenas.json yaw alone orients it. Material slot names map to material
instances in Tools/ImportArenaContent.py. Deterministic (fixed seeds): re-running writes
identical files. No third-party geometry.

Usage:  python Tools/BuildArenaMeshes.py [--only name,name]
"""
from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ArenaMeshKit import (IDENT, Mesh, Noise, add, cross, dot, frame, length, lerp, mm, mul, mv, norm, rot_x, rot_y, rot_z,  # noqa: E402
                          sub)

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Art/Arenas/Meshes"
BUILDERS = {}


def mesh(fn):
    BUILDERS[fn.__name__] = fn
    return fn


# ================================================================ shared helpers
def superellipsoid(m, mat, center, size, e=0.35, noise=None, amp=0.0, level=3, rot=IDENT, seed=1):
    """Rounded box (e -> 0 is a box, 1 is an ellipsoid) with optional fluffy/rocky noise."""
    n = Noise(seed)
    verts, faces = m.icosphere(level)
    idx = []
    for v in verts:
        p = tuple(math.copysign(abs(c) ** e, c) for c in v)
        k = 1.0 + (n.fbm((v[0] * 2.1 + seed, v[1] * 2.1, v[2] * 2.1), 3) * amp if amp else 0.0)
        q = (p[0] * size[0] * .5 * k, p[1] * size[1] * .5 * k, p[2] * size[2] * .5 * k)
        wp = add(center, mv(rot, q))
        idx.append(m.vert(mat, wp, (wp[0] / 100.0 + wp[2] / 170.0, wp[1] / 100.0 + wp[2] / 130.0)))
    for a, b, c in faces:
        m.tri(mat, idx[a], idx[b], idx[c])


def rock(m, mat, center, size, seed=1, rough=0.35, level=4, flat_bottom=True, crag=0.0, rot=IDENT, squash=1.0):
    """Natural boulder: fBm displaced ellipsoid, flattened under ground, optional crag facets."""
    n = Noise(seed)
    def disp(v):
        d = 1.0 + n.fbm((v[0] * 1.3 + seed * 3.1, v[1] * 1.3 - seed, v[2] * 1.3), 5) * rough
        if crag:
            d += crag * (abs(n.noise(v[0] * 3 + 11, v[1] * 3, v[2] * 3)) - 0.3)
        return max(0.45, d)
    m.blob(mat, center, (size[0], size[1], size[2] * squash), level=level, displace=disp, rot=rot,
           flat_bottom=(-size[2] * 0.5 * squash + size[2] * 0.08) if flat_bottom else None)


def hexprism(m, mat, base, radius, height, top_tilt=(0.0, 0.0), rot_deg=0.0, bevel=3.0):
    """Basalt column: a hexagonal prism with a tilted, slightly bevelled top."""
    pts = [(base[0] + radius * math.cos(math.radians(rot_deg + 60 * i)), base[1] + radius * math.sin(math.radians(rot_deg + 60 * i))) for i in range(6)]
    tz = lambda x, y: base[2] + height + (x - base[0]) * top_tilt[0] + (y - base[1]) * top_tilt[1]
    inner = [(base[0] + (radius - bevel) * math.cos(math.radians(rot_deg + 60 * i)), base[1] + (radius - bevel) * math.sin(math.radians(rot_deg + 60 * i))) for i in range(6)]
    for i in range(6):
        j = (i + 1) % 6
        a, b = pts[i], pts[j]
        m.poly(mat, [(a[0], a[1], base[2]), (b[0], b[1], base[2]), (b[0], b[1], tz(*b) - bevel), (a[0], a[1], tz(*a) - bevel)])
        ia, ib = inner[i], inner[j]
        m.poly(mat, [(a[0], a[1], tz(*a) - bevel), (b[0], b[1], tz(*b) - bevel), (ib[0], ib[1], tz(*ib)), (ia[0], ia[1], tz(*ia))])
    m.poly(mat, [(p[0], p[1], tz(*p)) for p in inner])


def hex_cluster(m, mat, seed, count, radius, spread, hmin, hmax, center=(0, 0), falloff=0.0, tilt=0.06):
    """Hex-packed basalt columns; heights fall off from the centre when falloff > 0."""
    rnd = random.Random(seed)
    placed = []
    dx = radius * math.sqrt(3) * 1.02
    rings = int(spread / dx) + 2
    cells = []
    for q in range(-rings, rings + 1):
        for r in range(-rings, rings + 1):
            x = dx * (q + r * 0.5)
            y = radius * 1.53 * r
            if math.hypot(x, y) <= spread:
                cells.append((x, y))
    rnd.shuffle(cells)
    cells.sort(key=lambda c: math.hypot(*c))
    for (x, y) in cells[:count]:
        d = math.hypot(x, y) / max(spread, 1)
        h = rnd.uniform(hmin, hmax) * (1 - falloff * d)
        hexprism(m, mat, (center[0] + x, center[1] + y, 0), radius * rnd.uniform(0.93, 1.0), max(20, h),
                 (rnd.uniform(-tilt, tilt), rnd.uniform(-tilt, tilt)), rot_deg=rnd.uniform(-4, 4))
        placed.append((x, y, h))
    return placed


def wheel(m, center, radius, width, axis_y=True, spokes=10, tyre="Iron", wood="Timber"):
    rot = rot_x(90) if axis_y else IDENT
    m.lathe(tyre, [(radius, -width / 2), (radius + 2.5, -width / 2 + 2), (radius + 2.5, width / 2 - 2), (radius, width / 2)], sides=20, center=center, rot=rot)
    m.lathe(wood, [(radius - 1, -width / 2 + 1), (radius - 7, -width / 2 + 1), (radius - 7, width / 2 - 1), (radius - 1, width / 2 - 1)], sides=20, center=center, rot=rot)
    m.lathe(wood, [(0.1, -width * 0.9), (9, -width * 0.9), (9, width * 0.9), (0.1, width * 0.9)], sides=10, center=center, rot=rot)
    for k in range(spokes):
        a = 2 * math.pi * k / spokes
        d = (math.cos(a), 0, math.sin(a)) if axis_y else (math.cos(a), math.sin(a), 0)
        m.beam(wood, add(center, mul(d, 8)), add(center, mul(d, radius - 6)), 4.5, 4.5, up=(0, 1, 0) if axis_y else (0, 0, 1))


def stone_course(m, mat, length_, depth, height, seed, top=True, taper=0.12):
    """Dry-stone wall body along Y built from jittered rough stones."""
    rnd = random.Random(seed)
    z = 0.0
    course = 0
    while z < height - 6:
        h = rnd.uniform(14, 22)
        y = -length_ / 2 + (course % 2) * 12
        d = depth * (1 - taper * z / height)
        while y < length_ / 2:
            w = min(rnd.uniform(24, 44), length_ / 2 - y)
            if w < 8:
                break
            for side in (-1, 1):
                superellipsoid(m, mat, (side * d * 0.25 + rnd.uniform(-2, 2), y + w / 2, z + h / 2),
                               (d * 0.52, w - 1.5, h - 1), e=0.3, amp=0.12, level=1, seed=rnd.randint(1, 9999),
                               rot=rot_z(rnd.uniform(-4, 4)))
            y += w
        z += h * 0.96
        course += 1
    if top:
        y = -length_ / 2 + 8
        while y < length_ / 2 - 8:
            w = rnd.uniform(12, 18)
            superellipsoid(m, mat, (0, y, z + 14), (depth * 0.8, w, 30), e=0.35, amp=0.15, level=1, seed=rnd.randint(1, 9999),
                           rot=mm(rot_z(rnd.uniform(-5, 5)), rot_x(rnd.uniform(-8, 8))))
            y += w + 1
    return z


# ================================================================ THE SUNLIT FIELDS
def round_bale(m, c, radius=80, length_=150, seed=1):
    """Round hay bale lying along Y, centre c (axis height = radius)."""
    n = Noise(seed)
    prof = [(radius * 0.74, 0), (radius * 0.9, 2), (radius * 0.97, 8), (radius, 22), (radius * 1.01, length_ / 2),
            (radius, length_ - 22), (radius * 0.97, length_ - 8), (radius * 0.9, length_ - 2), (radius * 0.74, length_)]
    rot = rot_x(90)  # lathe Z -> world -Y
    fluff = lambda t, z, r: r * (1 + 0.018 * n.noise(math.cos(t) * 2.5, math.sin(t) * 2.5, z / 40.0))
    rows = []
    sides = 28
    # side: texture V follows the circumference (straw wraps around the bale)
    circ = 2 * math.pi * radius / 100.0
    grid_rows, uvs = [], []
    for (r, z) in prof:
        row, ur = [], []
        for j in range(sides + 1):
            t = 2 * math.pi * j / sides
            rr = fluff(t, z, r)
            row.append(add(c, mv(rot, (rr * math.cos(t), rr * math.sin(t), z - length_ / 2))))
            ur.append((z / 100.0 * 1.4, circ * j / sides))
        grid_rows.append(row)
        uvs.append(ur)
    m.grid("Hay", grid_rows, uvs)
    # ends: slightly domed spiral faces (concentric straw)
    for end, zz in ((0, 0.0), (1, length_)):
        rings = [(radius * 0.74, 0.0), (radius * 0.55, 1.5), (radius * 0.35, 2.5), (radius * 0.15, 3.0), (0.5, 3.2)]
        rows2, uv2 = [], []
        for (r, dz) in rings:
            row, ur = [], []
            for j in range(sides + 1):
                t = 2 * math.pi * j / sides
                z = zz + (dz if end == 1 else -dz)
                row.append(add(c, mv(rot, (r * math.cos(t), r * math.sin(t), z - length_ / 2))))
                ur.append((r / 100.0 * 2.2 + 0.07 * math.sin(t * 3), t * max(r, 8) / 100.0))
            rows2.append(row)
            uv2.append(ur)
        m.grid("HayEnd", rows2, uv2, flip=(end == 0))


@mesh
def SM_Arena_HayBaleRound():
    m = Mesh("SM_Arena_HayBaleRound")
    round_bale(m, (0, 0, 80), seed=3)
    return m


def square_bale(m, c, size=(110, 55, 45), rot_deg=0.0, seed=1):
    rot = rot_z(rot_deg)
    superellipsoid(m, "Hay", c, size, e=0.22, amp=0.035, level=3, rot=rot, seed=seed)
    for off in (-0.25, 0.25):  # twine loops
        for k in range(8):
            a0, a1 = 2 * math.pi * k / 8, 2 * math.pi * (k + 1) / 8
            p = lambda a: add(c, mv(rot, (off * size[0], math.cos(a) * size[1] * 0.515, math.sin(a) * size[2] * 0.515)))
            m.beam("Twine", p(a0), p(a1), 1.2, 1.2, up=mv(rot, (1, 0, 0)))


@mesh
def SM_Arena_HayBaleSquare():
    m = Mesh("SM_Arena_HayBaleSquare")
    square_bale(m, (0, 0, 22.5), seed=5)
    return m


@mesh
def SM_Arena_HayStack():
    """Square bales in brick bond: 3 long x 2 deep x 5 high (230 cm) - a tall line-of-sight wall."""
    m = Mesh("SM_Arena_HayStack")
    rnd = random.Random(11)
    for layer in range(5):
        z = 22.5 + layer * 45
        for row in (-1, 1):
            shift = (layer % 2) * 0.5
            for i in range(3 if layer % 2 == 0 else 2):
                y = (i - 1 + shift) * 112 + (0 if layer % 2 == 0 else 0)
                if layer == 4 and row == 1 and i == 0:
                    continue  # a missing top bale breaks the silhouette
                square_bale(m, (row * 28 + rnd.uniform(-3, 3), y + rnd.uniform(-4, 4), z), (110, 55, 45), 90 + rnd.uniform(-3, 3), seed=rnd.randint(1, 999))
    return m


@mesh
def SM_Arena_HayPyramid():
    """Three round bales stacked 2 + 1 (about 300 cm high) - the signature field blocker."""
    m = Mesh("SM_Arena_HayPyramid")
    round_bale(m, (-82, 0, 80), seed=21)
    round_bale(m, (82, 4, 80), seed=22)
    round_bale(m, (0, -3, 80 + 139), seed=23)
    return m


@mesh
def SM_Arena_Stook():
    """Eight wheat sheaves leaning together in a tent (about 150 cm)."""
    m = Mesh("SM_Arena_Stook")
    rnd = random.Random(7)
    n = Noise(9)
    for i in range(8):
        row = -1 if i < 4 else 1
        y = (i % 4 - 1.5) * 26
        lean = rot_x(0)
        base = (row * 24, y, 0)
        axis = norm((-row * 0.22, rnd.uniform(-0.05, 0.05), 1.0))
        u, v, w = frame(axis)
        h = rnd.uniform(140, 155)
        prof = [(17, 0), (15, 20), (11, h * 0.5), (9.5, h * 0.55), (11, h * 0.62), (17, h * 0.8), (19, h * 0.9), (12, h * 0.97), (3, h)]
        sides = 10
        rows, uvs = [], []
        for (r, z) in prof:
            row, ur = [], []
            for j in range(sides + 1):
                t = 2 * math.pi * j / sides
                rr = r * (1 + 0.1 * n.noise(math.cos(t) * 2 + i, math.sin(t) * 2, z / 30))
                p = add(base, add(add(mul(u, rr * math.cos(t)), mul(v, rr * math.sin(t))), mul(w, z)))
                row.append(p)
                ur.append((j / sides * 0.8, z / 100.0))
            rows.append(row)
            uvs.append(ur)
        m.grid("Straw", rows[:6], uvs[:6])
        m.grid("Ears", rows[5:], uvs[5:])
        # binding band
        m.lathe("Twine", [(10.2, h * 0.5), (10.2, h * 0.56)], sides=10, center=base, rot=((u[0], v[0], w[0]), (u[1], v[1], w[1]), (u[2], v[2], w[2])))
    return m


@mesh
def SM_Arena_Scarecrow():
    m = Mesh("SM_Arena_Scarecrow")
    m.box("Timber", (0, 0, 115), (11, 11, 230))
    m.beam("Timber", (0, -95, 168), (0, 95, 172), 9, 9)
    # coat body (tapered, slightly flared hem) and sleeves
    m.lathe("CoatCloth", [(30, 92), (27, 110), (22, 140), (21, 162), (16, 176), (8, 181)], sides=10, center=(0, 0, 0),
            radial=lambda t, z, r: r * (1.0 + (0.12 * math.sin(t * 5 + z * 0.1) if z < 105 else 0)) * (0.72 if abs(math.sin(t)) < 0.5 else 1.0))
    for s in (-1, 1):
        m.tube("CoatCloth", [(0, s * 14, 170), (0, s * 45, 171), (0, s * 72, 169)], [11, 10, 12], sides=8)
        for k in range(5):  # straw poking from the cuffs
            a = k * 1.3
            m.tube("Hay", [(0, s * 74, 169), (math.cos(a) * 9, s * 86, 166 + math.sin(a) * 9)], [1.6, 0.4], sides=3)
    for k in range(9):  # straw under the hem
        a = 2 * math.pi * k / 9
        m.tube("Hay", [(math.cos(a) * 22, math.sin(a) * 18, 95), (math.cos(a) * 26, math.sin(a) * 22, 78)], [2.2, 0.5], sides=3)
    superellipsoid(m, "Sack", (0, 0, 196), (30, 28, 34), e=0.85, amp=0.08, level=2, seed=4)
    m.lathe("Twine", [(9, 181), (9.5, 184)], sides=8)
    # battered straw hat
    m.lathe("Thatch", [(34, 207), (31, 209), (18, 211), (16, 222), (12, 232), (4, 236)], sides=14,
            radial=lambda t, z, r: r * (1 + 0.06 * math.sin(t * 3)), cap_top=True)
    m.lathe("Thatch", [(34, 206.5), (18, 206.5)], sides=14)
    return m


@mesh
def SM_Arena_StoneWall():
    """Dry-stone field wall, 400 long (Y), about 125 high: cover, not a sight blocker."""
    m = Mesh("SM_Arena_StoneWall")
    stone_course(m, "FieldStone", 400, 70, 110, seed=31)
    return m


@mesh
def SM_Arena_RuinWall():
    """Ruined mortared wall, 500 long, 190-280 high with a jagged top: a sight blocker."""
    m = Mesh("SM_Arena_RuinWall")
    n = Noise(41)
    cols = 26
    L, D = 500.0, 70.0
    tops = [230 + 45 * n.fbm((i * 0.23, 3.1, 0.5), 3) + (35 if 8 < i < 13 else 0) for i in range(cols + 1)]
    for side in (-1, 1):
        rows, uvs = [], []
        for zi in range(7):
            row, ur = [], []
            for i in range(cols + 1):
                y = -L / 2 + L * i / cols
                zt = tops[i] * zi / 6
                x = side * (D / 2 - 4 * zi / 6) + 2.5 * n.noise(y / 40, zt / 40, side)
                row.append((x, y, zt))
                ur.append((y / 100.0, zt / 100.0))
            rows.append(row)
            uvs.append(ur)
        m.grid("RuinStone", rows, uvs, flip=(side == 1))
    for i in range(cols):  # jagged top
        y0, y1 = -L / 2 + L * i / cols, -L / 2 + L * (i + 1) / cols
        m.poly("RuinStone", [(-D / 2 + 4, y0, tops[i]), (D / 2 - 4, y0, tops[i]), (D / 2 - 4, y1, tops[i + 1]), (-D / 2 + 4, y1, tops[i + 1])])
    for s in (-1, 1):
        m.poly("RuinStone", [(-D / 2, s * L / 2, 0), (D / 2, s * L / 2, 0), (D / 2 - 4, s * L / 2, tops[0 if s < 0 else cols]), (-D / 2 + 4, s * L / 2, tops[0 if s < 0 else cols])], flip=(s < 0))
    rnd = random.Random(42)
    for k in range(10):  # fallen rubble at the foot
        s = rnd.choice((-1, 1))
        rock(m, "FieldStone", (s * rnd.uniform(45, 80), rnd.uniform(-230, 230), 12), (rnd.uniform(25, 45), rnd.uniform(25, 45), rnd.uniform(18, 30)), seed=rnd.randint(1, 999), rough=0.25, level=1)
    return m


@mesh
def SM_Arena_HayWagon():
    """Hay wagon with a heaped load (about 430 x 200 x 280): the big central-lane blocker."""
    m = Mesh("SM_Arena_HayWagon")
    m.box("Planks", (0, 0, 98), (400, 170, 12))
    for s in (-1, 1):
        m.box("Timber", (0, s * 88, 104), (410, 8, 24))
        for x in range(-180, 181, 60):  # hay ladders
            m.beam("Timber", (x, s * 90, 110), (x * 1.05, s * 108, 205), 5, 5)
        m.beam("Timber", (-195, s * 108, 205), (195, s * 108, 205), 6, 6)
    for x, r in ((-130, 72), (140, 58)):
        for s in (-1, 1):
            wheel(m, (x, s * 104, r), r, 11)
        m.box("Timber", (x, 0, r), (10, 200, 10))
    m.beam("Timber", (200, -30, 70), (330, -12, 42), 8, 8)
    m.beam("Timber", (200, 30, 70), (330, 12, 42), 8, 8)
    n = Noise(51)
    m.blob("Hay", (0, 0, 175), (440, 230, 190), level=4, displace=lambda v: 1 + 0.06 * n.fbm((v[0] * 3, v[1] * 3, v[2] * 3), 3) - (0.25 * max(0, -v[2]) ** 2),
           flat_bottom=-80)
    return m


@mesh
def SM_Arena_Windmill():
    """Stone tower mill (1350 tall) with a thatched cap; the sails are a separate spinning mesh."""
    m = Mesh("SM_Arena_Windmill")
    m.lathe("MillStone", [(430, 0), (410, 300), (370, 800), (330, 1250), (325, 1300)], sides=24, cap_top=True)
    m.lathe("Thatch", [(380, 1290), (360, 1350), (270, 1500), (150, 1620), (40, 1690), (5, 1700)], sides=24,
            radial=lambda t, z, r: r * (1 + 0.025 * math.sin(t * 9 + z * 0.02)))
    m.box("DarkWood", (410, 0, 120), (30, 150, 240))       # door
    for z in (500, 900):
        m.box("DarkWood", (math.cos(0.6) * 395, math.sin(0.6) * 395, z), (20, 60, 90), rot_z(34))
    m.lathe("Timber", [(28, 0), (28, 140)], sides=10, center=(300, 0, 1460), rot=rot_y(90))  # axle stub
    for k in range(10):  # gallery brackets
        a = 2 * math.pi * k / 10
        m.beam("Timber", (math.cos(a) * 330, math.sin(a) * 330, 1260), (math.cos(a) * 400, math.sin(a) * 400, 1290), 12, 12)
    return m


@mesh
def SM_Arena_WindmillSails():
    """Four lattice sails around the hub at the origin, in the YZ plane (spin about X)."""
    m = Mesh("SM_Arena_WindmillSails")
    m.lathe("Timber", [(45, -30), (45, 30)], sides=12, rot=rot_y(90), cap_top=True, cap_bottom=True)
    for k in range(4):
        a = math.radians(90 * k + 15)
        d = (0, math.cos(a), math.sin(a))
        side = (0, -math.sin(a), math.cos(a))
        m.beam("Timber", mul(d, 30), mul(d, 1150), 18, 18, up=(1, 0, 0))
        # lattice frame on one side of the stock
        for j in range(12):
            r = 250 + j * 75
            m.beam("Timber", add(mul(d, r), mul(side, 8)), add(mul(d, r), mul(side, 190)), 5, 5, up=(1, 0, 0))
        m.beam("Timber", add(mul(d, 250), mul(side, 190)), add(mul(d, 1150), mul(side, 190)), 7, 7, up=(1, 0, 0))
        # furled canvas panels (slightly offset so the lattice shows)
        for j in range(0, 12, 2):
            r0, r1 = 250 + j * 75, 250 + (j + 2) * 75
            p = [add(add(mul(d, r0), mul(side, 12)), (6, 0, 0)), add(add(mul(d, r1), mul(side, 12)), (6, 0, 0)),
                 add(add(mul(d, r1), mul(side, 186)), (6, 0, 0)), add(add(mul(d, r0), mul(side, 186)), (6, 0, 0))]
            m.poly("SailCloth", p)
            m.poly("SailCloth", p, flip=True)
    return m


@mesh
def SM_Arena_Barn():
    """Timber barn (1400 x 900) with plank walls, a thatched roof and big doors on the +X side."""
    m = Mesh("SM_Arena_Barn")
    L, W, H, R = 1400.0, 900.0, 430.0, 980.0
    m.box("FieldStone", (0, 0, 30), (L + 20, W + 20, 60))
    for s in (-1, 1):
        m.box("Planks", (s * (L / 2 - 6), 0, H / 2 + 30), (12, W, H - 40))
        m.box("Planks", (0, s * (W / 2 - 6), H / 2 + 30), (L, 12, H - 40))
        # gable triangles
        m.poly("Planks", [(s * (L / 2 - 6), -W / 2, H + 10), (s * (L / 2 - 6), W / 2, H + 10), (s * (L / 2 - 6), 0, R - 40)], flip=(s < 0))
        for x in range(-600, 601, 200):
            m.box("Timber", (x, s * (W / 2 + 2), H / 2 + 30), (20, 10, H - 40))
    m.box("DarkWood", (L / 2 + 2, 0, 200), (8, 360, 340))
    m.beam("Timber", (L / 2 + 8, -180, 40), (L / 2 + 8, 180, 360), 12, 6, up=(1, 0, 0))
    m.beam("Timber", (L / 2 + 8, 180, 40), (L / 2 + 8, -180, 360), 12, 6, up=(1, 0, 0))
    for s in (-1, 1):  # roof planes (thatch), with an overhang
        a = (-L / 2 - 50, s * (W / 2 + 70), H - 30)
        b = (L / 2 + 50, s * (W / 2 + 70), H - 30)
        c = (L / 2 + 50, 0, R)
        d = (-L / 2 - 50, 0, R)
        pts = [a, b, c, d] if s < 0 else [b, a, d, c]
        m.poly("Thatch", pts)
        m.poly("Thatch", [add(p, (0, 0, -18)) for p in pts], flip=True)
    m.lathe("Thatch", [(26, -L / 2 - 50), (26, L / 2 + 50)], sides=8, center=(0, 0, R - 8), rot=rot_y(90))
    return m


@mesh
def SM_Arena_WheatClump():
    """About 42 wheat stalks with ears in a 90 cm clump. Material 'Wheat': V = height/125, U > 1.5 marks ears."""
    m = Mesh("SM_Arena_WheatClump")
    rnd = random.Random(61)
    HMAX = 125.0
    for i in range(42):
        a, r = rnd.uniform(0, 2 * math.pi), 45 * math.sqrt(rnd.random())
        x, y = r * math.cos(a), r * math.sin(a)
        h = rnd.uniform(92, 122)
        lean = (rnd.uniform(-0.12, 0.12), rnd.uniform(-0.12, 0.12))
        droop = rnd.uniform(0.05, 0.22)
        path = []
        for k in range(6):
            t = k / 5
            path.append((x + lean[0] * h * t + droop * h * t ** 3 * 0.3, y + lean[1] * h * t, h * t))
        m.tube("Wheat", path, [0.9, 0.8, 0.7, 0.6, 0.55, 0.5], sides=3)
        top = path[-1]
        dirv = norm(sub(path[-1], path[-2]))
        ear_len = rnd.uniform(8, 11)
        u, v, w = frame(dirv)
        rows, uvs = [], []
        prof = [(0.6, 0), (1.6, 1.5), (1.9, ear_len * 0.45), (1.5, ear_len * 0.85), (0.3, ear_len)]
        for (rr, zz) in prof:
            row, ur = [], []
            for j in range(6):
                t = 2 * math.pi * j / 5
                p = add(top, add(add(mul(u, rr * math.cos(t) * 1.15), mul(v, rr * math.sin(t) * 0.8)), mul(w, zz)))
                row.append(p)
                ur.append((2.0 + j / 5, 0))
            rows.append(row)
            uvs.append(ur)
        m.grid("Wheat", rows, uvs)
        for k in range(4):  # awns
            base = add(top, mul(w, ear_len * (0.3 + 0.18 * k)))
            tip = add(base, add(mul(w, 9), mul(u if k % 2 else v, 3.5 * (1 if k < 2 else -1))))
            side = mul(u if k % 2 == 0 else v, 0.25)
            idx = [m.vert("Wheat", base, (2.5, 0)), m.vert("Wheat", add(base, side), (2.5, 0)), m.vert("Wheat", tip, (2.5, 0))]
            m.tri("Wheat", *idx)
            m.tri("Wheat", idx[0], idx[2], idx[1])
        if i % 3 == 0:  # a leaf blade low on the stalk
            lp = path[2]
            dirl = norm((math.cos(a + 1), math.sin(a + 1), 0.6))
            side = norm(cross(dirl, (0, 0, 1)))
            pts = [lp, add(add(lp, mul(dirl, 12)), mul(side, 1.2)), add(lp, add(mul(dirl, 26), (0, 0, -6))), add(add(lp, mul(dirl, 12)), mul(side, -1.2))]
            idx = [m.vert("Wheat", p, (0.5, 0)) for p in pts]
            m.quad("Wheat", *idx)
            m.quad("Wheat", idx[0], idx[3], idx[2], idx[1])
    g = m.groups["Wheat"]
    g["uv"] = [(uv[0], max(0.0, min(1.0, p[2] / HMAX)) if uv[0] < 1.5 else min(1.0, p[2] / HMAX) + 0.0) for p, uv in zip(g["p"], g["uv"])]
    return m


@mesh
def SM_Arena_StubbleTuft():
    """Short cut stubble tuft (20 cm) scattered over the harvested arena floor."""
    m = Mesh("SM_Arena_StubbleTuft")
    rnd = random.Random(71)
    for i in range(26):
        a, r = rnd.uniform(0, 2 * math.pi), 22 * math.sqrt(rnd.random())
        x, y = r * math.cos(a), r * math.sin(a)
        h = rnd.uniform(9, 22)
        m.tube("Wheat", [(x, y, 0), (x + rnd.uniform(-2, 2), y + rnd.uniform(-2, 2), h)], [0.8, 0.6], sides=3)
    g = m.groups["Wheat"]
    g["uv"] = [(0.5, 0.15 + p[2] / 125.0) for p in g["p"]]
    return m


# ================================================================ ICELAND - THE BLACK SHORE
@mesh
def SM_Arena_BasaltTall():
    """Tight cluster of tall basalt columns (up to about 430 cm): sight blocker, 300 cm across."""
    m = Mesh("SM_Arena_BasaltTall")
    hex_cluster(m, "Basalt", 101, 19, 32, 150, 300, 430, falloff=0.35)
    return m


@mesh
def SM_Arena_BasaltSteps():
    """Low stepped basalt columns (60-200): cover and perches, 360 cm across."""
    m = Mesh("SM_Arena_BasaltSteps")
    hex_cluster(m, "Basalt", 102, 30, 30, 180, 60, 200, falloff=0.55, tilt=0.03)
    return m


@mesh
def SM_Arena_BasaltWall():
    """Elongated basalt ridge (600 x 170, up to 330)."""
    m = Mesh("SM_Arena_BasaltWall")
    rnd = random.Random(103)
    for i in range(14):
        y = -270 + i * 41 + rnd.uniform(-4, 4)
        for x in (-38, 0, 38):
            if x and rnd.random() < 0.25:
                continue
            h = (330 - abs(y) * 0.28) * rnd.uniform(0.82, 1.0) - abs(x) * 0.8
            hexprism(m, "Basalt", (x + (i % 2) * 18 - 9, y, 0), 25 * rnd.uniform(0.92, 1.0), h, (rnd.uniform(-0.05, 0.05), rnd.uniform(-0.05, 0.05)), rot_deg=90)
    return m


@mesh
def SM_Arena_BasaltStack():
    """Sea stack of giant columns (about 900 x 700 x 2600) for the horizon."""
    m = Mesh("SM_Arena_BasaltStack")
    hex_cluster(m, "Basalt", 104, 34, 70, 380, 1400, 2600, falloff=0.6, tilt=0.1)
    rnd = random.Random(105)
    for k in range(12):
        a = rnd.uniform(0, 2 * math.pi)
        rock(m, "Basalt", (math.cos(a) * 420, math.sin(a) * 340, 60), (rnd.uniform(150, 260),) * 2 + (rnd.uniform(90, 160),), seed=k + 5, level=2)
    return m


@mesh
def SM_Arena_BasaltCliff():
    """Reynisfjara-style basalt cliff (2000 long, about 600 deep, 700-1400 tall): column rows stepping down to the front (-X)."""
    m = Mesh("SM_Arena_BasaltCliff")
    n = Noise(107)
    rnd = random.Random(108)
    r = 42.0
    dx = r * 1.5
    dy = r * math.sqrt(3)
    for col in range(7):  # col 0 is the front row
        x = -260 + col * dx
        y = -1000 + (col % 2) * dy * 0.5
        while y < 1000:
            top = 1050 + 380 * n.fbm((y / 900.0, 0.3, 0.0), 3)
            front = [0.22, 0.45, 0.7, 0.9, 1.0, 1.0, 1.0][col]
            h = max(40.0, top * front * rnd.uniform(0.86, 1.04) + rnd.uniform(-40, 40))
            hexprism(m, "Basalt", (x + rnd.uniform(-3, 3), y, 0), r * rnd.uniform(0.9, 1.0), h,
                     (rnd.uniform(-0.04, 0.04), rnd.uniform(-0.04, 0.04)), rot_deg=rnd.uniform(-6, 6))
            y += dy
    for k in range(18):  # rubble at the foot
        rock(m, "Basalt", (rnd.uniform(-420, -300), rnd.uniform(-960, 960), 20), (rnd.uniform(60, 130),) * 2 + (rnd.uniform(40, 80),),
             seed=rnd.randint(1, 999), rough=0.3, level=1)
    return m


@mesh
def SM_Arena_LavaRockLarge():
    m = Mesh("SM_Arena_LavaRockLarge")
    rock(m, "Lava", (0, 0, 110), (420, 320, 260), seed=111, rough=0.26, crag=0.12)
    return m


@mesh
def SM_Arena_LavaRockMedium():
    m = Mesh("SM_Arena_LavaRockMedium")
    rock(m, "Lava", (0, 0, 65), (220, 170, 150), seed=112, rough=0.28, crag=0.14)
    return m


@mesh
def SM_Arena_LavaOutcrop():
    m = Mesh("SM_Arena_LavaOutcrop")
    rock(m, "Lava", (0, 0, 30), (560, 420, 110), seed=113, rough=0.3, crag=0.1)
    return m


# ================================================================ MOAB - REDROCK CANYON
def strata_rock(m, mat, center, size, seed, rough=0.3, steps=6, level=4, flat_bottom=True):
    """Sandstone lump with horizontal ledges (terraced by quantising the radius per height band)."""
    n = Noise(seed)
    def disp(v):
        z = (v[2] + 1) * 0.5
        band = math.floor(z * steps) / steps
        ledge = 0.06 * math.cos((z * steps - math.floor(z * steps)) * math.pi)
        return max(0.5, 1 + n.fbm((v[0] * 1.4 + seed, v[1] * 1.4, band * 3), 4) * rough + ledge)
    m.blob(mat, center, size, level=level, displace=disp, flat_bottom=(-size[2] * 0.46) if flat_bottom else None)


@mesh
def SM_Arena_SandstoneArch():
    """Natural arch (1200 wide, 820 tall, 320 deep); the legs are the blockers, you can walk under the span."""
    m = Mesh("SM_Arena_SandstoneArch")
    strata_rock(m, "Sandstone", (-440, 0, 240), (320, 330, 520), 201, steps=5)
    strata_rock(m, "Sandstone", (440, 0, 250), (340, 310, 540), 202, steps=5)
    n = Noise(203)
    path, radii = [], []
    for k in range(17):
        t = k / 16
        a = math.pi * (1 - t)
        path.append((math.cos(a) * 470, 0, 380 + math.sin(a) * 330))
        radii.append(110 - 35 * math.sin(math.pi * t) + 15 * n.noise(t * 4, 0.3, 0.7))
    m.tube("Sandstone", path, radii, sides=14)
    strata_rock(m, "Sandstone", (0, 0, 745), (560, 280, 150), 204, steps=3, level=3, flat_bottom=False)
    return m


@mesh
def SM_Arena_Hoodoo():
    """Tall hoodoo (about 620): stacked terraced lumps under a harder cap rock."""
    m = Mesh("SM_Arena_Hoodoo")
    rnd = random.Random(211)
    z = 0
    w = 250
    for k in range(4):
        h = rnd.uniform(130, 170)
        strata_rock(m, "Sandstone", (rnd.uniform(-10, 10), rnd.uniform(-10, 10), z + h / 2), (w, w * 0.9, h * 1.15), 212 + k, steps=2, level=3, flat_bottom=(k == 0))
        z += h * 0.92
        w *= 0.82
    strata_rock(m, "SandstoneCap", (0, 0, z + 30), (w * 1.9, w * 1.7, 90), 220, steps=1, level=3, flat_bottom=False)
    return m


@mesh
def SM_Arena_SandstoneBlock():
    """Fallen angular sandstone block (300 x 240 x 210)."""
    m = Mesh("SM_Arena_SandstoneBlock")
    superellipsoid(m, "Sandstone", (0, 0, 100), (300, 240, 220), e=0.28, amp=0.12, level=4, seed=231, rot=rot_z(8))
    return m


@mesh
def SM_Arena_CanyonWall():
    """Terraced canyon wall (3000 long, about 700 deep, about 2000 tall): a closed volume, face toward -X."""
    m = Mesh("SM_Arena_CanyonWall")
    n = Noise(241)
    L, H = 3000.0, 2000.0
    cols, rows_ = 60, 40
    rows, uvs = [], []
    for zi in range(rows_ + 1):
        z = H * zi / rows_
        band = math.floor(z / 230.0)
        frac = z / 230.0 - band
        row, ur = [], []
        for i in range(cols + 1):
            y = -L / 2 + L * i / cols
            # each band steps back; a hard ledge at the top of every band and vertical joints between blocks
            recess = 70 * band + 110 * n.fbm((y / 700.0, band * 0.55, 1.3), 3)
            ledge = -38 * (1 - smoothstep(0.0, 0.18, frac)) + 22 * smoothstep(0.75, 1.0, frac)
            joint = 55 * max(0.0, abs(n.noise(y / 160.0, band * 1.7, 2.0)) - 0.25)
            x = -350 + recess + ledge + joint + 38 * n.fbm((y / 140.0, z / 140.0, 4.0), 4)
            row.append((x, y, z + 14 * n.noise(y / 300.0, 2.2, band)))
            ur.append((y / 100.0, z / 100.0))
        rows.append(row)
        uvs.append(ur)
    m.grid("Sandstone", rows, uvs, flip=True)
    top = rows[-1]
    for i in range(cols):  # flat mesa top
        a, b = top[i], top[i + 1]
        m.poly("Sandstone", [a, b, (350, b[1], b[2]), (350, a[1], a[2])])
    for i in range(cols):  # back face (so the wall never reads as a paper shell from behind)
        y0, y1 = -L / 2 + L * i / cols, -L / 2 + L * (i + 1) / cols
        m.poly("Sandstone", [(350, y1, 0), (350, y0, 0), (350, y0, top[i][2]), (350, y1, top[i + 1][2])])
    for s, idx in ((-1, 0), (1, cols)):  # end caps
        col = [r[idx] for r in rows]
        pts = [(350, col[0][1], 0)] + col + [(350, col[-1][1], col[-1][2])]
        m.poly("Sandstone", pts, flip=(s > 0))
    return m


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ================================================================ HORNBEAM GLADE
@mesh
def SM_Arena_FallenLog():
    """Fallen trunk (720 long, about 95 thick) with snapped branches."""
    m = Mesh("SM_Arena_FallenLog")
    n = Noise(301)
    path = [(0, -360 + 72 * k, 46 + 4 * math.sin(k * 0.9)) for k in range(11)]
    radii = [48 - 1.2 * k + 3 * n.noise(k * 0.5, 1, 2) for k in range(11)]
    m.tube("Bark", path, radii, sides=14, twist=0.05)
    for end, cz in ((0, 0), (10, 10)):
        c = path[end]
        r = radii[end]
        pts = [add(c, (math.cos(2 * math.pi * j / 14) * r * 0.97, 0, math.sin(2 * math.pi * j / 14) * r * 0.97)) for j in range(14)]
        m.poly("LogEnd", pts, flip=(end == 10))
    rnd = random.Random(302)
    for k in (2, 5, 7):
        base = path[k]
        a = rnd.uniform(0.4, 1.2) * rnd.choice((-1, 1))
        tip = add(base, (math.sin(a) * 140, rnd.uniform(-40, 40), math.cos(a) * 90))
        m.tube("Bark", [base, lerp(base, tip, 0.5), tip], [14, 10, 5], sides=8)
    return m


@mesh
def SM_Arena_RootPlate():
    """Upturned root plate of a toppled tree (about 380 tall, 120 thick): a sight blocker."""
    m = Mesh("SM_Arena_RootPlate")
    n = Noise(311)
    m.blob("RootEarth", (0, 0, 175), (110, 400, 370), level=4,
           displace=lambda v: 1 + 0.18 * n.fbm((v[0] * 2 + 3, v[1] * 2, v[2] * 2), 4) - 0.25 * max(0.0, v[0]) ** 2, flat_bottom=-160)
    rnd = random.Random(312)
    for k in range(16):
        a = rnd.uniform(0, 2 * math.pi)
        start = (rnd.uniform(-20, 20), math.cos(a) * 140, 175 + math.sin(a) * 140)
        end = (rnd.uniform(-90, 40), math.cos(a) * 230, 175 + math.sin(a) * 215)
        m.tube("Bark", [start, lerp(start, end, 0.5), end], [rnd.uniform(7, 12), 5, 1.5], sides=6)
    m.tube("Bark", [(-10, 0, 175), (-60, 5, 150)], [58, 56], sides=14)  # trunk stub (the log is placed separately)
    return m


@mesh
def SM_Arena_MossBoulder():
    m = Mesh("SM_Arena_MossBoulder")
    rock(m, "ForestRock", (0, 0, 120), (380, 300, 280), seed=321, rough=0.22, crag=0.08)
    return m


# ================================================================ THE DROWNED SANCTUM (underwater)
def column(m, mat, base, height, radius=48, broken=0.0, seed=1, flutes=20, capital=True):
    n = Noise(seed)
    shaft_top = height - (55 if capital and not broken else 0)
    m.lathe(mat, [(radius * 1.45, 0), (radius * 1.45, 26), (radius * 1.3, 34), (radius * 1.2, 48), (radius * 1.05, 58)], sides=24, center=base)
    prof = [(radius * 1.05, 58), (radius, 70)] + [(radius * (1 - 0.12 * t), 70 + (shaft_top - 70) * t) for t in (0.25, 0.5, 0.75, 1.0)]
    m.lathe(mat, prof, sides=40, center=base, radial=lambda t, z, r: r * (1 - 0.035 * max(0, math.cos(t * flutes)) ** 2))
    if broken:
        top = shaft_top
        rt = radius * 0.88
        ring = [add(base, (rt * math.cos(2 * math.pi * j / 20), rt * math.sin(2 * math.pi * j / 20), top + broken * n.noise(j * 0.7, seed, 0.3))) for j in range(20)]
        c = add(base, (0, 0, top + broken * 0.3))
        for j in range(20):
            m.poly(mat, [ring[j], ring[(j + 1) % 20], c])
    elif capital:
        m.lathe(mat, [(radius * 0.9, shaft_top), (radius * 1.25, shaft_top + 25), (radius * 1.35, shaft_top + 35)], sides=24, center=base)
        m.box(mat, add(base, (0, 0, height - 10)), (radius * 3.1, radius * 3.1, 20))


@mesh
def SM_Arena_Column():
    m = Mesh("SM_Arena_Column")
    column(m, "Ruin", (0, 0, 0), 620, seed=401)
    return m


@mesh
def SM_Arena_ColumnBroken():
    m = Mesh("SM_Arena_ColumnBroken")
    column(m, "Ruin", (0, 0, 0), 330, broken=45, seed=402)
    return m


@mesh
def SM_Arena_ColumnFallen():
    """Three toppled drums in a line (about 420 long, 100 high): cover."""
    m = Mesh("SM_Arena_ColumnFallen")
    for k, (y, yaw) in enumerate(((-150, 4), (0, -6), (150, 10))):
        rot = mm(rot_z(yaw), rot_x(90))
        m.lathe("Ruin", [(46, -64), (46, 64)], sides=32, center=(0, y, 46), rot=rot, cap_top=True, cap_bottom=True,
                radial=lambda t, z, r: r * (1 - 0.035 * max(0, math.cos(t * 20)) ** 2))
    return m


@mesh
def SM_Arena_RuinArch():
    """Two piers joined by a voussoir arch (760 wide, 700 tall); walk through the middle."""
    m = Mesh("SM_Arena_RuinArch")
    for s in (-1, 1):
        m.box("RuinBlock", (s * 300, 0, 230), (150, 140, 460))
        m.box("RuinBlock", (s * 300, 0, 470), (175, 160, 24))
    for k in range(13):
        a0, a1 = math.pi * k / 13, math.pi * (k + 1) / 13
        am = (a0 + a1) / 2
        c = (math.cos(am) * 300, 0, 480 + math.sin(am) * 170)
        m.box("RuinBlock", c, (68, 140, 60), mm(rot_y(-math.degrees(am) + 90), IDENT))
    return m


@mesh
def SM_Arena_Plinth():
    """Statue plinth with mouldings (160 x 160 x 180)."""
    m = Mesh("SM_Arena_Plinth")
    m.box("Ruin", (0, 0, 15), (170, 170, 30))
    m.box("Ruin", (0, 0, 95), (135, 135, 130))
    m.box("Ruin", (0, 0, 170), (165, 165, 20))
    return m


@mesh
def SM_Arena_Kelp():
    """A kelp clump: six wavy fronds 450-900 tall with blades. Material 'Kelp' sways with height (V)."""
    m = Mesh("SM_Arena_Kelp")
    rnd = random.Random(421)
    for i in range(6):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(0, 40)
        x, y = r * math.cos(a), r * math.sin(a)
        h = rnd.uniform(450, 900)
        segs = 14
        phase = rnd.uniform(0, 6)
        prev = None
        for k in range(segs + 1):
            t = k / segs
            p = (x + 30 * math.sin(t * 5 + phase) * t, y + 25 * math.cos(t * 4 + phase) * t, h * t)
            if prev is not None:
                m.beam("Kelp", prev, p, 3, 3, up=(1, 0, 0))
                if k % 2 == 0:  # blade
                    side = norm((math.cos(phase + k), math.sin(phase + k), 0.0))
                    w = 16 * (1 - 0.4 * t)
                    q = [p, add(p, add(mul(side, w), (0, 0, 25))), add(p, add(mul(side, w * 0.4), (0, 0, 70))), add(p, (0, 0, 8))]
                    idx = [m.vert("Kelp", pt, (0.5, pt[2] / 900.0)) for pt in q]
                    m.quad("Kelp", *idx)
                    m.quad("Kelp", idx[0], idx[3], idx[2], idx[1])
            prev = p
    g = m.groups["Kelp"]
    g["uv"] = [(uv[0], p[2] / 900.0) for p, uv in zip(g["p"], g["uv"])]
    return m


def coral_branch(m, mat, base, direction, length_, radius, depth, rnd):
    tip = add(base, mul(direction, length_))
    mid = add(lerp(base, tip, 0.5), (rnd.uniform(-4, 4), rnd.uniform(-4, 4), 0))
    m.tube(mat, [base, mid, tip], [radius, radius * 0.85, radius * 0.6], sides=6, cap=True)
    if depth > 0:
        for _ in range(2 if depth > 1 else 3):
            d = norm(add(direction, (rnd.uniform(-0.7, 0.7), rnd.uniform(-0.7, 0.7), rnd.uniform(0.1, 0.5))))
            coral_branch(m, mat, tip, d, length_ * rnd.uniform(0.6, 0.8), radius * 0.72, depth - 1, rnd)


@mesh
def SM_Arena_CoralBranch():
    m = Mesh("SM_Arena_CoralBranch")
    rnd = random.Random(431)
    for k in range(5):
        a = 2 * math.pi * k / 5
        coral_branch(m, "Coral", (math.cos(a) * 12, math.sin(a) * 12, 0), norm((math.cos(a) * 0.4, math.sin(a) * 0.4, 1)), 45, 7, 3, rnd)
    return m


@mesh
def SM_Arena_SeaRock():
    m = Mesh("SM_Arena_SeaRock")
    rock(m, "SeaRock", (0, 0, 90), (340, 280, 220), seed=441, rough=0.3, crag=0.12)
    return m


@mesh
def SM_Arena_Mote():
    """A 2 cm octahedron: drifting silt/plankton, animated by the motes material."""
    m = Mesh("SM_Arena_Mote")
    pts = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    for a, b, c in faces:
        m.poly("Mote", [mul(pts[a], 1.0), mul(pts[b], 1.0), mul(pts[c], 1.0)])
    return m


@mesh
def SM_Arena_LightShaft():
    """Open cone for fake light shafts (radius 260 at the bottom, 3200 tall). V: 0 bottom .. 1 top."""
    m = Mesh("SM_Arena_LightShaft")
    rows = m.lathe("Shaft", [(260, 0), (200, 1600), (140, 3200)], sides=20)
    g = m.groups["Shaft"]
    g["uv"] = [(uv[0], p[2] / 3200.0) for p, uv in zip(g["p"], g["uv"])]
    return m


# ================================================================ STAR STATION
@mesh
def SM_Arena_Container():
    """Cargo container (250 wide X, 600 long Y, 260 tall) with corrugated walls, corner posts and door bars on +Y."""
    m = Mesh("SM_Arena_Container")
    L, W, H = 600.0, 250.0, 260.0
    ribs = 30
    for s in (-1, 1):  # corrugated long walls
        for i in range(ribs):
            y0 = -L / 2 + 10 + (L - 20) * i / ribs
            y1 = -L / 2 + 10 + (L - 20) * (i + 1) / ribs
            ym = (y0 + y1) / 2
            x = s * W / 2
            dx = s * 4.5
            for (ya, yb, xa, xb) in ((y0, ym, x, x + dx), (ym, y1, x + dx, x)):
                pts = [(xa, ya, 8), (xb, yb, 8), (xb, yb, H - 8), (xa, ya, H - 8)]
                m.poly("ContainerPaint", pts, flip=(s < 0))
    m.box("ContainerPaint", (0, -L / 2 + 2, H / 2), (W - 12, 4, H - 16))
    m.box("ContainerPaint", (0, L / 2 - 2, H / 2), (W - 12, 4, H - 16))
    m.box("ContainerPaint", (0, 0, H - 4), (W - 10, L - 10, 8))
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box("SteelDark", (sx * (W / 2 - 8), sy * (L / 2 - 8), H / 2), (16, 16, H))
        m.box("SteelDark", (sx * (W / 2 - 8), 0, H - 8), (16, L, 16))
        m.box("SteelDark", (sx * (W / 2 - 8), 0, 8), (16, L, 16))
    for sy in (-1, 1):
        m.box("SteelDark", (0, sy * (L / 2 - 8), H - 8), (W, 16, 16))
        m.box("SteelDark", (0, sy * (L / 2 - 8), 8), (W, 16, 16))
    for x in (-80, -30, 30, 80):
        m.box("SteelDark", (x, L / 2 + 3, H / 2), (5, 6, H - 30))
    m.box("Emissive", (0, L / 2 + 4, H - 30), (W - 60, 3, 6))
    m.box("Emissive", (0, -L / 2 - 4, H - 30), (W - 60, 3, 6))
    return m


@mesh
def SM_Arena_CargoCrate():
    """Sci-fi cargo crate (150 cube) with a bevelled frame and a light strip."""
    m = Mesh("SM_Arena_CargoCrate")
    m.box("CratePanel", (0, 0, 75), (140, 140, 140))
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box("SteelDark", (sx * 70, sy * 70, 75), (14, 14, 150))
        m.box("SteelDark", (sx * 70, 0, 143), (14, 150, 14))
        m.box("SteelDark", (sx * 70, 0, 7), (14, 150, 14))
        m.box("SteelDark", (0, sx * 70, 143), (150, 14, 14))
        m.box("SteelDark", (0, sx * 70, 7), (150, 14, 14))
    for s in (-1, 1):
        m.box("Emissive", (s * 71, 0, 110), (2, 90, 4))
        m.box("Emissive", (0, s * 71, 110), (90, 2, 4))
    return m


@mesh
def SM_Arena_Pillar():
    """Structural pylon (140 x 140 x 1400) with ribs and light strips."""
    m = Mesh("SM_Arena_Pillar")
    m.box("Hull", (0, 0, 700), (110, 110, 1400))
    for z in range(100, 1400, 260):
        m.box("SteelDark", (0, 0, z), (140, 140, 40))
    for s in (-1, 1):
        m.box("Emissive", (s * 56, 0, 700), (3, 12, 1200))
        m.box("Emissive", (0, s * 56, 700), (12, 3, 1200))
    m.box("SteelDark", (0, 0, 20), (170, 170, 40))
    return m


@mesh
def SM_Arena_Barrier():
    """Deck barrier (300 x 60 x 115) with hazard-striped face."""
    m = Mesh("SM_Arena_Barrier")
    m.poly("Hazard", [(30, -150, 10), (30, 150, 10), (18, 150, 115), (18, -150, 115)])
    m.poly("Hazard", [(-30, 150, 10), (-30, -150, 10), (-18, -150, 115), (-18, 150, 115)])
    m.box("SteelDark", (0, 0, 118), (40, 300, 8))
    for s in (-1, 1):
        m.poly("SteelDark", [(-30, s * 150, 10), (30, s * 150, 10), (18, s * 150, 115), (-18, s * 150, 115)], flip=(s < 0))
    m.box("SteelDark", (0, 0, 5), (70, 300, 10))
    return m


@mesh
def SM_Arena_Bulkhead():
    """Hangar wall panel (1000 wide, 80 deep, 1200 tall) with ribs, a viewport frame and light strips."""
    m = Mesh("SM_Arena_Bulkhead")
    m.box("Hull", (0, 0, 600), (60, 1000, 1200))
    for y in range(-500, 501, 250):
        m.box("SteelDark", (35, y, 600), (30, 40, 1200))
    m.box("SteelDark", (35, 0, 1180), (40, 1000, 40))
    m.box("SteelDark", (35, 0, 60), (40, 1000, 120))
    m.box("Emissive", (52, 0, 125), (4, 980, 6))
    m.box("Emissive", (52, 0, 900), (4, 700, 5))
    return m


@mesh
def SM_Arena_Dropship():
    """Parked dropship on struts (1300 long, 900 span, about 480 tall): a central sight blocker."""
    m = Mesh("SM_Arena_Dropship")
    # fuselage: lathe along X
    prof = [(1, -650), (60, -620), (120, -520), (165, -300), (175, 0), (170, 300), (140, 480), (80, 600), (10, 650)]
    m.lathe("Hull", prof, sides=18, center=(0, 0, 260), rot=rot_y(90), radial=lambda t, z, r: r * (1.25 if abs(math.cos(t)) < 0.3 else 1.0) * (0.8 if math.sin(t) < -0.5 else 1.0))
    m.lathe("Glass", [(1, 470), (70, 500), (95, 560), (60, 620), (1, 640)], sides=12, center=(0, 0, 330), rot=rot_y(90))
    for s in (-1, 1):
        m.poly("Hull", [(-260, s * 150, 250), (200, s * 150, 250), (-40, s * 460, 235), (-360, s * 470, 235)], flip=(s < 0))
        m.poly("Hull", [(-260, s * 150, 238), (200, s * 150, 238), (-40, s * 460, 225), (-360, s * 470, 225)], flip=(s > 0))
        m.lathe("SteelDark", [(70, -330), (85, -250), (85, 120), (60, 200)], sides=14, center=(0, s * 330, 250), rot=rot_y(90))
        m.lathe("EngineGlow", [(55, -335), (55, -340)], sides=14, center=(0, s * 330, 250), rot=rot_y(90), cap_bottom=True)
        for x in (-300, 250):
            m.beam("SteelDark", (x, s * 110, 140), (x + 20, s * 150, 0), 14, 14)
            m.box("SteelDark", (x + 20, s * 150, 5), (60, 40, 10))
        m.box("Emissive", (-60, s * 176, 300), (500, 3, 6))
    m.poly("Hull", [(-560, 0, 300), (-640, 0, 300), (-660, 0, 520), (-600, 0, 520)])
    m.poly("Hull", [(-560, 0, 300), (-640, 0, 300), (-660, 0, 520), (-600, 0, 520)], flip=True)
    return m


@mesh
def SM_Arena_StationTower():
    """Distant station spire (1600 wide, 7000 tall) with rings and lit windows, for the skyline."""
    m = Mesh("SM_Arena_StationTower")
    m.lathe("Hull", [(700, 0), (650, 800), (420, 1200), (400, 5200), (250, 6200), (60, 7000)], sides=20)
    for z in (1400, 2600, 3800, 5000):
        m.lathe("SteelDark", [(420, z), (800, z + 60), (800, z + 200), (420, z + 260)], sides=20, cap_top=False)
        m.lathe("Emissive", [(801, z + 110), (801, z + 150)], sides=20)
    for z in range(1500, 5000, 300):
        for k in range(10):
            a = 2 * math.pi * k / 10 + z * 0.001
            m.box("Emissive", (math.cos(a) * 402, math.sin(a) * 402, z), (4, 60, 30), rot_z(math.degrees(a)))
    return m


def main() -> int:
    only = set(sys.argv[sys.argv.index("--only") + 1].split(",")) if "--only" in sys.argv else None
    OUT.mkdir(parents=True, exist_ok=True)
    meta_path = OUT / "ArenaMeshes.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() and only else {"meshes": []}
    rows = {r["name"]: r for r in meta["meshes"]}
    for name, fn in BUILDERS.items():
        if only and name not in only:
            continue
        info = fn().save(OUT)
        rows[name] = info
        print(f"{name}: {info['triangles']} tris, size {info['size']}, materials {info['materials']}")
    meta = {"generator": "Tools/BuildArenaMeshes.py", "units": "cm, Z up, base at 0, faces +X", "meshes": [rows[k] for k in sorted(rows)]}
    meta_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(f"CIRE_ARENA_MESHES_PASS meshes={len(rows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
