"""monster-rig: audit every Tripo monster/NPC body's rig, weighting and fused geometry (Docs/MonsterRig.md).

Bodies: Content/Data/RaceMeshes.tripo.json "archetypes" + NPCMeshes.tripo.json "archetypes" (+ variants), each mesh
once. Per body:
  * rig: bone count, root/pelvis, UE5-Mannequin naming (hand_r, lowerarm_r, calf_r, ball_r, spine_03, neck_01)
  * islands: connected geometry pieces (a separately modelled weapon/shield is its own island; Tripo usually fuses)
  * weighting anomalies, measured in the bind pose:
      - leg-bound: vertices dominated (>0.5) by a leg bone (thigh/calf/foot/ball) that sit above the pelvis
      - arm-bound low: vertices dominated by an arm/hand bone that sit below knee height
      - far: vertices more than 35% of the body height from their dominant bone's bind position (stretch risk)
    A fused weapon weighted to the wrong limb shows up as leg-bound (Gravemaw's axe on ball_r) or far.
Output: Saved/MonsterRigAudit.json. Log marker CIRE_MONSTER_RIG_AUDIT_DONE.
Run: UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs>/Tools/AuditMonsterRig.py -EnablePlugins=GeometryScripting
     -unattended -nullrhi [-CireRigAuditOnly=AbyssalStalker+MawOfTheDeep]
"""
import json
import math
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
GS = unreal.GeometryScript_BoneWeights
LEG = ("thigh_", "calf_", "foot_", "ball_")
ARM = ("upperarm_", "lowerarm_", "hand_", "thumb_", "index_", "middle_", "ring_", "pinky_")
UE5_NAMES = ("hand_r", "lowerarm_r", "calf_r", "ball_r", "spine_03", "neck_01", "head", "pelvis", "root")


def weights(dm, v):
    r = GS.get_vertex_bone_weights(dm, v)
    if isinstance(r, tuple):
        for item in r:
            if isinstance(item, (list, unreal.Array)):
                return list(item)
    return list(r) if isinstance(r, (list, unreal.Array)) else []


def bone_names(dm):
    info = GS.get_all_bones_info(dm)
    if isinstance(info, tuple):
        info = [x for x in info if isinstance(x, (list, unreal.Array))][0]
    return [str(b.name) for b in info]


def bind_positions(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    out = {}
    for i in range(comp.get_num_bones()):
        name = str(comp.get_bone_name(i))
        t = comp.get_socket_transform(name, unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        out[name] = (t.x, t.y, t.z)
    return out


def islands(dm, count):
    parent = list(range(count))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for t in range(unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dm)):
        tri, valid = unreal.GeometryScript_MeshQueries.get_triangle_indices(dm, t)
        if valid:
            a = find(tri.x)
            parent[find(tri.y)] = a
            parent[find(tri.z)] = a
    return [find(v) for v in range(count)]


def audit(path):
    mesh = unreal.load_asset(path.split(".")[0])
    if not isinstance(mesh, unreal.SkeletalMesh):
        return {"error": "missing"}
    dm = unreal.DynamicMesh()
    dm, _ = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    names = bone_names(dm)
    bind = bind_positions(mesh)
    count = unreal.GeometryScript_MeshQueries.get_vertex_count(dm)
    pos, dom = {}, {}
    step = max(1, count // 40000)  # sample big meshes
    for v in range(0, count, step):
        p, valid = unreal.GeometryScript_MeshQueries.get_vertex_position(dm, v)
        if not valid:
            continue
        w = weights(dm, v)
        if not w:
            continue
        best = max(w, key=lambda b: b.weight)
        pos[v] = (p.x, p.y, p.z)
        dom[v] = (names[best.bone_index], best.weight)
    zs = [p[2] for p in pos.values()]
    lo, hi = min(zs), max(zs)
    height = max(1e-3, hi - lo)
    pelvis_z = bind.get("pelvis", (0, 0, lo + .5 * height))[2]
    knee_z = min(bind.get("calf_l", (0, 0, lo + .28 * height))[2], bind.get("calf_r", (0, 0, lo + .28 * height))[2])
    leg_high, arm_low, far = {}, {}, {}
    for v, (bone, w) in dom.items():
        p = pos[v]
        if w > .5 and bone.startswith(LEG) and p[2] > pelvis_z + .08 * height:
            leg_high[bone] = leg_high.get(bone, 0) + 1
        if w > .5 and bone.startswith(ARM) and p[2] < knee_z:
            arm_low[bone] = arm_low.get(bone, 0) + 1
        b = bind.get(bone)
        if b and math.dist(p, b) > .35 * height:
            far[bone] = far.get(bone, 0) + 1
    isl = islands(dm, count)
    sizes = {}
    for v in range(count):
        sizes[isl[v]] = sizes.get(isl[v], 0) + 1
    ordered = sorted(sizes.values(), reverse=True)
    n = max(1, len(pos))
    top = lambda d: sorted(d.items(), key=lambda kv: -kv[1])[:3]
    return {
        "bones": len(names), "ue5Names": all(x in names for x in UE5_NAMES),
        "vertices": count, "triangles": unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dm),
        "islands": len(sizes), "bigIslands": [s for s in ordered[:6] if s > 200],
        "heightRaw": round(height, 1),
        "legBoundHighPct": round(100.0 * sum(leg_high.values()) / n, 2), "legBoundHigh": top(leg_high),
        "armBoundLowPct": round(100.0 * sum(arm_low.values()) / n, 2), "armBoundLow": top(arm_low),
        "farPct": round(100.0 * sum(far.values()) / n, 2), "far": top(far),
    }


def main():
    cmd = unreal.SystemLibrary.get_command_line()
    only = None
    for token in cmd.split():
        if token.lower().startswith("-cirerigauditonly="):
            only = set(token.split("=", 1)[1].split("+"))
    bodies = {}
    for file in ("RaceMeshes.tripo.json", "NPCMeshes.tripo.json"):
        data = json.loads((ROOT / "Content/Data" / file).read_text(encoding="utf-8"))
        for arch, row in data.get("archetypes", {}).items():
            rows = [row] + [v for v in (row.get("variants") or []) + (row.get("alternates") or []) if isinstance(v, dict)] if isinstance(row, dict) else row
            for r in rows if isinstance(rows, list) else [rows]:
                if isinstance(r, dict) and r.get("mesh"):
                    bodies.setdefault(r.get("variant") or r["mesh"].rsplit(".", 1)[1].split("_", 2)[-1], {"mesh": r["mesh"], "archetype": arch, "file": file})
    report = {}
    out = ROOT / "Saved/MonsterRigAudit.json"
    for variant, row in sorted(bodies.items()):
        if only and variant not in only:
            continue
        try:
            row.update(audit(row["mesh"]))
        except Exception as error:
            row["error"] = repr(error)
        report[variant] = row
        out.write_text(json.dumps(report, indent=1), encoding="utf-8")
    unreal.log("CIRE_MONSTER_RIG_AUDIT_DONE bodies=%d" % len(report))


main()
