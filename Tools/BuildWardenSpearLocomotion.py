"""UE 5.8 commandlet: Warden-only spear idle/locomotion clones (weapon-grips, Docs/WeaponLoadouts.md "Grip model").

The Aetheri Warden attacks with the GDH Spear set but its shared locomotion (/Game/FabDerived/Anim/TripoAetheriWarden,
BS_Fab_Locomotion_TripoAetheriWarden) comes from the two-handed (greatsword) set, so at rest the second hand cannot
reach the halberd (65 cm off). This tool clones the Spear set's idle and 8-way walk/run clips onto the Warden's body in
a Warden-only folder and builds a Warden-only BlendSpace from them; nothing shared is written:

  /Game/FabDerived/Warden/Warden_Spear/A_Warden_Spear_loco_<idle|walk_*|run_*>
  /Game/FabDerived/Warden/Warden_Spear/BS_Fab_Locomotion_Warden_Spear
  /Game/FabDerived/Warden/Warden_Spear/_template/...     (lancer BlendSpace retargeted onto the Warden: the grid template)
  /Game/FabDerived/Warden/Rigs/Warden_Spear/...          (IK rigs + retargeters for these clones)

The retarget steps are Tools/RetargetFabAnimations.py's own (retarget_body / build_locomotion, loaded without its
main), pointed at the Warden folder. /Game/FabDerived is derived from licensed packs: gitignored, local only.
Content/Data/FabAnimations.json "locomotionOverride" maps the body folder to the BlendSpace (CireFabAnimation::Locomotion);
bodies without the clone keep the shared BlendSpace.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildWardenSpearLocomotion.py -unattended -nullrhi
Marker CIRE_WARDEN_SPEAR_LOCO_PASS / _FAIL; report Saved/WardenSpearLocomotion.json.
"""
import json
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
BODY_FOLDER = "TripoAetheriWarden"
CLONE = "Warden_Spear"
SET = "spear"
OUT = "/Game/FabDerived/Warden"
BLEND = "%s/%s/BS_Fab_Locomotion_%s" % (OUT, CLONE, CLONE)


def load_retarget():
    """RetargetFabAnimations' helpers without running its main(), writing under the Warden folder only."""
    path = ROOT / "Tools/RetargetFabAnimations.py"
    text = path.read_text(encoding="utf-8")
    text = text[: text.rindex("\nmain()")]
    ns = {"__file__": str(path), "__name__": "RetargetFabAnimations_lib"}
    exec(compile(text, str(path), "exec"), ns)
    ns["OUT"] = OUT
    ns["RIGS"] = OUT + "/Rigs"
    return ns


def main():
    report = {"clone": CLONE, "blend": BLEND, "status": "failed"}
    try:
        rt = load_retarget()
        cfg = json.loads((ROOT / "Art/Fab/FabAnimMap.json").read_text(encoding="utf-8"))
        bodies = json.loads((ROOT / "Content/Data/ChampionAttacks02.json").read_text(encoding="utf-8"))["bodies"]
        mesh_path = next(m for m, f in bodies.items() if f == BODY_FOLDER)
        mesh = unreal.load_asset(mesh_path.split(".")[0])
        rt["require"](isinstance(mesh, unreal.SkeletalMesh), "Warden body missing")
        loco = {}
        for key, path in cfg["locomotion"][SET].items():
            anim = unreal.load_asset(path.split(".")[0])
            if isinstance(anim, unreal.AnimSequence):
                loco[key] = (anim, rt["source_mesh_for"](anim.get_editor_property("skeleton"), "/Game/" + path.split("/")[2]))
        report["sources"] = {k: rt["path_of"](a) for k, (a, _) in loco.items()}
        bodies_report = {}
        done = rt["retarget_body"](CLONE, mesh, {}, loco, bodies_report, False)
        report["retarget"] = bodies_report.get(CLONE, {})
        rt["require"]("loco_idle" in done and "loco_walk_f" in done, "idle/walk_f did not retarget")
        # The Warden's Tripo BlendSpace (BS_Idle_Walk_Run_AetheriWarden) evaluates to a static pose at runtime, and so did
        # every duplicate of it (the shared BS_Fab_Locomotion_TripoAetheriWarden too: idle and run rendered the same frame).
        # Template instead from the lancer's proven BlendSpace retargeted onto the Warden (paladin-hq make_template),
        # written under the clone folder (_template).
        rt["make_template"](CLONE, mesh, bodies_report)
        blend = rt["build_locomotion"](CLONE, mesh, done, loco, {}, None, bodies_report)
        rt["require"](blend == BLEND, "BlendSpace not built: %s" % bodies_report.get(CLONE, {}).get("errors"))
        report["locomotion"] = bodies_report[CLONE].get("locomotion")
        # Point only the Warden's body folder at the clone.
        data_path = ROOT / "Content/Data/FabAnimations.json"
        data = json.loads(data_path.read_text(encoding="utf-8"))
        data.setdefault("locomotionOverride", {})[BODY_FOLDER] = BLEND
        data_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        report["status"] = "pass"
        unreal.log("CIRE_WARDEN_SPEAR_LOCO_PASS clips=%d blend=%s" % (len(done), BLEND))
    except Exception as error:
        report["error"] = traceback.format_exc()
        unreal.log_error("CIRE_WARDEN_SPEAR_LOCO_FAIL " + str(error))
    finally:
        (ROOT / "Saved").mkdir(exist_ok=True)
        (ROOT / "Saved/WardenSpearLocomotion.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
