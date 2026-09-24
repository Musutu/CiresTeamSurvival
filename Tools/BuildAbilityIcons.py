"""Paint the shared ability icon set (original procedural art, no licensed sources).

Every skill in the native draft pool plus every planned roster skill gets a
256x256 icon: a school-colored painted backdrop (radial light, grain, vignette)
and a distinct glyph with dark outline, rim highlight and glow. Output PNGs go
to --out (default Saved/AbilityIcons/<stamp>); Tools/RunAbilityIcons.py imports
them as /Game/UI/Abilities/T_<id> (the CireUIStyle::FindAbilityIcon convention). Requires Pillow (pip install pillow;
--pylib adds a directory holding it to sys.path).
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import random
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
SIZE = 256
SS = 4  # supersampling
W = SIZE * SS

# name: (deep, mid, glyph, glow)
PALETTES = {
    'holy':     ((36, 24, 10), (128, 92, 38), (255, 232, 170), (255, 196, 90)),
    'light':    ((34, 28, 16), (140, 116, 64), (255, 246, 214), (255, 226, 140)),
    'frost':    ((8, 20, 38), (40, 96, 150), (214, 242, 255), (120, 200, 255)),
    'fire':     ((38, 10, 4), (150, 48, 12), (255, 214, 140), (255, 120, 30)),
    'ember':    ((34, 14, 6), (130, 60, 20), (255, 200, 120), (255, 140, 50)),
    'ash':      ((22, 18, 16), (92, 70, 56), (240, 208, 180), (230, 120, 60)),
    'venom':    ((10, 24, 6), (60, 110, 20), (214, 255, 150), (150, 240, 60)),
    'shadow':   ((16, 8, 28), (70, 36, 110), (226, 200, 255), (160, 90, 255)),
    'steel':    ((14, 18, 24), (70, 84, 100), (236, 242, 250), (170, 200, 230)),
    'war':      ((34, 8, 8), (120, 30, 26), (255, 220, 200), (240, 80, 60)),
    'blood':    ((30, 4, 6), (116, 14, 22), (255, 196, 190), (230, 40, 50)),
    'arcane':   ((10, 12, 36), (46, 60, 150), (210, 236, 255), (110, 170, 255)),
    'spectral': ((6, 24, 26), (26, 100, 104), (200, 255, 246), (90, 230, 210)),
    'spirit':   ((10, 26, 22), (60, 120, 104), (230, 255, 240), (160, 255, 210)),
    'stone':    ((22, 20, 18), (96, 88, 76), (238, 228, 208), (200, 180, 140)),
    'earth':    ((26, 18, 8), (112, 82, 40), (250, 226, 180), (220, 160, 80)),
    'primal':   ((26, 16, 8), (110, 70, 34), (255, 222, 170), (230, 150, 70)),
    'nature':   ((8, 24, 10), (44, 108, 42), (220, 255, 190), (130, 230, 100)),
    'ether':    ((6, 18, 34), (30, 84, 140), (200, 240, 255), (90, 190, 255)),
    'fel':      ((6, 22, 8), (40, 110, 24), (210, 255, 160), (120, 255, 60)),
    'dragon':   ((32, 10, 4), (130, 50, 16), (255, 222, 150), (255, 150, 40)),
}


def P(x, y):
    return (x * W, y * W)


class Glyph:
    """Unit-square drawing helpers onto a supersampled mask."""

    def __init__(self, draw):
        self.d = draw

    def poly(self, pts, fill=255):
        self.d.polygon([P(x, y) for x, y in pts], fill=fill)

    def line(self, pts, w, fill=255):
        self.d.line([P(x, y) for x, y in pts], fill=fill, width=int(w * W), joint='curve')
        for x, y in (pts[0], pts[-1]):
            self.circle(x, y, w / 2, fill)

    def circle(self, x, y, r, fill=255):
        self.d.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=fill)

    def ring(self, x, y, r, w, fill=255):
        self.d.ellipse([P(x - r, y - r), P(x + r, y + r)], outline=fill, width=int(w * W))

    def arc(self, x, y, r, a0, a1, w, fill=255):
        self.d.arc([P(x - r, y - r), P(x + r, y + r)], a0, a1, fill=fill, width=int(w * W))

    def rect(self, x0, y0, x1, y1, fill=255):
        self.d.rectangle([P(x0, y0), P(x1, y1)], fill=fill)

    def star(self, x, y, r0, r1, n, rot=-90, fill=255):
        pts = []
        for i in range(n * 2):
            a = math.radians(rot + i * 180 / n)
            r = r1 if i % 2 == 0 else r0
            pts.append((x + math.cos(a) * r, y + math.sin(a) * r))
        self.poly(pts, fill)


def rot(pts, cx, cy, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in pts]


def smooth(pts, n=3):
    """Chaikin corner cutting for closed outlines."""
    for _ in range(n):
        out = []
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            out += [(x0 * .75 + x1 * .25, y0 * .75 + y1 * .25), (x0 * .25 + x1 * .75, y0 * .25 + y1 * .75)]
        pts = out
    return pts


# ---------------------------------------------------------------- glyph library
def shield(g, cx=.5, cy=.5, s=1.0, boss=True):
    pts = [(-.26, -.30), (0, -.36), (.26, -.30), (.25, .02), (.15, .22), (0, .34), (-.15, .22), (-.25, .02)]
    g.poly([(cx + x * s, cy + y * s) for x, y in pts])
    if boss:
        g.poly([(cx + x * s * .62, cy + y * s * .62 - .02 * s) for x, y in pts], 0)
        g.line([(cx, cy - .2 * s), (cx, cy + .18 * s)], .05 * s)
        g.line([(cx - .13 * s, cy - .04 * s), (cx + .13 * s, cy - .04 * s)], .05 * s)


def sword(g, cx=.5, cy=.5, s=1.0, angle=45):
    blade = [(-.05, -.38), (0, -.47), (.05, -.38), (.05, .14), (-.05, .14)]
    guard = [(-.17, .13), (.17, .13), (.14, .20), (-.14, .20)]
    grip = [(-.03, .19), (.03, .19), (.03, .33), (-.03, .33)]
    for part in (blade, guard, grip):
        g.poly(rot([(cx + x * s, cy + y * s) for x, y in part], cx, cy, angle))
    px, py = rot([(cx, cy + .37 * s)], cx, cy, angle)[0]
    g.circle(px, py, .045 * s)


def axe(g, cx=.5, cy=.5, s=1.0, angle=30, flip=False):
    k = -1 if flip else 1
    haft = [(-.026, -.40), (.026, -.40), (.026, .42), (-.026, .42)]
    head = [(.02, -.27), (.10, -.30), (.20, -.43), (.30, -.32), (.35, -.14), (.32, .03), (.24, .15), (.13, -.01), (.02, -.07)]
    g.poly(rot([(cx + x * s, cy + y * s) for x, y in haft], cx, cy, angle))
    g.poly(smooth(rot([(cx + k * x * s, cy + y * s) for x, y in head], cx, cy, angle), 1))
    edge = [(.22, -.34), (.28, -.24), (.30, -.12), (.28, .0), (.23, .08)]
    g.line(rot([(cx + k * x * s, cy + y * s) for x, y in edge], cx, cy, angle), .02 * s, 0)


def flame(g, cx=.5, cy=.55, s=1.0, cut=True):
    outer = [(0, .36), (-.20, .28), (-.28, .08), (-.24, -.10), (-.14, -.20), (-.16, -.38), (-.04, -.26), (0, -.50),
             (.08, -.26), (.20, -.34), (.20, -.14), (.28, .04), (.22, .26)]
    g.poly(smooth([(cx + x * s, cy + y * s) for x, y in outer]))
    if cut:
        inner = [(0, .30), (-.11, .24), (-.14, .10), (-.08, -.04), (-.02, -.18), (.04, -.06), (.12, .04), (.14, .18), (.10, .26)]
        g.poly(smooth([(cx + x * s, cy + .02 * s + y * s) for x, y in inner]), 0)


def snowflake(g, cx=.5, cy=.5, s=1.0):
    for k in range(6):
        a = math.radians(k * 60 - 90)
        ex, ey = cx + math.cos(a) * .36 * s, cy + math.sin(a) * .36 * s
        g.line([(cx, cy), (ex, ey)], .045 * s)
        for f in (.55, .78):
            bx, by = cx + math.cos(a) * .36 * s * f, cy + math.sin(a) * .36 * s * f
            for side in (-1, 1):
                b = a + side * math.radians(45)
                g.line([(bx, by), (bx + math.cos(b) * .09 * s, by + math.sin(b) * .09 * s)], .035 * s)
    g.circle(cx, cy, .07 * s)


def bolt(g, cx=.5, cy=.5, s=1.0):
    pts = [(.10, -.42), (-.14, .02), (.02, .02), (-.10, .42), (.18, -.06), (.02, -.06), (.14, -.42)]
    g.poly([(cx + x * s, cy + y * s) for x, y in pts])


def arrow(g, cx=.5, cy=.5, s=1.0, angle=-45, fletch=True):
    shaft = [(-.02, -.28), (.02, -.28), (.02, .40), (-.02, .40)]
    head = [(0, -.46), (.09, -.26), (-.09, -.26)]
    parts = [shaft, head]
    if fletch:
        parts += [[(-.02, .26), (-.11, .38), (-.11, .46), (-.02, .36)], [(.02, .26), (.11, .38), (.11, .46), (.02, .36)]]
    for part in parts:
        g.poly(rot([(cx + x * s, cy + y * s) for x, y in part], cx, cy, angle))


def skull(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy - .06 * s, .24 * s)
    g.rect(cx - .13 * s, cy + .08 * s, cx + .13 * s, cy + .26 * s)
    for dx in (-.09, .09):
        g.circle(cx + dx * s, cy - .03 * s, .065 * s, 0)
    g.poly([(cx, cy + .06 * s), (cx - .035 * s, cy + .13 * s), (cx + .035 * s, cy + .13 * s)], 0)
    for dx in (-.07, 0, .07):
        g.rect(cx + dx * s - .012 * s, cy + .18 * s, cx + dx * s + .012 * s, cy + .27 * s, 0)


def cross(g, cx=.5, cy=.5, s=1.0, w=.12):
    g.rect(cx - w / 2 * s, cy - .36 * s, cx + w / 2 * s, cy + .36 * s)
    g.rect(cx - .30 * s, cy - .14 * s, cx + .30 * s, cy - .14 * s + w * s)


def sun(g, cx=.5, cy=.5, s=1.0, rays=12, core=.15):
    g.star(cx, cy, core * 1.25 * s, .40 * s, rays)
    g.circle(cx, cy, core * s)


def dome(g, cx=.5, cy=.62, s=1.0):
    g.arc(cx, cy, .34 * s, 180, 360, .06 * s)
    g.arc(cx, cy, .22 * s, 180, 360, .04 * s)
    g.rect(cx - .40 * s, cy, cx + .40 * s, cy + .05 * s)
    for k in range(5):
        a = math.radians(180 + k * 45)
        g.line([(cx + math.cos(a) * .22 * s, cy + math.sin(a) * .22 * s), (cx + math.cos(a) * .34 * s, cy + math.sin(a) * .34 * s)], .03 * s)


def wall(g, cx=.5, cy=.55, s=1.0):
    rows = 4
    for r in range(rows):
        y0 = cy - .28 * s + r * .15 * s
        off = .09 * s if r % 2 else 0
        for c in range(-2, 3):
            x0 = cx - .36 * s + c * .18 * s + off + .36 * s
            x0 -= .36 * s
            if x0 + .16 * s < cx - .38 * s or x0 > cx + .38 * s:
                continue
            g.rect(max(x0, cx - .38 * s), y0, min(x0 + .16 * s, cx + .38 * s), y0 + .12 * s)


def fist(g, cx=.5, cy=.52, s=1.0):
    g.rect(cx - .20 * s, cy - .12 * s, cx + .18 * s, cy + .16 * s)
    for k in range(4):
        g.circle(cx - .15 * s + k * .1 * s, cy - .14 * s, .06 * s)
    g.poly([(cx - .20 * s, cy + .02 * s), (cx - .30 * s, cy - .06 * s), (cx - .30 * s, cy + .08 * s), (cx - .20 * s, cy + .14 * s)])
    g.rect(cx - .14 * s, cy + .16 * s, cx + .12 * s, cy + .36 * s)


def dash(g, cx=.5, cy=.5, s=1.0):
    for k, (dy, l) in enumerate(((-.16, .30), (0, .42), (.16, .30))):
        g.line([(cx - l * s + .1 * s, cy + dy * s), (cx + .1 * s, cy + dy * s)], .05 * s)
    g.poly([(cx + .14 * s, cy - .26 * s), (cx + .40 * s, cy), (cx + .14 * s, cy + .26 * s), (cx + .22 * s, cy)])


def heart(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx - .12 * s, cy - .08 * s, .15 * s)
    g.circle(cx + .12 * s, cy - .08 * s, .15 * s)
    g.poly([(cx - .26 * s, cy - .02 * s), (cx + .26 * s, cy - .02 * s), (cx, cy + .30 * s)])


def drop(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy + .10 * s, .20 * s)
    g.poly([(cx - .18 * s, cy + .02 * s), (cx, cy - .36 * s), (cx + .18 * s, cy + .02 * s)])


def paw(g, cx=.5, cy=.5, s=1.0):
    g.d.ellipse([P(cx - .16 * s, cy - .02 * s), P(cx + .16 * s, cy + .24 * s)], fill=255)
    for dx, dy in ((-.2, -.08), (-.08, -.2), (.08, -.2), (.2, -.08)):
        g.circle(cx + dx * s, cy + dy * s, .065 * s)


def helm(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy - .02 * s, .26 * s)
    g.rect(cx - .26 * s, cy - .02 * s, cx + .26 * s, cy + .30 * s)
    g.rect(cx - .20 * s, cy - .02 * s, cx + .20 * s, cy + .04 * s, 0)
    g.rect(cx - .03 * s, cy + .04 * s, cx + .03 * s, cy + .26 * s, 0)
    g.poly([(cx - .04 * s, cy - .28 * s), (cx, cy - .44 * s), (cx + .04 * s, cy - .28 * s)])


def horn(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .32 * s, cy - .04 * s), (cx + .10 * s, cy - .24 * s), (cx + .10 * s, cy + .24 * s), (cx - .32 * s, cy + .06 * s)])
    g.rect(cx - .40 * s, cy - .07 * s, cx - .30 * s, cy + .09 * s)
    for k, r in enumerate((.20, .30, .40)):
        g.arc(cx + .02 * s, cy, r * s, -40, 40, .035 * s)


def spiral(g, cx=.5, cy=.5, s=1.0, turns=2.2):
    pts = []
    for i in range(160):
        t = i / 159
        a = t * turns * 2 * math.pi
        r = .04 + .32 * t
        pts.append((cx + math.cos(a) * r * s, cy + math.sin(a) * r * s))
    g.line(pts, .045 * s)


def eye(g, cx=.5, cy=.5, s=1.0):
    g.d.chord([P(cx - .38 * s, cy - .30 * s), P(cx + .38 * s, cy + .30 * s)], 200, 340, fill=255)
    g.d.chord([P(cx - .38 * s, cy - .30 * s), P(cx + .38 * s, cy + .30 * s)], 20, 160, fill=255)
    g.circle(cx, cy, .13 * s, 0)
    g.circle(cx, cy, .06 * s)


def crown(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .32 * s, cy + .18 * s), (cx - .32 * s, cy - .16 * s), (cx - .16 * s, cy + .02 * s), (cx, cy - .28 * s),
            (cx + .16 * s, cy + .02 * s), (cx + .32 * s, cy - .16 * s), (cx + .32 * s, cy + .18 * s)])
    g.rect(cx - .32 * s, cy + .22 * s, cx + .32 * s, cy + .30 * s)


def meteor(g, cx=.55, cy=.55, s=1.0):
    g.star(cx + .06 * s, cy + .06 * s, .08 * s, .20 * s, 5)
    for k, off in enumerate((-.08, 0, .08)):
        g.line([(cx - .36 * s + off * s, cy - .36 * s - off * s), (cx - .02 * s + off * s, cy - .02 * s - off * s)], (.03 if k != 1 else .05) * s)


def leaf(g, cx=.5, cy=.5, s=1.0, angle=-35):
    pts = []
    for i in range(41):
        t = i / 40
        y = -.38 + .76 * t
        w = .20 * math.sin(math.pi * t) ** .8
        pts.append((w, y))
    pts += [(-x, y) for x, y in reversed(pts)]
    g.poly(rot([(cx + x * s, cy + y * s) for x, y in pts], cx, cy, angle))
    g.line(rot([(cx, cy - .30 * s), (cx, cy + .44 * s)], cx, cy, angle), .025 * s, 0)


def roots(g, cx=.5, cy=.5, s=1.0):
    rnd = random.Random(7)
    for k in range(5):
        x = cx + (k - 2) * .13 * s
        pts = [(x, cy - .36 * s)]
        for j in range(1, 6):
            pts.append((x + rnd.uniform(-.05, .05) * s + (k - 2) * .03 * j * s, cy - .36 * s + j * .15 * s))
        g.line(pts, (.05 - .004 * abs(k - 2)) * s)


def tusk(g, cx=.5, cy=.5, s=1.0, flip=False):
    k = -1 if flip else 1
    pts = []
    for i in range(30):
        t = i / 29
        a = math.radians(200 - 150 * t)
        r = .32
        w = .14 * (1 - t) + .015
        pts.append((cx + k * math.cos(a) * r * s, cy - math.sin(a) * r * s + .1 * s, w))
    left = [(x - k * w * .0, y - w * s) for x, y, w in pts]
    right = [(x, y + w * s * .6) for x, y, w in reversed(pts)]
    g.poly(left + right)


def totem(g, cx=.5, cy=.5, s=1.0):
    g.rect(cx - .14 * s, cy - .40 * s, cx + .14 * s, cy + .40 * s)
    for y in (-.26, 0, .26):
        g.rect(cx - .10 * s, cy + (y - .03) * s, cx - .03 * s, cy + (y + .02) * s, 0)
        g.rect(cx + .03 * s, cy + (y - .03) * s, cx + .10 * s, cy + (y + .02) * s, 0)
        g.rect(cx - .06 * s, cy + (y + .07) * s, cx + .06 * s, cy + (y + .09) * s, 0)
    g.poly([(cx - .14 * s, cy - .30 * s), (cx - .32 * s, cy - .40 * s), (cx - .14 * s, cy - .18 * s)])
    g.poly([(cx + .14 * s, cy - .30 * s), (cx + .32 * s, cy - .40 * s), (cx + .14 * s, cy - .18 * s)])


def lantern(g, cx=.5, cy=.5, s=1.0):
    g.ring(cx, cy - .36 * s, .06 * s, .025 * s)
    g.poly([(cx - .12 * s, cy - .26 * s), (cx + .12 * s, cy - .26 * s), (cx + .18 * s, cy - .18 * s), (cx - .18 * s, cy - .18 * s)])
    g.rect(cx - .16 * s, cy - .18 * s, cx + .16 * s, cy + .24 * s)
    g.rect(cx - .11 * s, cy - .12 * s, cx + .11 * s, cy + .18 * s, 0)
    flame(g, cx, cy + .06 * s, .38 * s, cut=False)
    g.rect(cx - .20 * s, cy + .24 * s, cx + .20 * s, cy + .31 * s)


def wing(g, cx=.5, cy=.5, s=1.0, flip=False):
    k = -1 if flip else 1
    pts = [(-.34, -.06), (-.10, -.24), (.14, -.34), (.40, -.40), (.30, -.10), (.20, -.14), (.18, .10), (.06, -.02),
           (.00, .24), (-.10, .04), (-.20, .20), (-.24, .02), (-.34, .08)]
    g.poly([(cx + k * x * s, cy + y * s) for x, y in pts])
    for x, y in ((.30, -.10), (.18, .10), (.00, .24), (-.20, .20)):
        g.line([(cx - k * .30 * s, cy - .02 * s), (cx + k * x * s, cy + y * s)], .018 * s, 0)


def moon(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy, .32 * s)
    g.circle(cx + .14 * s, cy - .08 * s, .27 * s, 0)


def beam(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .06 * s, cy - .45 * s), (cx + .06 * s, cy - .45 * s), (cx + .20 * s, cy + .40 * s), (cx - .20 * s, cy + .40 * s)])
    g.poly([(cx - .02 * s, cy - .45 * s), (cx + .02 * s, cy - .45 * s), (cx + .08 * s, cy + .40 * s), (cx - .08 * s, cy + .40 * s)], 0)
    g.d.ellipse([P(cx - .34 * s, cy + .30 * s), P(cx + .34 * s, cy + .44 * s)], outline=255, width=int(.03 * s * W))


def gear(g, cx=.5, cy=.5, s=1.0):
    g.star(cx, cy, .28 * s, .36 * s, 10, rot=0)
    g.circle(cx, cy, .28 * s)
    g.circle(cx, cy, .13 * s, 0)
    g.circle(cx, cy, .06 * s)


def banner(g, cx=.5, cy=.5, s=1.0):
    g.rect(cx - .26 * s, cy - .42 * s, cx - .21 * s, cy + .42 * s)
    g.poly([(cx - .21 * s, cy - .36 * s), (cx + .30 * s, cy - .36 * s), (cx + .30 * s, cy + .16 * s), (cx + .04 * s, cy + .04 * s), (cx - .21 * s, cy + .16 * s)])
    g.circle(cx + .05 * s, cy - .14 * s, .08 * s, 0)


def hook(g, cx=.5, cy=.5, s=1.0):
    g.line([(cx - .30 * s, cy - .34 * s), (cx + .06 * s, cy + .02 * s)], .04 * s)
    g.arc(cx + .10 * s, cy + .14 * s, .16 * s, 150, 420, .07 * s)
    g.poly([(cx + .26 * s, cy + .08 * s), (cx + .34 * s, cy - .06 * s), (cx + .20 * s, cy)])


def mountain(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .42 * s, cy + .30 * s), (cx - .12 * s, cy - .30 * s), (cx + .04 * s, cy - .02 * s), (cx + .16 * s, cy - .18 * s), (cx + .42 * s, cy + .30 * s)])
    g.poly([(cx - .12 * s, cy - .30 * s), (cx - .20 * s, cy - .14 * s), (cx - .10 * s, cy - .18 * s), (cx - .04 * s, cy - .14 * s)], 0)


def pick(g, cx=.5, cy=.5, s=1.0, angle=35):
    parts = [[(-.022, -.30), (.022, -.30), (.022, .42), (-.022, .42)],
             [(-.36, -.24), (-.10, -.36), (.10, -.36), (.36, -.24), (.10, -.28), (-.10, -.28)]]
    for part in parts:
        g.poly(rot([(cx + x * s, cy + y * s) for x, y in part], cx, cy, angle))


def claw(g, cx=.5, cy=.5, s=1.0):
    for k in range(3):
        x = cx + (k - 1) * .14 * s
        pts = []
        for i in range(20):
            t = i / 19
            pts.append((x + .10 * s * math.sin(t * 1.3) - .05 * s, cy - .38 * s + t * .76 * s))
        g.line(pts, (.07 * (1 - .5 * abs(k - 1))) * s)


def ground_cone(g, cx=.5, cy=.78, s=1.0):
    g.poly([(cx, cy), (cx - .38 * s, cy - .58 * s), (cx + .38 * s, cy - .58 * s)])
    g.poly([(cx, cy - .12 * s), (cx - .26 * s, cy - .52 * s), (cx + .26 * s, cy - .52 * s)], 0)


def rune_circle(g, cx=.5, cy=.5, s=1.0, n=6):
    g.ring(cx, cy, .36 * s, .035 * s)
    g.ring(cx, cy, .24 * s, .025 * s)
    for k in range(n):
        a = math.radians(k * 360 / n - 90)
        g.line([(cx + math.cos(a) * .24 * s, cy + math.sin(a) * .24 * s), (cx + math.cos(a) * .36 * s, cy + math.sin(a) * .36 * s)], .03 * s)


def chains(g, cx=.5, cy=.5, s=1.0, angle=35):
    for k in range(-2, 3):
        x, y = rot([(cx + k * .15 * s, cy)], cx, cy, angle)[0]
        if k % 2 == 0:
            g.d.ellipse([P(x - .10 * s, y - .06 * s), P(x + .10 * s, y + .06 * s)], outline=255, width=int(.035 * s * W))
        else:
            g.d.ellipse([P(x - .04 * s, y - .09 * s), P(x + .04 * s, y + .09 * s)], outline=255, width=int(.035 * s * W))


def hourglass(g, cx=.5, cy=.5, s=1.0):
    g.rect(cx - .24 * s, cy - .40 * s, cx + .24 * s, cy - .34 * s)
    g.rect(cx - .24 * s, cy + .34 * s, cx + .24 * s, cy + .40 * s)
    g.poly([(cx - .18 * s, cy - .34 * s), (cx + .18 * s, cy - .34 * s), (cx + .03 * s, cy), (cx + .18 * s, cy + .34 * s), (cx - .18 * s, cy + .34 * s), (cx - .03 * s, cy)])


def orb(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy, .20 * s)
    g.ring(cx, cy, .31 * s, .025 * s)
    for k in range(4):
        a = math.radians(45 + k * 90)
        g.circle(cx + math.cos(a) * .38 * s, cy + math.sin(a) * .38 * s, .035 * s)


def constellation(g, cx=.5, cy=.5, s=1.0):
    pts = [(-.30, .10), (-.12, -.22), (.10, -.06), (.28, -.30), (.20, .26), (-.04, .30)]
    pts = [(cx + x * s, cy + y * s) for x, y in pts]
    for a, b in ((0, 1), (1, 2), (2, 3), (2, 4), (4, 5), (5, 0)):
        g.line([pts[a], pts[b]], .02 * s)
    for i, (x, y) in enumerate(pts):
        g.star(x, y, .03 * s, .075 * s, 4)


def hooves(g, cx=.5, cy=.5, s=1.0):
    for dx, dy in ((-.20, -.18), (.14, -.24), (-.10, .18), (.24, .12)):
        x, y = cx + dx * s, cy + dy * s
        g.d.chord([P(x - .11 * s, y - .11 * s), P(x + .11 * s, y + .11 * s)], 180, 360, fill=255)
        g.rect(x - .11 * s, y - .005 * s, x + .11 * s, y + .05 * s)
        g.rect(x - .015 * s, y - .12 * s, x + .015 * s, y + .06 * s, 0)


def tower(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .14 * s, cy + .40 * s), (cx - .10 * s, cy - .14 * s), (cx + .10 * s, cy - .14 * s), (cx + .14 * s, cy + .40 * s)])
    g.rect(cx - .16 * s, cy - .20 * s, cx + .16 * s, cy - .14 * s)
    g.circle(cx, cy - .30 * s, .09 * s)
    for k in range(8):
        a = math.radians(k * 45)
        g.line([(cx + math.cos(a) * .14 * s, cy - .30 * s + math.sin(a) * .14 * s), (cx + math.cos(a) * .22 * s, cy - .30 * s + math.sin(a) * .22 * s)], .025 * s)


def feather(g, cx=.5, cy=.5, s=1.0):
    leaf(g, cx, cy, s * .95, angle=30)
    for k in range(5):
        y = -.20 + k * .1
        x0, y0 = rot([(cx, cy + y * s)], cx, cy, 30)[0]
        x1, y1 = rot([(cx + .16 * s, cy + (y - .06) * s)], cx, cy, 30)[0]
        g.line([(x0, y0), (x1, y1)], .015 * s, 0)


def seed(g, cx=.5, cy=.55, s=1.0):
    g.d.ellipse([P(cx - .14 * s, cy - .06 * s), P(cx + .14 * s, cy + .26 * s)], fill=255)
    leaf(g, cx - .10 * s, cy - .18 * s, .45 * s, angle=-50)
    leaf(g, cx + .10 * s, cy - .20 * s, .45 * s, angle=45)


def thorn_vine(g, cx=.5, cy=.5, s=1.0):
    pts = [(cx - .40 * s + t * .80 * s, cy + math.sin(t * 6.3) * .12 * s) for t in [i / 40 for i in range(41)]]
    g.line(pts, .045 * s)
    for i in range(3, 40, 6):
        x, y = pts[i]
        d = 1 if i % 12 < 6 else -1
        g.poly([(x - .03 * s, y), (x + .03 * s, y), (x + .01 * s, y - d * .12 * s)])


def wolf_head(g, cx=.5, cy=.5, s=1.0):
    g.poly([(cx - .28 * s, cy - .30 * s), (cx - .14 * s, cy - .10 * s), (cx + .14 * s, cy - .10 * s), (cx + .28 * s, cy - .30 * s),
            (cx + .24 * s, cy + .04 * s), (cx + .08 * s, cy + .36 * s), (cx - .08 * s, cy + .36 * s), (cx - .24 * s, cy + .04 * s)])
    for dx in (-.10, .10):
        g.poly([(cx + dx * s - .05 * s, cy + .02 * s), (cx + dx * s + .05 * s, cy + .02 * s), (cx + dx * s, cy + .07 * s)], 0)


def bear_head(g, cx=.5, cy=.52, s=1.0):
    g.circle(cx, cy, .28 * s)
    g.circle(cx - .22 * s, cy - .22 * s, .09 * s)
    g.circle(cx + .22 * s, cy - .22 * s, .09 * s)
    g.d.ellipse([P(cx - .12 * s, cy + .02 * s), P(cx + .12 * s, cy + .20 * s)], fill=0)
    g.circle(cx, cy + .07 * s, .045 * s)
    for dx in (-.11, .11):
        g.circle(cx + dx * s, cy - .07 * s, .035 * s, 0)


def dragon_eye(g, cx=.5, cy=.5, s=1.0):
    eye(g, cx, cy, s)
    g.d.ellipse([P(cx - .03 * s, cy - .13 * s), P(cx + .03 * s, cy + .13 * s)], fill=0)


def trail(g, cx=.5, cy=.5, s=1.0):
    for k in range(4):
        t = k / 3
        x, y = cx - .30 * s + t * .60 * s, cy + .28 * s - t * .56 * s
        g.d.ellipse([P(x - .07 * s, y - .05 * s), P(x + .07 * s, y + .05 * s)], fill=255)


def javelin(g, cx=.5, cy=.5, s=1.0):
    arrow(g, cx, cy, s * 1.1, angle=40, fletch=False)
    leaf(g, cx - .18 * s, cy + .18 * s, .30 * s, angle=-60)


def slumber(g, cx=.5, cy=.5, s=1.0):
    moon(g, cx - .08 * s, cy + .06 * s, s * .8)
    for k, (x, y, r) in enumerate(((.22, -.24, .10), (.32, -.36, .07))):
        z = [(cx + (x - r) * s, cy + (y - r) * s), (cx + (x + r) * s, cy + (y - r) * s), (cx + (x - r) * s, cy + (y + r) * s), (cx + (x + r) * s, cy + (y + r) * s)]
        g.line(z, .025 * s)


def cleave(g, cx=.5, cy=.5, s=1.0):
    pts = []
    for i in range(25):
        a = math.radians(195 + i * 150 / 24)
        pts.append((cx + math.cos(a) * .38 * s, cy + .10 * s + math.sin(a) * .38 * s))
    for i in range(25):
        a = math.radians(345 - i * 150 / 24)
        r = .38 - .12 * math.sin(math.pi * i / 24)
        pts.append((cx + math.cos(a) * r * s, cy + .10 * s + math.sin(a) * r * s))
    g.poly(pts)
    sword(g, cx, cy + .12 * s, s * .72, angle=0)


def square_ward(g, cx=.5, cy=.5, s=1.0):
    for r, w in ((.34, .035), (.22, .03)):
        g.d.rectangle([P(cx - r * s, cy - r * s), P(cx + r * s, cy + r * s)], outline=255, width=int(w * s * W))
    g.star(cx, cy, .04 * s, .12 * s, 4, rot=-45)


def sigil(g, cx=.5, cy=.5, s=1.0):
    rune_circle(g, cx, cy, s, 3)
    g.poly([(cx, cy - .24 * s), (cx + .21 * s, cy + .12 * s), (cx - .21 * s, cy + .12 * s)])
    g.poly([(cx, cy - .10 * s), (cx + .09 * s, cy + .06 * s), (cx - .09 * s, cy + .06 * s)], 0)


def grave_line(g, cx=.5, cy=.5, s=1.0):
    for k in range(3):
        x, y = cx - .26 * s + k * .26 * s, cy + .20 * s - k * .20 * s
        g.poly([(x - .07 * s, y + .12 * s), (x - .07 * s, y - .06 * s), (x, y - .14 * s), (x + .07 * s, y - .06 * s), (x + .07 * s, y + .12 * s)])
        g.rect(x - .012 * s, y - .08 * s, x + .012 * s, y + .06 * s, 0)
        g.rect(x - .04 * s, y - .04 * s, x + .04 * s, y - .015 * s, 0)


def cinder_cone(g, cx=.5, cy=.5, s=1.0):
    ground_cone(g, cx, cy + .30 * s, s)
    flame(g, cx, cy - .06 * s, .55 * s)


def venom(g, cx=.5, cy=.5, s=1.0):
    g.d.ellipse([P(cx - .38 * s, cy + .12 * s), P(cx + .38 * s, cy + .36 * s)], outline=255, width=int(.035 * s * W))
    drop(g, cx, cy - .08 * s, .75 * s)
    for dx in (-.24, .26):
        g.circle(cx + dx * s, cy + .02 * s, .045 * s)


def sanctuary(g, cx=.5, cy=.5, s=1.0):
    rune_circle(g, cx, cy, s, 8)
    cross(g, cx, cy, s * .45, w=.14)


def wind(g, cx=.5, cy=.5, s=1.0):
    for k, (r, a0) in enumerate(((.34, 200), (.24, 20), (.14, 200))):
        g.arc(cx, cy, r * s, a0, a0 + 250, .045 * s)
    heart(g, cx, cy, .38 * s)


def last_stand(g, cx=.5, cy=.5, s=1.0):
    shield(g, cx, cy, s, boss=False)
    sword(g, cx, cy - .02 * s, s * .75, angle=0)


def challenge(g, cx=.5, cy=.5, s=1.0):
    sword(g, cx, cy, s * .9, angle=40)
    sword(g, cx, cy, s * .9, angle=-40)
    g.ring(cx, cy, .40 * s, .03 * s)


def seismic(g, cx=.5, cy=.5, s=1.0):
    fist(g, cx, cy - .10 * s, s * .7)
    for k, r in enumerate((.20, .30, .40)):
        g.arc(cx, cy + .30 * s, r * s, 200, 340, .03 * s)
    g.line([(cx - .30 * s, cy + .38 * s), (cx - .10 * s, cy + .30 * s), (cx, cy + .40 * s), (cx + .14 * s, cy + .30 * s), (cx + .32 * s, cy + .40 * s)], .03 * s)


def pack(g, cx=.5, cy=.5, s=1.0):
    wolf_head(g, cx - .16 * s, cy + .06 * s, s * .62)
    wolf_head(g, cx + .16 * s, cy + .06 * s, s * .62)
    wolf_head(g, cx, cy - .10 * s, s * .70)


def hunt(g, cx=.5, cy=.5, s=1.0):
    wolf_head(g, cx, cy + .06 * s, s * .9)
    moon(g, cx + .28 * s, cy - .30 * s, s * .35)


def aegis(g, cx=.5, cy=.5, s=1.0):
    for dx, sc in ((-.20, .55), (.20, .55), (0, .75)):
        shield(g, cx + dx * s, cy + (.04 if dx else -.02) * s, s * sc)


def wellspring(g, cx=.5, cy=.5, s=1.0):
    drop(g, cx, cy - .06 * s, .8 * s)
    g.circle(cx, cy + .02 * s, .09 * s, 0)
    for r in (.30, .40):
        g.d.ellipse([P(cx - r * s, cy + .24 * s), P(cx + r * s, cy + .38 * s)], outline=255, width=int(.025 * s * W))


def purify(g, cx=.5, cy=.5, s=1.0):
    g.star(cx, cy, .07 * s, .38 * s, 4)
    g.star(cx, cy, .04 * s, .20 * s, 4, rot=-45)


def restoring(g, cx=.5, cy=.5, s=1.0):
    sun(g, cx, cy, s * .9, rays=8, core=.12)
    cross(g, cx, cy, s * .35, w=.16)


def renewal(g, cx=.5, cy=.5, s=1.0):
    for k in range(3):
        a0 = k * 120
        g.arc(cx, cy, .32 * s, a0 + 10, a0 + 100, .06 * s)
        a = math.radians(a0 + 100)
        x, y = cx + math.cos(a) * .32 * s, cy + math.sin(a) * .32 * s
        t = a + math.pi / 2
        g.poly([(x + math.cos(t) * .10 * s, y + math.sin(t) * .10 * s), (x + math.cos(a) * .09 * s, y + math.sin(a) * .09 * s), (x - math.cos(a) * .09 * s, y - math.sin(a) * .09 * s)])
    heart(g, cx, cy, .40 * s)


def bastion(g, cx=.5, cy=.5, s=1.0):
    sun(g, cx, cy - .14 * s, s * .7, rays=10, core=.10)
    shield(g, cx, cy + .10 * s, s * .7)


def verdict(g, cx=.5, cy=.5, s=1.0):
    axe(g, cx + .08 * s, cy - .02 * s, s * 1.05, angle=-20)
    skull(g, cx - .20 * s, cy + .22 * s, s * .42)


def cataclysm(g, cx=.5, cy=.5, s=1.0):
    meteor(g, cx, cy - .04 * s, s)
    g.d.ellipse([P(cx - .36 * s, cy + .26 * s), P(cx + .36 * s, cy + .40 * s)], outline=255, width=int(.035 * s * W))


def starfall(g, cx=.5, cy=.5, s=1.0):
    for dx, dy, sc in ((-.18, -.16, .6), (.18, -.26, .45), (.06, .12, .75)):
        g.star(cx + dx * s, cy + dy * s, .05 * sc * s, .22 * sc * s, 5)
    g.d.ellipse([P(cx - .36 * s, cy + .30 * s), P(cx + .36 * s, cy + .42 * s)], outline=255, width=int(.03 * s * W))


def rhythm(g, cx=.5, cy=.5, s=1.0):
    g.line([(cx - .40 * s, cy), (cx - .20 * s, cy), (cx - .12 * s, cy - .26 * s), (cx, cy + .30 * s), (cx + .10 * s, cy - .12 * s), (cx + .16 * s, cy), (cx + .40 * s, cy)], .045 * s)
    g.ring(cx, cy, .40 * s, .025 * s)


def reserves(g, cx=.5, cy=.5, s=1.0):
    for k in range(3):
        g.d.chord([P(cx - .26 * s, cy - .30 * s + k * .20 * s), P(cx + .26 * s, cy - .10 * s + k * .20 * s)], 0, 360, fill=255)
        g.d.chord([P(cx - .20 * s, cy - .27 * s + k * .20 * s), P(cx + .20 * s, cy - .13 * s + k * .20 * s)], 0, 360, fill=0)
    bolt(g, cx, cy, s * .45)


def conduit(g, cx=.5, cy=.5, s=1.0):
    g.circle(cx, cy - .16 * s, .14 * s)
    g.circle(cx, cy - .16 * s, .07 * s, 0)
    g.poly([(cx - .05 * s, cy - .04 * s), (cx + .05 * s, cy - .04 * s), (cx + .02 * s, cy + .40 * s), (cx - .02 * s, cy + .40 * s)])
    for side in (-1, 1):
        g.arc(cx + side * .14 * s, cy + .08 * s, .18 * s, 90 if side < 0 else -90, 270 if side < 0 else 90, .03 * s)


def stone_skin(g, cx=.5, cy=.5, s=1.0):
    for dx, dy, r in ((-.16, -.12, .16), (.14, -.16, .14), (.0, .12, .20), (-.22, .18, .10), (.22, .14, .11)):
        g.star(cx + dx * s, cy + dy * s, r * .85 * s, r * s, 6, rot=15 * (dx > 0))


def guardian(g, cx=.5, cy=.5, s=1.0):
    helm(g, cx, cy, s * .9)
    g.ring(cx, cy, .44 * s, .025 * s)


def ember_lance(g, cx=.5, cy=.5, s=1.0):
    g.poly(rot([(cx - .03 * s, cy + .40 * s), (cx + .03 * s, cy + .40 * s), (cx + .03 * s, cy - .20 * s), (cx, cy - .44 * s), (cx - .03 * s, cy - .20 * s)], cx, cy, 45))
    flame(g, cx + .12 * s, cy - .08 * s, .55 * s, cut=False)


def frost_bind(g, cx=.5, cy=.5, s=1.0):
    snowflake(g, cx, cy, s * .8)
    g.ring(cx, cy, .40 * s, .03 * s)


def chain_spark(g, cx=.5, cy=.5, s=1.0):
    bolt(g, cx - .06 * s, cy, s * .8)
    for x, y in ((.28, -.28), (.30, .20), (-.32, .28)):
        g.circle(cx + x * s, cy + y * s, .05 * s)
    g.line([(cx + .02 * s, cy - .10 * s), (cx + .28 * s, cy - .28 * s)], .018 * s)
    g.line([(cx + .02 * s, cy + .06 * s), (cx + .30 * s, cy + .20 * s)], .018 * s)
    g.line([(cx - .12 * s, cy + .16 * s), (cx - .32 * s, cy + .28 * s)], .018 * s)


def piercing(g, cx=.5, cy=.5, s=1.0):
    arrow(g, cx, cy, s, angle=45)
    for r in (.10, .17):
        x, y = cx + .06 * s, cy - .06 * s
        g.ring(x, y, r * s, .02 * s)


def shadow_step(g, cx=.5, cy=.5, s=1.0):
    dash(g, cx - .04 * s, cy + .06 * s, s * .9)
    g.circle(cx + .30 * s, cy - .26 * s, .09 * s)


def war_cry(g, cx=.5, cy=.5, s=1.0):
    horn(g, cx, cy, s)


def iron_guard(g, cx=.5, cy=.5, s=1.0):
    shield(g, cx, cy, s * 1.05)


def shield_slam(g, cx=.5, cy=.5, s=1.0):
    shield(g, cx - .06 * s, cy + .02 * s, s * .85)
    for k in range(3):
        a = math.radians(-40 + k * 30)
        g.line([(cx + .24 * s + math.cos(a) * .06 * s, cy - .10 * s + math.sin(a) * .1 * s), (cx + .24 * s + math.cos(a) * .20 * s, cy - .10 * s + math.sin(a) * .26 * s)], .03 * s)


GLYPHS = {k: v for k, v in globals().items() if callable(v) and v.__module__ == __name__ and k not in ('P', 'rot', 'main', 'paint', 'planned_glyph')}

# Pool skills: (glyph, palette). Each pair is unique so every offered skill reads distinctly.
POOL = {
    'iron_guard': ('iron_guard', 'steel'), 'shield_slam': ('shield_slam', 'holy'), 'war_cry': ('war_cry', 'war'),
    'chain_spark': ('chain_spark', 'arcane'), 'ember_lance': ('ember_lance', 'fire'), 'venom_ground': ('venom', 'venom'),
    'cinder_cone': ('cinder_cone', 'fire'), 'grave_line': ('grave_line', 'shadow'), 'ashen_square': ('square_ward', 'ash'),
    'blight_sigil': ('sigil', 'venom'), 'frost_bind': ('frost_bind', 'frost'), 'cleaving_strike': ('cleave', 'steel'),
    'piercing_shot': ('piercing', 'steel'), 'shadow_step': ('shadow_step', 'shadow'), 'restoring_light': ('restoring', 'holy'),
    'sanctuary': ('sanctuary', 'light'), 'purify': ('purify', 'spirit'), 'summoned_wall': ('wall', 'stone'),
    'protection_dome': ('dome', 'ether'), 'oathbound_guardian': ('guardian', 'spectral'), 'spectral_pack': ('pack', 'spectral'),
    'second_wind': ('wind', 'nature'), 'stone_skin': ('stone_skin', 'stone'), 'battle_rhythm': ('rhythm', 'war'),
    'deep_reserves': ('reserves', 'arcane'), 'soul_conduit': ('conduit', 'holy'), 'bastion_of_dawn': ('bastion', 'holy'),
    'cataclysm': ('cataclysm', 'fire'), 'executioners_verdict': ('verdict', 'blood'), 'renewal': ('renewal', 'nature'),
    'last_stand': ('last_stand', 'war'), 'challenge_of_iron': ('challenge', 'steel'), 'seismic_reprisal': ('seismic', 'earth'),
    'starfall': ('starfall', 'arcane'), 'spectral_hunt': ('hunt', 'spectral'), 'mass_aegis': ('aegis', 'light'),
    'wellspring': ('wellspring', 'frost'),
}
# Class baseline traits (CireClassTraits): Support / Tank / DPS.
TRAITS = {'trait_mending_strikes': ('heart', 'spirit'), 'trait_natural_defense': ('shield', 'earth'), 'trait_keen_edge': ('sword', 'blood')}
# Planned roster skills: keyword -> glyph, falling back to the delivery shape.
KEYWORDS = [
    ('javelin', 'javelin'), ('grove_renewal', 'renewal'), ('spring_march', 'feather'), ('dragon_oath', 'spiral'), ('ancient_pact', 'dragon_eye'),
    ('totem_bulwark', 'shield'), ('ancestral_weight', 'hourglass'), ('living_granite', 'fist'), ('ancient_hide', 'claw'),
    ('ether_furnace', 'gear'), ('twin_throw', 'axe'), ('herd_call', 'horn'), ('steady_gait', 'hooves'), ('bear_maul', 'bear_head'),
    ('maul', 'claw'), ('roar', 'horn'), ('charge', 'dash'), ('slumber', 'slumber'), ('hibernate', 'slumber'), ('hide', 'stone_skin'),
    ('colossus', 'mountain'), ('righteous_flail', 'cleave'), ('holy_flail', 'lantern'), ('vow', 'cross'), ('censer', 'lantern'), ('pilgrim', 'sun'),
    ('pick', 'pick'), ('faultline', 'seismic'), ('lantern', 'lantern'), ('orehide', 'stone_skin'), ('mountain', 'mountain'),
    ('fist', 'fist'), ('anchor', 'rune_circle'), ('core', 'gear'), ('worldstone', 'mountain'), ('bloom', 'seed'),
    ('living_granite', 'stone_skin'), ('furnace', 'flame'), ('hook', 'hook'), ('banner', 'banner'), ('courage', 'crown'),
    ('earthshout', 'horn'), ('sweep', 'totem'), ('tusk', 'tusk'), ('bulwark', 'totem'), ('weight', 'hourglass'),
    ('stampede', 'hooves'), ('oath', 'dragon_eye'), ('scale', 'shield'), ('wing', 'wing'), ('ember', 'flame'),
    ('pact', 'dragon_eye'), ('frenzy', 'axe'), ('leap', 'dash'), ('hunger', 'skull'), ('red_moon', 'moon'),
    ('twin', 'axe'), ('returning', 'spiral'), ('snare', 'roots'), ('mend', 'seed'), ('thorn', 'thorn_vine'),
    ('covenant', 'leaf'), ('mote', 'orb'), ('tether', 'chains'), ('trail', 'trail'), ('soul', 'orb'),
    ('constellation', 'constellation'),  ('herd', 'hooves'), ('gait', 'feather'),
    ('march', 'leaf'), ('beam', 'beam'), ('ward', 'dome'), ('beacon', 'tower'), ('last_light', 'lantern'), ('sunrise', 'sun'),
]
DELIVERY = {'targeted': 'sword', 'ground_cone': 'ground_cone', 'ground_line': 'dash', 'ground_circle': 'rune_circle',
            'self': 'shield', 'ally': 'heart', 'projectile': 'arrow', 'summon': 'wolf_head', 'construct': 'wall',
            'transformation': 'spiral', 'passive': 'rune_circle', 'chain': 'bolt'}
# Distinct looks for planned skills sharing a keyword: flips/angles per id.
VARIANTS = {'troll_twin_throw': dict(angle=-30, flip=True), 'troll_axe_frenzy': dict(angle=20)}


def planned_glyph(skill_id, delivery):
    for key, glyph in KEYWORDS:
        if key in skill_id:
            return glyph
    return DELIVERY.get(delivery, 'rune_circle')


def paint(glyph_name, palette_name, seed, kwargs=None):
    from PIL import Image, ImageDraw, ImageFilter, ImageChops
    deep, mid, fg, glow = PALETTES[palette_name]
    rnd = random.Random(seed)
    # Backdrop: radial light slightly above centre, grain, vignette.
    bg = Image.new('RGB', (SIZE, SIZE))
    px = bg.load()
    cx, cy = SIZE * .5, SIZE * .42
    for y in range(SIZE):
        for x in range(SIZE):
            d = min(1.0, math.hypot(x - cx, y - cy) / (SIZE * .72))
            t = (1 - d) ** 1.6
            n = rnd.uniform(-7, 7)
            px[x, y] = tuple(int(max(0, min(255, deep[i] + (mid[i] - deep[i]) * t + n))) for i in range(3))
    # Soft painterly streaks.
    streak = Image.new('L', (SIZE, SIZE), 0)
    sd = ImageDraw.Draw(streak)
    for _ in range(9):
        x0, y0 = rnd.uniform(-40, SIZE), rnd.uniform(-40, SIZE)
        sd.line([(x0, y0), (x0 + rnd.uniform(60, 160), y0 + rnd.uniform(-40, 40))], fill=rnd.randint(20, 45), width=rnd.randint(8, 22))
    streak = streak.filter(ImageFilter.GaussianBlur(10))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), tuple(min(255, c + 40) for c in mid)), bg, streak)
    # Glyph mask (supersampled).
    mask = Image.new('L', (W, W), 0)
    GLYPHS[glyph_name](Glyph(ImageDraw.Draw(mask)), **(kwargs or {}))
    mask = mask.resize((SIZE, SIZE), Image.LANCZOS)
    glow_mask = mask.filter(ImageFilter.GaussianBlur(9)).point(lambda v: min(255, int(v * 1.6)))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), glow), bg, glow_mask.point(lambda v: int(v * .75)))
    outline = mask.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.GaussianBlur(1.2))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), tuple(int(c * .35) for c in deep)), bg, outline)
    # Glyph fill: vertical gradient fg -> glow, with a top-left rim highlight.
    fill = Image.new('RGB', (SIZE, SIZE))
    fd = ImageDraw.Draw(fill)
    for y in range(SIZE):
        t = y / SIZE
        fd.line([(0, y), (SIZE, y)], fill=tuple(int(fg[i] * (1 - t * .45) + glow[i] * t * .45) for i in range(3)))
    bg = Image.composite(fill, bg, mask)
    shade = ImageChops.subtract(mask, ImageChops.offset(mask, -3, -3)).filter(ImageFilter.GaussianBlur(1))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), tuple(int(c * .45) for c in glow)), bg, shade.point(lambda v: int(v * .6)))
    rim = ImageChops.subtract(mask, ImageChops.offset(mask, 3, 3)).filter(ImageFilter.GaussianBlur(.8))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), (255, 255, 245)), bg, rim.point(lambda v: int(v * .55)))
    # Vignette and a thin inner bevel.
    vig = Image.new('L', (SIZE, SIZE), 0)
    vd = ImageDraw.Draw(vig)
    for i in range(22):
        vd.rectangle([i, i, SIZE - 1 - i, SIZE - 1 - i], outline=int(150 * (1 - i / 22) ** 2))
    bg = Image.composite(Image.new('RGB', (SIZE, SIZE), (0, 0, 0)), bg, vig)
    bd = ImageDraw.Draw(bg)
    bd.rectangle([1, 1, SIZE - 2, SIZE - 2], outline=tuple(min(255, c + 50) for c in mid), width=1)
    return bg


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--pylib', type=Path, help='Directory containing Pillow')
    parser.add_argument('--ids', help='Comma-separated subset')
    parser.add_argument('--sheet', type=Path, help='Also write a labelled contact sheet PNG')
    parser.add_argument('--data', type=Path, help='Write the runtime school/accent table (Content/Data/AbilityIcons.json)')
    args = parser.parse_args()
    if args.pylib:
        sys.path.insert(0, str(args.pylib))
    from PIL import Image, ImageDraw
    rules = (ROOT / 'Source/CiresTeamSurvival/Rules/CiresRules.cpp').read_text(encoding='utf-8')
    catalog = re.findall(r'\{"([a-z_]+)",\s*"[^"]+",\s*SkillKind::(?:Active|Passive|Ultimate)\}', rules)
    missing = sorted(set(catalog) - set(POOL))
    if missing:
        raise SystemExit('Pool skills without an icon design: ' + ', '.join(missing))
    roster = json.loads((ROOT / 'Content/Data/ChampionRoster.json').read_text(encoding='utf-8'))
    jobs = {}
    for sid, (glyph, pal) in POOL.items():
        jobs[sid] = (glyph, pal, None)
    for sid, (glyph, pal) in TRAITS.items():
        jobs[sid] = (glyph, pal, None)
    for c in roster['champions']:
        for s in c['actives'] + [c['passive'], c['ultimate']]:
            if s['id'] not in jobs:
                pal = s['vfxFamily'] if s['vfxFamily'] in PALETTES else 'arcane'
                jobs[s['id']] = (planned_glyph(s['id'], s['delivery']), pal, VARIANTS.get(s['id']))
    combos = {}
    for sid in POOL:
        combos.setdefault(POOL[sid], []).append(sid)
    dupes = {k: v for k, v in combos.items() if len(v) > 1}
    if dupes:
        raise SystemExit(f'Pool icons must be distinct: {dupes}')
    if args.ids:
        wanted = set(args.ids.split(','))
        jobs = {k: v for k, v in jobs.items() if k in wanted}
    out = args.out or ROOT / 'Saved/AbilityIcons' / __import__('datetime').datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
    out.mkdir(parents=True, exist_ok=True)
    for i, (sid, (glyph, pal, kw)) in enumerate(sorted(jobs.items())):
        paint(glyph, pal, hash(sid) & 0xffff if False else sum(map(ord, sid)), kw).save(out / f'{sid}.png')
    manifest = {sid: dict(glyph=g, palette=p, pool=sid in POOL) for sid, (g, p, _) in jobs.items()}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    if args.data and not args.ids:
        rows = {sid: dict(school=p, glyph=g, accent='#%02x%02x%02x' % PALETTES[p][3], pool=sid in POOL)
                for sid, (g, p, _) in sorted(jobs.items())}
        args.data.write_text(json.dumps(dict(schemaVersion=1, generator='Tools/BuildAbilityIcons.py',
                                             texturePath='/Game/UI/Abilities/T_<id>', icons=rows), indent=2) + chr(10), encoding='utf-8')
    if args.sheet:
        ids = sorted(jobs)
        cols = 10
        cell = 112
        sheet = Image.new('RGB', (cols * cell, ((len(ids) + cols - 1) // cols) * (cell + 14)), (12, 12, 14))
        d = ImageDraw.Draw(sheet)
        for i, sid in enumerate(ids):
            im = Image.open(out / f'{sid}.png').resize((cell - 8, cell - 8))
            x, y = (i % cols) * cell + 4, (i // cols) * (cell + 14) + 4
            sheet.paste(im, (x, y))
            d.text((x, y + cell - 7), sid[:17], fill=(220, 210, 190) if sid in POOL else (150, 150, 150))
        sheet.save(args.sheet)
    print(json.dumps(dict(directory=str(out), icons=len(jobs), pool=sum(1 for k in jobs if k in POOL))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
