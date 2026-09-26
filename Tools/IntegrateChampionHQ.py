"""UE 5.8 (editor, champion-hq): integrate the HQ Tripo champion / summon bodies after the DCC Bridge transfer.

Source list: Art/ChampionHQ/TripoChampionHQ.json "exports" ({id: {export, folder, kind, look}}).
For each export whose Bridge folder /Game/TripoModels/<export> exists (or whose destination already exists):
  * moves the import to /Game/Tripo/ChampionsHQ/<folder>/ (Tools/RunChampionHQIntegration.py adds the skeleton
    core redirects the Bridge forgets, exactly like RunTripoRacesIntegration.py);
  * textures: character LOD group, mips on, in-game cap 4096 (the Bridge sends 4K sources), sRGB only on colour;
  * builds <export>_HQ, a MaterialInstanceConstant of /Game/Art/Materials/M_CireHero_PBR with the four PBR
    textures, the optional <export>_mask texture (R skin, G emissive; Tools/BuildChampionHQMasks.py) and the
    champion's "look" parameters (vibrance, emissive colour/intensity, rim), assigned to every slot;
  * skeletal bodies get 4 LODs (100 / 50 / 25 / 12 % triangles) through the skeletal mesh reduction;
  * renames the Tripo text-to-motion / AI-library clips to stable names (aiClips map);
  * writes raw bounds + clip paths to Saved/ChampionHQIntegration.json.
Scale is never baked; heights live in the binding data. Marker: CIRE_CHAMPION_HQ_INTEGRATION PASS / FAIL.
"""
from pathlib import Path
import json
import os
import traceback
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = PROJECT / "Art" / "ChampionHQ" / "TripoChampionHQ.json"
REPORT = PROJECT / "Saved" / "ChampionHQIntegration.json"
MASTER = "/Game/Art/Materials/M_CireHero_PBR"
DEST = "/Game/Tripo/ChampionsHQ"
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
ONLY = [s for s in os.environ.get("CIRE_CHAMPION_HQ_ONLY", "").split(",") if s]
TEXTURE_PARAMS = {"_basecolor": "BaseColorTex", "_normal": "NormalTex", "_roughness": "RoughnessTex", "_metallic": "MetallicTex",
                  "_mask": "MaskTex"}
AI_CLIPS = {"Archery_Aim": "attack_bow", "Standing_archer": "attack_bow", "Standing_marksman": "attack_crossbow",
            "Crossbow_Ai": "attack_crossbow", "Huge_warlord": "war_cry", "Massive_brute": "ground_slam",
            "Overhand_Ax": "axe_throw", "Two-Handed": "heavy_slam", "Two_Handed": "heavy_slam"}

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


def tune_textures(folder):
    found = {}
    for path in assets_in(folder + "/Textures"):
        tex = unreal.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            continue
        low = path.lower()
        # The Bridge names colour / normal "<export>_Tex" / "_Tex_1" on some exports: classify by the imported settings.
        if not any(low.endswith(s) for s in TEXTURE_PARAMS):
            if tex.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_NORMALMAP:
                low += "_normal"
            elif tex.get_editor_property("srgb"):
                low += "_basecolor"
        colour = low.endswith("_basecolor")
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        tex.set_editor_property("max_texture_size", 4096)
        if low.endswith("_normal"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif colour:
            tex.set_editor_property("srgb", True)
        else:
            tex.set_editor_property("srgb", False)
            # linear masks stay TC_Default: M_CireHero_PBR samples them as Linear Color
        EAL.save_loaded_asset(tex, only_if_is_dirty=False)
        for suffix, param in TEXTURE_PARAMS.items():
            if low.endswith(suffix):
                found[param] = tex
    return found


def build_material(folder, name, look):
    textures = tune_textures(folder)
    missing = [p for p in ("BaseColorTex", "NormalTex", "RoughnessTex", "MetallicTex") if p not in textures]
    if missing:
        raise RuntimeError("%s missing textures %s" % (folder, missing))
    path = folder + "/" + name + "_HQ"
    mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        mi = AT.create_asset(name + "_HQ", folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, unreal.load_asset(MASTER))
    for param, tex in textures.items():
        MEL.set_material_instance_texture_parameter_value(mi, param, tex)
    for key, value in (look or {}).items():
        if isinstance(value, (int, float)):
            MEL.set_material_instance_scalar_parameter_value(mi, key, float(value))
        elif isinstance(value, list) and len(value) in (3, 4):
            MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(*(list(value) + [1.0])[:4]))
    MEL.update_material_instance(mi)
    for param, tex in textures.items():
        if MEL.get_material_instance_texture_parameter_value(mi, param) != tex:
            raise RuntimeError("material parameter verification failed %s %s" % (path, param))
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def build_lods(mesh):
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    ok = False
    try:
        ok = sub.regenerate_lod(mesh, 4, False, False)
    except Exception as exc:
        report["errors"].append("lod %s: %s" % (mesh.get_path_name(), exc))
    count = None
    try:
        count = int(sub.get_lod_count(mesh))
    except Exception:
        pass
    return {"requested": 4, "ok": bool(ok), "lods": count}


def integrate(key, entry):
    export, folder = entry["export"], entry["folder"]
    src = "/Game/TripoModels/" + export
    dst = DEST + "/" + folder
    if EAL.does_directory_exist(src) and assets_in(src):
        if EAL.does_directory_exist(dst) and assets_in(dst):
            raise RuntimeError("destination %s already holds assets; bump the export name for a new candidate" % dst)
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
        raise RuntimeError("no mesh in " + dst)
    mi = build_material(dst, body.get_name(), entry.get("look"))
    lods = None
    if mesh:
        mats = list(mesh.get_editor_property("materials"))
        for m in mats:
            m.set_editor_property("material_interface", mi)
        mesh.modify()
        mesh.set_editor_property("materials", mats)
        if entry.get("lods", True):
            lods = build_lods(mesh)
    else:
        for i in range(len(static.get_editor_property("static_materials"))):
            static.set_material(i, mi)
    anims = {}
    for p in assets_in(dst + "/Animations"):
        a = unreal.load_asset(p)
        if not isinstance(a, unreal.AnimSequence):
            continue
        if skeleton is not None and a.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("animation %s does not use %s" % (p, skeleton.get_path_name()))
        short = a.get_name()[len(export) + 1:] if a.get_name().startswith(export + "_") else a.get_name()
        if short.endswith("_001") and short[:-4] in ("idle", "walk", "run", "slash", "cast_a_spell", "fall", "hit_to_body_01"):
            if AT.rename_assets([unreal.AssetRenameData(a, dst + "/Animations", "%s_%s" % (export, short[:-4]))]):
                short = short[:-4]
        for prefix, clean in AI_CLIPS.items():
            if short.startswith(prefix):
                if AT.rename_assets([unreal.AssetRenameData(a, dst + "/Animations", "%s_%s" % (export, clean))]):
                    short = clean
                break
        anims[short] = a.get_path_name()
    bounds = body.get_bounds()
    for p in assets_in(dst):
        EAL.save_asset(p, only_if_is_dirty=False)
    report["units"][key] = {
        "export": export, "mesh": body.get_path_name(), "skeletal": mesh is not None,
        "skeleton": skeleton.get_path_name() if skeleton else None, "material": mi.get_path_name(),
        "rawHeightCm": round(bounds.box_extent.z * 2, 2), "rawBottomCm": round(bounds.origin.z - bounds.box_extent.z, 2),
        "boundsExtentCm": [round(bounds.box_extent.x, 1), round(bounds.box_extent.y, 1), round(bounds.box_extent.z, 1)],
        "triangles": None, "lods": lods, "animations": anims}
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
    exports = json.loads(SOURCE.read_text(encoding="utf-8")).get("exports", {})
    done = 0
    for key, entry in exports.items():
        if ONLY and key not in ONLY:
            continue
        try:
            done += bool(integrate(key, entry))
        except Exception as exc:
            report["errors"].append("%s: %s\n%s" % (key, exc, traceback.format_exc()))
    report["redirectorsFixed"] = fix_redirectors("/Game/TripoModels") + fix_redirectors(DEST)
    for entry in exports.values():
        folder = "/Game/TripoModels/" + entry["export"]
        if EAL.does_directory_exist(folder) and not assets_in(folder):
            EAL.delete_directory(folder)
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_CHAMPION_HQ_INTEGRATION %s units=%d errors=%d" % ("PASS" if not report["errors"] else "FAIL", done, len(report["errors"])))


try:
    run()
except Exception as exc:
    report["errors"].append("fatal: %s\n%s" % (exc, traceback.format_exc()))
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log_error("CIRE_CHAMPION_HQ_INTEGRATION FAIL fatal")
if os.environ.get("CIRE_CHAMPION_HQ_QUIT", "1") == "1":
    unreal.SystemLibrary.quit_editor()
