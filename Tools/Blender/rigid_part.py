"""Make a fused prop rigid on one bone (Docs/RigAudit.md, "Blender round-trip").

    python Tools/RunBlender.py rigid_part.py -- --in X.fbx --out Y.fbx --bone spine_03
        --segment "l1,z1,l2,z2" --radius R [--depth D] [--feather F] [--extra "l,z,r;..."] [--qa DIR] [--report r.json]

Tripo fuses some props into the body island (the HQ Ranger's quiver lies diagonally across the back), so they bend and
twist with the skin. Instead of cutting them out (which leaves a hole and needs a new prop asset), this re-weights the
prop's vertices 100% to one bone so it moves rigidly with the torso, feathered into the surrounding skin.

The prop is found on the body's back: in the body frame (lateral = clavicle_r -> clavicle_l, up = +Z, back = -feet
forward, all in the armature's rest space, units of the head bone height H) a vertex belongs when its (lateral, z) lies
within --radius of the segment (l1,z1)-(l2,z2) (or of an --extra disc: arrows fanning out of a quiver), and its depth is
within --depth of the outermost vertex behind it in the same (lateral, z) cell (the outer layer: the prop, not the
torso under it). --feather blends the weights to the skin over that distance past --radius. Values are fractions of H.
--qa renders back/right/left views with the selection painted (sel_*.png) and after a 35 degree spine twist (twist_*.png).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(1e-12, ab.dot(ab))))
    return (p - (a + ab * t)).length


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bone", default="spine_03")
    ap.add_argument("--segment", required=True)
    ap.add_argument("--radius", type=float, required=True)
    ap.add_argument("--depth", type=float, default=.06)
    ap.add_argument("--feather", type=float, default=.012)
    ap.add_argument("--extra", default="")
    ap.add_argument("--qa", default="")
    ap.add_argument("--report", default="")
    a = ap.parse_args(common.script_args())
    t0 = time.time()
    common.reset_scene()
    arm, meshes = common.import_any(a.inp)
    mw = arm.matrix_world

    def head(n):
        return mw @ arm.data.bones[n].head_local
    H = head("head").z - min(head("foot_l").z, head("foot_r").z)
    # The side the prop hangs on. On the imported Tripo FBX the feet (foot -> ball) point to the body's back in Blender's
    # rest space (checked on the HQ Ranger: -feet views the face), so "back" is +feet here.
    back = (head("ball_l") - head("foot_l")) + (head("ball_r") - head("foot_r")); back.z = 0; back.normalize()
    left = head("clavicle_l") - head("clavicle_r"); left.z = 0; left.normalize()
    origin = head("pelvis"); origin.z = min(head("foot_l").z, head("foot_r").z)
    l1, z1, l2, z2 = [float(v) for v in a.segment.split(",")]
    A, B = Vector((l1, z1)), Vector((l2, z2))
    extras = [tuple(float(x) for x in e.split(",")) for e in a.extra.split(";") if e]
    R, F, D = a.radius, a.feather, a.depth

    def frame(p):
        d = p - origin
        return d.dot(left) / H, d.z / H, d.dot(back) / H

    selected = 0
    for obj in meshes:
        pts = [frame(obj.matrix_world @ v.co) for v in obj.data.vertices]
        cell = .01
        outer = {}
        for l, z, b in pts:
            k = (round(l / cell), round(z / cell))
            outer[k] = max(outer.get(k, -9.0), b)
        group = obj.vertex_groups.get(a.bone) or obj.vertex_groups.new(name=a.bone)
        weights = []
        for i, (l, z, b) in enumerate(pts):
            q = Vector((l, z))
            dist = seg_dist(q, A, B)
            for el, ez, er in extras:
                dist = min(dist, (q - Vector((el, ez))).length - (er - R))
            if dist >= R + F:
                continue
            k = (round(l / cell), round(z / cell))
            near = max(outer.get((k[0] + dx, k[1] + dy), -9.0) for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            if b < near - D:
                continue
            f = 1.0 if dist <= R else 1.0 - smooth((dist - R) / F)
            if f > 1e-3:
                weights.append((i, f))
        for i, f in weights:
            v = obj.data.vertices[i]
            for g in v.groups:
                if g.group != group.index:
                    g.weight *= (1.0 - f)
            cur = next((g.weight for g in v.groups if g.group == group.index), 0.0)
            group.add([i], cur * (1.0 - f) + f, "REPLACE")
            selected += f >= .999
        if a.qa:
            attr = obj.data.color_attributes.new("sel", "FLOAT_COLOR", "POINT")
            for i in range(len(obj.data.vertices)):
                attr.data[i].color = (.7, .7, .7, 1)
            for i, f in weights:
                attr.data[i].color = (.7 + .3 * f, .7 - .6 * f, .7 - .6 * f, 1)
            obj.data.color_attributes.active_color = attr
    common.export_ue_fbx(a.out, [arm] + meshes)
    report = {"in": a.inp, "out": a.out, "bone": a.bone, "rigid_verts": selected, "H": H, "stats": common.stats(meshes, arm)}
    if a.qa:
        out = Path(a.qa).resolve(); out.mkdir(parents=True, exist_ok=True)
        sc = bpy.context.scene
        sc.render.engine = "BLENDER_WORKBENCH"; sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "VERTEX"
        sc.render.resolution_x = sc.render.resolution_y = 768
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
        cam.data.type = "ORTHO"; cam.data.ortho_scale = H * .75
        centre = origin + Vector((0, 0, H * .72))

        def shoot(name, d):
            cam.location = centre + d * H * 3
            cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
            sc.render.filepath = str(out / name); bpy.ops.render.render(write_still=True)
        right = -left
        views = {"back": back, "right": right, "left": left, "back_right": (back + right).normalized()}
        for n, d in views.items():
            shoot("sel_%s.png" % n, d)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.mode_set(mode="POSE")
        for n in ("spine_01", "spine_02"):
            pb = arm.pose.bones.get(n)
            if pb:
                pb.rotation_mode = "XYZ"; pb.rotation_euler = (0, math.radians(17.5), 0)
        pb = arm.pose.bones.get("upperarm_r")
        if pb:
            pb.rotation_mode = "XYZ"; pb.rotation_euler = (math.radians(-40), 0, 0)
        bpy.ops.object.mode_set(mode="OBJECT"); bpy.context.view_layer.update()
        for n, d in views.items():
            shoot("twist_%s.png" % n, d)
    report["seconds"] = round(time.time() - t0, 1)
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=1), encoding="utf-8")
    common.result(**report)


main()
