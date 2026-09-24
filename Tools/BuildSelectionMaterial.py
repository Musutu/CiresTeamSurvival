"""Build the original local selection-edge material in the isolated UE content project."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Tools/ContentBuilder"

def build(unreal):
    editor = unreal.MaterialEditingLibrary
    library = unreal.EditorAssetLibrary
    asset_path = "/Game/Art/Materials/M_SelectionEdge"
    material = library.load_asset(asset_path) if library.does_asset_exist(asset_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_SelectionEdge", "/Game/Art/Materials", unreal.Material, unreal.MaterialFactoryNew())
    editor.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", False)
    editor.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH, True)
    color = editor.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -420, -120)
    color.set_editor_property("parameter_name", "SelectionTint")
    color.set_editor_property("default_value", unreal.LinearColor(.05, 1.8, 1.35, 1))
    edge = editor.create_material_expression(material, unreal.MaterialExpressionFresnel, -420, 80)
    edge.set_editor_property("exponent", 4.0)
    edge.set_editor_property("base_reflect_fraction", 0.0)
    strength = editor.create_material_expression(material, unreal.MaterialExpressionMultiply, -170, 80)
    strength.set_editor_property("const_b", .42)
    editor.connect_material_expressions(edge, "", strength, "A")
    editor.connect_material_property(strength, "", unreal.MaterialProperty.MP_OPACITY)
    editor.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editor.layout_material_expressions(material)
    editor.recompile_material(material)
    if not library.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Selection material save failed")
    unreal.log("CIRE_SELECTION_MATERIAL_PASS")

if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        log = STAGING / "Saved/Logs/SelectionMaterial.log"
        console = STAGING / "Saved/Logs/SelectionMaterial-console.log"
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"), "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript", f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
        with console.open("w", encoding="utf-8") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if result.returncode or "CIRE_SELECTION_MATERIAL_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
            raise RuntimeError(f"Selection material generation failed: {log}")
        destination = ROOT / "Content/Art/Materials/M_SelectionEdge.uasset"
        shutil.copy2(STAGING / "Content/Art/Materials/M_SelectionEdge.uasset", destination)
        print(f"CIRE_SELECTION_MATERIAL_COPIED {destination}")
    else:
        build(unreal)
