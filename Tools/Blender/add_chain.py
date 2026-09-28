"""Add tentacle bone chains to a skinned body and weight the tentacle region onto them (Docs/RigAudit.md, "Blender round-trip").

    python Tools/RunBlender.py add_chain.py -- --in X.fbx --out Y.fbx --ref Saved/Blender/ref/<body>.json
        --region cx,cy,cz,rx,ry,rz,rootZ,tipZ [--parent head] [--chains 3] [--bones 4] [--prefix tentacle] [--report r.json]

Tripo's humanoid rig gives tentacle beards, skirts and vines no bones; the game swayed them in the skin material (world
position offset on MonsterArt.json "sway.regions"). This script gives such a region real bones:
  * --region is the MonsterArt.json sway region in the UE raw mesh space (centre, radii, rootZ, tipZ). It is mapped into
    Blender's world by a least-squares affine fit of the --ref bind points (MonsterRigPoints JSON, UE space) onto the
    armature's bone heads, so no axis/unit convention is assumed.
  * --chains chains (spread across the region's width, x) of --bones bones each run from rootZ to tipZ, parented to
    --parent (not connected). Names: <prefix>_<chain>_<bone> (tentacle_0_0 ... ); all deform bones.
  * Weights: inside the ellipsoid, a vertex's weight blends from its existing weights to the chain bones by a smooth
    falloff (1 at the centre, 0 at the ellipsoid surface) and a root ramp (0 at rootZ, full a quarter of the way down),
    so the tentacle root stays glued to the face. Along the chain it is split between the two nearest bones; across
    chains by distance in x. Only the new bones gain weight (the existing weights are scaled, never redistributed).
Existing bones, their rest pose, UVs and materials are untouched, so every existing clip plays unchanged; the new bones
sit at rest unless the game drives them (CireMonsterAnim tentacle wave).
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def fit_affine(ref: dict, arm) -> Matrix:
    """UE raw point -> Blender world, fitted on shared bone names."""
    src, dst = [], []
    for bone in arm.data.bones:
        p = ref.get(bone.name)
        if p is None:
            continue
        src.append([p[0], p[1], p[2], 1.0])
        dst.append(list(arm.matrix_world @ bone.head_local))
    if len(src) < 6:
        raise RuntimeError("too few shared bones for the fit: %d" % len(src))
    a, res, _, _ = np.linalg.lstsq(np.array(src), np.array(dst), rcond=None)
    m = Matrix.Identity(4)
    for r in range(3):
        for c in range(4):
            m[r][c] = float(a[c][r])
    err = max((Vector(d) - m @ Vector(s[:3])).length for s, d in zip(src, dst))
    return m, err, len(src)


def smooth(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--region", required=True)
    ap.add_argument("--box", default="", help="xmin,xmax,ymin,ymax (UE raw): a column between rootZ and tipZ instead of the "
                    "ellipsoid, for tentacles that hang past the sway region (a beard lying in front of the chest)")
    ap.add_argument("--parent", default="head")
    ap.add_argument("--chains", type=int, default=3)
    ap.add_argument("--bones", type=int, default=4)
    ap.add_argument("--prefix", default="tentacle")
    ap.add_argument("--report", default="")
    args = ap.parse_args(common.script_args())
    started = time.time()
    common.reset_scene()
    arm, meshes = common.import_any(args.inp)
    if arm is None or not meshes:
        raise RuntimeError("no armature/mesh in " + args.inp)
    ref = json.loads(Path(args.ref).read_text(encoding="utf-8"))["bind"]
    ue_to_world, fit_err, fit_n = fit_affine(ref, arm)
    cx, cy, cz, rx, ry, rz, root_z, tip_z = [float(v) for v in args.region.split(",")]
    world_to_ue = ue_to_world.inverted()
    box = [float(v) for v in args.box.split(",")] if args.box else None
    if box:
        cx, rx = (box[0] + box[1]) / 2, (box[1] - box[0]) / 2
    before = common.stats(meshes, arm)

    # Chain joints in UE raw space: spread across x, root -> tip along z at the region's forward (y) centre.
    chains = []
    for c in range(args.chains):
        off = 0.0 if args.chains == 1 else (c / (args.chains - 1) - .5) * 1.2 * rx
        joints = [Vector((cx + off, cy, root_z + (tip_z - root_z) * i / args.bones)) for i in range(args.bones + 1)]
        chains.append(joints)

    bpy.context.view_layer.objects.active = arm
    for o in bpy.context.selected_objects:
        o.select_set(False)
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    if args.parent not in eb:
        raise RuntimeError("parent bone missing: " + args.parent)
    inv = arm.matrix_world.inverted()
    names = []
    parent_roll_ref = eb[args.parent]
    for c, joints in enumerate(chains):
        prev = eb[args.parent]
        row = []
        for i in range(args.bones):
            name = "%s_%d_%d" % (args.prefix, c, i)
            if name in eb:
                eb.remove(eb[name])
            b = eb.new(name)
            b.head = inv @ (ue_to_world @ joints[i])
            b.tail = inv @ (ue_to_world @ joints[i + 1])
            b.align_roll(parent_roll_ref.z_axis)
            b.parent = prev
            b.use_connect = i > 0
            b.use_deform = True
            prev = b
            row.append(name)
        names.append(row)
    bpy.ops.object.mode_set(mode="OBJECT")

    weighted = 0
    for obj in meshes:
        groups = {n: (obj.vertex_groups.get(n) or obj.vertex_groups.new(name=n)) for row in names for n in row}
        mw = obj.matrix_world
        for v in obj.data.vertices:
            p = world_to_ue @ (mw @ v.co)
            if box:
                if not (box[0] <= p.x <= box[1] and box[2] <= p.y <= box[3] and min(root_z, tip_z) <= p.z <= max(root_z, tip_z)):
                    continue
                edge = min(p.x - box[0], box[1] - p.x, p.y - box[2], box[3] - p.y)
                d = (1.0 - smooth(edge / 1.0)) ** 2  # soft 1-unit rim
            else:
                d = ((p.x - cx) / rx) ** 2 + ((p.y - cy) / ry) ** 2 + ((p.z - cz) / rz) ** 2
            if d >= 1.0:
                continue
            span = root_z - tip_z
            along = (root_z - p.z) / span if abs(span) > 1e-6 else 0.0  # 0 at the root, 1 at the tip
            if along <= 0.0:
                continue
            f = smooth(1.0 - d ** .5) * smooth(along / .25)
            f = min(1.0, f * 1.6)
            if f < 1e-3:
                continue
            # across chains by x distance (inverse distance, sharp), along the chain between the two nearest bones
            if args.chains == 1:
                cw = [1.0]
            else:
                xs = [chains[c][0].x for c in range(args.chains)]
                raw = [1.0 / (abs(p.x - x) + .15 * rx) ** 2 for x in xs]
                total = sum(raw); cw = [r / total for r in raw]
            t = max(0.0, min(args.bones - 1e-4, along * args.bones - .5))
            i0 = int(t); frac = t - i0; i1 = min(args.bones - 1, i0 + 1)
            for g in v.groups:
                g.weight *= (1.0 - f)
            for c in range(args.chains):
                if cw[c] < 1e-3:
                    continue
                groups[names[c][i0]].add([v.index], f * cw[c] * (1.0 - frac), "ADD")
                if i1 != i0:
                    groups[names[c][i1]].add([v.index], f * cw[c] * frac, "ADD")
            weighted += 1

    out = common.export_ue_fbx(args.out, [arm] + meshes)
    after = common.stats(meshes, arm)
    report = {"in": args.inp, "out": str(out), "bones_added": [n for row in names for n in row], "weighted_verts": weighted,
              "fit_error": round(fit_err, 5), "fit_bones": fit_n, "before": before, "after": after,
              "seconds": round(time.time() - started, 1)}
    if args.report:
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(json.dumps(report, indent=1), encoding="utf-8")
    common.result(**report)


main()
