"""Build /Game/Art/Characters/Motion03/SK_BearMotion (+ SKEL_BearMotion) from SK_Bear.

The Tripo bear import has only a hip bone on its right rear leg, so that leg could only swing
rigidly. This script duplicates SK_Bear and its skeleton, mirrors the left rear chain
(1_Left_Limb_1..4) onto the right hip, reparents it under 1_Right_Limb_0 and copies the mirrored
left-rear skin weights onto the right-rear vertices (nearest mirrored vertex, mean distance ~0.7 units).
The original SK_Bear and its skeleton are never modified.

Run it in an ISOLATED copy of Tools/CreatureBuilder whose .uproject additionally enables the
GeometryScripting and MeshModelingToolset plugins, e.g.
  UnrealEditor-Cmd.exe <copy>/BearLab.uproject -ExecutePythonScript=<this file> -unattended -nullrhi
then copy <copy>/Content/Art/Characters/Motion03/*.uasset into the main project.
A JSON log is written to <copy>/Saved/BearMotion03Build.json.
"""
import json, math, unreal
OUT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "BearMotion03Build.json"
LOG = {}
def log(k, v):
    LOG[k] = v
    unreal.log("BEARBUILD %s=%s" % (k, v))
def flush():
    open(OUT, "w").write(json.dumps(LOG, indent=1, default=str))

eal = unreal.EditorAssetLibrary
SRC = "/Game/Art/Characters/CreatureSurfaces02/SK_Bear"
DST_DIR = "/Game/Art/Characters/Motion03"
DST = DST_DIR + "/SK_BearMotion"
DST_SKEL = DST_DIR + "/SKEL_BearMotion"

def weights(dm, v):
    r = unreal.GeometryScript_BoneWeights.get_vertex_bone_weights(dm, v)
    if isinstance(r, tuple):
        for item in r:
            if isinstance(item, (list, unreal.Array)):
                return list(item)
    return []

def bone_names(dm):
    info = unreal.GeometryScript_BoneWeights.get_all_bones_info(dm)
    if isinstance(info, tuple):
        info = [x for x in info if isinstance(x, (list, unreal.Array))][0]
    return [str(b.name) for b in info]

def run():
    src = unreal.load_asset(SRC)
    src_skel = src.get_editor_property("skeleton")
    log("src_skeleton", src_skel.get_path_name())
    for p in (DST, DST_SKEL):
        if eal.does_asset_exist(p):
            eal.delete_asset(p)
    skel = eal.duplicate_asset(src_skel.get_path_name(), DST_SKEL)
    mesh = eal.duplicate_asset(SRC, DST)
    try:
        mesh.set_skeleton(skel)
    except Exception as e:
        log("set_skeleton_method", repr(e))
        mesh.call_method("SetSkeleton", (skel,))
    mesh.modify()
    assert mesh.get_editor_property("skeleton").get_path_name().startswith(DST_DIR), "duplicate still points at the original skeleton"
    log("dst_skeleton", mesh.get_editor_property("skeleton").get_path_name())

    mod = unreal.SkeletonModifier()
    assert mod.set_skeletal_mesh(mesh)
    before = [str(n) for n in mod.get_all_bone_names()]
    opts = unreal.MirrorOptions()
    opts.set_editor_property("left_string", "Left")
    opts.set_editor_property("right_string", "Right")
    opts.set_editor_property("mirror_children", True)
    ok = mod.mirror_bones(["1_Left_Limb_1"], opts)
    # Mirroring keeps the source parent for the chain root; hang it from the real right hip.
    g = mod.get_bone_transform("1_Right_Limb_1", True)
    log("reparent", mod.parent_bone("1_Right_Limb_1", "1_Right_Limb_0"))
    g2 = mod.get_bone_transform("1_Right_Limb_1", True)
    log("reparent_global_drift", round((g.translation - g2.translation).length(), 4))
    after = [str(n) for n in mod.get_all_bone_names()]
    log("mirror_ok", ok)
    log("added", [b for b in after if b not in before])
    for b in ["1_Right_Limb_1", "1_Right_Limb_2", "1_Right_Limb_3", "1_Right_Limb_4"]:
        assert b in after, "missing " + b
        t = mod.get_bone_transform(b, True)
        l = mod.get_bone_transform(b.replace("Right", "Left"), True)
        log(b, {"parent": str(mod.get_parent_name(b)),
                "global": [round(t.translation.x, 2), round(t.translation.y, 2), round(t.translation.z, 2)],
                "left_global": [round(l.translation.x, 2), round(l.translation.y, 2), round(l.translation.z, 2)]})
    assert mod.commit_skeleton_to_skeletal_mesh()
    flush()

    dm = unreal.DynamicMesh()
    dm, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    names = bone_names(dm)
    log("dm_bones", len(names))
    idx = {n: i for i, n in enumerate(names)}
    remap = {}
    for n in names:
        if n.startswith("1_Left_Limb_"):
            r = n.replace("Left", "Right")
            if r in idx:
                remap[idx[n]] = idx[r]
    log("remap", {names[k]: names[v] for k, v in remap.items()})
    count = unreal.GeometryScript_MeshQueries.get_vertex_count(dm)
    pos = {}
    wts = {}
    left_rear = []
    right_rear = []
    rr0 = idx["1_Right_Limb_0"]
    lr = set(i for n, i in idx.items() if n.startswith("1_Left_Limb_"))
    for v in range(count):
        p, valid = unreal.GeometryScript_MeshQueries.get_vertex_position(dm, v)
        if not valid:
            continue
        w = weights(dm, v)
        pos[v] = (p.x, p.y, p.z)
        wts[v] = w
        if sum(b.weight for b in w if b.bone_index in lr) > .05 and p.x > 0:
            left_rear.append(v)
        if sum(b.weight for b in w if b.bone_index == rr0) > .05 and p.x < 0:
            right_rear.append(v)
    log("left_rear_vertices", len(left_rear))
    log("right_rear_vertices", len(right_rear))
    flush()
    cell = 3.0
    grid = {}
    for v in left_rear:
        x, y, z = pos[v]
        grid.setdefault((int(math.floor(x / cell)), int(math.floor(y / cell)), int(math.floor(z / cell))), []).append(v)

    def nearest(x, y, z):
        best = None
        bd = 1e18
        c = (int(math.floor(x / cell)), int(math.floor(y / cell)), int(math.floor(z / cell)))
        for r in range(0, 5):
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    for dz in range(-r, r + 1):
                        if max(abs(dx), abs(dy), abs(dz)) != r:
                            continue
                        for v in grid.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                            px, py, pz = pos[v]
                            d = (px - x) ** 2 + (py - y) ** 2 + (pz - z) ** 2
                            if d < bd:
                                bd = d
                                best = v
            if best is not None and math.sqrt(bd) <= r * cell:
                break
        return best, (math.sqrt(bd) if best is not None else 1e9)

    changed = 0
    dist = []
    for v in right_rear:
        x, y, z = pos[v]
        m, d = nearest(-x, y, z)
        if m is None or d > 6.0:
            continue
        dist.append(d)
        rr_share = sum(b.weight for b in wts[v] if b.bone_index == rr0)
        acc = {}
        for b in wts[v]:
            if b.bone_index != rr0:
                acc[b.bone_index] = acc.get(b.bone_index, 0) + b.weight
        mirrored = {}
        for b in wts[m]:
            bi = remap.get(b.bone_index, b.bone_index)
            mirrored[bi] = mirrored.get(bi, 0) + b.weight
        total = sum(mirrored.values()) or 1
        for bi, wv in mirrored.items():
            acc[bi] = acc.get(bi, 0) + wv / total * rr_share
        s = sum(acc.values()) or 1
        out = []
        for bi, wv in acc.items():
            bw = unreal.GeometryScriptBoneWeight()
            bw.bone_index = bi
            bw.weight = wv / s
            out.append(bw)
        unreal.GeometryScript_BoneWeights.set_vertex_bone_weights(dm, v, out)
        changed += 1
    log("reweighted", changed)
    log("mirror_max_dist", round(max(dist) if dist else 0, 3))
    log("mirror_mean_dist", round(sum(dist) / max(1, len(dist)), 3))
    flush()
    copts = unreal.GeometryScriptCopyMeshToAssetOptions()
    for k, v in (("replace_materials", False), ("remap_bone_indices_to_match_asset", True), ("use_original_vertex_order", True),
                 ("enable_recompute_normals", False), ("enable_recompute_tangents", False)):
        try:
            copts.set_editor_property(k, v)
        except Exception as e:
            log("opt_" + k, repr(e))
    res = unreal.GeometryScript_AssetUtils.copy_mesh_to_skeletal_mesh(dm, mesh, copts, unreal.GeometryScriptMeshWriteLOD())
    log("copy_to_asset", str(res))

    check = unreal.DynamicMesh()
    check, o = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, check, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    cn = bone_names(check)
    owned = {n: 0 for n in ("1_Right_Limb_0", "1_Right_Limb_1", "1_Right_Limb_2", "1_Right_Limb_3", "1_Right_Limb_4", "1_Left_Limb_1", "1_Left_Limb_2")}
    for v in range(unreal.GeometryScript_MeshQueries.get_vertex_count(check)):
        for b in weights(check, v):
            n = cn[b.bone_index] if b.bone_index < len(cn) else None
            if n in owned and b.weight > .3:
                owned[n] += 1
    log("verify_vertices_over_30pct", owned)
    log("verify_vertex_count", unreal.GeometryScript_MeshQueries.get_vertex_count(check))
    log("verify_bones", len(cn))
    eal.save_asset(DST_SKEL)
    eal.save_asset(DST)
    log("done", True)
    flush()

try:
    run()
except Exception as e:
    import traceback
    log("error", traceback.format_exc())
    flush()
unreal.log("BEARBUILD_DONE")
