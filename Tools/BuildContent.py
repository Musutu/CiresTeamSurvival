"""Generate prototype UE assets without requiring the game's native module.

Run with ordinary Python: python Tools/BuildContent.py
The hidden UE commandlet invokes this same file inside the staging project.
Only this new project's content is written; the user's original project is untouched.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


SCRIPT = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT.parent.parent
STAGING = SCRIPT.parent / "ContentBuilder"
MATERIAL_PATH = "/Game/Art/Materials"
MATERIALS = {
    "M_Stone": {"color": (0.075, 0.095, 0.12), "roughness": 0.88, "metallic": 0.0},
    "M_Metal": {"color": (0.115, 0.077, 0.046), "roughness": 0.63, "metallic": 0.78},
    "M_Ember": {"color": (0.018, 0.32, 0.28), "roughness": 0.38, "metallic": 0.1, "emission": (0.055, 2.8, 2.1)},
    "M_Dusk": {"color": (0.26, 0.048, 0.018), "roughness": 0.5, "metallic": 0.1, "emission": (2.2, 0.16, 0.04)},
    "M_Gold": {"color": (0.52, 0.30, 0.08), "roughness": 0.3, "metallic": 0.82},
    "M_Slate": {"color": (0.035, 0.052, 0.07), "roughness": 0.92, "metallic": 0.0},
}


def generate_in_editor(unreal):
    assets = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    material_editor = unreal.MaterialEditingLibrary
    library.make_directory(MATERIAL_PATH)
    library.make_directory("/Game/Maps")
    generated = []

    for name, spec in MATERIALS.items():
        asset_path = f"{MATERIAL_PATH}/{name}"
        material = library.load_asset(asset_path) if library.does_asset_exist(asset_path) else None
        if material is None:
            material = assets.create_asset(name, MATERIAL_PATH, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError(f"Could not create {asset_path}")
        material_editor.delete_all_material_expressions(material)
        material.set_editor_property("two_sided", False)
        # Runtime arena geometry is built from InstancedStaticMeshComponents.
        material_editor.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
        if not material_editor.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
            raise RuntimeError(f"Instanced static mesh usage was not enabled for {name}")

        def scalar(value, prop, x, y):
            node = material_editor.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
            node.set_editor_property("r", float(value))
            if not material_editor.connect_material_property(node, "", prop):
                raise RuntimeError(f"Failed to connect {prop} for {name}")

        def color(value, prop, x, y):
            node = material_editor.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x, y)
            node.set_editor_property("constant", unreal.LinearColor(*value, 1.0))
            if not material_editor.connect_material_property(node, "", prop):
                raise RuntimeError(f"Failed to connect {prop} for {name}")

        color(spec["color"], unreal.MaterialProperty.MP_BASE_COLOR, -360, -140)
        scalar(spec["roughness"], unreal.MaterialProperty.MP_ROUGHNESS, -360, 50)
        scalar(spec["metallic"], unreal.MaterialProperty.MP_METALLIC, -360, 180)
        if "emission" in spec:
            color(spec["emission"], unreal.MaterialProperty.MP_EMISSIVE_COLOR, -360, 310)
        material_editor.layout_material_expressions(material)
        material_editor.recompile_material(material)
        if not library.save_loaded_asset(material, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {asset_path}")
        generated.append(asset_path)

    map_path = "/Game/Maps/Citadel"
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if library.does_asset_exist(map_path):
        if not levels.load_level(map_path):
            raise RuntimeError("Could not load existing staging Citadel map")
    elif not levels.new_level(map_path):
        raise RuntimeError("Could not create staging Citadel map")
    if not levels.save_current_level():
        raise RuntimeError("Could not save staging Citadel map")
    generated.append(map_path)
    manifest = {"status": "generated", "assets": generated, "engine": unreal.SystemLibrary.get_engine_version()}
    (STAGING / "Saved").mkdir(parents=True, exist_ok=True)
    (STAGING / "Saved" / "ContentBuild.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    unreal.log("CIRE_CONTENT_BUILD_SUCCESS: " + ", ".join(generated))


def run_builder():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=Path("F:/UE_5.8"))
    parser.add_argument("--skip-editor", action="store_true", help="Copy previously generated verified staging assets only")
    options = parser.parse_args()
    log_dir = STAGING / "Saved" / "Logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "ContentBuild-commandlet.log"
    console_path = log_dir / "ContentBuild-console.log"

    if not options.skip_editor:
        executable = options.engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
        if not executable.is_file():
            raise FileNotFoundError(executable)
        command = [str(executable), str(STAGING / "ContentBuilder.uproject"), "-unattended", "-nullrhi",
                   "-nosplash", "-nosound", "-nop4", "-run=pythonscript", f"-script={SCRIPT}",
                   "-stdout", "-FullStdOutLogOutput", f"-abslog={log_path}"]
        process_options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        print(f"Generating UE content. Log: {log_path}", flush=True)
        with console_path.open("w", encoding="utf-8") as console:
            result = subprocess.run(command, stdout=console, stderr=subprocess.STDOUT, **process_options)
        log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
        if result.returncode or "CIRE_CONTENT_BUILD_SUCCESS" not in log:
            raise RuntimeError(f"Content commandlet failed (exit {result.returncode}). Inspect {log_path} and {console_path}")

    staged_content = STAGING / "Content"
    required = [Path("Maps/Citadel.umap")] + [Path(f"Art/Materials/{name}.uasset") for name in MATERIALS]
    for asset in required:
        if not (staged_content / asset).is_file():
            raise FileNotFoundError(staged_content / asset)
    content = PROJECT_ROOT / "Content"
    for asset_folder in ("Art", "Maps"):
        shutil.copytree(staged_content / asset_folder, content / asset_folder, dirs_exist_ok=True)

    mannequin_source = options.engine / "Templates/TemplateResources/High/Characters/Content"
    if not mannequin_source.is_dir():
        raise FileNotFoundError(mannequin_source)
    shutil.copytree(mannequin_source, content / "Characters", dirs_exist_ok=True)
    mannequin_files = ["Mannequins/Meshes/SKM_Manny_Simple.uasset", "Mannequins/Anims/Unarmed/ABP_Unarmed.uasset"]
    for asset in mannequin_files:
        if not (content / "Characters" / asset).is_file():
            raise FileNotFoundError(content / "Characters" / asset)
    report = {"status": "copied", "content": str(content), "map": "/Game/Maps/Citadel",
              "materials": list(MATERIALS), "mannequin_source": str(mannequin_source),
              "mannequin_files": sum(1 for p in (content / "Characters").rglob("*") if p.is_file())}
    (STAGING / "Saved" / "ContentCopied.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        run_builder()
    else:
        generate_in_editor(unreal)
