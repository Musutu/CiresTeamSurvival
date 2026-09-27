"""QA turnaround: front / back / left / right, face close-up, both hands, plus an elbow-flex test pose; and a stats JSON.

    python Tools/RunBlender.py qa_turnaround.py -- --in <mesh.fbx|glb> --out <dir> [--res 768] [--no-flex]

Renders with Workbench (texture colour, studio light): fast and GPU-independent in background mode. The test pose flexes
both elbows 90 degrees toward the body's front (anatomical flexion: forearm swings forward in the upper arm's plane), so
bad weights (candy-wrapper, tearing) and a backwards-bending rig show up in <out>/flex_*.png. Stats land in <out>/stats.json:
tris, loose parts, non-manifold edges, flipped-normal estimate, bone count, zero-weight verts, and per arm the bind-pose
elbow angle (negative = hyperextended in the bind pose) and the palm direction (from the finger bones).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def look(cam, target: Vector, direction: Vector, distance: float, ortho: float | None = None):
    cam.location = target + direction.normalized() * distance
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    if ortho:
        cam.data.type = "ORTHO"; cam.data.ortho_scale = ortho
    else:
        cam.data.type = "PERSP"; cam.data.lens = 50


def bone_head(arm, name):
    b = arm.pose.bones.get(name)
    return (arm.matrix_world @ b.head) if b else None


def render(path: Path):
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def arm_report(arm, side: str, forward: Vector) -> dict:
    pb = arm.pose.bones
    up, lo, hand = pb.get(f"upperarm_{side}"), pb.get(f"lowerarm_{side}"), pb.get(f"hand_{side}")
    if not (up and lo and hand):
        return {"present": False}
    mw = arm.matrix_world
    s, e, w = mw @ up.head, mw @ lo.head, mw @ hand.head
    u, f = (e - s).normalized(), (w - e).normalized()
    # signed elbow angle in the plane of the arm, positive when the forearm leans toward the body's front
    perp = f - u * f.dot(u)
    ant = forward - u * forward.dot(u)
    signed = math.degrees(math.atan2(perp.dot(ant.normalized()) if ant.length > 1e-6 else 0.0, f.dot(u)))
    out = {"present": True, "elbowSignedDeg": round(signed, 2), "lowerarmRollDeg": round(math.degrees(lo.bone.matrix_local.to_euler().y), 2)}
    mid = pb.get(f"middle_01_{side}")
    if mid:
        knuckle = (mw @ mid.head - w).normalized()
        out["knuckleDir"] = [round(c, 3) for c in knuckle]
    return out


def flex_elbows(arm, forward: Vector, degrees: float = 90.0):
    """Swing each forearm toward the body front by `degrees` about the elbow hinge axis (world-defined)."""
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    for side in ("l", "r"):
        up, lo = arm.pose.bones.get(f"upperarm_{side}"), arm.pose.bones.get(f"lowerarm_{side}")
        if not (up and lo):
            continue
        mw = arm.matrix_world
        u = (mw @ lo.head - mw @ up.head).normalized()
        axis = u.cross(forward).normalized()  # rotating the forearm by +angle about this carries u toward forward
        if axis.length < 1e-6:
            continue
        rot_world = Quaternion(axis, math.radians(degrees))
        # pose-space rotation of lowerarm about its head
        m = mw.inverted() @ Matrix.Translation(mw @ lo.head) @ rot_world.to_matrix().to_4x4() @ Matrix.Translation(-(mw @ lo.head)) @ mw
        lo.matrix = m @ lo.matrix
        bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="src", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--res", type=int, default=768)
    p.add_argument("--no-flex", action="store_true")
    a = p.parse_args(common.script_args())
    t0 = time.time()
    common.reset_scene()
    arm, meshes = common.import_any(Path(a.src).resolve())
    out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)  # absolute: Blender resolves relative render paths against the drive root
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.show_cavity = True
    sc.render.resolution_x = sc.render.resolution_y = a.res
    sc.render.film_transparent = False
    sc.world = bpy.data.worlds.new("w") if not sc.world else sc.world
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    lo, hi = common.bbox_world(meshes)
    centre = (lo + hi) / 2; height = hi.z - lo.z
    # Blender import of a UE-style FBX: the character faces -Y (UE +X forward exported with -Y forward). Detect from feet.
    forward = Vector((0, -1, 0))
    if arm:
        ball = [bone_head(arm, n) for n in ("ball_l", "ball_r")]; foot = [bone_head(arm, n) for n in ("foot_l", "foot_r")]
        if all(ball) and all(foot):
            f = (ball[0] - foot[0]) + (ball[1] - foot[1]); f.z = 0
            if f.length > 1e-6:
                forward = f.normalized()
    right = forward.cross(Vector((0, 0, 1))).normalized()
    views = {"front": forward, "back": -forward, "left": -right, "right": right}
    for name, d in views.items():
        look(cam, centre, d, height * 3, ortho=height * 1.15)
        render(out / f"{name}.png")
    if arm:
        head = bone_head(arm, "head")
        if head:
            look(cam, head + Vector((0, 0, height * .03)), forward, height * .6, ortho=height * .22)
            render(out / "face.png")
        for side in ("l", "r"):
            h = bone_head(arm, f"hand_{side}")
            if h:
                look(cam, h, forward + Vector((0, 0, .3)), height * .6, ortho=height * .2)
                render(out / f"hand_{side}.png")
    report = {"source": a.src, "stats": common.stats(meshes, arm), "heightUnits": round(height, 3), "forward": list(forward)}
    if arm:
        report["arms"] = {s: arm_report(arm, s, forward) for s in ("l", "r")}
        if not a.no_flex:
            flex_elbows(arm, forward, 90)
            report["armsFlexed"] = {s: arm_report(arm, s, forward) for s in ("l", "r")}
            for name in ("front", "left", "right"):
                look(cam, centre, views[name], height * 3, ortho=height * 1.15)
                render(out / f"flex_{name}.png")
    report["seconds"] = round(time.time() - t0, 2)
    (out / "stats.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    common.result(kind="qa_turnaround", out=str(out), **{k: report[k] for k in ("stats", "seconds")}, arms=report.get("arms"))


main()
