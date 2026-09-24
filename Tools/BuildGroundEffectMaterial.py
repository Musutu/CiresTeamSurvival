"""Build the ground-area vertex-color material in the isolated content project.

Run with ordinary Python after the project's UE processes have stopped. The
material supplies a translucent unlit fill and a brighter opaque-looking rim;
procedural meshes provide both colors and alpha without collision or decals.
"""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Tools/ContentBuilder"


def build(unreal):
    editor = unreal.MaterialEditingLibrary
    library = unreal.EditorAssetLibrary
    asset_path = "/Game/Art/Materials/M_GroundArea"
    material = library.load_asset(asset_path) if library.does_asset_exist(asset_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_GroundArea", "/Game/Art/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create ground effect material")
    editor.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # Keep normal depth testing: areas cannot be seen through walls or floors.
    material.set_editor_property("disable_depth_test", False)
    vertex = editor.create_material_expression(material, unreal.MaterialExpressionVertexColor, -400, 0)
    # UE 5.8 names the composite RGB output ""; only component outputs R/G/B/A
    # have names recognized by MaterialEditingLibrary.
    if not editor.connect_material_property(vertex, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Could not connect area RGB")
    if not editor.connect_material_property(vertex, "A", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("Could not connect area opacity")
    editor.layout_material_expressions(material)
    editor.recompile_material(material)
    if not library.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Ground material save failed")
    unreal.log("CIRE_GROUND_MATERIAL_PASS")


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        log = STAGING / "Saved/Logs/GroundMaterial.log"
        console = STAGING / "Saved/Logs/GroundMaterial-console.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"),
                   "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript",
                   f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
        with console.open("w", encoding="utf-8") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if result.returncode or "CIRE_GROUND_MATERIAL_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
            raise RuntimeError(f"Ground material generation failed: {log}")
        destination = ROOT / "Content/Art/Materials/M_GroundArea.uasset"
        shutil.copy2(STAGING / "Content/Art/Materials/M_GroundArea.uasset", destination)
        print(f"CIRE_GROUND_MATERIAL_COPIED {destination}")
    else:
        build(unreal)
