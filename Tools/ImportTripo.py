"""Import a locally downloaded Tripo rigged FBX/ZIP through an isolated UE project.

Usage: python Tools/ImportTripo.py "C:/path/to/export.zip"
No account access or remote download is performed. Raw inputs are retained in
Art/Imports/IronWarden; generated Unreal assets go to Content/Art/Characters.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import zipfile


SCRIPT = Path(__file__).resolve()
PROJECT = SCRIPT.parent.parent
STAGING = SCRIPT.parent / "ContentBuilder"
ASSET_FOLDER = "/Game/Art/Characters/IronWarden"
MESH_NAME = "SK_IronWarden"
REPORT = STAGING / "Saved" / "TripoImportReport.json"
CONFIG = STAGING / "Saved" / "TripoImportRequest.json"
TEXTURE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr"}


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def stage_local_input(source, selector=None):
    if not source.is_file() or source.suffix.lower() not in {".zip", ".fbx"}:
        raise ValueError("Provide an existing local .zip or rigged .fbx export.")
    source_hash = digest(source)
    destination = PROJECT / "Art" / "Imports" / "IronWarden" / source_hash[:12]
    destination.mkdir(parents=True, exist_ok=True)
    if source.suffix.lower() == ".zip":
        shutil.copy2(source, destination / "source.zip")
        with zipfile.ZipFile(source) as archive:
            members = archive.infolist()
            if len(members) > 10000 or sum(item.file_size for item in members) > 4 * 1024**3:
                raise ValueError("Export archive is unexpectedly large; inspect it before importing.")
            validated = []
            for item in members:
                relative = PurePosixPath(item.filename.replace("\\", "/"))
                if relative.is_absolute() or ".." in relative.parts or any(":" in part for part in relative.parts):
                    raise ValueError(f"Archive has an unsafe member path: {item.filename}")
                if stat.S_ISLNK(item.external_attr >> 16):
                    raise ValueError(f"Archive has a symbolic link: {item.filename}")
                target = destination.joinpath(*relative.parts).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError(f"Archive member escapes import folder: {item.filename}")
                if not item.is_dir():
                    validated.append((item, target))
            for item, target in validated:
                # Export assets and provenance only; executable payloads are never extracted.
                if target.suffix.lower() not in TEXTURE_EXTENSIONS | {".fbx", ".obj", ".mtl", ".json", ".txt", ".glb", ".gltf", ".bin"}:
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(item) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
    else:
        shutil.copy2(source, destination / source.name)
        for texture_directory in {source.parent / "textures", source.parent / "Textures", source.with_suffix(".fbm")}:
            if texture_directory.is_dir():
                shutil.copytree(texture_directory, destination / texture_directory.name, dirs_exist_ok=True)
    candidates = sorted(destination.rglob("*.fbx")) + sorted(destination.rglob("*.FBX"))
    if selector:
        selected = (destination / selector).resolve()
        if not selected.is_relative_to(destination.resolve()) or selected not in [p.resolve() for p in candidates]:
            raise ValueError("--fbx must identify an extracted FBX beneath this export folder.")
    else:
        if not candidates:
            raise ValueError("The export contains no FBX. Export a rigged FBX from Tripo first.")
        # The geometry FBX is normally larger than any optional animation-only FBXs.
        selected = max(candidates, key=lambda path: path.stat().st_size)
    return selected, {"source": str(source), "source_sha256": source_hash,
                      "staged_folder": str(destination), "fbx_candidates": [str(p) for p in candidates]}


def import_in_editor(unreal):
    request = json.loads(CONFIG.read_text(encoding="utf-8"))
    library = unreal.EditorAssetLibrary
    library.make_directory(ASSET_FOLDER)

    def import_mesh(scale):
        options = unreal.FbxImportUI()
        for name, value in {"import_as_skeletal": True, "import_mesh": True,
                            "mesh_type_to_import": unreal.FBXImportType.FBXIT_SKELETAL_MESH,
                            "automated_import_should_detect_type": False, "import_animations": False,
                            "import_materials": True, "import_textures": True, "create_physics_asset": True,
                            "override_full_name": True}.items():
            options.set_editor_property(name, value)
        data = options.get_editor_property("skeletal_mesh_import_data")
        data.set_editor_property("import_uniform_scale", scale)
        data.set_editor_property("convert_scene", True)
        data.set_editor_property("convert_scene_unit", True)
        task = unreal.AssetImportTask()
        for name, value in {"filename": request["fbx"], "destination_path": ASSET_FOLDER,
                            "destination_name": MESH_NAME, "automated": True, "save": True,
                            "replace_existing": True, "replace_existing_settings": True,
                            "options": options, "factory": unreal.FbxFactory()}.items():
            task.set_editor_property(name, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        meshes = [obj for obj in task.get_objects() if isinstance(obj, unreal.SkeletalMesh)]
        if len(meshes) != 1:
            raise RuntimeError(f"Expected one rigged skeletal mesh, imported {len(meshes)}. Inspect the export and log.")
        mesh = meshes[0]
        if mesh.get_name() != MESH_NAME:
            if not library.rename_asset(mesh.get_path_name(), f"{ASSET_FOLDER}/{MESH_NAME}"):
                raise RuntimeError("Could not rename the imported skeletal mesh")
        return mesh

    def bounds_info(mesh):
        bounds = mesh.get_imported_bounds()
        origin, extent = bounds.origin, bounds.box_extent
        return {"origin_cm": [origin.x, origin.y, origin.z],
                "size_cm": [extent.x * 2, extent.y * 2, extent.z * 2],
                "bottom_cm": origin.z - extent.z, "top_cm": origin.z + extent.z}

    def inspect_rig(mesh):
        component = unreal.SkeletalMeshComponent()
        component.set_skeletal_mesh_asset(mesh)
        names = [str(component.get_bone_name(index)) for index in range(component.get_num_bones())]
        parents = {name: str(component.get_parent_bone(name)) for name in names}
        skeleton = mesh.get_editor_property("skeleton")
        return {"skeleton": skeleton.get_path_name() if skeleton else None,
                "bones": names, "parents": parents, "bone_count": len(names)}

    mesh = import_mesh(1.0)
    initial_bounds = bounds_info(mesh)
    height = initial_bounds["size_cm"][2]
    if height <= 0.01:
        raise RuntimeError("Imported mesh has no usable vertical bounds")
    fit_scale = request["height_cm"] / height
    if abs(fit_scale - 1.0) > 0.015:
        mesh = import_mesh(fit_scale)
    rig = inspect_rig(mesh)
    if rig["bone_count"] < 5:
        raise RuntimeError("The mesh does not contain a usable humanoid rig; auto-rig it in Tripo before export")
    final_bounds = bounds_info(mesh)
    if abs(final_bounds["size_cm"][2] - request["height_cm"]) > 5.0:
        raise RuntimeError(f"Scale normalization did not converge: {final_bounds}")
    comparison = {"retarget_required": True, "reason": "Imported with its own skeleton; bind pose and animation compatibility require verification."}
    manny = library.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    if manny:
        reference = inspect_rig(manny)
        names, ref_names = set(rig["bones"]), set(reference["bones"])
        comparison.update({"mannequin_skeleton": reference["skeleton"], "mannequin_bone_count": reference["bone_count"],
                           "common_bones": sorted(names & ref_names), "missing_mannequin_bones": sorted(ref_names - names),
                           "extra_tripo_bones": sorted(names - ref_names),
                           "parent_differences": {name: {"tripo": rig["parents"][name], "mannequin": reference["parents"][name]}
                                                  for name in sorted(names & ref_names) if rig["parents"][name] != reference["parents"][name]}})
    else:
        comparison["mannequin_comparison"] = "Reference mesh unavailable in staging"
    if not library.save_directory(ASSET_FOLDER, only_if_is_dirty=False, recursive=True):
        raise RuntimeError("Saving the imported asset directory failed")
    report = {"status": "imported", "mesh": mesh.get_path_name(), "fbx": request["fbx"],
              "provenance": request["provenance"], "initial_bounds": initial_bounds,
              "import_scale": fit_scale, "bounds": final_bounds, "rig": rig, "comparison": comparison,
              "materials": [str(item.material_interface.get_path_name()) if item.material_interface else None
                            for item in mesh.get_editor_property("materials")]}
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_TRIPO_IMPORT_SUCCESS " + mesh.get_path_name())


def run_local():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--fbx", help="Relative FBX path inside the export, when it contains multiple FBXs")
    parser.add_argument("--engine", type=Path, default=Path("F:/UE_5.8"))
    parser.add_argument("--height-cm", type=float, default=185.0)
    options = parser.parse_args()
    if not 100 <= options.height_cm <= 300:
        parser.error("--height-cm must be between 100 and 300 for this humanoid importer")
    source = options.source.expanduser().resolve()
    fbx, provenance = stage_local_input(source, options.fbx)
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps({"fbx": str(fbx), "height_cm": options.height_cm, "provenance": provenance}, indent=2), encoding="utf-8")
    mannequin_source = PROJECT / "Content" / "Characters"
    if mannequin_source.is_dir():
        shutil.copytree(mannequin_source, STAGING / "Content" / "Characters", dirs_exist_ok=True)
    logs = STAGING / "Saved" / "Logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / "TripoImport-commandlet.log"
    command = [str(options.engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(STAGING / "ContentBuilder.uproject"),
               "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript", f"-script={SCRIPT}",
               "-ExecCmds=Interchange.FeatureFlags.Import.FBX 0", f"-abslog={log}", "-stdout", "-FullStdOutLogOutput"]
    print(f"Importing {fbx}. Unreal log: {log}", flush=True)
    process_options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with (logs / "TripoImport-console.log").open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, **process_options)
    logged = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    if result.returncode or "CIRE_TRIPO_IMPORT_SUCCESS" not in logged:
        raise RuntimeError(f"Tripo import failed (exit {result.returncode}). See {log}. No generated assets copied to the game.")
    imported = STAGING / "Content/Art/Characters/IronWarden"
    target = PROJECT / "Content/Art/Characters/IronWarden"
    if not (imported / f"{MESH_NAME}.uasset").is_file():
        raise FileNotFoundError(imported / f"{MESH_NAME}.uasset")
    shutil.copytree(imported, target, dirs_exist_ok=True)
    shutil.copy2(REPORT, Path(provenance["staged_folder"]) / "UnrealImportReport.json")
    print(REPORT.read_text(encoding="utf-8"), flush=True)


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        run_local()
    else:
        import_in_editor(unreal)
