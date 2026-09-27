"""Shared helpers for the headless Blender scripts (run them through Tools/RunBlender.py, see Docs/RigAudit.md).

* import_any(path): FBX / GLB / glTF into an empty scene; returns (armature or None, [mesh objects]).
* export_ue_fbx(path, objects): UE-ready FBX. Scale 1.0 with FBX_SCALE_UNITS (Blender metres -> UE centimetres), Z up,
  -Y forward (UE's own FBX import convention), no leaf bones, bone names untouched (UE5 preset names stay exact),
  deform bones only, no baked animation. Deterministic: same input, same settings.
* stats(objects, armature): the QA numbers (tris, loose parts, non-manifold edges, flipped-normal estimate, bone count,
  zero-weight verts).
* result(**kw): prints one BLENDER_RESULT {json} line for the wrapper.
"""
from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

PROJECT = Path(os.environ.get("CIRE_PROJECT_ROOT", Path(__file__).resolve().parents[2]))


def script_args() -> list[str]:
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def result(**kw) -> None:
    print("BLENDER_RESULT " + json.dumps(kw, default=str), flush=True)


def reset_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_any(path: str | Path):
    path = Path(path)
    before = set(bpy.data.objects)
    ext = path.suffix.lower()
    if ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(path), use_anim=False, ignore_leaf_bones=True, automatic_bone_orientation=False,
                                 use_prepost_rot=True)
    elif ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=str(path))
    else:
        raise ValueError(f"unsupported input {path}")
    new = [o for o in bpy.data.objects if o not in before]
    arm = next((o for o in new if o.type == "ARMATURE"), None)
    meshes = [o for o in new if o.type == "MESH"]
    return arm, meshes


def export_ue_fbx(path: str | Path, objects) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, apply_scale_options="FBX_SCALE_UNITS", global_scale=1.0,
                             axis_forward="-Y", axis_up="Z", add_leaf_bones=False, use_armature_deform_only=True,
                             bake_anim=False, primary_bone_axis="Y", secondary_bone_axis="X", mesh_smooth_type="FACE",
                             use_tspace=False, path_mode="COPY", embed_textures=False, object_types={"ARMATURE", "MESH"})
    return path


def triangles(obj) -> int:
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def stats(meshes, arm=None) -> dict:
    out = {"tris": 0, "verts": 0, "loose_parts": 0, "non_manifold_edges": 0, "flipped_normals_est": 0,
           "bones": len(arm.data.bones) if arm else 0, "zero_weight_verts": 0, "meshes": len(meshes)}
    for obj in meshes:
        me = obj.data
        out["tris"] += triangles(obj)
        out["verts"] += len(me.vertices)
        bm = bmesh.new(); bm.from_mesh(me)
        # UE's FBX export splits vertices at UV/normal seams: weld coincident ones so seams do not read as holes.
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bm.verts.ensure_lookup_table(); bm.verts.index_update()
        out["non_manifold_edges"] += sum(1 for e in bm.edges if not e.is_manifold)
        # loose parts (islands)
        seen = set(); parts = 0
        for v in bm.verts:
            if v.index in seen: continue
            parts += 1; stack = [v]; seen.add(v.index)
            while stack:
                cur = stack.pop()
                for e in cur.link_edges:
                    o = e.other_vert(cur)
                    if o.index not in seen:
                        seen.add(o.index); stack.append(o)
        out["loose_parts"] += parts
        # flipped normals: faces whose normal points toward the object's centre (rough, fine for closed bodies)
        centre = sum((v.co for v in bm.verts), Vector()) / max(1, len(bm.verts))
        out["flipped_normals_est"] += sum(1 for f in bm.faces if f.normal.dot(f.calc_center_median() - centre) < -1e-6)
        bm.free()
        if arm and obj.vertex_groups:
            deform = {b.name for b in arm.data.bones if b.use_deform}
            idx = {g.index for g in obj.vertex_groups if g.name in deform}
            out["zero_weight_verts"] += sum(1 for v in me.vertices if sum(g.weight for g in v.groups if g.group in idx) < 1e-4)
    return out


def bbox_world(objects):
    lo = Vector((1e9, 1e9, 1e9)); hi = Vector((-1e9, -1e9, -1e9))
    for o in objects:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi


def angle_deg(a: Vector, b: Vector) -> float:
    if a.length < 1e-9 or b.length < 1e-9:
        return 0.0
    return math.degrees(a.angle(b))
