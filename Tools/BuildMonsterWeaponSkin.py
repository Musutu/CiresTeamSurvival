"""Re-skin the weapons fused into Tripo monster meshes so they stay in the hand.

Tripo's auto-rig spreads a baked weapon's weights between the hand and whatever it
hung next to in the bind pose (the Pack Leader's greataxe follows its thigh, so the
haft stretches when the arm swings). For every body listed below this script:

  1. copies the ORIGINAL skeletal mesh to /Game/Art/Characters/MonsterAnim/Bodies/SK_<Body>
     (same skeleton, so every original clip still plays on it; the original is never saved),
  2. splits the mesh into connected islands and picks the weapon island(s): islands that
     carry hand weights and reach far from the hand (a weapon, not a glove or sleeve),
  3. weights every vertex of those islands 100% to the holding hand bone (rigid weapon).

Default: read-only analysis -> Saved/MonsterWeaponSkin.json. Pass -CireMonsterWeaponBuild to write.
Needs GeometryScripting, enabled on the command line only (the .uproject is not changed):
  UnrealEditor-Cmd <project> -run=pythonscript -script=Tools/BuildMonsterWeaponSkin.py
      -EnablePlugins=GeometryScripting -unattended -nullrhi [-CireMonsterWeaponBuild]
"""
import json
import math
import stat
from pathlib import Path

import unreal

BODIES = {
    # body: (source mesh, holding hand). The analysis (Saved/MonsterWeaponSkin.json, 2026-09-24) found the
    # Siegebreaker maul, both caster staves and the Hunter B crossbow fused into the hand geometry and already
    # following it; only the Pack Leader's greataxe is a separate island weighted to the right foot (ball_r 0.77).
    "GravemawPackLeader": ("/Game/Tripo/Monsters/GravemawPackLeader/CTS_Boss_GravemawPackLeader", "hand_r"),
}
ANALYSE_ONLY = {
    "HollowSiegebreaker": ("/Game/Tripo/Monsters/HollowSiegebreaker/CTS_Boss_HollowSiegebreaker", "hand_r"),
    "BlightCaster": ("/Game/Tripo/Monsters/BlightCaster/CTS_Monster_BlightCaster", "hand_r"),
    "BlightCasterB": ("/Game/Tripo/Monsters/BlightCasterB/CTS_Monster_BlightCasterB", "hand_r"),
    "BarbedHunterB": ("/Game/Tripo/Monsters/BarbedHunterB/CTS_Monster_BarbedHunterB", "hand_r"),
}
OUT_DIR = "/Game/Art/Characters/MonsterAnim/Bodies"
GS = unreal.GeometryScript_BoneWeights


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


def ref_positions(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    out = {}
    for i in range(comp.get_num_bones()):
        name = comp.get_bone_name(i)
        out[str(name)] = comp.get_ref_pose_transform(i) if hasattr(comp, "get_ref_pose_transform") else None
    return comp


def islands(dm):
    """Union-find over triangles -> vertex island id."""
    count = unreal.GeometryScript_MeshQueries.get_vertex_count(dm)
    parent = list(range(count))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    tri_count = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dm)
    for t in range(tri_count):
        tri, valid = unreal.GeometryScript_MeshQueries.get_triangle_indices(dm, t)
        if not valid:
            continue
        a, b, c = find(tri.x), find(tri.y), find(tri.z)
        parent[b] = a
        parent[find(c)] = a
    return [find(v) for v in range(count)]


def analyse(body, path, hand, build):
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        return {"error": "missing " + path}
    dm = unreal.DynamicMesh()
    dm, _ = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    names = bone_names(dm)
    index = {n: i for i, n in enumerate(names)}
    hand_bones = {index[n] for n in names if n == hand or n.endswith(hand[-2:]) and any(n.startswith(f) for f in ("thumb", "index", "middle", "ring", "pinky"))}
    count = unreal.GeometryScript_MeshQueries.get_vertex_count(dm)
    pos, wts = {}, {}
    for v in range(count):
        p, valid = unreal.GeometryScript_MeshQueries.get_vertex_position(dm, v)
        if valid:
            pos[v] = (p.x, p.y, p.z)
            wts[v] = weights(dm, v)
    island = islands(dm)
    groups = {}
    for v in pos:
        groups.setdefault(island[v], []).append(v)
    # Hand position in the bind pose: weighted mean of vertices dominated by the hand bones.
    hand_pts = [pos[v] for v in pos if sum(b.weight for b in wts[v] if b.bone_index in hand_bones) > .6]
    hand_c = tuple(sum(p[i] for p in hand_pts) / max(1, len(hand_pts)) for i in range(3))
    rows = []
    for gid, verts in groups.items():
        xs = [pos[v] for v in verts]
        lo = [min(p[i] for p in xs) for i in range(3)]
        hi = [max(p[i] for p in xs) for i in range(3)]
        hand_share = sum(sum(b.weight for b in wts[v] if b.bone_index in hand_bones) for v in verts) / len(verts)
        bones = {}
        for v in verts:
            for b in wts[v]:
                bones[names[b.bone_index]] = bones.get(names[b.bone_index], 0) + b.weight
        top = sorted(bones.items(), key=lambda kv: -kv[1])[:5]
        near = min(math.dist(p, hand_c) for p in xs) if hand_pts else 1e9
        far = max(math.dist(p, hand_c) for p in xs) if hand_pts else 0
        rows.append({"island": gid, "vertices": len(verts), "lo": [round(x, 1) for x in lo], "hi": [round(x, 1) for x in hi],
                     "hand_share": round(hand_share, 3), "near_hand": round(near, 1), "far_from_hand": round(far, 1),
                     "top_bones": [(n, round(w / len(verts), 3)) for n, w in top]})
    rows.sort(key=lambda r: -r["vertices"])
    height = max(p[2] for p in pos.values()) - min(p[2] for p in pos.values())
    # Weapon islands: touch the hand, reach well beyond it, and are not the body itself.
    body_island = rows[0]["island"]
    weapon = [r for r in rows if r["island"] != body_island and r["near_hand"] < .2 * height and r["far_from_hand"] > .18 * height and r["vertices"] > 100]
    result = {"mesh": path, "vertices": count, "islands": len(rows), "height": round(height, 1), "hand_centre": [round(x, 1) for x in hand_c],
              "weapon_islands": [r["island"] for r in weapon], "rows": rows[:25]}
    if build and weapon:
        target = OUT_DIR + "/SK_" + body
        lib = unreal.EditorAssetLibrary
        file = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())) / ("Art/Characters/MonsterAnim/Bodies/SK_" + body + ".uasset")
        if file.exists():
            file.chmod(file.stat().st_mode | stat.S_IWRITE)
            lib.delete_asset(target)
        copy = lib.duplicate_asset(path, target)
        cdm = unreal.DynamicMesh()
        cdm, _ = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
            copy, cdm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        rigid = unreal.GeometryScriptBoneWeight()
        rigid.bone_index = index[hand]
        rigid.weight = 1.0
        changed = 0
        chosen = set(r["island"] for r in weapon)
        for v in pos:
            if island[v] in chosen:
                GS.set_vertex_bone_weights(cdm, v, [rigid])
                changed += 1
        opts = unreal.GeometryScriptCopyMeshToAssetOptions()
        for k, val in (("replace_materials", False), ("remap_bone_indices_to_match_asset", True), ("use_original_vertex_order", True),
                       ("enable_recompute_normals", False), ("enable_recompute_tangents", False)):
            try:
                opts.set_editor_property(k, val)
            except Exception as error:
                result.setdefault("option_errors", []).append(repr(error))
        res = unreal.GeometryScript_AssetUtils.copy_mesh_to_skeletal_mesh(cdm, copy, opts, unreal.GeometryScriptMeshWriteLOD())
        result["reweighted"] = changed
        result["copy_result"] = str(res)
        result["skeleton_kept"] = copy.get_editor_property("skeleton") == mesh.get_editor_property("skeleton")
        result["saved"] = bool(lib.save_loaded_asset(copy, only_if_is_dirty=False))
        result["output"] = target
    return result


def main():
    build = "-ciremonsterweaponbuild" in unreal.SystemLibrary.get_command_line().lower()
    report = {"build": build, "bodies": {}}
    for body, (path, hand) in list(BODIES.items()) + list(ANALYSE_ONLY.items()):
        try:
            report["bodies"][body] = analyse(body, path, hand, build and body in BODIES)
        except Exception as error:
            report["bodies"][body] = {"error": repr(error)}
    out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "MonsterWeaponSkin.json"
    out.write_text(json.dumps(report, indent=1), encoding="utf-8")
    unreal.log("CIRE_MONSTER_WEAPON_SKIN_DONE " + str(out))


main()
