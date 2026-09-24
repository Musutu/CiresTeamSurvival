"""Integrate Tripo batch 03 (monsters, bosses, town kit, landmarks) after the Bridge transfer.

Run inside the editor:  UnrealEditor.exe <project> -ExecCmds="py Tools/IntegrateTripoBatch03.py" -unattended
It moves the Bridge imports from /Game/TripoModels/<ExportName> to /Game/Tripo/{Monsters,Town,Landmarks}/<Name>,
rebuilds the PBR material instances (the Bridge omits BaseColor and some instances were never saved), bakes a
per-asset suggested metre-correct scale for the town/landmark static meshes, enables Nanite, sets collision, and writes
Saved/TripoBatch03Integration.json. It never touches champion/hero assets or maps and quits the editor when done.
"""
from pathlib import Path
import json
import traceback
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
REPORT = PROJECT / "Saved" / "TripoBatch03Integration.json"
MASTER = "/Game/TripoModels/Materials/M_Tripo_PBR_Master"
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
SMS = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

# Bridge export name -> (category, clean name, uniform build scale, collision)
# Raw Tripo meshes are normalised to ~98 cm on their longest axis with the pivot at bottom centre.
STATICS = {
    "CTS_Town_CastleKeep": ("Town", "CastleKeep", 24.0, "complex"),
    "CTS_Town_CastleGate": ("Town", "CastleGate", 22.0, "complex"),
    "CTS_Town_WallSection": ("Town", "WallSection", 10.5, "complex"),
    "CTS_Town_HouseA": ("Town", "HouseA", 11.0, "complex"),
    "CTS_Town_HouseB": ("Town", "HouseB", 13.0, "complex"),
    "CTS_Town_HouseC": ("Town", "HouseC", 9.5, "complex"),
    "CTS_Town_MarketStallA": ("Town", "MarketStallA", 3.4, "complex"),
    "CTS_Town_MarketStallB": ("Town", "MarketStallB", 3.6, "complex"),
    "CTS_Town_Fountain": ("Town", "Fountain", 7.0, "box"),
    "CTS_Town_Well": ("Town", "Well", 3.6, "box"),
    "CTS_Town_Cart": ("Town", "Cart", 4.2, "box"),
    "street_lamp_3d_model": ("Town", "Lamp", 3.8, "box"),
    "CTS_Town_Banner": ("Town", "Banner", 6.0, "box"),
    "CTS_Town_Barricade": ("Town", "Barricade", 6.0, "complex"),
    "CTS_Landmark_Gatehouse": ("Landmarks", "Gatehouse", 20.5, "complex"),
    "CTS_Landmark_Watchtower": ("Landmarks", "Watchtower", 15.0, "complex"),
    "CTS_Landmark_DawnwellShrine": ("Landmarks", "DawnwellShrine", 8.6, "complex"),
}
# Bridge export name -> (clean name, role, archetype ids)
SKELETAL = {
    "CTS_Monster_HollowInfantry": ("HollowInfantry", "bruiser", ["hollow_infantry"]),
    "CTS_Monster_HollowInfantryB": ("HollowInfantryB", "bruiser", ["hollow_infantry"]),
    "CTS_Monster_IronboundBruiser": ("IronboundBruiser", "bruiser", ["ironbound_bruiser"]),
    "CTS_Monster_HollowShieldbearer": ("HollowShieldbearer", "tank", ["hollow_shieldbearer"]),
    "CTS_Monster_HollowShieldbearerV2": ("HollowShieldbearerV2", "tank", ["hollow_shieldbearer"]),
    "CTS_Monster_IronboundBruiserV2": ("IronboundBruiserV2", "bruiser", ["ironbound_bruiser"]),
    "CTS_Monster_BlightCaster": ("BlightCaster", "caster", ["blight_caster"]),
    "CTS_Monster_BlightCasterB": ("BlightCasterB", "caster", ["blight_caster"]),
    "CTS_Monster_BarbedHunter": ("BarbedHunter", "ranged", ["barbed_hunter"]),
    "CTS_Monster_BarbedHunterB": ("BarbedHunterB", "ranged", ["barbed_hunter"]),
    "CTS_Boss_GravemawPackLeader": ("GravemawPackLeader", "pack_leader", ["gravemaw_pack_leader"]),
    "CTS_Boss_HollowSiegebreaker": ("HollowSiegebreaker", "tank", ["hollow_siegebreaker"]),
}
# Text-to-motion clips get readable names (matched on the start of the Bridge's prompt-derived name).
AI_CLIPS = {"Standing_archer": "attack_bow", "Standing_marksman": "attack_crossbow",
            "Huge_warlord": "war_cry", "Massive_brute": "ground_slam"}
TEXTURE_PARAMS = {"_basecolor": "BaseColorTex", "_normal": "NormalTex",
                  "_roughness": "RoughnessTex", "_metallic": "MetallicTex"}

report = {"statics": {}, "skeletal": {}, "moved": [], "errors": []}


def assets_in(folder, recursive=True):
    return [str(p).split(".")[0] for p in EAL.list_assets(folder, recursive=recursive, include_folder=False)]


def move_folder(src, dst):
    if not EAL.does_directory_exist(src):
        if EAL.does_directory_exist(dst):
            return dst
        raise RuntimeError("Missing Bridge import " + src)
    for path in assets_in(src):
        EAL.load_asset(path)
    data = []
    for path in assets_in(src):
        rel = path[len(src):]
        new_dir = (dst + rel).rsplit("/", 1)[0]
        name = path.rsplit("/", 1)[1]
        data.append(unreal.AssetRenameData(EAL.load_asset(path), new_dir, name))
    if not AT.rename_assets(data):
        raise RuntimeError("rename_assets failed for " + src)
    report["moved"].append({"from": src, "to": dst, "assets": len(data)})
    return dst


def fix_redirectors(root):
    redirectors = []
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(root, recursive=True):
        if str(data.asset_class_path.asset_name) == "ObjectRedirector":
            redirectors.append(unreal.load_object(None, str(data.package_name) + "." + str(data.asset_name)))
    redirectors = [r for r in redirectors if r]
    if redirectors:
        AT.fixup_referencers(redirectors)
    return len(redirectors)


def build_material(folder, name):
    master = unreal.load_asset(MASTER)
    params = [str(p) for p in unreal.MaterialEditingLibrary.get_texture_parameter_names(master)]
    textures = {}
    for path in assets_in(folder + "/Textures"):
        low = path.lower()
        for suffix, param in TEXTURE_PARAMS.items():
            if low.endswith(suffix):
                textures[param] = unreal.load_asset(path)
    missing = [p for p in TEXTURE_PARAMS.values() if p not in textures]
    if missing:
        raise RuntimeError("%s missing textures %s" % (folder, missing))
    mi_path = folder + "/" + name + "_Mat"
    mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        mi = AT.create_asset(name + "_Mat", folder, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
    for param, tex in textures.items():
        if param not in params:
            raise RuntimeError("Master lacks parameter " + param)
        # UE 5.8 returns False even on success; verify the actual value instead.
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, param, tex)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    for param, tex in textures.items():
        actual = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, param)
        if actual != tex:
            raise RuntimeError("Material parameter verification failed %s %s" % (mi_path, param))
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def integrate_static(export, category, name, scale, collision):
    folder = move_folder("/Game/TripoModels/" + export, "/Game/Tripo/%s/%s" % (category, name))
    mesh = next((unreal.load_asset(p) for p in assets_in(folder, False)
                 if isinstance(unreal.load_asset(p), unreal.StaticMesh)), None)
    if mesh is None:
        raise RuntimeError("No static mesh in " + folder)
    mi = build_material(folder, mesh.get_name())
    for i in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(i, mi)
    build = SMS.get_lod_build_settings(mesh, 0)
    # Keep the raw ~98 cm build: a baked BuildScale3D renders scaled but UE 5.8 keeps the unscaled asset
    # bounds for these Nanite meshes, which breaks bounds-driven footprint fitting. Scale is applied per
    # placement instead (see suggestedScale / Content/Data/TownAssetSlots.tripo.json).
    build.set_editor_property("build_scale3d", unreal.Vector(1.0, 1.0, 1.0))
    SMS.set_lod_build_settings(mesh, 0, build)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", True)
    SMS.set_nanite_settings(mesh, nanite, apply_changes=True)
    body = mesh.get_editor_property("body_setup")
    if collision == "complex":
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    else:
        SMS.remove_collisions(mesh)
        SMS.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    for p in assets_in(folder):
        EAL.save_asset(p, only_if_is_dirty=False)
    report["statics"][name] = {
        "category": category, "export": export, "mesh": mesh.get_path_name(), "material": mi.get_path_name(),
        "suggestedScale": scale, "nanite": True, "collision": collision,
        "sizeCm": [round(size.x, 1), round(size.y, 1), round(size.z, 1)],
        "bottomCm": round(bounds.min.z, 2), "triangles": SMS.get_number_verts(mesh, 0)}


def integrate_skeletal(export, name, role, archetypes):
    folder = move_folder("/Game/TripoModels/" + export, "/Game/Tripo/Monsters/" + name)
    mesh = skeleton = None
    for p in assets_in(folder, False):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
        elif isinstance(a, unreal.Skeleton):
            skeleton = a
    if mesh is None:
        raise RuntimeError("No skeletal mesh in " + folder)
    mi = build_material(folder, mesh.get_name())
    mats = list(mesh.get_editor_property("materials"))
    for m in mats:
        m.set_editor_property("material_interface", mi)
    mesh.modify()
    mesh.set_editor_property("materials", mats)
    anims = {}
    for p in assets_in(folder + "/Animations"):
        a = unreal.load_asset(p)
        if not isinstance(a, unreal.AnimSequence):
            continue
        if skeleton is not None and a.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Animation %s does not use %s" % (p, skeleton.get_path_name()))
        short = a.get_name()[len(export) + 1:] if a.get_name().startswith(export + "_") else a.get_name()
        for prefix, clean in AI_CLIPS.items():
            if short.startswith(prefix):
                new = "%s_%s" % (export, clean)
                if AT.rename_assets([unreal.AssetRenameData(a, folder + "/Animations", new)]):
                    short = clean
                break
        anims[short] = a.get_path_name()
    bounds = mesh.get_bounds()
    height = bounds.box_extent.z * 2
    bottom = bounds.origin.z - bounds.box_extent.z
    for p in assets_in(folder):
        EAL.save_asset(p, only_if_is_dirty=False)
    report["skeletal"][name] = {
        "role": role, "archetypes": archetypes, "export": export, "mesh": mesh.get_path_name(),
        "skeleton": skeleton.get_path_name() if skeleton else None, "material": mi.get_path_name(),
        "rawHeightCm": round(height, 2), "rawBottomCm": round(bottom, 2),
        "boundsExtentCm": [round(bounds.box_extent.x, 1), round(bounds.box_extent.y, 1), round(bounds.box_extent.z, 1)],
        "animations": anims}


def run():
    for export, (category, name, scale, collision) in STATICS.items():
        try:
            integrate_static(export, category, name, scale, collision)
        except Exception as exc:
            report["errors"].append("%s: %s\n%s" % (export, exc, traceback.format_exc()))
    for export, (name, role, archetypes) in SKELETAL.items():
        try:
            integrate_skeletal(export, name, role, archetypes)
        except Exception as exc:
            report["errors"].append("%s: %s\n%s" % (export, exc, traceback.format_exc()))
    report["redirectorsFixed"] = fix_redirectors("/Game/TripoModels") + fix_redirectors("/Game/Tripo")
    for export in list(STATICS) + list(SKELETAL):
        folder = "/Game/TripoModels/" + export
        if EAL.does_directory_exist(folder) and not assets_in(folder):
            EAL.delete_directory(folder)
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_TRIPO_BATCH03_INTEGRATION %s statics=%d skeletal=%d errors=%d" % (
        "PASS" if not report["errors"] else "FAIL", len(report["statics"]), len(report["skeletal"]),
        len(report["errors"])))


try:
    run()
except Exception as exc:
    report["errors"].append("fatal: %s\n%s" % (exc, traceback.format_exc()))
    REPORT.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log_error("CIRE_TRIPO_BATCH03_INTEGRATION FAIL fatal")
unreal.SystemLibrary.quit_editor()
