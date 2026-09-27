"""List (and optionally split out) the loose parts of a skinned body (Docs/RigAudit.md).

    python Tools/RunBlender.py parts.py -- --in X.fbx [--out parts.json]
        [--split "<part ids>" --split-out Y.fbx --body-out Z.fbx [--decimate-tris N]]

Per loose part: triangles, world bounds, centre, and the dominant deform bones by summed weight. With --split the listed
parts become their own object: Y.fbx (static, weights removed, its rest-pose placement kept in the armature's space) and
the body without them in Z.fbx (same rig). --decimate-tris collapses the split part to about N triangles, keeping UVs.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

import bmesh
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def islands(bm):
    seen, out = set(), []
    for v in bm.verts:
        if v.index in seen:
            continue
        comp, stack = [], [v]
        seen.add(v.index)
        while stack:
            cur = stack.pop()
            comp.append(cur.index)
            for e in cur.link_edges:
                o = e.other_vert(cur)
                if o.index not in seen:
                    seen.add(o.index)
                    stack.append(o)
        out.append(comp)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="src", required=True)
    p.add_argument("--out", default="")
    p.add_argument("--split", default="")
    p.add_argument("--split-out", default="")
    p.add_argument("--body-out", default="")
    p.add_argument("--decimate-tris", type=int, default=0)
    a = p.parse_args(common.script_args())
    common.reset_scene()
    arm, meshes = common.import_any(Path(a.src).resolve())
    mesh = meshes[0]
    me = mesh.data
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)  # weld UE's seam splits so islands are real parts
    bm.verts.ensure_lookup_table(); bm.verts.index_update()
    welded = bmesh.new(); welded = bm
    # Map welded verts back to original vertex indices by position.
    by_pos = defaultdict(list)
    for v in me.vertices:
        by_pos[tuple(round(c, 5) for c in v.co)].append(v.index)
    groups = {g.index: g.name for g in mesh.vertex_groups}
    parts = []
    for i, comp in enumerate(islands(welded)):
        orig = []
        for vi in comp:
            orig += by_pos.get(tuple(round(c, 5) for c in welded.verts[vi].co), [])
        weights = defaultdict(float)
        for oi in orig:
            for g in me.vertices[oi].groups:
                weights[groups.get(g.group, "?")] += g.weight
        cos = [mesh.matrix_world @ me.vertices[oi].co for oi in orig]
        lo = [min(c[k] for c in cos) for k in range(3)]; hi = [max(c[k] for c in cos) for k in range(3)]
        tris = sum(len(f.verts) - 2 for f in welded.faces if f.verts[0].index in set(comp))
        top = sorted(weights.items(), key=lambda kv: -kv[1])[:4]
        parts.append({"id": i, "verts": len(orig), "tris": tris, "min": [round(x, 3) for x in lo], "max": [round(x, 3) for x in hi],
                      "bones": [[b, round(w / max(1, len(orig)), 3)] for b, w in top], "orig": orig})
    parts.sort(key=lambda r: -r["tris"])
    listing = [{k: v for k, v in r.items() if k != "orig"} for r in parts]
    if a.out:
        Path(a.out).write_text(json.dumps(listing, indent=1), encoding="utf-8")
    result = {"parts": len(parts), "top": listing[:14]}
    if a.split:
        ids = {int(x) for x in a.split.split(",") if x}
        take = sorted({oi for r in parts if r["id"] in ids for oi in r["orig"]})
        bpy.context.view_layer.objects.active = mesh
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="DESELECT"); bpy.ops.object.mode_set(mode="OBJECT")
        for oi in take:
            me.vertices[oi].select = True
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_linked(); bpy.ops.mesh.separate(type="SELECTED"); bpy.ops.object.mode_set(mode="OBJECT")
        part = next(o for o in bpy.context.scene.objects if o.type == "MESH" and o is not mesh)
        part.name = "SplitPart"
        for md in list(part.modifiers):
            part.modifiers.remove(md)
        part.vertex_groups.clear()
        part.parent = None
        if a.decimate_tris:
            now = common.triangles(part)
            if now > a.decimate_tris:
                d = part.modifiers.new("Decimate", "DECIMATE"); d.ratio = a.decimate_tris / now; d.use_collapse_triangulate = True
                bpy.context.view_layer.objects.active = part; bpy.ops.object.modifier_apply(modifier="Decimate")
        result.update({"splitTris": common.triangles(part), "bodyTris": common.triangles(mesh)})
        if a.split_out:
            common.export_static_fbx(Path(a.split_out).resolve(), [part])
        if a.body_out:
            common.export_ue_fbx(Path(a.body_out).resolve(), [arm, mesh])
    common.result(kind="parts", **result)


main()
