"""Integrate Tripo race-unit bodies (Art/TripoRaces.json) after the DCC Bridge transfer.

Run inside the editor (Tools/RunTripoRacesIntegration.py does this with the skeleton core redirects):
    UnrealEditor.exe <project> -ExecCmds="py Tools/IntegrateTripoRaces.py" -unattended
For every unit in Art/TripoRaces.json with an "export" name whose Bridge folder /Game/TripoModels/<export> exists, it
moves the import to /Game/Tripo/Races/<race>/<Folder>/, rebuilds the PBR material instance on the Tripo master (the
Bridge omits BaseColor; the four texture parameters are what M_CireMonsterSkin copies for the rank/race tint), assigns
it to every slot, renames text-to-motion clips (unit "aiClips": {promptPrefix: clipName}) and writes raw bounds to
Saved/TripoRacesIntegration.json. Scale is never baked (meshScale lives in Content/Data/RaceMeshes.tripo.json).
"""
from pathlib import Path
import json
import os
import traceback
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = PROJECT / "Art" / "TripoRaces.json"
REPORT = PROJECT / "Saved" / "TripoRacesIntegration.json"
MASTER = "/Game/TripoModels/Materials/M_Tripo_PBR_Master"
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
# CIRE_TRIPO_PENDING_ONLY=1: only integrate units whose Bridge import is still pending under /Game/TripoModels.
PENDING_ONLY = os.environ.get("CIRE_TRIPO_PENDING_ONLY") == "1"
TEXTURE_PARAMS = {"_basecolor": "BaseColorTex", "_normal": "NormalTex",
                  "_roughness": "RoughnessTex", "_metallic": "MetallicTex"}

report = {"units": {}, "moved": [], "errors": []}
if REPORT.exists():
    report["units"].update(json.loads(REPORT.read_text(encoding="utf-8")).get("units", {}))


def assets_in(folder, recursive=True):
    return [str(p).split(".")[0] for p in EAL.list_assets(folder, recursive=recursive, include_folder=False)]


def move_folder(src, dst):
    for path in assets_in(src):
        EAL.load_asset(path)
    data = []
    for path in assets_in(src):
        rel = path[len(src):]
        data.append(unreal.AssetRenameData(EAL.load_asset(path), (dst + rel).rsplit("/", 1)[0], path.rsplit("/", 1)[1]))
    if not AT.rename_assets(data):
        raise RuntimeError("rename_assets failed for " + src)
    report["moved"].append({"from": src, "to": dst, "assets": len(data)})
    return dst


def build_material(folder, name):
    master = unreal.load_asset(MASTER)
    textures = {}
    for path in assets_in(folder + "/Textures"):
        for suffix, param in TEXTURE_PARAMS.items():
            if path.lower().endswith(suffix):
                textures[param] = unreal.load_asset(path)
    missing = [p for p in TEXTURE_PARAMS.values() if p not in textures]
    if missing:
        raise RuntimeError("%s missing textures %s" % (folder, missing))
    mi_path = folder + "/" + name + "_Mat"
    mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        mi = AT.create_asset(name + "_Mat", folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
    for param, tex in textures.items():
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, param, tex)  # returns False on 5.8 even on success
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    for param, tex in textures.items():
        if unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, param) != tex:
            raise RuntimeError("Material parameter verification failed %s %s" % (mi_path, param))
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def integrate(unit):
    export, race, name = unit["export"], unit.get("race", ""), unit["folder"]
    src = "/Game/TripoModels/" + export
    dst = unit.get("dest") or "/Game/Tripo/Races/%s/%s" % (race, name)
    if PENDING_ONLY and not (EAL.does_directory_exist(src) and assets_in(src)):
        return False  # already integrated (committed, read-only) bodies are left untouched
    if EAL.does_directory_exist(src) and assets_in(src):
        move_folder(src, dst)
    elif not EAL.does_directory_exist(dst):
        return False
    mesh = skeleton = static = None
    for p in assets_in(dst, False):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
        elif isinstance(a, unreal.Skeleton):
            skeleton = a
        elif isinstance(a, unreal.StaticMesh):
            static = a
    body = mesh or static
    if body is None:
        raise RuntimeError("No mesh in " + dst)
    mi = build_material(dst, body.get_name())
    if mesh:
        mats = list(mesh.get_editor_property("materials"))
        for m in mats:
            m.set_editor_property("material_interface", mi)
        mesh.modify()
        mesh.set_editor_property("materials", mats)
    else:
        for i in range(len(static.get_editor_property("static_materials"))):
            static.set_material(i, mi)
        sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        nanite = static.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", True)  # weapon props: M_Tripo_PBR_Master carries the Nanite usage flag
        sms.set_nanite_settings(static, nanite, apply_changes=True)
    anims = {}
    for p in assets_in(dst + "/Animations"):
        a = unreal.load_asset(p)
        if not isinstance(a, unreal.AnimSequence):
            continue
        if skeleton is not None and a.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Animation %s does not use %s" % (p, skeleton.get_path_name()))
        short = a.get_name()[len(export) + 1:] if a.get_name().startswith(export + "_") else a.get_name()
        for prefix, clean in unit.get("aiClips", {}).items():
            if short.startswith(prefix):
                if AT.rename_assets([unreal.AssetRenameData(a, dst + "/Animations", "%s_%s" % (export, clean))]):
                    short = clean
                break
        anims[short] = a.get_path_name()
    bounds = body.get_bounds()
    for p in assets_in(dst):
        EAL.save_asset(p, only_if_is_dirty=False)
    report["units"][unit["unit"]] = {
        "race": race, "export": export, "mesh": body.get_path_name(), "skeletal": mesh is not None,
        "skeleton": skeleton.get_path_name() if skeleton else None, "material": mi.get_path_name(),
        "rawHeightCm": round(bounds.box_extent.z * 2, 2), "rawBottomCm": round(bounds.origin.z - bounds.box_extent.z, 2),
        "boundsExtentCm": [round(bounds.box_extent.x, 1), round(bounds.box_extent.y, 1), round(bounds.box_extent.z, 1)],
        "animations": anims}
    return True


def fix_redirectors(root):
    found = []
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(root, recursive=True):
        if str(data.asset_class_path.asset_name) == "ObjectRedirector":
            found.append(unreal.load_object(None, str(data.package_name) + "." + str(data.asset_name)))
    found = [r for r in found if r]
    if found:
        AT.fixup_referencers(found)
    return len(found)


def run():
    units = [u for u in json.loads(SOURCE.read_text(encoding="utf-8"))["units"] if u.get("export")]
    champions = PROJECT / "Art" / "TripoChampions.json"  # new playable champions, mounts and weapon props
    if champions.exists():
        for item in json.loads(champions.read_text(encoding="utf-8"))["items"]:
            if item.get("export"):
                units.append(dict(item, unit=item["id"], race=item.get("race", "champions")))
    art3d = PROJECT / "Art" / "TripoArt3D.json"  # art-collector-3d batch: Aetheri race, realistic quadrupeds, props
    if art3d.exists():
        units += [u for u in json.loads(art3d.read_text(encoding="utf-8"))["units"] if u.get("export")]
    done = 0
    for unit in units:
        try:
            done += bool(integrate(unit))
        except Exception as exc:
            report["errors"].append("%s: %s\n%s" % (unit["unit"], exc, traceback.format_exc()))
    report["redirectorsFixed"] = fix_redirectors("/Game/TripoModels") + fix_redirectors("/Game/Tripo/Races")
    for unit in units:
        folder = "/Game/TripoModels/" + unit["export"]
        if EAL.does_directory_exist(folder) and not assets_in(folder):
            EAL.delete_directory(folder)
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_TRIPO_RACES_INTEGRATION %s units=%d errors=%d" % ("PASS" if not report["errors"] else "FAIL", done, len(report["errors"])))


try:
    run()
except Exception as exc:
    report["errors"].append("fatal: %s\n%s" % (exc, traceback.format_exc()))
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log_error("CIRE_TRIPO_RACES_INTEGRATION FAIL fatal")
unreal.SystemLibrary.quit_editor()
