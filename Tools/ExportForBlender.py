"""UE commandlet: export skeletal / static meshes to FBX for the Blender pipeline (Docs/RigAudit.md).

    UnrealEditor <project> -ExecutePythonScript="<abs>/Tools/ExportForBlender.py" -RenderOffscreen -unattended -nosound -NoLiveCoding
        -CireExport=/Game/Tripo/ChampionsHQ/Ranger/CTS_ChampHQ_Ranger,/Game/...   [-CireExportDir=<abs dir>]
        [-CireExportAnim=/Game/.../A_clip,...]   (animation sequences, exported with their preview mesh)

Output: Saved/Blender/export/<AssetName>.fbx (never a committed path). Fab/Epic-licensed assets may be exported for
local inspection only; the tool refuses any output directory inside Content/, Art/ or Source/.
Prints CIRE_EXPORT_OK <asset> <file> / CIRE_EXPORT_FAIL <asset> <why>, then CIRE_EXPORT_DONE and quits the editor.
The full editor is required: the FBX skeletal-mesh exporter reads CPU-skinned vertices, which a commandlet (no render
scene) cannot provide ("Assertion failed: MeshObject").
"""
import os
import re
import sys

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def arg(name, default=""):
    m = re.search(r"-" + name + r"=(\"[^\"]*\"|\S+)", " ".join(sys.argv) + " " + unreal.SystemLibrary.get_command_line())
    return m.group(1).strip('"') if m else default


def main():
    out_dir = os.path.abspath(arg("CireExportDir", os.path.join(ROOT, "Saved", "Blender", "export")))
    for forbidden in ("Content", "Art", "Source", "Tools", "Docs"):
        if os.path.commonpath([out_dir, os.path.join(ROOT, forbidden)]) == os.path.join(ROOT, forbidden):
            unreal.log_error(f"CIRE_EXPORT_FAIL refuses committed output dir {out_dir}")
            return
    os.makedirs(out_dir, exist_ok=True)
    assets = [a for a in arg("CireExport").split(",") if a] + [a for a in arg("CireExportAnim").split(",") if a]
    for path in assets:
        obj = unreal.load_asset(path)
        if not obj:
            unreal.log_error(f"CIRE_EXPORT_FAIL {path} not found")
            continue
        name = obj.get_name()
        task = unreal.AssetExportTask()
        task.object = obj
        task.filename = os.path.join(out_dir, name + ".fbx")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        opts = unreal.FbxExportOption()
        opts.ascii = False
        opts.collision = False
        opts.level_of_detail = False
        opts.export_morph_targets = False
        opts.export_preview_mesh = isinstance(obj, unreal.AnimSequence)
        opts.map_skeletal_motion_to_root = False
        # Material baking needs a CPU-skinned render mesh (asserts in a commandlet): geometry, weights and UVs only.
        if hasattr(unreal, "FbxMaterialBakeMode"):
            opts.bake_material_inputs = unreal.FbxMaterialBakeMode.DISABLED
        task.options = opts
        ok = unreal.Exporter.run_asset_export_task(task)
        if ok and os.path.exists(task.filename):
            unreal.log(f"CIRE_EXPORT_OK {path} {task.filename}")
        else:
            unreal.log_error(f"CIRE_EXPORT_FAIL {path} exporter returned {ok}")


try:
    main()
finally:
    unreal.log("CIRE_EXPORT_DONE")
    if "-cireexportstay" not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()
