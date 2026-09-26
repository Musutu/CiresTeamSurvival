"""UE 5.8 commandlet: inventory every creature skeletal mesh in the installed Fab packs (monster-expansion).

For every SkeletalMesh under the scanned pack folders it records the skeleton, bone count, bounds, material slots
(material, parent, blend mode, texture parameters), and every AnimSequence on that skeleton (name, length). It then
marks which meshes the game already uses (any path named in the committed mapping data) so the unused ones can be
integrated. Report: Saved/CreatureInventory.json. Marker CIRE_CREATURE_INVENTORY_DONE.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/InventoryCreatures.py -unattended -nullrhi
"""
import json
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
PACKS = ["UndeadPack", "Monster", "ROG_Creatures", "QuadrapedCreatures", "GDHBundle", "CastleTown", "Polyphoria", "MaleLocomotionSet",
         "Gun_and_Sword", "CrossbowPackAnim", "Medieval_Weapons", "Medieval_Weapons_VOL2", "FXVarietyPack", "Fab"]
DATA = ["Content/Data/RaceMeshes.fab.json", "Content/Data/ChampionArtBindings.fab.json", "Content/Data/WeaponLoadouts.fab.json",
        "Content/Data/TownAssetSlots.fabkit.json", "Content/Data/TownAssetSlots.fab.json", "Content/Data/MonsterExpansion.json"]
reg = u.AssetRegistryHelpers.get_asset_registry()


def material_info(mi):
    if mi is None:
        return None
    out = {"path": mi.get_path_name().split(".")[0]}
    try:
        base = mi.get_base_material()
        out["base"] = base.get_path_name().split(".")[0]
        out["blend"] = str(base.get_editor_property("blend_mode"))
    except Exception:
        pass
    tex = {}
    try:
        for name in u.MaterialEditingLibrary.get_texture_parameter_names(mi) if isinstance(mi, u.Material) else []:
            tex[str(name)] = ""
    except Exception:
        pass
    if isinstance(mi, u.MaterialInstance):
        try:
            for p in mi.get_editor_property("texture_parameter_values"):
                t = p.get_editor_property("parameter_value")
                tex[str(p.get_editor_property("parameter_info").get_editor_property("name"))] = t.get_path_name().split(".")[0] if t else ""
        except Exception as e:
            out["texErr"] = str(e)
    out["textures"] = tex
    return out


def main():
    used_text = "\n".join((ROOT / p).read_text(encoding="utf-8") for p in DATA if (ROOT / p).exists())
    folders = [p for p in PACKS if u.EditorAssetLibrary.does_directory_exist("/Game/" + p)]
    reg.scan_paths_synchronous(["/Game/" + p for p in folders], True)
    anims_by_skel = {}
    meshes = []
    for pack in folders:
        for a in reg.get_assets_by_path("/Game/" + pack, recursive=True):
            c = str(a.asset_class_path.asset_name)
            if c == "AnimSequence":
                tag = str(a.get_tag_value("Skeleton") or "")
                skel = tag.split("'")[1] if "'" in tag else tag
                anims_by_skel.setdefault(skel.split(".")[0], []).append(str(a.package_name))
            elif c == "SkeletalMesh":
                meshes.append(a)
    out = {"packs": folders, "meshes": []}
    for a in meshes:
        pkg = str(a.package_name)
        m = a.get_asset()
        skel = m.get_editor_property("skeleton")
        skel_path = skel.get_path_name().split(".")[0] if skel else ""
        b = m.get_bounds()
        comp = u.SkeletalMeshComponent()
        comp.set_skeletal_mesh_asset(m)
        mats = []
        for sm in m.get_editor_property("materials"):
            mats.append({"slot": str(sm.get_editor_property("material_slot_name")), **(material_info(sm.get_editor_property("material_interface")) or {})})
        clips = []
        for p in sorted(anims_by_skel.get(skel_path, [])):
            s = u.load_asset(p)
            clips.append({"path": p, "length": round(float(s.get_play_length()), 2)} if s else {"path": p})
        out["meshes"].append({"path": pkg, "skeleton": skel_path, "bones": comp.get_num_bones(),
                              "size": [round(b.box_extent.x * 2, 1), round(b.box_extent.y * 2, 1), round(b.box_extent.z * 2, 1)],
                              "used": pkg in used_text, "materials": mats, "clips": clips})
    (ROOT / "Saved").mkdir(exist_ok=True)
    (ROOT / "Saved/CreatureInventory.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    u.log("CIRE_CREATURE_INVENTORY_DONE meshes=%d" % len(out["meshes"]))


main()
