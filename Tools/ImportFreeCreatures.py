"""Import the CC0 animated creatures and register them as free race bodies (world-dressing).

Run with the regular Python: launches an unattended UnrealEditor-Cmd commandlet that
  1. imports each GLB from Saved/CreatureSources (Tools/FetchFreeCreatures.py) into /Game/Free/Creatures/<Model>
     (skeletal mesh, skeleton, one AnimSequence per clip; duplicate "AnimalArmature|" exports are removed),
  2. records sockets named like the Tripo humanoid bones (head, pelvis, spine_03, hand_l/r, foot_l/r, ball_l/r) on
     the animal bones; CireMonsterArt adds them to the mesh when the body is applied (in memory, the socket name is
     read-only to editor Python), so aura sockets, footsteps and the native checks work without an AnimBP,
  3. gives every material slot an instance of the race skin (/Game/Art/Materials/M_CireMonsterSkin) with a
     solid slot-colour texture (most Quaternius animals have no UVs: flat colours per slot), so race palettes
     and rank colours apply like on Tripo bodies,
  4. measures each clip on the reference skeleton (facing, head height, feet travel -> natural walk/run speed)
     and writes Art/Creatures/Free/ImportReport.json.
Then (outside Unreal) it writes Content/Data/RaceMeshes.free.json: the lowest-priority race body overlay read
by CireMonsterArt (after NPCMeshes and RaceMeshes.tripo.json).

Usage: python Tools/ImportFreeCreatures.py [--data-only]
"""
from __future__ import annotations

import json
import math
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Saved/CreatureSources"
ART = ROOT / "Art/Creatures/Free"
REPORT = ART / "ImportReport.json"
PKG = "/Game/Free/Creatures"
SKIN = "/Game/Art/Materials/M_CireMonsterSkin.M_CireMonsterSkin"

# Model -> socket name -> bone. Roles -> clip name (without the model prefix).
RIGS = {
    "Wolf": {"lockRoot": True, "sockets": {"head": "Head", "pelvis": "Body", "spine_03": "Torso3", "hand_l": "FF_L", "hand_r": "FF_R",
                         "foot_l": "FFB_L", "foot_r": "FFB_R", "ball_l": "FF_L", "ball_r": "FF_R"},
             "roles": {"idle": "Idle", "walk": "Walk", "run": "Gallop", "attack": "Attack", "hit": "Idle_HitReact_Left", "death": "Death"}},
    "Stag": {"lockRoot": True, "sockets": {"head": "Head", "pelvis": "Body", "spine_03": "Torso3", "hand_l": "FF_L", "hand_r": "FF_R",
                         "foot_l": "FFB_L", "foot_r": "FFB_R", "ball_l": "FF_L", "ball_r": "FF_R"},
             "roles": {"idle": "Idle", "walk": "Walk", "run": "Gallop", "attack": "Attack_Headbutt", "attackAlt": "Attack_Kick",
                       "hit": "Idle_HitReact_Left", "death": "Death"}},
    "Bull": {"lockRoot": True, "sockets": {"head": "Head", "pelvis": "Body", "spine_03": "Torso3", "hand_l": "FF_L", "hand_r": "FF_R",
                         "foot_l": "FFB_L", "foot_r": "FFB_R", "ball_l": "FF_L", "ball_r": "FF_R"},
             "roles": {"idle": "Idle", "walk": "Walk", "run": "Gallop", "attack": "Attack_Kick", "attackAlt": "Attack_Headbutt",
                       "hit": "Idle_HitReact_Left", "death": "Death"}},
    "Pig": {"sockets": {"head": "Head", "pelvis": "All", "spine_03": "Neck", "hand_l": "FrontLeg_L_end", "hand_r": "FrontLeg_R_end",
                        "foot_l": "BackLeg_L_end", "foot_r": "BackLeg_R_end", "ball_l": "FrontLeg_L_end", "ball_r": "FrontLeg_R_end"},
            "roles": {"idle": "Idle", "walk": "Walk", "run": "Run", "attack": "Headbutt", "hit": "Jump_Start", "death": "Death"}},
    "Spider": {"sockets": {"head": "Head", "pelvis": "Body", "spine_03": "Thorax", "hand_l": "FrontFoot_L", "hand_r": "FrontFoot2_R",
                           "foot_l": "BackFoot_L", "foot_r": "BackFoot_R", "ball_l": "FrontFoot_L", "ball_r": "FrontFoot2_R"},
               "roles": {"idle": "Spider_Idle", "walk": "Spider_Walk", "run": "Spider_Walk", "attack": "Spider_Attack",
                         "hit": "Spider_Attack", "death": "Spider_Death"}},
}

# Race units filled by a free body: model, target head height (cm, actor scale 1), variant name, speed-up of the
# run cycle when the model has no separate run clip, and the palette hint recorded for review.
UNITS = {
    "dire_wolf": ("Wolf", 150, "FreeWolf", 1.0),
    "grave_hound": ("Wolf", 105, "FreeWolfHound", 1.0),
    "feral_shaman": ("Stag", 200, "FreeStag", 1.0),
    "tusked_behemoth": ("Bull", 235, "FreeBull", 1.0),
    "bristleback": ("Pig", 75, "FreePig", 1.0),
    "crystal_ballista": ("Spider", 95, "FreeSpider", 2.2),
}
# Darker, realistic palettes for the flat Quaternius colours (linear RGB per material slot, "*" = every slot).
# Race palettes and rank colours are applied on top by CireRaces::ApplySkin.
COLOURS = {
    "Wolf": {"Main": (0.040, 0.028, 0.019), "Main_Light": (0.15, 0.13, 0.10)},
    "Stag": {"Material": (0.075, 0.045, 0.024), "Material.003": (0.20, 0.17, 0.12)},
    "Pig": {"*": (0.030, 0.022, 0.017)},
    "Spider": {"Material": (0.022, 0.024, 0.030), "Material.001": (0.22, 0.03, 0.02)},
}
HAND_PROPS = ["hand_r", "hand_l", "spine_03", "head", "back", "spine_02"]


def glb_materials(path: Path):
    data = path.read_bytes()
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + length])
    out = []
    for m in gltf.get("materials", []):
        pbr = m.get("pbrMetallicRoughness", {})
        out.append({"name": m.get("name"), "color": pbr.get("baseColorFactor", [1, 1, 1, 1])[:3],
                    "roughness": pbr.get("roughnessFactor", 1.0), "textured": "baseColorTexture" in pbr})
    return out


def normalized_glb(model: str) -> Path:
    """Copy of the GLB whose skinned mesh nodes have an identity transform.

    Per the glTF spec a skinned mesh's node transform is ignored (the joints place the skin), but these Blender
    exports carry scale 100 / -90 deg X on the mesh node and Interchange bakes it into the vertices, so the skin
    rendered 100x too large while every bone measured correctly. Only that node transform changes."""
    data = (SRC / f"{model}.glb").read_bytes()
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + length])
    for node in gltf["nodes"]:
        if "skin" in node:
            for key in ("translation", "rotation", "scale", "matrix"):
                node.pop(key, None)
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    rest = data[20 + length:]
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + len(rest)) + struct.pack("<I", len(text)) + b"JSON" + text + rest
    target = SRC / "Normalized" / f"{model}.glb"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(out)
    return target


def solid_png(path: Path, rgb):
    """4x4 PNG of one sRGB colour (the glTF factor is linear: convert)."""
    def srgb(c):
        c = max(0.0, min(1.0, c))
        return round(255 * (c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055))
    px = bytes(srgb(c) for c in rgb)
    raw = b"".join(bytes([0]) + px * 4 for _ in range(4))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(bytes([137, 80, 78, 71, 13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# ------------------------------------------------------------------ Unreal side
def run(u):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary
    skin = lib.load_asset(SKIN)
    assert skin, SKIN
    report = {"models": {}}
    swatches = ROOT / "Saved/CreatureSwatches"
    swatches.mkdir(parents=True, exist_ok=True)

    def import_file(filename, dest, name=None):
        task = u.AssetImportTask()
        for k, v in (("filename", str(filename)), ("destination_path", dest), ("automated", True), ("replace_existing", True), ("save", True)):
            task.set_editor_property(k, v)
        if name:
            task.set_editor_property("destination_name", name)
        tools.import_asset_tasks([task])
        return list(task.get_editor_property("imported_object_paths"))

    # texture settings the skin master's samplers expect: copy them from a Tripo body's material
    ref_settings = {}
    for p in lib.list_assets("/Game/Tripo/Monsters", recursive=True):
        a = lib.load_asset(p)
        if isinstance(a, u.MaterialInstanceConstant):
            for role in ("BaseColorTex", "NormalTex", "MetallicTex", "RoughnessTex"):
                t = edit.get_material_instance_texture_parameter_value(a, role)
                if t:
                    ref_settings[role] = (t.get_editor_property("srgb"), t.get_editor_property("compression_settings"), t)
            if len(ref_settings) == 4:
                break
    assert len(ref_settings) == 4, f"no Tripo reference material: {ref_settings.keys()}"

    def texture(name, dest, rgb, role):
        path = f"{dest}/{name}"
        png = swatches / f"{name}.png"
        solid_png(png, rgb)
        import_file(png, dest, name)
        t = lib.load_asset(path)
        srgb, comp, _ = ref_settings[role]
        t.set_editor_property("srgb", srgb)
        t.set_editor_property("compression_settings", comp)
        lib.save_loaded_asset(t, only_if_is_dirty=False)
        return t

    for model, rig in RIGS.items():
        dest = f"{PKG}/{model}"
        if lib.does_directory_exist(dest):
            lib.delete_directory(dest)
        import_file(SRC / "Normalized" / f"{model}.glb", dest)
        mesh, anims = None, {}
        for p in lib.list_assets(dest, recursive=True):
            a = lib.load_asset(p)
            if isinstance(a, u.SkeletalMesh):
                mesh = a
            elif isinstance(a, u.AnimSequence):
                anims[a.get_name()] = a
        assert mesh, model
        skeleton = mesh.get_editor_property("skeleton")
        # one clip per name: strip the model prefix and every "AnimalArmature"/"SpiderArmature" layer
        clips = {}
        # Prefer the "AnimalArmature|X" exports: the plain-named duplicates also key the armature node itself
        # (the proxy root joint then carries scale 10000 instead of the reference 100).
        for name, a in sorted(anims.items(), key=lambda kv: (0 if "Armature" in kv[0] else 1, len(kv[0]))):
            key = name[len(model):] if name.startswith(model) else name
            for prefix in ("AnimalArmature_", "SpiderArmature_"):
                while key.startswith(prefix):
                    key = key[len(prefix):]
            key = key.strip("_")
            if key in clips:
                lib.delete_asset(a.get_path_name())
            else:
                clips[key] = a
        # materials: race-skin instances with solid slot colours (or the model's own atlas texture)
        colours = glb_materials(SRC / f"{model}.glb")
        mats = mesh.get_editor_property("materials")
        normal = ref_settings["NormalTex"][2]
        black = texture(f"T_{model}_Metal", f"{dest}/Textures", (0, 0, 0), "MetallicTex")
        new_mats = []
        for i, slot in enumerate(mats):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            src = next((c for c in colours if c["name"] == slot_name), colours[min(i, len(colours) - 1)])
            mi_name = f"MI_{model}_{slot_name.replace('.', '_')}"
            mi_path = f"{dest}/Materials/{mi_name}"
            mi = tools.create_asset(mi_name, f"{dest}/Materials", u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew()) \
                if not lib.does_asset_exist(mi_path) else lib.load_asset(mi_path)
            edit.set_material_instance_parent(mi, skin)
            old = slot.get_editor_property("material_interface")
            atlas = edit.get_material_instance_texture_parameter_value(old, "BaseColorTexture") if isinstance(old, u.MaterialInstanceConstant) else None
            override = COLOURS.get(model, {}).get(slot_name) or COLOURS.get(model, {}).get("*")
            base = atlas if (src["textured"] and atlas and not override) else                 texture(f"T_{model}_{slot_name.replace('.', '_')}_Col", f"{dest}/Textures", override or src["color"], "BaseColorTex")
            rough = texture(f"T_{model}_{slot_name.replace('.', '_')}_Rough", f"{dest}/Textures", (min(1, src["roughness"]) * .85,) * 3, "RoughnessTex")
            for role, t in (("BaseColorTex", base), ("NormalTex", normal), ("MetallicTex", black), ("RoughnessTex", rough)):
                edit.set_material_instance_texture_parameter_value(mi, role, t)
            lib.save_loaded_asset(mi, only_if_is_dirty=False)
            slot.set_editor_property("material_interface", mi)
            new_mats.append(slot)
        mesh.set_editor_property("materials", new_mats)
        lib.save_loaded_asset(mesh, only_if_is_dirty=False)
        lib.save_loaded_asset(skeleton, only_if_is_dirty=False)

        # measurements on the reference skeleton and the clips
        ref = u.AnimPoseExtensions.get_reference_pose(skeleton)
        root_name = str(u.AnimPoseExtensions.get_bone_names(ref)[0])

        root_ref = u.AnimPoseExtensions.get_bone_pose(ref, root_name, u.AnimPoseSpaces.WORLD)

        def loc(pose, bone):
            """Component-space position with the root joint held at its reference transform (as the game does)."""
            world = u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD).translation
            if not rig.get("lockRoot"):
                return world
            root = u.AnimPoseExtensions.get_bone_pose(pose, root_name, u.AnimPoseSpaces.WORLD)
            return u.MathLibrary.transform_location(root_ref, u.MathLibrary.inverse_transform_location(root, world))
        head, body = loc(ref, rig["sockets"]["head"]), loc(ref, rig["sockets"]["pelvis"])
        forward = (head.x - body.x, head.y - body.y)
        yaw = -math.degrees(math.atan2(forward[1], forward[0]))
        names = [str(n) for n in u.AnimPoseExtensions.get_bone_names(ref)]
        reach = max(math.hypot(loc(ref, n).x, loc(ref, n).y) for n in names)
        opts = u.AnimPoseEvaluationOptions()
        clip_rows = {}
        for key, a in clips.items():
            length = a.get_play_length()
            samples = []
            for k in range(13):
                pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, length * k / 12, opts)
                samples.append({"head": loc(pose, rig["sockets"]["head"]).z,
                                "feet": min(loc(pose, rig["sockets"][f]).z for f in ("foot_l", "foot_r", "ball_l", "ball_r")),
                                "paw": loc(pose, rig["sockets"]["ball_l"]), "body": loc(pose, rig["sockets"]["pelvis"])})
            # stride: front paw travel along the facing axis relative to the body, over one cycle
            fx, fy = forward[0] / max(1e-6, math.hypot(*forward)), forward[1] / max(1e-6, math.hypot(*forward))
            along = [(s["paw"].x - s["body"].x) * fx + (s["paw"].y - s["body"].y) * fy for s in samples]
            clip_rows[key] = {"asset": a.get_path_name(), "length": round(length, 3), "stride": round(max(along) - min(along), 1),
                              "headMin": round(min(s["head"] for s in samples), 1), "headMax": round(max(s["head"] for s in samples), 1),
                              "headEnd": round(samples[-1]["head"], 1), "feetMin": round(min(s["feet"] for s in samples), 1)}
        report["models"][model] = {"mesh": mesh.get_path_name(), "skeleton": skeleton.get_path_name(), "yaw": round(yaw, 1),
                                   "headZ": round(head.z, 1), "reach": round(reach, 1), "bones": len(names), "clips": clip_rows}
        u.log(f"CIRE_FREE_CREATURE {model} clips={len(clips)} yaw={yaw:.0f} head={head.z:.0f}")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    u.log(f"CIRE_FREE_CREATURES_IMPORT_PASS models={len(report['models'])}")


# ------------------------------------------------------------------ data (outside Unreal)
def write_data() -> None:
    report = json.loads(REPORT.read_text(encoding="utf-8"))["models"]
    units = {}
    for unit, (model, head_cm, variant, run_rate) in UNITS.items():
        m = report[model]
        idle_head = m["clips"][RIGS[model]["roles"]["idle"]]["headMax"]
        scale = head_cm / idle_head
        roles = RIGS[model]["roles"]
        anims = {role: m["clips"][clip]["asset"] for role, clip in roles.items() if clip in m["clips"]}
        walk, run = m["clips"][roles["walk"]], m["clips"][roles["run"]]
        # in-place cycles: a planted paw sweeps its stride back during ~55% of the cycle
        walk_speed = walk["stride"] * scale / max(.1, .55 * walk["length"])
        run_speed = run["stride"] * scale / max(.1, .5 * run["length"]) * (run_rate if roles["run"] == roles["walk"] else 1.0)
        units[unit] = {"variant": variant, "mesh": m["mesh"], "meshScale": round(scale, 4), "yaw": m["yaw"],
                       "heightCm": round(head_cm / .95, 1), "rig": "quadruped", "lockRoot": bool(RIGS[model].get("lockRoot")),
                       # clamped to plausible gaits (the spider's splayed legs overstate its stride)
                       "walkSpeedCm": round(min(300, max(60, walk_speed)), 1),
                       "runSpeedCm": round(min(900, max(min(300, max(60, walk_speed)) * 1.6, run_speed)), 1),
                       "reachCm": round(max(m["reach"] * scale * 1.35, head_cm * 2.4), 1),
                       "dropPropBones": HAND_PROPS, "sockets": RIGS[model]["sockets"], "animations": anims,
                       "source": "Quaternius (CC0 1.0), see Art/Creatures/Free/PROVENANCE.md"}
    doc = {"schemaVersion": 1, "priority": "lowest",
           "description": ("Free (CC0) animated creature bodies for race units without art (world-dressing). Read by CireMonsterArt "
                           "after NPCMeshes.tripo.json and RaceMeshes.tripo.json: a unit listed there keeps that art. Same shape as "
                           "RaceMeshes.tripo.json plus rig/walkSpeedCm/runSpeedCm/reachCm/dropPropBones. Written by "
                           "Tools/ImportFreeCreatures.py."),
           "archetypes": units}
    (ROOT / "Content/Data/RaceMeshes.free.json").write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    for unit, row in units.items():
        print(f"{unit:18s} {row['variant']:14s} scale={row['meshScale']:.3f} walk={row['walkSpeedCm']:.0f} run={row['runSpeedCm']:.0f} reach={row['reachCm']:.0f}")


def launch(args):
    if "--data-only" not in args:
        import stat
        for model in RIGS:
            normalized_glb(model)
        for path in (ROOT / "Content/Free/Creatures").rglob("*"):
            if path.is_file():
                path.chmod(path.stat().st_mode | stat.S_IWRITE)
        log = ROOT / "Saved/Logs/FreeCreatures.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
                   "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
                   f"-script={Path(__file__).resolve()}", f"-abslog={log}"]
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        text = log.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if "CIRE_FREE_CREATURE" in line or ("Error" in line and ("Python" in line or "Traceback" in line)):
                print(line.split("LogPython: ")[-1])
        if result.returncode != 0 or "CIRE_FREE_CREATURES_IMPORT_PASS" not in text:
            print(f"import failed exit={result.returncode} log={log}")
            return 1
    write_data()
    return 0


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch(sys.argv[1:]))
    else:
        run(unreal)
