"""Re-bind a skinned body onto a clean rest pose (Docs/RigAudit.md, "Blender round-trip").

    python Tools/RunBlender.py rebind.py -- --in X.fbx --out Y.fbx [--ref Saved/.../MonsterRigPoints/<body>.json]
                                           [--chains arms,legs] [--identity] [--report out.json]

Some Tripo bodies reached UE with an animation frame as their reference pose (the HQ Ranger stands mid-run: right elbow
bent 57 degrees, legs mid-stride). Every tool that reads the bind pose (the FK clip transfer, grips, the rig audit) then
works from a bent arm. This script poses the limb chains onto straight reference directions, deforms the mesh with it
(the armature modifier applied) and makes that pose the new rest pose, so the skin is unchanged for any animated pose:
  vertex' = Bind' * Bind^-1 * vertex   =>   Pose * Bind'^-1 * vertex' = Pose * Bind^-1 * vertex.
Bone names, hierarchy, weights, UVs and materials are kept; only rest transforms (and the rest-pose geometry) change.

Reference directions: --ref is a UE bind-point JSON ({"bind": {bone: [x,y,z]}}, e.g. a monster's MonsterRigPoints file, the
source rig of the FK transfer). Directions are compared in each body's own frame (left = clavicle_r -> clavicle_l,
up = pelvis -> neck_01, forward = up x left), so axis conventions between UE and Blender do not matter. Without --ref a
canonical pose is used: arms straight, 15 degrees below horizontal, legs straight down.
--identity round-trips without changes (export test).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

CHAINS = {
    "spine": [("pelvis", "spine_01"), ("spine_01", "spine_02"), ("spine_02", "spine_03"), ("spine_03", "neck_01"), ("neck_01", "head")],
    "arms": [(f"{b}_{s}", f"{c}_{s}") for s in ("l", "r") for b, c in (("upperarm", "lowerarm"), ("lowerarm", "hand"), ("hand", "middle_01"))],
    "legs": [(f"{b}_{s}", f"{c}_{s}") for s in ("l", "r") for b, c in (("thigh", "calf"), ("calf", "foot"), ("foot", "ball"))],
}


def frame(points: dict[str, Vector], unreal_space: bool = False):
    """World-up body frame: up = +Z (UE component and Blender world are both Z-up), left = the horizontal clavicle line.
    UE is left-handed and the FBX conversion mirrors one axis, so forward = up x left in UE coordinates but left x up in
    Blender's (checked on the Tripo reference rigs: foot -> ball runs along +forward in both)."""
    up = Vector((0.0, 0.0, 1.0))
    left = points["clavicle_l"] - points["clavicle_r"]
    left = (left - up * left.dot(up)).normalized()
    fwd = (up.cross(left) if unreal_space else left.cross(up)).normalized()
    return left, up, fwd


def to_frame(v: Vector, f) -> Vector:
    return Vector((v.dot(f[0]), v.dot(f[1]), v.dot(f[2])))


def from_frame(c: Vector, f) -> Vector:
    return f[0] * c.x + f[1] * c.y + f[2] * c.z


def canonical(bone: str) -> Vector:
    """(left, up, forward) coordinates of the canonical straight pose."""
    if bone in ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01"):
        return Vector((0.0, 1.0, 0.0))
    if bone.startswith("foot"):
        return Vector((0.0, -0.45, 0.89))
    side = 1.0 if bone.endswith("_l") else -1.0
    if bone.startswith(("upperarm", "lowerarm", "hand")):
        d = Vector((side * math.cos(math.radians(15)), -math.sin(math.radians(15)), 0.0))
        return d
    return Vector((0.0, -1.0, 0.0))  # legs straight down


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="src", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--ref", default="")
    p.add_argument("--chains", default="spine,arms,legs")
    p.add_argument("--identity", action="store_true")
    p.add_argument("--report", default="")
    a = p.parse_args(common.script_args())
    t0 = time.time()
    common.reset_scene()
    arm, meshes = common.import_any(Path(a.src).resolve())
    if not arm:
        raise SystemExit("no armature")
    report = {"source": a.src, "changed": {}}
    if not a.identity:
        ref_frame = ref_pts = None
        if a.ref:
            data = json.loads(Path(a.ref).read_text(encoding="utf-8"))["bind"]
            ref_pts = {k: Vector(v) for k, v in data.items()}
            ref_frame = frame(ref_pts, unreal_space=True)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.mode_set(mode="POSE")
        pb = arm.pose.bones
        mw = arm.matrix_world

        def world_heads():
            bpy.context.view_layer.update()
            return {b.name: mw @ b.head for b in pb}

        heads = world_heads()
        body = frame(heads)
        balls = [heads[b] - heads[f] for f, b in (("foot_l", "ball_l"), ("foot_r", "ball_r")) if f in heads and b in heads]
        report["forwardCheck"] = round(sum(balls, Vector()).normalized().dot(body[2]), 3) if balls else None  # > 0: frame is right
        pairs = [pair for c in a.chains.split(",") if c for pair in CHAINS[c]]
        # Hinge fronts from the ORIGINAL pose: a bent elbow shows the humerus' front (the forearm swings to it), a bent knee
        # the thigh's back (the calf swings behind it). Straightening by the shortest arc alone keeps whatever twist the
        # captured frame had, so the limb's front would no longer face the body's front; each hinge root is also twisted
        # about its own axis to put that front forward (knees: backward) in the new rest pose.
        HINGES = {f"upperarm_{s}": (f"lowerarm_{s}", f"hand_{s}", 1.0) for s in ("l", "r")}
        HINGES.update({f"thigh_{s}": (f"calf_{s}", f"foot_{s}", -1.0) for s in ("l", "r")})
        fronts = {}
        for root, (mid, end, sign) in HINGES.items():
            if not all(n in heads for n in (root, mid, end)):
                continue
            u = (heads[mid] - heads[root]).normalized(); f = (heads[end] - heads[mid]).normalized()
            perp = f - u * f.dot(u)
            if perp.length > math.sin(math.radians(15)):
                # carried in the root bone's own (pose) frame, so it follows the bone as it is re-aimed
                fronts[root] = (pb[root].matrix.to_3x3().inverted() @ (arm.matrix_world.to_3x3().inverted() @ (perp.normalized() * sign)))
        report["hingeFronts"] = sorted(fronts)
        for bone, child in pairs:
            if bone not in pb or child not in pb:
                continue
            heads = world_heads()
            cur = (heads[child] - heads[bone]).normalized()
            if ref_pts and bone in ref_pts and child in ref_pts:
                want = from_frame(to_frame((ref_pts[child] - ref_pts[bone]).normalized(), ref_frame), body).normalized()
            else:
                want = from_frame(canonical(bone), body).normalized()
            angle = math.degrees(cur.angle(want)) if cur.length and want.length else 0.0
            if angle < .5:
                continue
            rot = cur.rotation_difference(want).to_matrix().to_4x4()
            head = heads[bone]
            world = mw @ pb[bone].matrix
            world = Matrix.Translation(head) @ rot @ Matrix.Translation(-head) @ world
            pb[bone].matrix = mw.inverted() @ world
            report["changed"][bone] = round(angle, 2)
            if bone in fronts:
                bpy.context.view_layer.update()
                heads = world_heads()
                axis = (heads[HINGES[bone][0]] - heads[bone]).normalized()
                front = (mw.to_3x3() @ (pb[bone].matrix.to_3x3() @ fronts[bone]))
                front = (front - axis * front.dot(axis)).normalized()
                goal = body[2] - axis * body[2].dot(axis)  # body forward
                if goal.length > 1e-4 and front.length > 1e-4:
                    goal.normalize()
                    twist = math.atan2(axis.dot(front.cross(goal)), front.dot(goal))
                    spin = Matrix.Rotation(twist, 4, axis)
                    head = heads[bone]
                    world = Matrix.Translation(head) @ spin @ Matrix.Translation(-head) @ (mw @ pb[bone].matrix)
                    pb[bone].matrix = mw.inverted() @ world
                    report.setdefault("twisted", {})[bone] = round(math.degrees(twist), 2)
        bpy.context.view_layer.update()
        bpy.ops.object.mode_set(mode="OBJECT")
        # Bake the pose into the geometry, then make it the rest pose.
        for m in meshes:
            mod = next((md for md in m.modifiers if md.type == "ARMATURE"), None)
            if not mod:
                continue
            name = mod.name
            bpy.context.view_layer.objects.active = m
            bpy.ops.object.modifier_copy(modifier=name)
            bpy.ops.object.modifier_apply(modifier=name)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.mode_set(mode="POSE")
        bpy.ops.pose.select_all(action="SELECT")
        bpy.ops.pose.armature_apply(selected=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        for m in meshes:  # the copied modifier (now first) keeps deforming from the new rest
            for md in m.modifiers:
                if md.type == "ARMATURE":
                    md.name = "Armature"
    out = common.export_ue_fbx(Path(a.out).resolve(), [arm] + meshes)
    report.update({"out": str(out), "seconds": round(time.time() - t0, 2), "stats": common.stats(meshes, arm),
                   "armatureScale": list(arm.scale)})
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=1), encoding="utf-8")
    common.result(kind="rebind", **report)


main()
