"""Small geometry library for the original arena mesh kit (Tools/BuildArenaMeshes.py).

Pure Python, no Unreal. Centimetres, Z up. UVs are in metres (1 UV = 100 cm) unless a
material expects something else (the wheat material reads V as normalised stalk height
and U > 1.5 as "ear"). Groups are keyed by material slot name; Tools/ImportArenaContent.py
maps slot names to material instances.
"""
from __future__ import annotations

import math
import random
from pathlib import Path


# ---------------------------------------------------------------- vectors
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return (0.0, 0.0, 1.0) if l < 1e-12 else (a[0] / l, a[1] / l, a[2] / l)
def lerp(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


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


def frame(axis):
    """Orthonormal basis (u, v, w=axis)."""
    w = norm(axis)
    helper = (0, 0, 1) if abs(w[2]) < 0.9 else (1, 0, 0)
    u = norm(cross(helper, w))
    v = cross(w, u)
    return u, v, w


# ---------------------------------------------------------------- noise
class Noise:
    def __init__(self, seed=1):
        rnd = random.Random(seed)
        self.perm = list(range(256))
        rnd.shuffle(self.perm)
        self.perm += self.perm
        self.grad = [norm((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) for _ in range(256)]

    def _g(self, ix, iy, iz):
        return self.grad[self.perm[(self.perm[(self.perm[ix & 255] + iy) & 255] + iz) & 255]]

    def noise(self, x, y, z):
        ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
        fx, fy, fz = x - ix, y - iy, z - iz
        fade = lambda t: t * t * t * (t * (t * 6 - 15) + 10)
        u, v, w = fade(fx), fade(fy), fade(fz)
        total = 0.0
        res = {}
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    g = self._g(ix + dx, iy + dy, iz + dz)
                    res[(dx, dy, dz)] = g[0] * (fx - dx) + g[1] * (fy - dy) + g[2] * (fz - dz)
        x00 = res[(0, 0, 0)] + u * (res[(1, 0, 0)] - res[(0, 0, 0)])
        x10 = res[(0, 1, 0)] + u * (res[(1, 1, 0)] - res[(0, 1, 0)])
        x01 = res[(0, 0, 1)] + u * (res[(1, 0, 1)] - res[(0, 0, 1)])
        x11 = res[(0, 1, 1)] + u * (res[(1, 1, 1)] - res[(0, 1, 1)])
        y0 = x00 + v * (x10 - x00)
        y1 = x01 + v * (x11 - x01)
        return (y0 + w * (y1 - y0)) * 1.6

    def fbm(self, p, octaves=4, lacunarity=2.0, gain=0.5):
        amp, freq, total = 1.0, 1.0, 0.0
        for _ in range(octaves):
            total += amp * self.noise(p[0] * freq, p[1] * freq, p[2] * freq)
            amp *= gain
            freq *= lacunarity
        return total


# ---------------------------------------------------------------- mesh
class Mesh:
    def __init__(self, name):
        self.name = name
        self.groups = {}  # mat -> {"p": [], "n": [], "uv": [], "tri": [], "smooth": bool}

    def group(self, mat, smooth=False):
        g = self.groups.setdefault(mat, {"p": [], "n": [], "uv": [], "tri": [], "auto": []})
        return g

    # ---- low level
    def vert(self, mat, p, uv, n=None):
        g = self.group(mat)
        g["p"].append(tuple(p))
        g["uv"].append(tuple(uv))
        g["n"].append(n)
        g["auto"].append(n is None)
        return len(g["p"]) - 1

    def tri(self, mat, a, b, c):
        self.group(mat)["tri"].append((a, b, c))

    def quad(self, mat, a, b, c, d):
        self.tri(mat, a, b, c)
        self.tri(mat, a, c, d)

    # ---- flat polygons with metric UVs
    def poly(self, mat, pts, uvs=None, flip=False):
        pts = list(reversed(pts)) if flip else list(pts)
        n = (0.0, 0.0, 0.0)
        for i, p in enumerate(pts):
            q = pts[(i + 1) % len(pts)]
            n = add(n, ((p[1] - q[1]) * (p[2] + q[2]), (p[2] - q[2]) * (p[0] + q[0]), (p[0] - q[0]) * (p[1] + q[1])))
        n = norm(n)
        if uvs is None:
            ax = max(range(3), key=lambda i: abs(n[i]))
            a, b = [(1, 2), (0, 2), (0, 1)][ax]
            uvs = [(p[a] / 100.0, p[b] / 100.0) for p in pts]
        elif flip:
            uvs = list(reversed(uvs))
        idx = [self.vert(mat, p, uv, n) for p, uv in zip(pts, uvs)]
        for k in range(1, len(idx) - 1):
            self.tri(mat, idx[0], idx[k], idx[k + 1])

    def box(self, mat, c, s, rot=IDENT, uv_jitter=True):
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        o = ((c[0] * 0.37 + c[2] * 0.11) / 100.0, (c[1] * 0.29 + c[2] * 0.07) / 100.0) if uv_jitter else (0, 0)
        corner = lambda x, y, z: add(c, mv(rot, (x, y, z)))
        faces = [
            ([(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)], (1, 2)),
            ([(-hx, hy, -hz), (-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz)], (1, 2)),
            ([(hx, hy, -hz), (-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz)], (0, 2)),
            ([(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)], (0, 2)),
            ([(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)], (0, 1)),
            ([(-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz), (-hx, -hy, -hz)], (0, 1)),
        ]
        for loc, (a, b) in faces:
            self.poly(mat, [corner(*p) for p in loc], [(p[a] / 100.0 + o[0], p[b] / 100.0 + o[1]) for p in loc])

    def beam(self, mat, a, b, w, d, up=(0, 0, 1)):
        axis = norm(sub(b, a))
        side = norm(cross(up, axis)) if length(cross(up, axis)) > 1e-6 else (0, 1, 0)
        n = cross(axis, side)
        rot = ((axis[0], side[0], n[0]), (axis[1], side[1], n[1]), (axis[2], side[2], n[2]))
        self.box(mat, mul(add(a, b), .5), (length(sub(b, a)), w, d), rot)

    # ---- smooth surfaces
    def grid(self, mat, rows, uvs, wrap=False, flip=False):
        """rows[i][j] points; smooth auto normals; wrap joins the last column to the first."""
        idx = [[self.vert(mat, p, uv) for p, uv in zip(r, ur)] for r, ur in zip(rows, uvs)]
        cols = len(rows[0])
        for i in range(len(rows) - 1):
            for j in range(cols - (0 if wrap else 1)):
                a, b = idx[i][j], idx[i][(j + 1) % cols]
                c, d = idx[i + 1][(j + 1) % cols], idx[i + 1][j]
                if flip:
                    self.quad(mat, a, d, c, b)
                else:
                    self.quad(mat, a, b, c, d)
        return idx

    def lathe(self, mat, profile, sides=16, center=(0, 0, 0), rot=IDENT, radial=None, cap_top=False, cap_bottom=False, u_scale=None, seam_dup=True):
        """profile: [(radius, z)] bottom to top. radial(theta, z, r) -> r' (flutes, bulges)."""
        circ = u_scale if u_scale is not None else 2 * math.pi * max(p[0] for p in profile) / 100.0
        rows, uvs = [], []
        vacc = 0.0
        prev = None
        for (r, z) in profile:
            if prev is not None:
                vacc += math.hypot(r - prev[0], z - prev[1]) / 100.0
            prev = (r, z)
            row, urow = [], []
            n = sides + (1 if seam_dup else 0)
            for j in range(n):
                t = 2 * math.pi * (j % sides) / sides if not seam_dup else 2 * math.pi * j / sides
                rr = radial(t, z, r) if radial else r
                row.append(add(center, mv(rot, (rr * math.cos(t), rr * math.sin(t), z))))
                urow.append((circ * j / sides, vacc))
            rows.append(row)
            uvs.append(urow)
        self.grid(mat, rows, uvs, wrap=not seam_dup)
        if cap_top and profile[-1][0] > 0.01:
            self.poly(mat, rows[-1][:sides])
        if cap_bottom and profile[0][0] > 0.01:
            self.poly(mat, list(reversed(rows[0][:sides])))
        return rows

    def tube(self, mat, path, radii, sides=6, v_scale=None, u_offset=0.0, twist=0.0, cap=False):
        """Smooth tube along a polyline (stalks, branches). V runs along the path in metres (or 0..1 * v_scale)."""
        total = sum(length(sub(path[i + 1], path[i])) for i in range(len(path) - 1)) or 1.0
        rows, uvs = [], []
        acc = 0.0
        u0, v0, _ = frame(sub(path[1], path[0]))
        for i, p in enumerate(path):
            if i > 0:
                acc += length(sub(p, path[i - 1]))
            t = sub(path[min(i + 1, len(path) - 1)], path[max(i - 1, 0)])
            _, _, w = frame(t)
            u = norm(sub(u0, mul(w, dot(u0, w))))
            v = cross(w, u)
            row, urow = [], []
            for j in range(sides + 1):
                a = 2 * math.pi * j / sides + twist * i
                row.append(add(p, add(mul(u, radii[i] * math.cos(a)), mul(v, radii[i] * math.sin(a)))))
                vv = acc / total * v_scale if v_scale is not None else acc / 100.0
                urow.append((u_offset + j / sides, vv))
            rows.append(row)
            uvs.append(urow)
        self.grid(mat, rows, uvs)
        if cap:
            self.poly(mat, rows[-1][:sides])

    def icosphere(self, level):
        t = (1 + 5 ** .5) / 2
        verts = [norm(v) for v in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
                                   (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
                 (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        for _ in range(level):
            cache = {}
            def mid(a, b):
                key = (min(a, b), max(a, b))
                if key not in cache:
                    verts.append(norm(mul(add(verts[a], verts[b]), .5)))
                    cache[key] = len(verts) - 1
                return cache[key]
            nf = []
            for a, b, c in faces:
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
            faces = nf
        return verts, faces

    def blob(self, mat, center, size, level=3, displace=None, rot=IDENT, flat_bottom=None):
        """Displaced ellipsoid. displace(unit_dir) -> radius multiplier. flat_bottom: z (local) clamp."""
        verts, faces = self.icosphere(level)
        idx = []
        for v in verts:
            k = displace(v) if displace else 1.0
            p = (v[0] * size[0] * .5 * k, v[1] * size[1] * .5 * k, v[2] * size[2] * .5 * k)
            if flat_bottom is not None and p[2] < flat_bottom:
                p = (p[0], p[1], flat_bottom + (p[2] - flat_bottom) * 0.05)
            wp = add(center, mv(rot, p))
            idx.append(self.vert(mat, wp, (wp[0] / 100.0 + wp[2] / 173.0, wp[1] / 100.0 + wp[2] / 131.0)))
        for a, b, c in faces:
            self.tri(mat, idx[a], idx[b], idx[c])

    # ---- output
    def finish_normals(self):
        for g in self.groups.values():
            acc = [(0.0, 0.0, 0.0)] * len(g["p"])
            for a, b, c in g["tri"]:
                n = cross(sub(g["p"][b], g["p"][a]), sub(g["p"][c], g["p"][a]))  # area weighted
                for i in (a, b, c):
                    if g["auto"][i]:
                        acc[i] = add(acc[i], n)
            for i in range(len(g["p"])):
                if g["auto"][i]:
                    g["n"][i] = norm(acc[i])

    def triangles(self):
        return sum(len(g["tri"]) for g in self.groups.values())

    def bounds(self):
        pts = [p for g in self.groups.values() for p in g["p"]]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        return lo, hi

    def save(self, folder: Path):
        self.finish_normals()
        folder.mkdir(parents=True, exist_ok=True)
        lines = ["# Original Cire's Team Survival arena geometry (Tools/BuildArenaMeshes.py). Centimetres, Z up.", f"o {self.name}"]
        body = []
        base = 1
        for mat in sorted(self.groups):
            g = self.groups[mat]
            if not g["tri"]:
                continue
            for p in g["p"]:
                lines.append("v %.3f %.3f %.3f" % p)
            for uv in g["uv"]:
                lines.append("vt %.5f %.5f" % uv)
            for n in g["n"]:
                lines.append("vn %.5f %.5f %.5f" % n)
            body.append(f"usemtl {mat}")
            for a, b, c in g["tri"]:
                body.append("f %d/%d/%d %d/%d/%d %d/%d/%d" % (a + base, a + base, a + base, b + base, b + base, b + base, c + base, c + base, c + base))
            base += len(g["p"])
        (folder / f"{self.name}.obj").write_text("\n".join(lines + body) + "\n", encoding="ascii")
        lo, hi = self.bounds()
        return {"name": self.name, "triangles": self.triangles(), "materials": sorted(m for m, g in self.groups.items() if g["tri"]),
                "boundsMin": [round(v, 1) for v in lo], "boundsMax": [round(v, 1) for v in hi],
                "size": [round(hi[k] - lo[k], 1) for k in range(3)]}
