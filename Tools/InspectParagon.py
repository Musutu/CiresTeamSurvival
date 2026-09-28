"""UE 5.8 commandlet: inventory every installed Paragon pack (Content/Paragon*, Epic-licensed, LOCAL ONLY).

For every hero folder (Content/Paragon<X>/Characters/Heroes/<Hero>) it records:
  meshes (base + skins: path, skeleton, height, material slots), the anim blueprint (+ whether its class loads),
  every anim sequence (name, length) and montage (name, length, slot names), blend spaces,
  Cascade / Niagara systems grouped by their FX/Particles/.../Abilities/<Ability> folder, and the sound cues.
ParagonMinions is inventoried as creeps (meshes, anim blueprints, anims) for monster variants.
Report: Saved/ParagonInspect.json (never committed; Tools/AuthorParagonChampions.py reads it).
Marker CIRE_PARAGON_INSPECT_DONE.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/InspectParagon.py -unattended -nullrhi
     optional -CireParagonPacks=ParagonGreystone+ParagonKwang
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
reg = unreal.AssetRegistryHelpers.get_asset_registry()


def cls(data):
    return str(data.asset_class_path.asset_name)


def packs():
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith("-cireparagonpacks="):
            return token.split("=", 1)[1].split("+")
    return sorted(p.name for p in (ROOT / "Content").iterdir() if p.is_dir() and p.name.startswith("Paragon"))


def mesh_info(pkg, asset):
    b = asset.get_bounds()
    mats = []
    try:
        for m in asset.get_editor_property("materials"):
            mi = m.get_editor_property("material_interface")
            mats.append({"slot": str(m.get_editor_property("material_slot_name")), "material": mi.get_path_name().split(".")[0] if mi else ""})
    except Exception:
        pass
    sk = asset.get_editor_property("skeleton")
    return {"path": pkg, "skeleton": sk.get_path_name().split(".")[0] if sk else "", "height": round(b.box_extent.z * 2, 1),
            "radius": round(max(b.box_extent.x, b.box_extent.y), 1), "materials": mats}


def main():
    names = packs()
    reg.scan_paths_synchronous(["/Game/" + p for p in names], True)
    out = {}
    for pack in names:
        root = "/Game/" + pack
        assets = reg.get_assets_by_path(root, recursive=True)
        entry = {"count": len(assets), "heroes": {}, "fx": {}, "sounds": [], "classes": {}}
        for a in assets:
            c = cls(a)
            entry["classes"][c] = entry["classes"].get(c, 0) + 1
            pkg = str(a.package_name)
            name = str(a.asset_name)
            parts = pkg.split("/")
            hero = None
            if len(parts) > 5 and parts[3] == "Characters" and parts[4] == "Heroes":
                hero = parts[5]
            elif len(parts) > 5 and parts[3] == "Characters":
                hero = parts[4] + "/" + parts[5]  # minions: Characters/<Group>/<Unit>
            h = entry["heroes"].setdefault(hero, {"meshes": [], "animbp": [], "anims": {}, "montages": {}, "blendspaces": []}) if hero else None
            try:
                if c == "SkeletalMesh" and h is not None:
                    if name.endswith("_CylShadows") or name.endswith("_ShadowCyl") or "Physics" in name:
                        continue
                    info = mesh_info(pkg, a.get_asset())
                    info["skin"] = parts[7] if len(parts) > 7 and parts[6] == "Skins" else ""
                    h["meshes"].append(info)
                elif c == "AnimBlueprint" and h is not None:
                    bp = a.get_asset()
                    gen = unreal.EditorAssetLibrary.load_blueprint_class(pkg)
                    sk = bp.get_editor_property("target_skeleton")
                    h["animbp"].append({"path": pkg, "classLoads": bool(gen), "skeleton": sk.get_path_name().split(".")[0] if sk else ""})
                elif c == "AnimSequence" and h is not None:
                    seq = a.get_asset()
                    sk = seq.get_editor_property("skeleton")
                    key = name if name not in h["anims"] else name + "#" + parts[-2]
                    h["anims"][key] = {"path": pkg, "length": round(seq.get_play_length(), 3),
                                       "skeleton": sk.get_path_name().split(".")[0] if sk else ""}

                elif c == "AnimMontage" and h is not None:
                    mon = a.get_asset()
                    slots = []
                    try:
                        for t in mon.get_editor_property("slot_anim_tracks"):
                            slots.append(str(t.get_editor_property("slot_name")))
                    except Exception:
                        pass
                    h["montages"][name] = {"path": pkg, "length": round(mon.get_play_length(), 3), "slots": slots}
                elif c in ("BlendSpace", "BlendSpace1D", "AimOffsetBlendSpace") and h is not None:
                    h["blendspaces"].append(pkg)
                elif c in ("ParticleSystem", "NiagaraSystem"):
                    group = "misc"
                    if "Abilities" in parts:
                        i = parts.index("Abilities")
                        group = parts[i + 1] if len(parts) > i + 2 else "misc"
                    elif "Particles" in parts:
                        i = parts.index("Particles")
                        group = parts[i + 1] if len(parts) > i + 2 else "misc"
                    entry["fx"].setdefault(group, []).append({"path": pkg, "kind": "cascade" if c == "ParticleSystem" else "niagara"})
                elif c == "SoundCue":
                    entry["sounds"].append(pkg)
            except Exception as e:  # keep going; record the failure
                entry.setdefault("errors", []).append("%s: %s" % (pkg, e))
        out[pack] = entry
        unreal.log("CIRE_PARAGON_INSPECT %s assets=%d heroes=%d" % (pack, len(assets), len(entry["heroes"])))
    (ROOT / "Saved").mkdir(exist_ok=True)
    (ROOT / "Saved" / "ParagonInspect.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    unreal.log("CIRE_PARAGON_INSPECT_DONE packs=%d" % len(out))


main()
