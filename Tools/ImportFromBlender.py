"""UE: import a Blender-fixed FBX back onto an existing skeleton (Docs/RigAudit.md, "Blender round-trip").

    UnrealEditor-Cmd <project> -run=pythonscript -script="<abs>/Tools/ImportFromBlender.py" -unattended -nullrhi -nosound
        -CireImport="<fbx>|<dest mesh object path>|<skeleton object path>"   (several rows separated by ';')
        [-CireImportCompare=<original mesh object path>]  (report bone / vertex / bounds deltas against it)
        [-CireImportMaterials=<material object path,...>]  (slot materials; default: the replaced asset's own)
        [-CireImportUpdateRef]  (also move the Skeleton asset's reference pose to the new rest pose; real replacements only)

The mesh is imported onto the given Skeleton asset (no new skeleton), without materials, textures or animations, and the
skeleton's reference pose is updated to the new rest pose with -CireImportUpdateRef. When dest is the original asset it is replaced in place,
keeping its material slots (the FBX keeps the slot names Unreal exported) and every reference to it. A dest outside the
original (e.g. /Game/_BlenderRoundTrip/...) is a dry run. Prints CIRE_IMPORT_OK / CIRE_IMPORT_FAIL and CIRE_IMPORT_COMPARE.
"""
import os
import re
import stat
import sys

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def arg(name, default=""):
    line = " ".join(sys.argv) + " " + unreal.SystemLibrary.get_command_line()
    m = re.search(r"-" + name + r"=(\"[^\"]*\"|\S+)", line)
    return m.group(1).strip('"') if m else default


def writable(object_path):
    package = object_path.split(".")[0]
    rel = package.replace("/Game/", "", 1) + ".uasset"
    path = os.path.join(ROOT, "Content", rel)
    if os.path.exists(path):
        os.chmod(path, os.stat(path).st_mode | stat.S_IWRITE)  # lockable LFS files are checked out read-only


def import_row(fbx, dest, skeleton_path):
    skeleton = unreal.load_asset(skeleton_path)
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("skeleton not found " + skeleton_path)
    package, name = dest.split(".")[0].rsplit("/", 1)
    writable(dest)
    # The FBX import (materials off) leaves WorldGridMaterial in every slot: remember the replaced asset's materials.
    previous = unreal.load_asset(dest) if unreal.EditorAssetLibrary.does_asset_exist(dest.split(".")[0]) else None
    kept = [m.get_editor_property("material_interface") for m in previous.materials] if isinstance(previous, unreal.SkeletalMesh) else []
    override = [unreal.load_asset(x) for x in arg("CireImportMaterials").split(",") if x]
    if override:
        kept = override
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = skeleton
    ui.import_materials = False
    ui.import_textures = False
    ui.import_animations = False
    ui.create_physics_asset = False
    ui.automated_import_should_detect_type = False
    data = ui.skeletal_mesh_import_data
    settings = {
        "import_morph_targets": False, "import_morph_targets_and_lods": False,
        # Only a real replacement updates the shared Skeleton asset's reference pose (-CireImportUpdateRef); dry runs never do.
        "update_skeleton_reference_pose": "-cireimportupdateref" in unreal.SystemLibrary.get_command_line().lower(),
        "use_t0_as_ref_pose": False, "convert_scene": True, "force_front_x_axis": False, "import_uniform_scale": 1.0,
        "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    }
    for key, value in settings.items():  # property names move between engine versions: set what exists
        try:
            data.set_editor_property(key, value)
        except Exception:  # noqa: BLE001
            unreal.log_warning("CIRE_IMPORT_OPTION_SKIPPED " + key)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = package
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(dest)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("import produced no skeletal mesh at " + dest)
    if mesh.skeleton != skeleton:
        raise RuntimeError("imported mesh is not on the requested skeleton")
    if kept:
        slots = list(mesh.materials)
        for i, slot in enumerate(slots):
            if i < len(kept) and kept[i]:
                slot.set_editor_property("material_interface", kept[i])
        mesh.set_editor_property("materials", slots)
        unreal.log("CIRE_IMPORT_MATERIALS %s" % [k.get_path_name() if k else None for k in kept])
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    # blender-rig: bones the FBX adds (tentacle chains, add_chain.py) are merged into the Skeleton asset: save it too.
    writable(skeleton_path)
    unreal.EditorAssetLibrary.save_loaded_asset(skeleton, only_if_is_dirty=False)
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    unreal.log_warning("CIRE_IMPORT_BONES %s bones=%d" % (dest, comp.get_num_bones()))
    return mesh


def compare(new, old):
    def bones(m):
        comp = unreal.SkeletalMeshComponent()
        comp.set_skeletal_mesh_asset(m)
        return [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    if not isinstance(old, unreal.SkeletalMesh):
        unreal.log_warning("CIRE_IMPORT_COMPARE skipped: original not loadable")
        return
    bn, bo = bones(new), bones(old)
    bn_box, bo_box = new.get_bounds(), old.get_bounds()
    unreal.log("CIRE_IMPORT_COMPARE bones=%d/%d same_names=%s extent_new=%s extent_old=%s materials=%d/%d" % (
        len(bn), len(bo), bn == bo, bn_box.box_extent, bo_box.box_extent, len(new.materials), len(old.materials)))


def main():
    rows = [r for r in arg("CireImport").split(";") if r]
    original = arg("CireImportCompare")
    for row in rows:
        try:
            fbx, dest, skeleton = row.split("|")
            mesh = import_row(fbx, dest, skeleton)
            unreal.log_warning("CIRE_IMPORT_OK %s <- %s" % (dest, fbx))
            if original:
                compare(mesh, unreal.load_asset(original))
        except Exception as error:  # noqa: BLE001
            unreal.log_error("CIRE_IMPORT_FAIL %s %s" % (row, error))
    unreal.log("CIRE_IMPORT_DONE")


main()
