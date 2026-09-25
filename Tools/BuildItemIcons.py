"""progression-shop: generate the original item icons (pure Python, no dependencies).

Every icon is drawn here from signed-distance shapes (blades, gems, flasks, shields...)
with bevel lighting, metal/gem materials, an accent glow and a themed backdrop, so the art
is original to this project (see Content/UI/Items/LICENSES.md). Writes 128x128 RGBA PNGs to
Content/UI/Items/src/T_Item_<id>.png; Tools/ImportItemContent.py imports them as UI textures
(/Game/UI/Items/T_Item_<id>). Run with any Python 3:  python Tools/BuildItemIcons.py

Painted overrides: when Art/Icons/ChatGPT/Items/T_Item_<id>.png exists (256x256 icons generated for
Eric via ChatGPT and sliced by Tools/SliceIconSheet.py) it is copied instead of the procedural
render. Pass --procedural to ignore the overrides.
"""
from pathlib import Path
import json
import math
import random
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Content/UI/Items/src"
PAINTED = ROOT / "Art/Icons/ChatGPT/Items"
SIZE = 128


def png(path, w, h, pixels):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            raw += bytes(max(0, min(255, int(round(c * 255)))) for c in (r, g, b, a))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


# ----------------------------------------------------------------------- SDF primitives
# Coordinates: x right, y DOWN, both in [-1, 1]. Negative distance = inside.
def circle(x, y, cx, cy, r):
    return math.hypot(x - cx, y - cy) - r


def ellipse(x, y, cx, cy, rx, ry):
    k = math.hypot((x - cx) / rx, (y - cy) / ry)
    return (k - 1.0) * min(rx, ry)


def seg(x, y, ax, ay, bx, by, r):
    px, py, dx, dy = x - ax, y - ay, bx - ax, by - ay
    t = max(0.0, min(1.0, (px * dx + py * dy) / (dx * dx + dy * dy + 1e-12)))
    return math.hypot(px - dx * t, py - dy * t) - r


def box(x, y, cx, cy, hw, hh, ang=0.0, rr=0.0):
    c, s = math.cos(ang), math.sin(ang)
    px, py = x - cx, y - cy
    px, py = c * px + s * py, -s * px + c * py
    qx, qy = abs(px) - hw + rr, abs(py) - hh + rr
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - rr


def poly(x, y, pts):
    d = 1e9
    inside = False
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy + 1e-12)))
        d = min(d, math.hypot(x - ax - dx * t, y - ay - dy * t))
        if (ay > y) != (by > y) and x < ax + (y - ay) * dx / (by - ay):
            inside = not inside
    return -d if inside else d


def ring(x, y, cx, cy, r, t):
    return abs(math.hypot(x - cx, y - cy) - r) - t


def arc(x, y, cx, cy, r, t, a0, a1):
    """Ring segment between angles a0..a1 (radians, y down)."""
    ang = math.atan2(y - cy, x - cx)
    mid, half = (a0 + a1) * .5, (a1 - a0) * .5
    delta = (ang - mid + math.pi) % (2 * math.pi) - math.pi
    if abs(delta) <= half:
        return abs(math.hypot(x - cx, y - cy) - r) - t
    ea = mid + math.copysign(half, delta)
    return math.hypot(x - cx - r * math.cos(ea), y - cy - r * math.sin(ea)) - t


def union(*ds):
    return min(ds)


def sub(a, b):
    return max(a, -b)


def inter(a, b):
    return max(a, b)


# ----------------------------------------------------------------------- materials
M = {
    "steel": ((.70, .74, .80), .9), "darksteel": ((.36, .39, .45), .9), "gold": ((.96, .72, .28), 1.0),
    "bronze": ((.72, .45, .22), .9), "bone": ((.90, .85, .72), .3), "wood": ((.52, .33, .18), .2),
    "leather": ((.46, .28, .15), .2), "darkleather": ((.28, .17, .10), .2), "cloth_red": ((.62, .10, .10), .1),
    "cloth_purple": ((.40, .16, .55), .1), "cloth_grey": ((.40, .42, .46), .1), "cloth_green": ((.18, .40, .22), .1),
    "ruby": ((.95, .12, .18), 1.2), "sapphire": ((.20, .45, 1.0), 1.2), "emerald": ((.18, .85, .45), 1.2),
    "amethyst": ((.72, .35, 1.0), 1.2), "topaz": ((1.0, .78, .25), 1.2), "obsidian": ((.16, .12, .22), 1.0),
    "stone": ((.52, .52, .55), .15), "glass": ((.72, .86, .95), 1.1), "potion_red": ((.90, .10, .12), .8),
    "potion_blue": ((.20, .45, 1.0), .8), "potion_amber": ((1.0, .62, .15), .8), "potion_purple": ((.62, .22, .95), .8),
    "potion_iron": ((.66, .70, .78), .8), "flame": ((1.0, .72, .25), 0.0), "blueflame": ((.45, .85, 1.0), 0.0),
    "parchment": ((.88, .80, .60), .1), "moss": ((.35, .55, .25), .1), "blood": ((.70, .02, .06), .9),
    "feather": ((.14, .15, .20), .6), "silver": ((.86, .88, .92), 1.0), "white": ((.95, .95, .92), .5),
}
THEMES = {
    "attack": (.34, .07, .05), "spell": (.20, .07, .34), "defense": (.08, .16, .30), "support": (.05, .26, .24),
    "speed": (.10, .24, .10), "consumable": (.08, .19, .10), "tome": (.30, .20, .06), "crit": (.30, .05, .12),
}
LIGHT = (-.55, -.83)  # from the top-left (y down)


def render(item_id, theme, glow, layers, seed, sparkle=0.0, rays=0):
    """layers: list of (sdf(x, y), material, outline, emissive)."""
    rnd = random.Random(seed)
    bg = THEMES[theme]
    px = []
    aa = 2.0 / SIZE
    stars = [(rnd.uniform(-.9, .9), rnd.uniform(-.9, .9), rnd.uniform(.01, .025)) for _ in range(int(sparkle))]
    for j in range(SIZE):
        for i in range(SIZE):
            x = (i + .5) / SIZE * 2 - 1
            y = (j + .5) / SIZE * 2 - 1
            # Backdrop: radial theme gradient, vignette, faint diagonal grain.
            r = math.hypot(x, y)
            v = max(0.0, 1.0 - r * .85)
            grain = .012 * math.sin((x * 37 + y * 23)) * math.sin(y * 53 - x * 11)
            col = [bg[k] * (.35 + .95 * v) + grain for k in range(3)]
            # Accent glow behind the emblem (+ optional rays).
            g = math.exp(-r * r * 2.6) * .75
            if rays:
                ang = math.atan2(y, x)
                g += .22 * max(0.0, math.cos(ang * rays)) ** 6 * max(0.0, 1 - r)
            col = [col[k] + glow[k] * g for k in range(3)]
            for sx, sy, sr in stars:
                d = math.hypot(x - sx, y - sy)
                if d < sr * 3:
                    s = max(0.0, 1 - d / (sr * 3)) ** 2
                    col = [c + s * .8 for c in col]
            for fn, mat, outline, emissive in layers:
                d = fn(x, y)
                width = .045 if outline else 0.0
                if d > width + aa:
                    continue
                base, spec = M[mat]
                # Dark outline ring.
                if outline and d > 0:
                    a = max(0.0, min(1.0, (width + aa - d) / aa))
                    col = [col[k] * (1 - a) + .02 * a for k in range(3)]
                    continue
                cover = max(0.0, min(1.0, (aa - d) / aa)) if not outline else 1.0
                # Bevel lighting from the distance-field gradient near edges.
                e = .01
                nx = fn(x + e, y) - fn(x - e, y)
                ny = fn(x, y + e) - fn(x, y - e)
                nl = math.hypot(nx, ny) + 1e-9
                nx, ny = nx / nl, ny / nl
                depth = max(0.0, min(1.0, -d / .10))
                bevel = (1 - depth) * (-(nx * LIGHT[0] + ny * LIGHT[1]))  # facing the light -> brighter
                shade = .78 + .35 * (-(x * .45 + y * .75)) * .5 + .45 * bevel
                shade = max(.25, shade)
                c = [base[k] * shade for k in range(3)]
                if spec > .5:
                    # Metallic/gem specular streak across the upper-left of each shape.
                    streak = max(0.0, 1 - abs((x + y) * .9 + .25 - (-d) * 2) * 3.2) * (1 - depth * .6)
                    c = [c[k] + streak * .45 * spec for k in range(3)]
                if emissive:
                    c = [base[k] * (1.1 + .5 * depth) for k in range(3)]
                col = [col[k] * (1 - cover) + c[k] * cover for k in range(3)]
            # Inner frame shadow so icons read inside the kit's slot frame.
            edge = max(abs(x), abs(y))
            if edge > .88:
                f = (edge - .88) / .12
                col = [c * (1 - .55 * f) for c in col]
            px.append((min(1, col[0]), min(1, col[1]), min(1, col[2]), 1.0))
    png(OUT / f"T_Item_{item_id}.png", SIZE, SIZE, px)


# ----------------------------------------------------------------------- shape kit
def L(fn, mat, outline=True, emissive=False):
    return (fn, mat, outline, emissive)


def sword(ang=-.785, blade="steel", guard="gold", grip="leather", gem=None, broad=1.0, length=1.0, curve=0.0):
    c, s = math.cos(ang), math.sin(ang)

    def rot(x, y):
        return c * x + s * y, -s * x + c * y

    def blade_fn(x, y):
        u, v = rot(x, y)
        v -= curve * (u + .2) ** 2
        body = box(u, v, .12 * length, 0, .56 * length, .085 * broad)
        tip = poly(u, v, [(.68 * length, -.085 * broad), (.9 * length, 0), (.68 * length, .085 * broad)])
        return union(body, tip)

    def guard_fn(x, y):
        u, v = rot(x, y)
        return box(u, v, -.46, 0, .045, .26, 0, .03)

    def grip_fn(x, y):
        u, v = rot(x, y)
        return box(u, v, -.62, 0, .15, .05, 0, .02)

    def pommel_fn(x, y):
        u, v = rot(x, y)
        return circle(u, v, -.8, 0, .08)
    layers = [L(blade_fn, blade), L(grip_fn, grip), L(guard_fn, guard), L(pommel_fn, guard)]
    if gem:
        layers.append(L(lambda x, y: (lambda u, v: circle(u, v, -.46, 0, .06))(*rot(x, y)), gem))
    return layers


def gem_shape(cx=0, cy=0, s=1.0, mat="ruby"):
    pts = [(cx, cy - .6 * s), (cx + .42 * s, cy - .22 * s), (cx + .3 * s, cy + .5 * s), (cx - .3 * s, cy + .5 * s), (cx - .42 * s, cy - .22 * s)]
    facet = [(cx, cy - .6 * s), (cx + .2 * s, cy - .1 * s), (cx, cy + .5 * s), (cx - .2 * s, cy - .1 * s)]
    return [L(lambda x, y: poly(x, y, pts), mat), L(lambda x, y: poly(x, y, facet), "white" if mat == "glass" else mat, False)]


def flask(liquid, tall=False, cork="wood"):
    if tall:
        body = lambda x, y: union(box(x, y, 0, .2, .26, .45, 0, .12), box(x, y, 0, -.38, .09, .2))
        liq = lambda x, y: inter(box(x, y, 0, .2, .22, .41, 0, .1), -.02 - y)
        cork_fn = lambda x, y: box(x, y, 0, -.6, .12, .08, 0, .03)
    else:
        body = lambda x, y: union(circle(x, y, 0, .22, .48), box(x, y, 0, -.35, .12, .22))
        liq = lambda x, y: inter(circle(x, y, 0, .22, .43), .02 - y)
        cork_fn = lambda x, y: box(x, y, 0, -.6, .15, .08, 0, .03)
    shine = lambda x, y: ellipse(x, y, -.18, .05 if not tall else .0, .06, .16)
    return [L(body, "glass"), L(liq, liquid, False), L(cork_fn, cork), L(shine, "white", False, True)]


def book(cover, gem=None, eye=False):
    layers = [L(lambda x, y: box(x, y, .04, .02, .52, .66, -.12, .05), "parchment"),
              L(lambda x, y: box(x, y, -.03, 0, .5, .64, -.12, .06), cover),
              L(lambda x, y: box(x, y, -.03, 0, .36, .5, -.12, .02), "gold", False)]
    layers.append(L(lambda x, y: box(x, y, -.03, 0, .32, .46, -.12, .02), cover, False))
    if eye:
        layers.append(L(lambda x, y: ellipse(x, y, -.03, 0, .26, .14), "white"))
        layers.append(L(lambda x, y: circle(x, y, -.03, 0, .1), "amethyst", False, True))
    if gem:
        layers += gem_shape(-.03, 0, .32, gem)
    return layers


def boots(mat="leather", trim="bronze", wings=False, plate=False):
    shape = [(-.35, -.7), (.12, -.7), (.12, .25), (.62, .38), (.66, .66), (-.38, .66)]
    layers = [L(lambda x, y: poly(x, y, shape), mat),
              L(lambda x, y: box(x, y, -.12, -.62, .27, .08), trim),
              L(lambda x, y: box(x, y, .14, .6, .52, .06), "darkleather", False)]
    if plate:
        layers.append(L(lambda x, y: box(x, y, -.12, -.15, .25, .22, 0, .06), "steel"))
        layers.append(L(lambda x, y: circle(x, y, -.12, -.15, .07), "gold"))
    if wings:
        layers.append(L(lambda x, y: poly(x, y, [(-.35, -.35), (-.85, -.62), (-.7, -.3), (-.9, -.15), (-.36, -.08)]), "silver"))
    return layers


def shield_tower(face="darksteel", trim="gold", emblem="bone"):
    pts = [(-.55, -.72), (.55, -.72), (.55, .2), (0, .78), (-.55, .2)]
    inner = [(-.42, -.6), (.42, -.6), (.42, .15), (0, .62), (-.42, .15)]
    return [L(lambda x, y: poly(x, y, pts), trim), L(lambda x, y: poly(x, y, inner), face, False),
            L(lambda x, y: union(circle(x, y, 0, -.08, .17), box(x, y, 0, .18, .06, .16)), emblem)]


# ----------------------------------------------------------------------- the catalog
def icons():
    I = {}
    # Consumables
    I["vial_of_crimson"] = ("consumable", (.9, .15, .15), flask("potion_red"))
    I["aether_phial"] = ("consumable", (.25, .5, 1.0), flask("potion_blue", tall=True))
    I["elixir_of_iron"] = ("defense", (.7, .75, .85), flask("potion_iron", tall=True, cork="darksteel") + [L(lambda x, y: box(x, y, 0, .25, .12, .12, .785), "steel")])
    I["elixir_of_wrath"] = ("attack", (1.0, .45, .15), flask("potion_amber") + [L(lambda x, y: poly(x, y, [(0, .0), (.14, .28), (-.02, .26), (.06, .5), (-.14, .2), (.02, .22)]), "flame", True, True)])
    I["elixir_of_black_sorcery"] = ("spell", (.7, .3, 1.0), flask("potion_purple", tall=True) + [L(lambda x, y: ring(x, y, 0, .25, .12, .025), "amethyst", False, True)])
    lantern = [L(lambda x, y: box(x, y, 0, .12, .34, .44, 0, .08), "darksteel"),
               L(lambda x, y: box(x, y, 0, .12, .24, .34, 0, .05), "blueflame", False, True),
               L(lambda x, y: ellipse(x, y, 0, .2, .1, .18), "white", False, True),
               L(lambda x, y: arc(x, y, 0, -.38, .2, .035, math.pi, 2 * math.pi), "darksteel"),
               L(lambda x, y: box(x, y, 0, -.36, .38, .05), "bronze"), L(lambda x, y: box(x, y, 0, .6, .38, .05), "bronze")]
    I["watchers_lantern"] = ("support", (.4, .8, 1.0), lantern)
    I["tome_of_insight"] = ("tome", (.4, .8, 1.0), book("cloth_green", "sapphire"))
    I["tome_of_ascendance"] = ("tome", (1.0, .75, .3), book("cloth_red", "topaz"))
    I["greater_tome_of_ascendance"] = ("tome", (1.0, .6, .2), book("cloth_purple", "ruby"))
    # Basic components
    I["rusted_longsword"] = ("attack", (.9, .35, .2), sword(blade="bronze", guard="darksteel"))
    I["bone_dagger"] = ("attack", (1.0, .8, .4), sword(ang=-.9, blade="bone", guard="bone", grip="darkleather", length=.75, curve=.25))
    wand = [L(lambda x, y: seg(x, y, -.6, .6, .45, -.45, .07), "wood"),
            L(lambda x, y: seg(x, y, .25, -.25, .38, -.38, .1), "gold"),
            L(lambda x, y: circle(x, y, .5, -.5, .17), "amethyst"),
            L(lambda x, y: circle(x, y, .45, -.55, .05), "white", False, True)]
    I["ashwood_wand"] = ("spell", (.7, .35, 1.0), wand)
    I["bloodstone_shard"] = ("defense", (1.0, .2, .2), gem_shape(0, .05, 1.1, "ruby"))
    I["nightglass_shard"] = ("spell", (.3, .5, 1.0), gem_shape(0, .05, 1.1, "sapphire"))
    I["boiled_jerkin"] = ("defense", (.8, .6, .35), [
        L(lambda x, y: poly(x, y, [(-.62, -.52), (-.25, -.66), (0, -.48), (.25, -.66), (.62, -.52), (.5, .0), (.42, .7), (-.42, .7), (-.5, .0)]), "leather"),
        L(lambda x, y: box(x, y, 0, .06, .05, .56), "darkleather", False),
        L(lambda x, y: union(*[circle(x, y, .14, -.3 + k * .22, .04) for k in range(4)]), "bronze", False)])
    I["hexweave_cloak"] = ("spell", (.5, .6, 1.0), [
        L(lambda x, y: poly(x, y, [(-.25, -.7), (.25, -.7), (.62, .72), (0, .6), (-.62, .72)]), "cloth_grey"),
        L(lambda x, y: ring(x, y, 0, .08, .22, .03), "sapphire", False, True),
        L(lambda x, y: poly(x, y, [(0, -.1), (.17, .2), (-.17, .2)]), "sapphire", False, True),
        L(lambda x, y: circle(x, y, 0, -.6, .1), "gold")])
    fist = [L(lambda x, y: box(x, y, 0, .38, .3, .3, 0, .08), "darksteel"),
            L(lambda x, y: union(*[box(x, y, -.24 + k * .16, -.18, .075, .24, 0, .06) for k in range(4)]), "steel"),
            L(lambda x, y: box(x, y, .36, .02, .1, .2, -.5, .05), "steel"),
            L(lambda x, y: box(x, y, 0, .38, .32, .06), "gold", False)]
    I["gauntlet_of_the_ox"] = ("attack", (1.0, .5, .3), fist)
    I["band_of_the_fox"] = ("speed", (.5, 1.0, .5), [L(lambda x, y: ring(x, y, 0, .1, .45, .1), "bronze"),
                                                   L(lambda x, y: circle(x, y, 0, -.36, .16), "emerald")])
    I["circlet_of_the_owl"] = ("spell", (.5, .6, 1.0), [
        L(lambda x, y: arc(x, y, 0, .55, .7, .07, math.pi * 1.1, math.pi * 1.9), "silver"),
        L(lambda x, y: poly(x, y, [(-.2, -.02), (0, -.42), (.2, -.02)]), "silver"),
        L(lambda x, y: circle(x, y, 0, -.08, .13), "sapphire")])
    I["road_worn_boots"] = ("speed", (.6, .9, .4), boots())
    hourglass = [L(lambda x, y: box(x, y, 0, -.62, .42, .07), "wood"), L(lambda x, y: box(x, y, 0, .62, .42, .07), "wood"),
                 L(lambda x, y: union(poly(x, y, [(-.32, -.54), (.32, -.54), (.05, 0), (.32, .54), (-.32, .54), (-.05, 0)])), "glass"),
                 L(lambda x, y: union(poly(x, y, [(-.12, -.2), (.12, -.2), (0, -.02)]), poly(x, y, [(-.25, .5), (.25, .5), (0, .22)])), "topaz", False, True),
                 L(lambda x, y: union(box(x, y, -.38, 0, .04, .6), box(x, y, .38, 0, .04, .6)), "wood")]
    I["sandglass_charm"] = ("support", (1.0, .8, .4), hourglass)
    fang = [L(lambda x, y: poly(x, y, [(-.35, -.62), (.35, -.62), (.2, -.1), (.02, .66), (-.12, .05)]), "bone"),
            L(lambda x, y: circle(x, y, .03, .5, .07), "blood", False, True),
            L(lambda x, y: box(x, y, 0, -.62, .4, .08), "darkleather")]
    I["vampire_fang"] = ("attack", (.9, .1, .15), fang)
    eye = [L(lambda x, y: ellipse(x, y, 0, 0, .7, .36), "obsidian"),
           L(lambda x, y: circle(x, y, 0, 0, .28), "topaz", False, True),
           L(lambda x, y: ellipse(x, y, 0, 0, .06, .24), "obsidian", False),
           L(lambda x, y: circle(x, y, -.1, -.1, .05), "white", False, True)]
    I["ravens_eye"] = ("crit", (1.0, .75, .3), eye)
    I["gravemoss_pendant"] = ("support", (.5, 1.0, .5), [
        L(lambda x, y: arc(x, y, 0, -.25, .45, .03, math.pi * 1.05, math.pi * 1.95), "silver"),
        L(lambda x, y: ellipse(x, y, 0, .25, .3, .38), "silver"),
        L(lambda x, y: ellipse(x, y, 0, .25, .22, .3), "moss", False),
        L(lambda x, y: union(circle(x, y, -.06, .18, .06), circle(x, y, .08, .33, .05)), "emerald", False, True)])
    idol = [L(lambda x, y: circle(x, y, 0, -.42, .2), "stone"),
            L(lambda x, y: poly(x, y, [(-.16, -.22), (.16, -.22), (.36, .68), (-.36, .68)]), "stone"),
            L(lambda x, y: union(seg(x, y, -.12, -.05, -.34, .18, .07), seg(x, y, .12, -.05, .34, .18, .07)), "stone"),
            L(lambda x, y: ring(x, y, 0, -.42, .3, .025), "topaz", False, True)]
    I["pilgrims_idol"] = ("support", (1.0, .85, .5), idol)
    # Epic components
    I["serrated_cleaver"] = ("attack", (1.0, .4, .2), [
        L(lambda x, y: poly(x, y, [(-.5, -.35), (.62, -.55), (.66, .1), (.52, .02), (.44, .14), (.34, .04), (.24, .16), (.12, .06), (-.5, .1)]), "steel"),
        L(lambda x, y: box(x, y, -.66, -.12, .2, .07, .1, .03), "darkleather"),
        L(lambda x, y: circle(x, y, .46, -.34, .06), "obsidian", False)])
    I["grimoire_of_whispers"] = ("spell", (.7, .3, 1.0), book("cloth_purple", eye=True))
    chain = [L(lambda x, y: poly(x, y, [(-.66, -.5), (-.25, -.66), (0, -.5), (.25, -.66), (.66, -.5), (.55, .05), (.45, .7), (-.45, .7), (-.55, .05)]), "steel"),
             L(lambda x, y: union(*[ring(x, y, -.36 + (k % 5) * .18 + (k // 5 % 2) * .09, -.3 + (k // 5) * .2, .07, .018) for k in range(25)]), "darksteel", False),
             L(lambda x, y: box(x, y, 0, .55, .46, .06), "bronze", False)]
    I["chainmail_of_the_fallen"] = ("defense", (.6, .7, .9), chain)
    I["bloodletter"] = ("attack", (.9, .1, .15), sword(blade="silver", guard="blood", grip="darkleather", broad=.7) +
                        [L(lambda x, y: union(circle(x, y, .35, .45, .06), circle(x, y, .52, .62, .045)), "blood", False, True)])
    I["mantle_of_ash"] = ("defense", (.8, .45, .3), [
        L(lambda x, y: poly(x, y, [(-.3, -.7), (.3, -.7), (.68, .7), (.2, .55), (0, .72), (-.2, .55), (-.68, .7)]), "cloth_grey"),
        L(lambda x, y: union(ring(x, y, 0, -.1, .2, .03), seg(x, y, 0, -.3, 0, .1, .03)), "flame", False, True),
        L(lambda x, y: circle(x, y, 0, -.6, .1), "ruby")])
    # Legendary: boots
    I["greaves_of_the_undying"] = ("defense", (.6, .8, 1.0), boots("darksteel", "gold", plate=True))
    I["stalkers_treads"] = ("speed", (1.0, .8, .3), boots("darkleather", "gold") + [L(lambda x, y: seg(x, y, -.15, -.45, .05, .15, .05), "steel")])
    I["veilwalker_slippers"] = ("spell", (.6, .6, 1.0), boots("cloth_purple", "silver", wings=True))
    # Legendary: physical
    I["nightfall_reaver"] = ("attack", (.6, .3, 1.0), sword(blade="darksteel", guard="gold", grip="darkleather", gem="amethyst", broad=1.25, curve=.12) +
                             [L(lambda x, y: arc(x, y, .25, -.25, .55, .03, math.pi * .95, math.pi * 1.55), "amethyst", False, True)])
    I["sanguine_sabre"] = ("attack", (1.0, .15, .2), sword(ang=-.7, blade="silver", guard="gold", grip="cloth_red", gem="ruby", broad=.85, curve=.3))
    axe = [L(lambda x, y: seg(x, y, -.55, .72, .4, -.62, .06), "wood"),
           L(lambda x, y: sub(poly(x, y, [(.0, -.62), (.62, -.7), (.72, -.05), (.3, .18), (.12, -.2)]), circle(x, y, .62, -.2, .22)), "steel"),
           L(lambda x, y: box(x, y, .15, -.36, .09, .14, -.7, .02), "gold")]
    I["headsmans_greataxe"] = ("attack", (.9, .3, .2), axe)
    bow = [L(lambda x, y: arc(x, y, .55, 0, .95, .06, math.pi * .68, math.pi * 1.32), "darksteel"),
           L(lambda x, y: seg(x, y, -.25, -.62, -.25, .62, .012), "white", False, True),
           L(lambda x, y: union(seg(x, y, -.25, 0, .7, 0, .025), poly(x, y, [(.62, -.08), (.8, 0), (.62, .08)])), "silver"),
           L(lambda x, y: circle(x, y, -.4, 0, .07), "emerald", False, True)]
    I["wraithstring"] = ("speed", (.4, 1.0, .7), bow)
    # Legendary: spell
    crown = [L(lambda x, y: poly(x, y, [(-.6, .45), (-.62, -.3), (-.32, .05), (0, -.55), (.32, .05), (.62, -.3), (.6, .45)]), "gold"),
             L(lambda x, y: box(x, y, 0, .38, .6, .09), "bronze"),
             L(lambda x, y: union(circle(x, y, 0, .38, .07), circle(x, y, -.36, .38, .05), circle(x, y, .36, .38, .05)), "ruby"),
             L(lambda x, y: union(poly(x, y, [(-.1, -.55), (0, -.85), (.08, -.6)]), poly(x, y, [(-.5, -.35), (-.45, -.62), (-.36, -.4)]), poly(x, y, [(.5, -.35), (.47, -.64), (.38, -.42)])), "flame", False, True)]
    I["crown_of_cinders"] = ("spell", (1.0, .45, .15), crown)
    orb = [L(lambda x, y: poly(x, y, [(-.35, .72), (.35, .72), (.2, .45), (-.2, .45)]), "darksteel"),
           L(lambda x, y: circle(x, y, 0, -.05, .5), "obsidian"),
           L(lambda x, y: ring(x, y, 0, -.05, .28, .04), "amethyst", False, True),
           L(lambda x, y: circle(x, y, 0, -.05, .12), "amethyst", False, True),
           L(lambda x, y: ellipse(x, y, -.2, -.28, .1, .06), "white", False, True)]
    I["voidglass_orb"] = ("spell", (.6, .2, 1.0), orb)
    I["hourglass_of_ages"] = ("spell", (1.0, .8, .3), [L(lambda x, y: ring(x, y, 0, 0, .78, .04), "gold", False)] + hourglass)
    # Legendary: tank
    I["gravewarden_bulwark"] = ("defense", (.6, .75, 1.0), shield_tower() + [
        L(lambda x, y: union(*[poly(x, y, [(sx - .06, sy), (sx, sy - .14), (sx + .06, sy)]) for sx, sy in ((-.5, -.72), (0, -.72), (.5, -.72))]), "steel")])
    aegis = [L(lambda x, y: circle(x, y, 0, 0, .72), "gold"), L(lambda x, y: circle(x, y, 0, 0, .6), "sapphire", False),
             L(lambda x, y: circle(x, y, 0, 0, .5), "darksteel", False),
             L(lambda x, y: union(box(x, y, 0, 0, .07, .42), box(x, y, 0, -.08, .3, .07)), "silver"),
             L(lambda x, y: ring(x, y, 0, 0, .66, .015), "white", False, True)]
    I["aegis_of_the_last_oath"] = ("defense", (.5, .7, 1.0), aegis)
    heart = [L(lambda x, y: union(circle(x, y, -.25, -.18, .32), circle(x, y, .25, -.18, .32), poly(x, y, [(-.55, -.05), (.55, -.05), (0, .68)])), "stone"),
             L(lambda x, y: union(seg(x, y, -.2, -.3, .05, .05, .02), seg(x, y, .05, .05, -.05, .35, .02), seg(x, y, .05, .05, .3, -.1, .02)), "flame", False, True)]
    I["stoneheart"] = ("defense", (1.0, .5, .2), heart)
    bell = [L(lambda x, y: union(poly(x, y, [(-.3, -.45), (.3, -.45), (.42, .2), (.6, .48), (-.6, .48), (-.42, .2)]), circle(x, y, 0, -.45, .3)), "bronze"),
            L(lambda x, y: circle(x, y, 0, .6, .1), "darksteel"),
            L(lambda x, y: ring(x, y, 0, -.8, .09, .03), "darksteel"),
            L(lambda x, y: union(arc(x, y, 0, .1, .8, .02, -.5, .5), arc(x, y, 0, .1, .8, .02, math.pi - .5, math.pi + .5)), "white", False, True)]
    I["gravebell"] = ("defense", (.7, .8, .6), bell)
    # Legendary: support
    censer = [L(lambda x, y: union(seg(x, y, 0, -.85, 0, -.32, .02)), "silver"),
              L(lambda x, y: union(circle(x, y, 0, .05, .36), box(x, y, 0, -.28, .16, .06)), "gold"),
              L(lambda x, y: union(*[circle(x, y, -.2 + k * .2, .05, .05) for k in range(3)]), "flame", False, True),
              L(lambda x, y: union(circle(x, y, -.25, -.55, .1), circle(x, y, .3, -.7, .08), circle(x, y, .1, -.9, .06)), "cloth_grey", False)]
    I["censer_of_dawn"] = ("support", (1.0, .85, .4), censer)
    chalice = [L(lambda x, y: union(poly(x, y, [(-.5, -.5), (.5, -.5), (.3, .0), (-.3, .0)]), box(x, y, 0, .25, .06, .25), box(x, y, 0, .58, .32, .07, 0, .04)), "gold"),
               L(lambda x, y: inter(poly(x, y, [(-.46, -.46), (.46, -.46), (.3, -.08), (-.3, -.08)]), -.38 - y), "potion_red", False, True),
               L(lambda x, y: circle(x, y, 0, .2, .07), "ruby")]
    I["chalice_of_mercy"] = ("support", (1.0, .3, .35), chalice)
    banner = [L(lambda x, y: seg(x, y, -.5, -.78, -.5, .8, .04), "wood"),
              L(lambda x, y: poly(x, y, [(-.46, -.66), (.55, -.66), (.55, .4), (.05, .2), (-.46, .4)]), "cloth_red"),
              L(lambda x, y: union(circle(x, y, .05, -.2, .16), poly(x, y, [(.05, -.5), (.12, -.2), (.05, .1), (-.02, -.2)])), "gold", False),
              L(lambda x, y: circle(x, y, -.5, -.8, .07), "gold")]
    I["banner_of_the_vigil"] = ("support", (1.0, .8, .4), banner)
    def vane(x, y):
        c, s_ = math.cos(.785), math.sin(.785)
        u, v = c * x - s_ * y, s_ * x + c * y  # u along the quill (bottom-left -> top-right)
        body = ellipse(u, v, .12, 0, .62, .24)
        notch = union(box(u, v, -.05, .22, .03, .12, .5), box(u, v, .3, -.2, .03, .12, -.5), box(u, v, .45, .17, .025, .1, .6))
        return sub(body, notch)
    feather = [L(vane, "feather"),
               L(lambda x, y: seg(x, y, -.66, .66, .5, -.5, .03), "silver"),
               L(lambda x, y: circle(x, y, -.58, .58, .07), "sapphire", False, True)]
    I["ravenfeather_mantle"] = ("spell", (.4, .6, 1.0), feather)
    return I


def main():
    catalog = json.loads((ROOT / "Content/Data/Items.json").read_text(encoding="utf-8"))
    ids = [item["id"] for item in catalog["items"]]
    defs = icons()
    missing = [i for i in ids if i not in defs]
    if missing:
        raise SystemExit(f"No icon recipe for: {missing}")
    args = sys.argv[1:]
    procedural = "--procedural" in args
    only = {a for a in args if not a.startswith("--")}
    painted = 0
    for index, item_id in enumerate(ids):
        if only and item_id not in only:
            continue
        override = PAINTED / f"T_Item_{item_id}.png"
        if override.is_file() and not procedural:
            OUT.mkdir(parents=True, exist_ok=True)
            (OUT / override.name).write_bytes(override.read_bytes())
            painted += 1
            print("icon", item_id, "(painted)", flush=True)
            continue
        theme, glow, layers = defs[item_id]
        tier = next(item["tier"] for item in catalog["items"] if item["id"] == item_id)
        sparkle = {"legendary": 6, "epic": 3}.get(tier, 0)
        rays = 8 if tier == "legendary" else 0
        glow_scale = {"legendary": 1.0, "epic": .8, "basic": .55, "consumable": .6}[tier]
        render(item_id, theme, tuple(c * glow_scale for c in glow), layers, seed=index + 7, sparkle=sparkle, rays=rays)
        print("icon", item_id, flush=True)
    print(f"CIRE_ITEM_ICONS_PASS count={len(ids)} painted={painted} out={OUT}")


if __name__ == "__main__":
    main()
