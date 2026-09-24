"""Author the original medieval-town mesh kit as OBJ sources.

Pure Python; never launches Unreal. Writes Art/Environment/Town/Meshes/*.obj and
Art/Environment/Town/Meshes/TownMeshes.json. Units are centimetres, Z up. Every
building faces +X (its street facade) with its footprint centred on the origin at
ground level, so layout yaw alone orients it. UVs are in metres (1 UV = 100 cm)
and follow each part's own axes (roof UVs run along the slope), so the tiling
PBR materials in /Game/Environment/Town/Materials never stretch.

Material slot names map to material instances in Tools/ImportTownContent.py.
These are original authored meshes (no third-party geometry).
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Art/Environment/Town/Meshes"


# ---------------------------------------------------------------- vector math
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return (0.0, 0.0, 1.0) if l < 1e-9 else mul(a, 1 / l)


def rot_z(deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def rot_x(deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((1, 0, 0), (0, c, -s), (0, s, c))


def rot_y(deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, 0, s), (0, 1, 0), (-s, 0, c))


IDENT = ((1, 0, 0), (0, 1, 0), (0, 0, 1))


def mm(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def mv(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2])


def basis(u, n):
    """Rotation whose local X=u, Y=n, Z=u x n (both unit, orthogonal)."""
    z = cross(u, n)
    return ((u[0], n[0], z[0]), (u[1], n[1], z[1]), (u[2], n[2], z[2]))


# ---------------------------------------------------------------- mesh builder
class Mesh:
    def __init__(self, name):
        self.name = name
        self.faces = []  # (material, [points], normal, [uvs])

    def poly(self, pts, mat, uvs=None):
        n = (0.0, 0.0, 0.0)
        for i, p in enumerate(pts):  # Newell normal, robust for any planar polygon
            q = pts[(i + 1) % len(pts)]
            n = add(n, ((p[1] - q[1]) * (p[2] + q[2]), (p[2] - q[2]) * (p[0] + q[0]), (p[0] - q[0]) * (p[1] + q[1])))
        n = norm(n)
        if uvs is None:
            ax = max(range(3), key=lambda i: abs(n[i]))
            a, b = [(1, 2), (0, 2), (0, 1)][ax]
            uvs = [(p[a] / 100.0, p[b] / 100.0) for p in pts]
        self.faces.append((mat, list(pts), n, list(uvs)))

    def box(self, c, s, mat, rot=IDENT, uv_offset=None):
        """Oriented box: centre c, size s along the rotation's local axes."""
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        o = uv_offset if uv_offset is not None else ((c[0] * 0.37 + c[2] * 0.11) / 100.0, (c[1] * 0.29 + c[2] * 0.07) / 100.0)
        corner = lambda x, y, z: add(c, mv(rot, (x, y, z)))
        faces = [  # local axis pair used for UVs per face
            ([(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)], (1, 2)),
            ([(-hx, hy, -hz), (-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz)], (1, 2)),
            ([(hx, hy, -hz), (-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz)], (0, 2)),
            ([(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)], (0, 2)),
            ([(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)], (0, 1)),
            ([(-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz), (-hx, -hy, -hz)], (0, 1)),
        ]
        for loc, (a, b) in faces:
            self.poly([corner(*p) for p in loc], mat, [(p[a] / 100.0 + o[0], p[b] / 100.0 + o[1]) for p in loc])

    def beam(self, a, b, w, d, mat, up=(0, 0, 1)):
        """Rectangular beam from a to b (w across, d along 'up'-ish normal)."""
        axis = norm(sub(b, a))
        side = norm(cross(up, axis)) if length(cross(up, axis)) > 1e-6 else (0, 1, 0)
        n = cross(axis, side)
        rot = ((axis[0], side[0], n[0]), (axis[1], side[1], n[1]), (axis[2], side[2], n[2]))
        self.box(mul(add(a, b), .5), (length(sub(b, a)), w, d), mat, rot)

    def cylinder(self, c, r, h, mat, n=16, top=True, bottom=False, r2=None, rot=IDENT):
        r2 = r if r2 is None else r2
        ring = lambda rad, z: [add(c, mv(rot, (rad * math.cos(2 * math.pi * i / n), rad * math.sin(2 * math.pi * i / n), z))) for i in range(n)]
        lo, hi = ring(r, 0), ring(r2, h)
        circ = 2 * math.pi * max(r, r2) / 100.0
        for i in range(n):
            j = (i + 1) % n
            u0, u1 = circ * i / n, circ * (i + 1) / n
            self.poly([lo[i], lo[j], hi[j], hi[i]], mat, [(u0, 0), (u1, 0), (u1, h / 100.0), (u0, h / 100.0)])
        if top and r2 > 0.01:
            self.poly(hi, mat)
        if bottom:
            self.poly(list(reversed(lo)), mat)

    def cone(self, c, r, h, mat, n=16, rot=IDENT):
        apex = add(c, mv(rot, (0, 0, h)))
        ring = [add(c, mv(rot, (r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), 0))) for i in range(n)]
        slant = math.hypot(r, h) / 100.0
        for i in range(n):
            j = (i + 1) % n
            w = 2 * math.pi * r / n / 100.0
            self.poly([ring[i], ring[j], apex], mat, [(i * w, 0), ((i + 1) * w, 0), ((i + .5) * w, slant)])
        self.poly(list(reversed(ring)), mat)

    def save(self):
        OUT.mkdir(parents=True, exist_ok=True)
        lines = ["# Original Cire's Team Survival town geometry. Centimetres, Z up.", f"o {self.name}"]
        vi = 1
        body = []
        mats = sorted({f[0] for f in self.faces})
        tris = 0
        lo = [1e9] * 3
        hi = [-1e9] * 3
        for mat in mats:
            body.append(f"usemtl {mat}")
            for m, pts, n, uvs in self.faces:
                if m != mat:
                    continue
                idx = []
                for p, uv in zip(pts, uvs):
                    lines.append("v %.3f %.3f %.3f" % p)
                    lines.append("vt %.5f %.5f" % uv)
                    lines.append("vn %.5f %.5f %.5f" % n)
                    idx.append(vi)
                    vi += 1
                    for k in range(3):
                        lo[k] = min(lo[k], p[k]); hi[k] = max(hi[k], p[k])
                for k in range(1, len(idx) - 1):
                    body.append("f %d/%d/%d %d/%d/%d %d/%d/%d" % (idx[0], idx[0], idx[0], idx[k], idx[k], idx[k], idx[k + 1], idx[k + 1], idx[k + 1]))
                    tris += 1
        (OUT / f"{self.name}.obj").write_text("\n".join(lines + body) + "\n", encoding="ascii")
        return {"name": self.name, "triangles": tris, "materials": mats,
                "boundsMin": [round(v, 1) for v in lo], "boundsMax": [round(v, 1) for v in hi]}


# ---------------------------------------------------------------- facade helpers
class Wall:
    """A planar facade: origin at its centre on the ground line, U along it, N outward."""

    def __init__(self, mesh, origin, u, n, width):
        self.m, self.o, self.u, self.n, self.w = mesh, origin, u, n, width
        self.rot = basis(u, n)  # local X=u, Y=n, Z=up (u x n must be +Z)
        if cross(u, n)[2] < 0:
            self.rot = basis(mul(u, -1), n)
            self.flip = True
        else:
            self.flip = False

    def at(self, u, z, off):
        return add(add(self.o, mul(self.u, u)), add(mul(self.n, off), (0, 0, z)))

    def panel(self, u, z, w, h, depth, off, mat, tilt=0.0):
        rot = self.rot if not tilt else mm(self.rot, rot_y(tilt if not self.flip else -tilt))
        self.m.box(self.at(u, z, off), (w, depth, h), mat, rot)

    def post(self, u, z0, z1, t, mat, off=4):
        self.panel(u, (z0 + z1) / 2, t, z1 - z0, 10, off, mat)

    def rail(self, u0, u1, z, t, mat, off=4):
        self.panel((u0 + u1) / 2, z, abs(u1 - u0), t, 10, off, mat)

    def brace(self, u0, z0, u1, z1, t, mat, off=4):
        a, b = self.at(u0, z0, off), self.at(u1, z1, off)
        self.m.beam(a, b, 10, t, mat, up=self.n)

    def window(self, u, z, w, h, lit, shutters=True, frame="Timber", sill="Stone"):
        self.panel(u, z, w + 18, h + 18, 12, 3, frame)
        self.panel(u, z, w, h, 6, 6, "GlassLit" if lit else "GlassDark")
        self.panel(u, z, 5, h, 8, 8, frame)            # mullion
        self.panel(u, z + h * .12, w, 5, 8, 8, frame)   # transom
        self.panel(u, z - h / 2 - 14, w + 34, 10, 20, 8, sill)
        if shutters:
            for s in (-1, 1):
                self.panel(u + s * (w / 2 + 9 + w / 4), z, w / 2, h + 6, 5, 7, "Planks")
                self.panel(u + s * (w / 2 + 9 + w / 4), z + h * .25, w / 2 - 4, 4, 7, 9, "Iron")
                self.panel(u + s * (w / 2 + 9 + w / 4), z - h * .25, w / 2 - 4, 4, 7, 9, "Iron")

    def slit(self, u, z, h):
        self.panel(u, z, 16, h, 14, 2, "GlassDark")
        self.panel(u, z + h / 2 + 8, 34, 14, 16, 3, "Castle")

    def door(self, u, w, h, mat="Planks", arch=True, step=True):
        self.panel(u, h / 2, w + 24, h + 20, 14, 3, "Timber")
        self.panel(u, h / 2, w, h, 8, 7, mat)
        for z in (h * .22, h * .75):
            self.panel(u, z, w - 10, 7, 4, 12, "Iron")
        self.panel(u + w * .3, h * .5, 8, 8, 6, 13, "Iron")
        if arch:
            self.panel(u, h + 18, w + 40, 22, 18, 5, "Stone")
        if step:
            self.panel(u, 6, w + 50, 12, 40, 20, "Stone")


def timber_frame(wall, z0, z1, windows, t=16, spacing=115, braces=True, mat="Timber"):
    """Corner posts, rails, studs (skipping window bays) and braces on a wall."""
    half = wall.w / 2
    wall.post(-half + t / 2, z0, z1, t, mat)
    wall.post(half - t / 2, z0, z1, t, mat)
    wall.rail(-half, half, z0 + t / 2, t, mat)
    wall.rail(-half, half, z1 - t / 2, t + 4, mat)
    mid = (z0 + z1) / 2 - 20
    blocked = [(u - w / 2 - 22, u + w / 2 + 22) for u, w in windows]
    free = lambda x: all(not (a < x < b) for a, b in blocked)
    n = max(1, int(wall.w // spacing))
    for i in range(1, n):
        x = -half + wall.w * i / n
        if free(x):
            wall.post(x, z0, z1, t - 3, mat)
    if not windows:
        wall.rail(-half, half, mid, t - 3, mat)
    if braces:
        span = min(90, wall.w * .22)
        wall.brace(-half + t, z0 + t, -half + t + span, z1 - t, t - 4, mat)
        wall.brace(half - t, z0 + t, half - t - span, z1 - t, t - 4, mat)


def gable_roof(m, cx, cy, z0, length_, span, rise, mat, axis="y", overhang=45, thick=16, ridge="Timber", ends="Plaster", end_frame=True):
    """Two slabs meeting at a ridge along 'axis'. Gable walls close each end."""
    half = span / 2
    ang = math.degrees(math.atan2(rise, half))
    slope = math.hypot(half + overhang, rise + overhang * rise / half)
    for s in (-1, 1):
        # slab rotated around the ridge axis; its underside rests on the wall plate
        run = half + overhang
        cz = z0 + rise / 2 - overhang * rise / half / 2 + thick / 2 * math.cos(math.radians(ang))
        if axis == "y":
            centre = (cx + s * run / 2, cy, cz)
            rot = mm(rot_z(90), rot_x(s * ang))  # local X along the ridge (world Y), local Y along the slope
        else:
            centre = (cx, cy + s * run / 2, cz)
            rot = rot_x(-s * ang)
        m.box(centre, (length_ + 2 * overhang, slope, thick), mat, rot)
    # ridge cap
    if axis == "y":
        m.box((cx, cy, z0 + rise + thick * .8), (26, length_ + 2 * overhang + 10, 22), ridge)
    else:
        m.box((cx, cy, z0 + rise + thick * .8), (length_ + 2 * overhang + 10, 26, 22), ridge)
    # gable triangles
    for s in (-1, 1):
        if axis == "y":
            y = cy + s * length_ / 2
            tri = [(cx - half, y, z0), (cx + half, y, z0), (cx, y, z0 + rise)]
        else:
            x = cx + s * length_ / 2
            tri = [(x, cy + half, z0), (x, cy - half, z0), (x, cy, z0 + rise)]
        # outward winding: normal must point along s * axis
        n = cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))
        want = (0, s, 0) if axis == "y" else (s, 0, 0)
        if dot(n, want) < 0:
            tri = [tri[1], tri[0], tri[2]]
        m.poly(tri, ends)
        if end_frame:
            top = (cx, cy + s * (length_ / 2 + 4), z0 + rise - 10) if axis == "y" else (cx + s * (length_ / 2 + 4), cy, z0 + rise - 10)
            base = (cx, cy + s * (length_ / 2 + 4), z0) if axis == "y" else (cx + s * (length_ / 2 + 4), cy, z0)
            m.beam(base, top, 14, 10, "Timber", up=(0, s, 0) if axis == "y" else (s, 0, 0))
            for k in (-1, 1):
                if axis == "y":
                    a = (cx + k * (half - 8), cy + s * (length_ / 2 + 4), z0 + 6)
                else:
                    a = (cx + s * (length_ / 2 + 4), cy + k * (half - 8), z0 + 6)
                m.beam(a, top, 14, 10, "Timber", up=(0, s, 0) if axis == "y" else (s, 0, 0))


def chimney(m, x, y, z_base, height, w=70):
    m.box((x, y, z_base + height / 2), (w, w, height), "Stone")
    m.box((x, y, z_base + height + 8), (w + 20, w + 20, 16), "Stone")
    m.box((x, y, z_base + height + 26), (w - 30, w - 30, 22), "Stone")


def crenellate(m, a, b, z, thick, mat, merlon=70, gap=55, h=75):
    """Merlons along the top of a wall running from a to b (xy)."""
    d = sub(b, a)
    L = length(d)
    u = norm(d)
    yaw = math.degrees(math.atan2(u[1], u[0]))
    n = max(1, int((L + gap) // (merlon + gap)))
    used = n * merlon + (n - 1) * gap
    start = (L - used) / 2 + merlon / 2
    for i in range(n):
        p = add(a, mul(u, start + i * (merlon + gap)))
        m.box((p[0], p[1], z + h / 2), (merlon, thick, h), mat, rot_z(yaw))


# ---------------------------------------------------------------- buildings
def house(name, W, D, floors, roof_mat, ground="Stone", upper="Plaster", jetty=35, roof_axis="y",
          rise=None, lit=(1, 0, 1, 0, 1, 1), chimney_at=(0.25, 0.3), shop=False, sign=False, dormer=False, seed=1,
          door_u=None, back_windows=True):
    rng = random.Random(seed)
    m = Mesh(name)
    lit_cycle = list(lit)
    k = [0]

    def next_lit():
        k[0] += 1
        return lit_cycle[k[0] % len(lit_cycle)]

    m.box((0, 0, 30), (D + 24, W + 24, 60), "Stone")  # plinth
    z = 0
    for fi, h in enumerate(floors):
        grow = jetty if fi > 0 else 0
        d = D + grow * 2 * (1 if fi > 0 else 0) * .5
        fx = grow * .5 if fi > 0 else 0  # upper floors oversail the street facade
        w = W + (18 if fi > 0 else 0)
        mat = ground if fi == 0 else upper
        m.box((fx, 0, z + h / 2), (D + (grow if fi > 0 else 0), w, h), mat)
        if fi > 0:  # jetty joists under the oversail
            for i in range(-int(w // 70) // 2, int(w // 70) // 2 + 1):
                m.box((D / 2 + grow * .5, i * 62, z - 6), (grow + 16, 14, 14), "Timber")
            m.box((D / 2 + grow, 0, z + 4), (16, w + 6, 20), "Timber")
        front = Wall(m, (D / 2 + (grow if fi > 0 else 0), 0, 0), (0, 1, 0), (1, 0, 0), w)
        back = Wall(m, (-D / 2, 0, 0), (0, -1, 0), (-1, 0, 0), w)
        right = Wall(m, (fx, w / 2, 0), (-1, 0, 0), (0, 1, 0), D + (grow if fi > 0 else 0))
        left = Wall(m, (fx, -w / 2, 0), (1, 0, 0), (0, -1, 0), D + (grow if fi > 0 else 0))
        win_h, win_w = min(125, h * .48), 70
        zc = z + h * .52
        for wall, count in ((front, max(1, int(W // 230))), (back, max(1, int(W // 300)) if back_windows else 0),
                            (right, max(1, int(D // 320))), (left, max(1, int(D // 320)))):
            slots = [(-wall.w / 2 + wall.w * (i + .5) / count) for i in range(count)]
            wins = []
            for i, u in enumerate(slots):
                if fi == 0 and wall is front:
                    du = door_u if door_u is not None else (slots[len(slots) // 2] if count % 2 else 0)
                    if abs(u - du) < 110:
                        continue
                if shop and fi == 0 and wall is front:
                    continue
                wins.append((u, win_w))
                wall.window(u, zc, win_w, win_h, next_lit(), shutters=(fi > 0 or ground != "Stone"))
            if mat in ("Plaster",):
                timber_frame(wall, z, z + h, wins, braces=(rng.random() < .8))
        if fi == 0:
            du = door_u if door_u is not None else 0
            front.door(du, 105, 215, arch=(ground == "Stone"))
            if shop:
                # open shop front: counter, posts and a plank awning
                for s in (-1, 1):
                    front.panel(s * (W / 2 - 150), 110, 150, 150, 20, 10, "GlassLit" if s > 0 else "GlassDark")
                    front.panel(s * (W / 2 - 150), 38, 190, 76, 40, 30, "Planks")
                awn = front.at(0, 250, 70)
                m.box(awn, (130, W - 40, 10), "Planks", rot_y(-18))
                for s in (-1, 1):
                    p = front.at(s * (W / 2 - 40), 125, 125)
                    m.box(p, (12, 12, 250), "Timber")
        z += h
    # roof
    top_d = D + jetty
    span = W + 18 if roof_axis == "x" else top_d
    rl = top_d if roof_axis == "x" else W + 18
    r = rise if rise else span * .62
    gable_roof(m, jetty * .5 if roof_axis == "y" else jetty * .5, 0, z, rl, span, r, roof_mat, axis=roof_axis,
               ends="Plaster" if upper == "Plaster" else upper)
    if chimney_at:
        cx = (chimney_at[0] - .5) * D
        cy = (chimney_at[1] - .5) * W
        chimney(m, cx, cy, z - 40, r + 90)
    if dormer:
        # a small front dormer on the street slope
        dz = z + r * .25
        m.box((D / 2 - 40, 0, dz + 70), (90, 120, 140), upper)
        Wall(m, (D / 2 + 5, 0, 0), (0, 1, 0), (1, 0, 0), 120).window(0, dz + 70, 55, 70, True, shutters=False)
        gable_roof(m, D / 2 - 40, 0, dz + 140, 130, 140, 70, roof_mat, axis="x", overhang=15, thick=10, ends=upper, end_frame=False)
    if sign:
        p = (D / 2 + jetty + 5, W / 2 - 60, floors[0] + 40)
        m.beam(p, add(p, (110, 0, 0)), 8, 8, "Iron")
        m.box(add(p, (80, 0, -50)), (80, 8, 60), "Planks")
        m.beam(add(p, (50, 0, 0)), add(p, (50, 0, -22)), 3, 3, "Iron")
        m.beam(add(p, (110, 0, 0)), add(p, (110, 0, -22)), 3, 3, "Iron")
    return m


def stone_house(name, W, D, H, roof_mat, seed=3):
    m = Mesh(name)
    m.box((0, 0, H / 2), (D, W, H), "Stone")
    m.box((0, 0, 20), (D + 20, W + 20, 40), "Stone")
    front = Wall(m, (D / 2, 0, 0), (0, 1, 0), (1, 0, 0), W)
    front.door(-W * .22, 100, 205)
    front.window(W * .22, H * .45, 70, 95, True)
    front.window(-W * .22 + 0, H * .82, 50, 55, False, shutters=False)
    for wall in (Wall(m, (0, W / 2, 0), (-1, 0, 0), (0, 1, 0), D), Wall(m, (0, -W / 2, 0), (1, 0, 0), (0, -1, 0), D)):
        wall.window(0, H * .5, 60, 80, seed % 2 == 0)
    # quoins
    for sx in (-1, 1):
        for sy in (-1, 1):
            for i in range(int(H // 55)):
                w = 36 if i % 2 else 52
                m.box((sx * (D / 2 + 2 - w / 2 * 0), sy * (W / 2 + 2), 28 + i * 55), (w if i % 2 else 24, 24 if i % 2 else w, 50), "Castle")
    gable_roof(m, 0, 0, H, W, D, D * .7, roof_mat, axis="y", ends="Stone", end_frame=False, overhang=35, thick=28)
    chimney(m, -D * .2, W * .35, H - 20, D * .7 + 80, 80)
    return m


def chapel(name):
    m = Mesh(name)
    L, W, H = 1300, 760, 820
    m.box((0, 0, H / 2), (L, W, H), "Castle")
    m.box((0, 0, 30), (L + 40, W + 40, 60), "Stone")
    # buttresses
    for i in range(-2, 3):
        for s in (-1, 1):
            m.box((i * 260, s * (W / 2 + 45), H * .4), (90, 90, H * .8), "Castle")
            m.box((i * 260, s * (W / 2 + 30), H * .8 + 30), (90, 60, 80), "Castle", rot_x(s * 30))
    side = [Wall(m, (0, s * W / 2, 0), (-s, 0, 0) if s > 0 else (1, 0, 0), (0, s, 0), L) for s in (-1, 1)]
    for wall in side:
        for i in range(-2, 2):
            u = i * 260 + 130
            wall.panel(u, H * .55, 90, 330, 10, 4, "Stone")
            wall.panel(u, H * .55, 60, 300, 6, 7, "GlassLit")
            wall.panel(u, H * .55 + 165, 60, 30, 12, 7, "Stone", tilt=0)
    gable_roof(m, 0, 0, H, L, W, 560, "RoofSlate", axis="x", overhang=40, thick=24, ends="Castle", end_frame=False)
    # tower on the front (+X)
    tx = L / 2 + 180
    m.box((tx, 0, 900), (360, 360, 1800), "Castle")
    m.box((tx, 0, 1820), (400, 400, 40), "Stone")
    tw = Wall(m, (tx + 180, 0, 0), (0, 1, 0), (1, 0, 0), 360)
    tw.door(0, 150, 300)
    tw.panel(0, 360, 180, 60, 20, 8, "Stone")
    tw.window(0, 1000, 70, 180, True, shutters=False, frame="Stone")
    for s in (-1, 1):
        Wall(m, (tx, s * 180, 0), (-s, 0, 0) if s > 0 else (1, 0, 0), (0, s, 0), 360).window(0, 1560, 90, 190, False, shutters=False, frame="Stone")
    Wall(m, (tx + 180, 0, 0), (0, 1, 0), (1, 0, 0), 360).window(0, 1560, 90, 190, False, shutters=False, frame="Stone")
    m.cone((tx, 0, 1840), 250, 900, "RoofSlate", n=4, rot=rot_z(45))
    m.cylinder((tx, 0, 2735), 6, 160, "Iron", n=6)
    m.box((tx, 0, 2850), (8, 70, 8), "Iron")
    return m


def market_hall(name):
    m = Mesh(name)
    L, W, H = 1200, 700, 420
    for i in range(5):
        for s in (-1, 1):
            x = -L / 2 + i * L / 4
            m.box((x, s * W / 2, 30), (70, 70, 60), "Stone")
            m.box((x, s * W / 2, H / 2 + 30), (34, 34, H), "Timber")
            m.beam((x, s * W / 2, H - 60), (x + 70 * (1 if i < 4 else -1), s * W / 2, H), 16, 14, "Timber", up=(0, s, 0))
    for s in (-1, 1):
        m.box((0, s * W / 2, H + 30), (L + 40, 30, 36), "Timber")
    for i in range(5):
        x = -L / 2 + i * L / 4
        m.box((x, 0, H + 40), (26, W + 20, 26), "Timber")
    gable_roof(m, 0, 0, H + 48, L + 40, W + 40, 360, "RoofClay", axis="x", overhang=70, thick=18, end_frame=True, ends="Planks")
    m.box((0, 0, 3), (L + 60, W + 60, 6), "Stone")
    return m


def warehouse(name):
    m = Mesh(name)
    D, W, H = 700, 900, 520
    m.box((0, 0, H / 2), (D, W, H), "Planks")
    m.box((0, 0, 30), (D + 20, W + 20, 60), "Stone")
    f = Wall(m, (D / 2, 0, 0), (0, 1, 0), (1, 0, 0), W)
    f.panel(0, 150, 300, 300, 10, 6, "Planks")
    f.panel(0, 150, 320, 320, 12, 3, "Timber")
    f.panel(0, 150, 10, 300, 12, 12, "Timber")
    f.brace(-145, 10, -5, 290, 12, "Timber", off=12)
    f.brace(145, 10, 5, 290, 12, "Timber", off=12)
    f.panel(0, 420, 120, 120, 10, 6, "Planks")
    f.panel(0, 420, 136, 136, 12, 3, "Timber")
    m.beam(f.at(0, 520, 20), f.at(0, 520, 160), 14, 14, "Timber")
    m.cylinder(f.at(0, 510, 150), 12, 20, "Timber", n=10, rot=rot_x(90))
    for wall in (f, Wall(m, (0, W / 2, 0), (-1, 0, 0), (0, 1, 0), D), Wall(m, (0, -W / 2, 0), (1, 0, 0), (0, -1, 0), D)):
        timber_frame(wall, 0, H, [(0, 330)] if wall is f else [], t=20, spacing=160)
    gable_roof(m, 0, 0, H, W, D, 330, "RoofSlate", axis="y", ends="Planks", end_frame=False)
    return m


def townhouse_row(name, n=3, seed=11):
    """Terrace of narrow gabled houses sharing walls; each unit distinct."""
    rng = random.Random(seed)
    m = Mesh(name)
    y = 0
    widths = [rng.choice((430, 480, 520)) for _ in range(n)]
    total = sum(widths)
    y = -total / 2
    for i, w in enumerate(widths):
        cy = y + w / 2
        h1, h2 = 300, rng.choice((280, 300, 320))
        floors = [h1, h2] + ([260] if rng.random() < .5 else [])
        D = 720
        z = 0
        m.box((0, cy, 30), (D + 24, w, 60), "Stone")
        for fi, h in enumerate(floors):
            over = 30 * fi
            mat = "Stone" if fi == 0 else ("Plaster" if (i + fi) % 3 else "Planks")
            m.box((over / 2, cy, z + h / 2), (D + over, w, h), mat)
            wall = Wall(m, (D / 2 + over, cy, 0), (0, 1, 0), (1, 0, 0), w)
            if fi == 0:
                wall.door(-w * .2, 95, 205)
                wall.window(w * .22, z + h * .5, 70, 110, rng.random() < .6)
            else:
                wins = [(-w * .22, 64), (w * .22, 64)] if w > 450 else [(0, 80)]
                for u, ww in wins:
                    wall.window(u, z + h * .52, ww, min(120, h * .45), rng.random() < .55)
                if mat == "Plaster":
                    timber_frame(wall, z, z + h, wins, spacing=100)
                m.box((D / 2 + over, cy, z + 4), (18, w, 18), "Timber")
            z += h
        rise = w * .9
        gable_roof(m, D / 2 * .05 + 30, cy, z, D + 60, w, rise, rng.choice(("RoofSlate", "RoofClay", "RoofSlate")), axis="x",
                   overhang=22, ends="Plaster", end_frame=True)
        if rng.random() < .7:
            chimney(m, -D * .25, cy + w * .3, z - 30, rise + 70, 60)
        y += w
    return m


# ---------------------------------------------------------------- fortifications
def curtain_wall(name, L=1000, H=900, T=320, mat="Castle", walk=True):
    m = Mesh(name)
    m.box((0, 0, H / 2), (T, L, H), mat)
    m.box((T * .1, 0, 40), (T + 60, L, 80), mat)  # batter
    crenellate(m, (T / 2 - 20, -L / 2, 0), (T / 2 - 20, L / 2, 0), H, 40, mat)
    crenellate(m, (-T / 2 + 20, -L / 2, 0), (-T / 2 + 20, L / 2, 0), H, 40, mat, merlon=110, gap=90, h=50)
    m.box((T / 2 + 6, 0, H - 30), (24, L, 26), mat)  # string course
    for i in range(-1, 2):
        Wall(m, (T / 2, i * L * .3, 0), (0, 1, 0), (1, 0, 0), 60).slit(0, H * .55, 110)
    return m


def round_tower(name, r=360, H=1500, roof="RoofSlate", mat="Castle"):
    m = Mesh(name)
    m.cylinder((0, 0, 0), r + 40, 120, mat, n=20, r2=r)
    m.cylinder((0, 0, 120), r, H - 120, mat, n=20)
    m.cylinder((0, 0, H - 40), r + 45, 60, mat, n=20)
    for i in range(10):
        a = 2 * math.pi * i / 10
        m.box(((r + 25) * math.cos(a), (r + 25) * math.sin(a), H + 50), (60, 80, 90), mat, rot_z(math.degrees(a)))
        m.box(((r + 55) * math.cos(a + .31), (r + 55) * math.sin(a + .31), H - 70), (30, 40, 40), mat, rot_z(math.degrees(a)))
    if roof:
        m.cone((0, 0, H + 20), r + 20, r * 2.1, roof, n=20)
        m.cylinder((0, 0, H + 20 + r * 2.1 - 20), 5, 150, "Iron", n=6)
    for i in range(4):
        a = math.pi / 2 * i + .4
        u, n = (-math.sin(a), math.cos(a), 0), (math.cos(a), math.sin(a), 0)
        for z in (H * .35, H * .68):
            Wall(m, (r * math.cos(a), r * math.sin(a), 0), u, n, 60).slit(0, z, 120)
    return m


def gatehouse(name, gate_w=900, gate_h=620, H=1350, D=900, tower_r=330, mat="Castle", hoarding=False, flank=0):
    """Twin-towered gatehouse. The passage spans y in [-gate_w/2, gate_w/2] full depth, open to the sky
    only through the arch; its collision leaves a clear rectangular walkway."""
    m = Mesh(name)
    half = gate_w / 2
    block_w = 520
    for s in (-1, 1):
        cy = s * (half + block_w / 2)
        m.box((0, cy, H / 2), (D, block_w, H), mat)
        m.cylinder((D / 2 - 40, s * (half + block_w - tower_r * .8), 0), tower_r, H + 150, mat, n=18)
        crenellate(m, (D / 2 - 40 - tower_r, s * (half + block_w - tower_r * .8), 0), (D / 2 - 40 + tower_r, s * (half + block_w - tower_r * .8), 0), H + 150, tower_r * 2 - 40, mat, merlon=80, gap=60, h=80)
        m.cone((D / 2 - 40, s * (half + block_w - tower_r * .8), H + 230), tower_r + 30, tower_r * 1.8, "RoofSlate", n=18)
        tw = Wall(m, (D / 2 + tower_r * .1, s * (half + block_w * .45), 0), (0, 1, 0), (1, 0, 0), 60)
        tw.slit(0, H * .4, 130)
        tw.slit(0, H * .72, 130)
    # lintel block above the arch
    m.box((0, 0, gate_h + (H - gate_h) / 2), (D, gate_w, H - gate_h), mat)
    # Vaulted passage: a segmental arch from the springing line up to gate_h, run through the full depth.
    rise = half * .55
    zs = gate_h - rise
    R = (half * half + rise * rise) / (2 * rise)
    cz = gate_h - R
    n = 28
    for k in range(n):
        y0 = -half + gate_w * k / n
        ym = y0 + gate_w / n / 2
        zb = cz + math.sqrt(max(0.0, R * R - ym * ym))
        if gate_h - zb > 1:
            m.box((0, ym, (zb + gate_h) / 2 + 1), (D, gate_w / n + 1.5, gate_h - zb + 2), mat)
    # Continuous voussoir ring proud of both faces, with a keystone.
    a_left = math.atan2(zs - cz, -half)
    a_right = math.atan2(zs - cz, half)
    segs = 15
    for fx, sgn in ((D / 2, 1), (-D / 2, -1)):
        x0, x1 = fx - sgn * 4, fx + sgn * 16
        for k in range(segs):
            a = a_left + (a_right - a_left) * k / segs
            b = a_left + (a_right - a_left) * (k + 1) / segs
            ro = R + (95 if k == segs // 2 else 70)
            ia = (math.cos(a) * R, cz + math.sin(a) * R)
            ib = (math.cos(b) * R, cz + math.sin(b) * R)
            oa = (math.cos(a) * ro, cz + math.sin(a) * ro)
            ob = (math.cos(b) * ro, cz + math.sin(b) * ro)
            P = lambda x, yz: (x, yz[0], yz[1])
            faces = [
                ([P(x1, ia), P(x1, ib), P(x1, ob), P(x1, oa)], (sgn, 0, 0)),
                ([P(x0, ia), P(x0, ib), P(x0, ob), P(x0, oa)], (-sgn, 0, 0)),
                ([P(x0, ia), P(x1, ia), P(x1, ib), P(x0, ib)], (0, -math.cos((a + b) / 2), -math.sin((a + b) / 2))),
                ([P(x0, oa), P(x1, oa), P(x1, ob), P(x0, ob)], (0, math.cos((a + b) / 2), math.sin((a + b) / 2))),
                ([P(x0, ia), P(x1, ia), P(x1, oa), P(x0, oa)], (0, math.sin(a), -math.cos(a))),
                ([P(x0, ib), P(x1, ib), P(x1, ob), P(x0, ob)], (0, -math.sin(b), math.cos(b))),
            ]
            for pts, want in faces:
                nrm = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
                if dot(nrm, want) < 0:
                    pts = list(reversed(pts))
                m.poly(pts, "Stone")
        for sy in (-1, 1):  # jamb stones down to the ground
            for j in range(int(zs // 60)):
                w = 58 if j % 2 else 44
                m.box((fx + sgn * 6, sy * (half + w / 2 - 8), 30 + j * 60), (20, w, 56), "Stone")
    # portcullis (raised: only the bottom teeth show under the lintel)
    for i in range(9):
        y = -half + 40 + i * (gate_w - 80) / 8
        m.box((D / 2 - 80, y, gate_h - 30), (12, 12, 110), "Iron")
        m.box((D / 2 - 80, y, gate_h - 92), (8, 8, 18), "Iron")
    m.box((D / 2 - 80, 0, gate_h + 10), (14, gate_w - 60, 14), "Iron")
    # battlements
    crenellate(m, (D / 2 - 20, -half - block_w, 0), (D / 2 - 20, half + block_w, 0), H, 40, mat)
    crenellate(m, (-D / 2 + 20, -half - block_w, 0), (-D / 2 + 20, half + block_w, 0), H, 40, mat)
    m.box((D / 2 + 25, 0, H - 120), (50, gate_w + 2 * block_w, 60), mat)  # machicolation course
    for i in range(-7, 8):
        m.box((D / 2 + 25, i * 90, H - 175), (40, 30, 50), mat)
    if hoarding:
        m.box((D / 2 + 70, 0, H + 110), (120, gate_w + block_w, 220), "Planks")
        gable_roof(m, D / 2 + 70, 0, H + 220, gate_w + block_w, 160, 110, "RoofSlate", axis="y", overhang=20, thick=12, ends="Planks", end_frame=False)
    # side walls continuing to the edges (flank)
    if flank:
        for s in (-1, 1):
            y0 = half + block_w
            m.box((0, s * (y0 + flank / 2), H * .35), (D * .45, flank, H * .7), mat)
            crenellate(m, (D * .225 - 15, s * y0, 0), (D * .225 - 15, s * (y0 + flank), 0), H * .7, 36, mat)
    # heraldic banners on the front
    for s in (-1, 1):
        y = s * (half + block_w * .5)
        m.box((D / 2 + 8, y, H * .62), (6, 150, 380), "BannerCloth")
        m.box((D / 2 + 12, y, H * .62 + 195), (12, 170, 12), "Iron")
    return m


def keep(name):
    m = Mesh(name)
    S, H = 1500, 2600
    m.box((0, 0, H / 2), (S, S, H), "Castle")
    m.box((0, 0, 70), (S + 120, S + 120, 140), "Castle")
    m.box((0, 0, H - 60), (S + 70, S + 70, 50), "Castle")
    for a, b in (((-S / 2, -S / 2), (S / 2, -S / 2)), ((S / 2, -S / 2), (S / 2, S / 2)), ((S / 2, S / 2), (-S / 2, S / 2)), ((-S / 2, S / 2), (-S / 2, -S / 2))):
        crenellate(m, (a[0] * 1.02, a[1] * 1.02, 0), (b[0] * 1.02, b[1] * 1.02, 0), H - 35, 50, "Castle", merlon=90, gap=65, h=100)
    for sx in (-1, 1):
        for sy in (-1, 1):
            c = (sx * S / 2, sy * S / 2, 0)
            m.cylinder(c, 260, H + 500, "Castle", n=18)
            m.cylinder(add(c, (0, 0, H + 460)), 300, 60, "Castle", n=18)
            m.cone(add(c, (0, 0, H + 520)), 310, 800, "RoofSlate", n=18)
            m.cylinder(add(c, (0, 0, H + 1300)), 5, 170, "Iron", n=6)
    # central roof
    gable_roof(m, 0, 0, H - 40, S - 260, S - 260, 520, "RoofSlate", axis="x", overhang=0, thick=30, ends="Castle", end_frame=False)
    for wall in (Wall(m, (S / 2, 0, 0), (0, 1, 0), (1, 0, 0), S), Wall(m, (-S / 2, 0, 0), (0, -1, 0), (-1, 0, 0), S),
                 Wall(m, (0, S / 2, 0), (-1, 0, 0), (0, 1, 0), S), Wall(m, (0, -S / 2, 0), (1, 0, 0), (0, -1, 0), S)):
        for u in (-380, 0, 380):
            wall.window(u, 1350, 90, 200, u == 0, shutters=False, frame="Stone", sill="Castle")
            wall.window(u, 1950, 80, 170, u != 0, shutters=False, frame="Stone", sill="Castle")
            wall.slit(u, 700, 150)
    f = Wall(m, (S / 2, 0, 0), (0, 1, 0), (1, 0, 0), S)
    f.door(0, 240, 420)
    f.panel(0, 280, 600, 30, 60, 30, "Castle")
    for s in (-1, 1):
        f.panel(s * 380, 1350 - 330, 260, 560, 8, 10, "BannerCloth")
    return m


def town_wall_tower(name):
    return round_tower(name, r=300, H=1150, roof="RoofSlate", mat="Stone")


def watchtower(name):
    m = Mesh(name)
    S, H = 420, 1250
    m.box((0, 0, H * .35), (S + 60, S + 60, H * .7), "Stone")
    m.box((0, 0, H * .72), (S, S, H * .08), "Stone")
    m.box((0, 0, H * .85), (S + 80, S + 80, H * .2), "Planks")
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * (S / 2 + 30), sy * (S / 2 + 30), H * .85), (26, 26, H * .22), "Timber")
    for i in range(4):
        w = Wall(m, mv(rot_z(90 * i), ((S + 80) / 2, 0, 0)), mv(rot_z(90 * i), (0, 1, 0)), mv(rot_z(90 * i), (1, 0, 0)), S + 80)
        w.panel(0, H * .86, 180, 70, 8, 4, "GlassDark")
        w.panel(0, H * .75, S + 80, 20, 20, 6, "Timber")
    gable_roof(m, 0, 0, H * .95, S + 100, S + 100, 260, "RoofSlate", axis="x", overhang=40, thick=14, ends="Planks", end_frame=False)
    Wall(m, ((S + 60) / 2, 0, 0), (0, 1, 0), (1, 0, 0), S + 60).door(0, 110, 220)
    return m


# ---------------------------------------------------------------- street furniture
def market_stall(name, canvas="Canvas", W=380, D=230, seed=5):
    rng = random.Random(seed)
    m = Mesh(name)
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = 250 if sx > 0 else 290
            m.box((sx * D / 2, sy * W / 2, h / 2), (12, 12, h), "Timber")
    m.box((D / 2 - 30, 0, 85), (60, W, 10), "Planks")              # counter top
    m.box((D / 2 - 30, 0, 42), (50, W - 20, 84), "Planks")          # counter front
    m.box((-D / 2 + 20, 0, 60), (40, W - 20, 8), "Planks")          # back shelf
    m.box((-D / 2 + 20, 0, 130), (40, W - 20, 8), "Planks")
    # sloped canvas awning with a scalloped valance
    ang = math.degrees(math.atan2(40, D))
    m.box((10, 0, 272), (D + 80, W + 40, 4), canvas, rot_y(ang))
    for i in range(int(W // 38)):
        y = -W / 2 + 19 + i * 38
        m.box((D / 2 + 50, y, 243), (3, 34, 30 if i % 2 else 22), canvas)
    for s in (-1, 1):
        m.box((0, s * (W / 2 + 18), 260), (D + 60, 3, 30), canvas, rot_y(ang))
    # goods: sacks and small crates on the counter
    for i in range(3):
        y = -W / 3 + i * W / 3
        m.box((D / 2 - 30, y, 108), (40, 60, 36), "Planks" if i != 1 else "Sack", rot_z(rng.uniform(-10, 10)))
    m.cylinder((-D / 2 + 20, -W / 4, 64), 18, 40, "Sack", n=8)
    m.cylinder((-D / 2 + 20, W / 5, 64), 16, 36, "Sack", n=8)
    return m


def cart(name):
    m = Mesh(name)
    m.box((0, 0, 95), (260, 150, 10), "Planks")
    for s in (-1, 1):
        m.box((0, s * 72, 125), (260, 6, 55), "Planks")
        m.box((0, s * 60, 83), (300, 10, 14), "Timber")
    for s in (-1, 1):
        m.box((s * 127, 0, 125), (6, 150, 55), "Planks")
    for s in (-1, 1):  # shafts
        m.beam((140, s * 55, 88), (360, s * 45, 55), 8, 8, "Timber")
    for s in (-1, 1):  # spoked wheels
        c = (-20, s * 88, 62)
        rim_n = 16
        for i in range(rim_n):
            a0, a1 = 2 * math.pi * i / rim_n, 2 * math.pi * (i + 1) / rim_n
            p0 = add(c, (58 * math.cos(a0), 0, 58 * math.sin(a0)))
            p1 = add(c, (58 * math.cos(a1), 0, 58 * math.sin(a1)))
            m.beam(p0, p1, 8, 10, "Timber", up=(0, 1, 0))
            if i % 2 == 0:
                m.beam(c, add(c, (54 * math.cos(a0), 0, 54 * math.sin(a0))), 5, 5, "Timber", up=(0, 1, 0))
        m.cylinder(add(c, (0, -s * 8, 0)), 12, 16, "Iron", n=10, rot=rot_x(90), bottom=True)
    m.box((0, 0, 7), (120, 170, 14), "Timber")  # axle bed
    # load
    m.box((30, -30, 135), (80, 60, 70), "Planks", rot_z(8))
    m.cylinder((-60, 25, 102), 28, 60, "Sack", n=10)
    m.cylinder((-40, -40, 102), 25, 50, "Sack", n=10)
    return m


def well(name):
    m = Mesh(name)
    m.cylinder((0, 0, 0), 125, 90, "Stone", n=20)
    m.cylinder((0, 0, 90), 132, 14, "Stone", n=20)
    m.cylinder((0, 0, 60), 100, 42, "GlassDark", n=20)
    for s in (-1, 1):
        m.box((0, s * 112, 200), (18, 18, 240), "Timber")
    m.cylinder((0, -105, 245), 9, 210, "Timber", n=8, rot=rot_x(-90))
    m.beam((0, 120, 245), (0, 120, 200), 5, 5, "Iron")
    m.beam((0, 120, 200), (30, 120, 200), 5, 5, "Iron")
    gable_roof(m, 0, 0, 310, 300, 260, 110, "RoofSlate", axis="y", overhang=10, thick=10, ends="Planks", end_frame=False)
    m.beam((0, 0, 240), (0, 0, 150), 2, 2, "Iron")
    m.cylinder((0, 0, 120), 16, 30, "Planks", n=10)
    return m


def fence(name, L=400):
    m = Mesh(name)
    n = int(L // 100)
    for i in range(n + 1):
        y = -L / 2 + i * L / n
        m.box((0, y, 55), (12, 12, 110), "Timber", rot_z(7 * (i % 3 - 1)))
    for z in (40, 90):
        m.box((0, 0, z), (5, L, 12), "Planks")
    return m


def low_wall(name, L=500):
    m = Mesh(name)
    m.box((0, 0, 45), (60, L, 90), "Stone")
    m.box((0, 0, 95), (70, L + 10, 12), "Stone")
    return m


def lamp_post(name):
    m = Mesh(name)
    m.box((0, 0, 15), (50, 50, 30), "Stone")
    m.cylinder((0, 0, 30), 7, 330, "Iron", n=8, r2=5)
    m.beam((0, 0, 330), (70, 0, 330), 5, 5, "Iron")
    m.beam((0, 0, 280), (50, 0, 330), 4, 4, "Iron")
    # lantern cage
    c = (70, 0, 270)
    m.box(add(c, (0, 0, 32)), (40, 40, 6), "Iron")
    m.box(add(c, (0, 0, -32)), (34, 34, 6), "Iron")
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box(add(c, (sx * 16, sy * 16, 0)), (3, 3, 64), "Iron")
    m.box(c, (28, 28, 56), "Flame")
    m.cone(add(c, (0, 0, 35)), 26, 22, "Iron", n=4, rot=rot_z(45))
    m.beam(add(c, (0, 0, 57)), (70, 0, 330), 2, 2, "Iron")
    return m


def wall_lantern(name):
    m = Mesh(name)
    m.box((0, 0, 0), (8, 22, 30), "Iron")
    m.beam((0, 0, 0), (38, 0, 0), 4, 4, "Iron")
    c = (40, 0, -26)
    m.box(add(c, (0, 0, 22)), (26, 26, 4), "Iron")
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box(add(c, (sx * 10, sy * 10, 0)), (2, 2, 42), "Iron")
    m.box(c, (18, 18, 36), "Flame")
    return m


def banner_pole(name, cloth="BannerCloth"):
    m = Mesh(name)
    m.box((0, 0, 20), (60, 60, 40), "Stone")
    m.cylinder((0, 0, 40), 6, 620, "Timber", n=8, r2=5)
    m.beam((0, -5, 610), (0, 150, 610), 5, 5, "Timber")
    m.box((0, 75, 440), (3, 140, 330), cloth)
    for i in range(3):  # swallow-tail
        m.box((0, 30 + i * 45, 262), (3, 40, 30 if i != 1 else 10), cloth)
    m.cone((0, 0, 660), 9, 22, "Iron", n=6)
    return m


def fountain(name):
    m = Mesh(name)
    m.cylinder((0, 0, 0), 420, 30, "Stone", n=28)
    m.cylinder((0, 0, 30), 380, 50, "Castle", n=28)
    m.cylinder((0, 0, 80), 392, 12, "Stone", n=28)
    m.cylinder((0, 0, 30), 350, 40, "Water", n=28)
    m.cylinder((0, 0, 30), 70, 150, "Castle", n=12)
    m.cylinder((0, 0, 180), 160, 26, "Stone", n=16, r2=180)
    m.cylinder((0, 0, 190), 150, 10, "Water", n=16)
    m.cylinder((0, 0, 206), 40, 170, "Castle", n=10)
    m.cylinder((0, 0, 376), 80, 22, "Stone", n=12)
    m.cone((0, 0, 398), 30, 60, "Stone", n=8)
    return m


def woodpile(name):
    m = Mesh(name)
    for row in range(4):
        for i in range(6 - row):
            y = -130 + i * 50 + row * 25
            m.cylinder((-100, y, 24 + row * 42), 21, 200, "LogEnd", n=7, rot=rot_y(90), bottom=True)
    m.box((0, 0, 3), (230, 330, 6), "Planks")
    gable_roof(m, 0, 0, 220, 340, 230, 50, "Planks", axis="y", overhang=10, thick=6, ends="Planks", end_frame=False)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 110, sy * 165, 110), (12, 12, 220), "Timber")
    return m


def hay_sacks(name):
    m = Mesh(name)
    for i, (x, y, r, z) in enumerate(((0, 0, 30, 0), (50, 40, 28, 0), (-40, 55, 27, 0), (20, 20, 26, 45))):
        m.cylinder((x, y, z), r, 55, "Sack", n=10, r2=r * .75)
    m.box((-90, -60, 35), (110, 60, 70), "Thatch", rot_z(12))
    return m


def gibbet(name):
    m = Mesh(name)
    m.box((0, 0, 20), (80, 80, 40), "Stone")
    m.box((0, 0, 290), (24, 24, 540), "Timber")
    m.beam((0, 0, 560), (170, 0, 560), 20, 20, "Timber")
    m.beam((0, 0, 460), (90, 0, 560), 12, 12, "Timber")
    m.beam((150, 0, 555), (150, 0, 470), 3, 3, "Iron")
    for i in range(8):
        a = 2 * math.pi * i / 8
        m.box((150 + 30 * math.cos(a), 30 * math.sin(a), 390), (3, 3, 160), "Iron")
    for z in (310, 470):
        m.cylinder((150, 0, z), 32, 4, "Iron", n=8)
    return m


def barricade(name):
    m = Mesh(name)
    for i in range(7):
        y = -270 + i * 90
        m.beam((0, y, 0), (70, y, 190), 12, 12, "Timber")
        m.beam((0, y, 0), (-70, y + 20, 170), 12, 12, "Timber")
    m.box((0, 0, 60), (16, 600, 16), "Timber")
    return m


def castle_stairs(name):
    m = Mesh(name)
    for i in range(6):
        m.box((-i * 40, 0, 10 + i * 20), (60, 900 - i * 40, 20), "Stone")
    return m


BUILDERS = {
    "SM_Town_HouseTimberA": lambda: house("SM_Town_HouseTimberA", 780, 620, [300, 290], "RoofSlate", seed=1, chimney_at=(.3, .8)),
    "SM_Town_HouseTimberB": lambda: house("SM_Town_HouseTimberB", 560, 760, [300, 280, 260], "RoofClay", roof_axis="x", seed=2,
                                          lit=(0, 1, 1, 0), chimney_at=(.2, .25)),
    "SM_Town_HouseTimberC": lambda: house("SM_Town_HouseTimberC", 900, 640, [310, 300], "RoofSlate", seed=3, dormer=True,
                                          lit=(1, 0, 0, 1, 1)),
    "SM_Town_HouseShop": lambda: house("SM_Town_HouseShop", 820, 660, [330, 290], "RoofClay", shop=True, sign=True, seed=4,
                                       ground="Stone", chimney_at=(.25, .2)),
    "SM_Town_Tavern": lambda: house("SM_Town_Tavern", 1150, 760, [340, 300, 250], "RoofSlate", seed=5, sign=True, dormer=True,
                                    lit=(1, 1, 0, 1), chimney_at=(.2, .85)),
    "SM_Town_HousePlanks": lambda: house("SM_Town_HousePlanks", 640, 560, [290, 250], "Thatch", ground="Stone", upper="Planks",
                                         jetty=0, seed=6, lit=(0, 1, 0), chimney_at=(.3, .7), rise=380),
    "SM_Town_CottageStone": lambda: stone_house("SM_Town_CottageStone", 820, 560, 330, "Thatch"),
    "SM_Town_CottageStoneSlate": lambda: stone_house("SM_Town_CottageStoneSlate", 700, 600, 380, "RoofSlate", seed=4),
    "SM_Town_TownhouseRow": lambda: townhouse_row("SM_Town_TownhouseRow", 3, seed=11),
    "SM_Town_TownhouseRowB": lambda: townhouse_row("SM_Town_TownhouseRowB", 3, seed=23),
    "SM_Town_Warehouse": lambda: warehouse("SM_Town_Warehouse"),
    "SM_Town_Chapel": lambda: chapel("SM_Town_Chapel"),
    "SM_Town_MarketHall": lambda: market_hall("SM_Town_MarketHall"),
    "SM_Town_Watchtower": lambda: watchtower("SM_Town_Watchtower"),
    "SM_Town_Gatehouse": lambda: gatehouse("SM_Town_Gatehouse", gate_w=800, gate_h=560, H=1100, D=760, tower_r=280, mat="Stone", hoarding=True),
    "SM_Town_WallSegment": lambda: curtain_wall("SM_Town_WallSegment", L=1000, H=780, T=300, mat="Stone"),
    "SM_Town_WallTower": lambda: town_wall_tower("SM_Town_WallTower"),
    "SM_Castle_Gatehouse": lambda: gatehouse("SM_Castle_Gatehouse", gate_w=900, gate_h=640, H=1500, D=1000, tower_r=360),
    "SM_Castle_Curtain": lambda: curtain_wall("SM_Castle_Curtain", L=1000, H=1100, T=360),
    "SM_Castle_Tower": lambda: round_tower("SM_Castle_Tower", r=380, H=1750),
    "SM_Castle_Keep": lambda: keep("SM_Castle_Keep"),
    "SM_Town_MarketStallRed": lambda: market_stall("SM_Town_MarketStallRed", "Canvas", seed=5),
    "SM_Town_MarketStallBlue": lambda: market_stall("SM_Town_MarketStallBlue", "CanvasAlt", W=320, D=250, seed=9),
    "SM_Town_Cart": lambda: cart("SM_Town_Cart"),
    "SM_Town_Well": lambda: well("SM_Town_Well"),
    "SM_Town_Fence": lambda: fence("SM_Town_Fence"),
    "SM_Town_LowWall": lambda: low_wall("SM_Town_LowWall"),
    "SM_Town_LampPost": lambda: lamp_post("SM_Town_LampPost"),
    "SM_Town_WallLantern": lambda: wall_lantern("SM_Town_WallLantern"),
    "SM_Town_BannerPole": lambda: banner_pole("SM_Town_BannerPole"),
    "SM_Town_Fountain": lambda: fountain("SM_Town_Fountain"),
    "SM_Town_Woodpile": lambda: woodpile("SM_Town_Woodpile"),
    "SM_Town_HaySacks": lambda: hay_sacks("SM_Town_HaySacks"),
    "SM_Town_Gibbet": lambda: gibbet("SM_Town_Gibbet"),
    "SM_Town_Barricade": lambda: barricade("SM_Town_Barricade"),
    "SM_Castle_Stairs": lambda: castle_stairs("SM_Castle_Stairs"),
}


def main():
    rows = [BUILDERS[name]().save() for name in BUILDERS]
    (OUT / "TownMeshes.json").write_text(json.dumps({"generator": "Tools/BuildTownMeshes.py", "units": "cm", "up": "Z",
                                                     "front": "+X", "meshes": rows}, indent=2) + "\n", encoding="utf-8")
    for r in rows:
        print(f"{r['name']:32s} tris={r['triangles']:6d} bounds={r['boundsMin']}..{r['boundsMax']}")
    print(f"CIRE_TOWN_MESHES_PASS meshes={len(rows)} triangles={sum(r['triangles'] for r in rows)}")


if __name__ == "__main__":
    main()
