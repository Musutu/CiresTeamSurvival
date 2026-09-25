"""UE 5.8 commandlet: inventory the installed Fab packs and check their dependencies.

For every top-level pack folder named in -CireFabPacks=A+B (default: every folder listed in
Art/Fab/PurchasedPacks.json "folders" plus the known pack folders) it records:
  skeletons (bone count, first bones), skeletal meshes (skeleton, height), anim sequences per skeleton
  (name, length, fps), blend spaces, Niagara systems, static meshes (count + sample), and every hard
  package dependency outside /Game/<Pack> or /Engine|/Script that does not exist in this project
  (a missing dependency means the pack needs another folder migrated, or a plugin).
Report: Saved/FabInspect.json. Marker CIRE_FAB_INSPECT_DONE.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/InspectFabPacks.py -unattended -nullrhi
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEFAULT = ["CastleTown", "CrossbowPackAnim", "Earth_Spells", "Forest_VFX", "FXVarietyPack", "GDHBundle", "Gun_and_Sword",
           "MaleLocomotionSet", "Medieval_Weapons", "Medieval_Weapons_VOL2", "Monster", "Polyphoria", "QuadrapedCreatures",
           "RealisticBlood", "ROG_Creatures", "State_VFX", "UndeadPack", "Big_Pack_Magic_VFX", "Shadow_Magic"]
reg = unreal.AssetRegistryHelpers.get_asset_registry()


def cls(data):
    return str(data.asset_class_path.asset_name)


def main():
    packs = DEFAULT
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith("-cirefabpacks="):
            packs = token.split("=", 1)[1].split("+")
    reg.scan_paths_synchronous(["/Game/" + p for p in packs], True)
    out = {}
    for pack in packs:
        root = "/Game/" + pack
        assets = reg.get_assets_by_path(root, recursive=True)
        entry = {"count": len(assets), "classes": {}, "skeletons": {}, "meshes": [], "anims": {}, "blendspaces": [],
                 "niagara": [], "static_meshes": 0, "static_sample": [], "maps": [], "missing_deps": {}}
        packages = set()
        for a in assets:
            c = cls(a)
            entry["classes"][c] = entry["classes"].get(c, 0) + 1
            pkg = str(a.package_name)
            packages.add(pkg)
            name = str(a.asset_name)
            if c == "Skeleton":
                sk = a.get_asset()
                entry["skeletons"][pkg] = {}
            elif c == "SkeletalMesh":
                m = a.get_asset()
                comp = unreal.SkeletalMeshComponent()
                comp.set_skeletal_mesh_asset(m)
                bones = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
                b = m.get_bounds()
                entry["meshes"].append({"path": pkg, "skeleton": m.get_editor_property("skeleton").get_path_name().split(".")[0],
                                        "bones": len(bones), "first": bones[:12], "height": round(b.box_extent.z * 2, 1)})
            elif c == "AnimSequence":
                tag = a.get_tag_value("Skeleton") or ""
                sk = tag.split("'")[1].split(".")[0] if "'" in tag else tag.split(".")[0]
                entry["anims"].setdefault(sk, []).append(pkg)
            elif c in ("BlendSpace", "BlendSpace1D"):
                entry["blendspaces"].append(pkg)
            elif c == "NiagaraSystem":
                entry["niagara"].append(pkg)
            elif c == "StaticMesh":
                entry["static_meshes"] += 1
                if len(entry["static_sample"]) < 400:
                    entry["static_sample"].append(pkg)
            elif c == "World":
                entry["maps"].append(pkg)
        # Dependencies outside the pack that are missing.
        opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                                     include_searchable_names=False, include_soft_management_references=False,
                                                     include_hard_management_references=False)
        for pkg in packages:
            deps = reg.get_dependencies(pkg, opts) or []
            for d in deps:
                d = str(d)
                if d.startswith(root + "/") or not d.startswith("/Game/"):
                    continue
                if not unreal.EditorAssetLibrary.does_asset_exist(d):
                    entry["missing_deps"].setdefault(d, []).append(pkg)
        entry["missing_deps"] = {k: v[:3] + (["+%d" % (len(v) - 3)] if len(v) > 3 else []) for k, v in entry["missing_deps"].items()}
        entry["anims"] = {k: {"count": len(v), "sample": sorted(v)[:5]} for k, v in entry["anims"].items()}
        out[pack] = entry
        unreal.log("CIRE_FAB_INSPECT %s assets=%d missing_deps=%d" % (pack, len(assets), len(entry["missing_deps"])))
    (ROOT / "Saved" / "FabInspect.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    unreal.log("CIRE_FAB_INSPECT_DONE packs=%d" % len(out))


main()
