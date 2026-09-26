"""UE 5.8 commandlet: body-only locomotion BlendSpaces for the Tripo champion fallback bodies (weapon-grips,
Docs/WeaponLoadouts.md "Grip model").

The new-champions Tripo BlendSpaces (/Game/Tripo/Champions/<Name>/Animations/BS_Idle_Walk_Run_<Name>) evaluate to one
static pose at runtime, and so does every duplicate of them, including the shared BS_Fab_Locomotion_Tripo<Name> built
by RetargetFabAnimations.py: idle and run render the same frame (grip gallery --hq-off). The fix builds each body its
own BlendSpace on the lancer's proven grid (the lancer BlendSpace retargeted onto the body: paladin-hq make_template)
and points only that body folder at it (Content/Data/FabAnimations.json "locomotionOverride",
CireFabAnimation::Locomotion). Nothing shared is written:

* Aetheri Warden (attacks with the Spear set, its shared locomotion is the two-handed set): the Spear idle and 8-way
  walk/run are cloned onto the body first, so the second hand reaches the halberd at rest.
    /Game/FabDerived/Warden/Warden_Spear/A_Warden_Spear_loco_*, BS_Fab_Locomotion_Warden_Spear
    /Game/FabDerived/Warden/Rigs/Warden_Spear/...   (IK rigs + retargeters of the clones)
* Gunblade, Witch Slayer, Huntress, Aetheri Artificer: their shared clips (/Game/FabDerived/Anim/<Folder>/A_<Folder>_loco_*)
  are sound and are only referenced (read-only).
    /Game/FabDerived/BodyLoco/<Clone>/BS_Fab_Locomotion_<Clone>

Each run builds <out>/<Clone>/_template (the grid scaffold: the batch retarget carries the lancer's sample grid but
not its clips) and deletes it at the end. A scaffold left by an older run is removed as files before anything loads
it: an empty-sample BlendSpace on disk fails to load ("sample with no/invalid animation") and the commandlet exits 1.

/Game/FabDerived is derived from licensed packs: gitignored, local only. The retarget steps are
Tools/RetargetFabAnimations.py's own (retarget_body / make_template / build_locomotion, loaded without its main).

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildWardenSpearLocomotion.py -unattended -nullrhi
     [-CireBodyLocoOnly=TripoGunblade+TripoHuntress]
Marker CIRE_WARDEN_SPEAR_LOCO_PASS / _FAIL (one CIRE_BODY_LOCO line per body); report Saved/WardenSpearLocomotion.json.
"""
import json
import shutil
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SHARED = "/Game/FabDerived/Anim"
# body folder -> (clone name, output root, Fab set cloned onto the body, or None to reference the shared clips)
TARGETS = {
    "TripoAetheriWarden": ("Warden_Spear", "/Game/FabDerived/Warden", "spear"),
    "TripoGunblade": ("Gunblade_Loco", "/Game/FabDerived/BodyLoco", None),
    "TripoWitchSlayer": ("WitchSlayer_Loco", "/Game/FabDerived/BodyLoco", None),
    "TripoHuntress": ("Huntress_Loco", "/Game/FabDerived/BodyLoco", None),
    "TripoAetheriArtificer": ("Artificer_Loco", "/Game/FabDerived/BodyLoco", None),
}
LOCO_KEYS = ["idle"] + ["%s_%s" % (g, d) for g in ("walk", "run") for d in ("f", "b", "l", "r", "fl", "fr", "bl", "br")]


def drop_stale_template(template):
    """A scaffold left on disk by an older run is removed as files before anything loads it; the registry forgets it."""
    folder = ROOT / "Content" / template[len("/Game/"):]
    if folder.is_dir():
        shutil.rmtree(folder)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([template], True)
        return True
    return False


def load_retarget():
    """RetargetFabAnimations helpers without running its main(); OUT / RIGS are set per body before each call."""
    path = ROOT / "Tools/RetargetFabAnimations.py"
    text = path.read_text(encoding="utf-8")
    text = text[: text.rindex("\nmain()")]
    ns = {"__file__": str(path), "__name__": "RetargetFabAnimations_lib"}
    exec(compile(text, str(path), "exec"), ns)
    return ns


def build(rt, cfg, mesh, folder, clone, out, clone_set, report):
    rt["OUT"], rt["RIGS"] = out, out + "/Rigs"
    blend_path = "%s/%s/BS_Fab_Locomotion_%s" % (out, clone, clone)
    template = "%s/%s/_template" % (out, clone)
    entry = report.setdefault(folder, {"clone": clone, "blend": blend_path})
    entry["staleTemplateRemoved"] = drop_stale_template(template)
    bodies_report = {}
    if clone_set:
        loco = {}
        for key, path in cfg["locomotion"][clone_set].items():
            anim = unreal.load_asset(path.split(".")[0])
            if isinstance(anim, unreal.AnimSequence):
                loco[key] = (anim, rt["source_mesh_for"](anim.get_editor_property("skeleton"), "/Game/" + path.split("/")[2]))
        done = rt["retarget_body"](clone, mesh, {}, loco, bodies_report, False)
        entry["retargetErrors"] = bodies_report.get(clone, {}).get("errors")
    else:
        # The body's shared Fab clips, referenced read-only (only those on this body's skeleton).
        done = {}
        for key in LOCO_KEYS:
            path = "%s/%s/A_%s_loco_%s" % (SHARED, folder, folder, key)
            anim = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
            if isinstance(anim, unreal.AnimSequence) and anim.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"):
                done["loco_" + key] = path
        bodies_report[clone] = {"clips": {}, "errors": {}}
    entry["clips"] = len(done)
    rt["require"]("loco_idle" in done and "loco_walk_f" in done, "%s: idle/walk_f missing" % folder)
    rt["make_template"](clone, mesh, bodies_report)
    blend = rt["build_locomotion"](clone, mesh, done, {}, {}, None, bodies_report)
    rt["require"](blend == blend_path, "%s: BlendSpace not built: %s" % (folder, bodies_report.get(clone, {}).get("errors")))
    entry["samples"] = bodies_report[clone].get("locomotion", {}).get("samples")
    # The scaffold has served as the grid; nothing references it, so it does not stay on disk.
    lib = unreal.EditorAssetLibrary
    if lib.does_directory_exist(template):
        lib.delete_directory(template)
    rt["require"](not lib.does_directory_exist(template) or not lib.list_assets(template), "template scaffold left behind")
    unreal.log("CIRE_BODY_LOCO %s clips=%d blend=%s" % (folder, len(done), blend_path))
    return blend_path


def main():
    report = {"status": "failed", "bodies": {}}
    only = None
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith("-cirebodylocoonly="):
            only = set(token.split("=", 1)[1].split("+"))
    try:
        rt = load_retarget()
        cfg = json.loads((ROOT / "Art/Fab/FabAnimMap.json").read_text(encoding="utf-8"))
        bodies = json.loads((ROOT / "Content/Data/ChampionAttacks02.json").read_text(encoding="utf-8"))["bodies"]
        data_path = ROOT / "Content/Data/FabAnimations.json"
        data = json.loads(data_path.read_text(encoding="utf-8"))
        overrides = dict(data.get("locomotionOverride", {}))
        built = 0
        for folder, (clone, out, clone_set) in TARGETS.items():
            if only and folder not in only:
                continue
            mesh_path = next((m for m, f in bodies.items() if f == folder), None)
            mesh = unreal.load_asset(mesh_path.split(".")[0]) if mesh_path else None
            if not isinstance(mesh, unreal.SkeletalMesh):
                report["bodies"][folder] = {"skipped": "body missing"}
                continue
            overrides[folder] = build(rt, cfg, mesh, folder, clone, out, clone_set, report["bodies"])
            built += 1
        # Point only these body folders at their own BlendSpace.
        data["locomotionOverride"] = dict(sorted(overrides.items()))
        data_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        report["status"] = "pass"
        unreal.log("CIRE_WARDEN_SPEAR_LOCO_PASS bodies=%d" % built)
    except Exception as error:
        report["error"] = traceback.format_exc()
        unreal.log_error("CIRE_WARDEN_SPEAR_LOCO_FAIL " + str(error))
    finally:
        (ROOT / "Saved").mkdir(exist_ok=True)
        (ROOT / "Saved/WardenSpearLocomotion.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
