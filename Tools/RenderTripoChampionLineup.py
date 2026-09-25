"""Render the Tripo champion lineup (tripo-races): the five new champion bodies at gameplay height in their idle,
again at the contact frame of their ChampionAttacks02 basic-attack clip, the sabercat mount, and the five Tripo
props at their loadout length. Editor Python in an unsaved temporary level (pattern: RenderTripoBatch03Gallery.py).

UnrealEditor.exe <project> -ExecCmds="py Tools/RenderTripoChampionLineup.py" -unattended -RenderOffscreen
Captures: Saved/TripoChampionLineup/<stamp>/*.png + report.json.
"""
from datetime import datetime, timezone
from pathlib import Path
import json
import math
import time
import traceback
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
ART = json.loads((PROJECT / "Content" / "Data" / "ChampionArt.tripo.json").read_text(encoding="utf-8"))
OUT = PROJECT / "Saved" / "TripoChampionLineup" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
OUT.mkdir(parents=True, exist_ok=True)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Tripo/_GalleryTemp/L_TripoChampionLineup")
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ORIGIN = unreal.Vector(0.0, 0.0, 0.0)
report = {"directory": str(OUT), "champions": {}, "captures": [], "errors": []}
spawned = []
CONTACT = {"slash": 1.3, "cast_a_spell": 2.4, "war_cry": 1.6, "attack_bow": 0.92, "attack_crossbow": 0.7}
MOTION_CLIP = {"tripo_gunblade": "slash", "tripo_witch_slayer": "cast_a_spell", "tripo_huntress": "slash",
               "artificer": "cast_a_spell", "aetheri_warden": "slash"}
SHOTS = [
    ("01_champions_idle", (700, 0, 190), (0, 0, 110), 70),
    ("02_champions_idle_close_left", (330, -250, 170), (0, -250, 120), 60),
    ("03_champions_idle_close_right", (330, 250, 170), (0, 250, 120), 60),
    ("04_champions_attack_contact", (-200, 0, 190), (-900, 0, 110), 70),
    ("05_sabercat_and_props", (700, 300, 260), (150, 250, 40), 60),
]


def V(x, y, z):
    return unreal.Vector(ORIGIN.x + x, ORIGIN.y + y, ORIGIN.z + z)


def spawn(asset, loc, yaw=0.0, scale=1.0):
    actor = ACTORS.spawn_actor_from_object(asset, loc, unreal.Rotator(0, 0, yaw))
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    spawned.append(actor)
    return actor


def bone(comp, name):
    return comp.get_socket_location(name)


def pose(comp, mesh, anim, t):
    comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    comp.set_update_animation_in_editor(True)
    try:
        comp.set_skinned_asset_and_update(mesh, True)
    except Exception:
        pass
    comp.set_animation(anim)
    comp.override_animation_data(anim, False, False, t, 0.0)
    comp.set_position(t, False)


def place(mesh, loc, height):
    actor = spawn(mesh, loc, 0.0, 1.0)
    comp = actor.skeletal_mesh_component
    raw = mesh.get_bounds().box_extent.z * 2
    scale = height / raw
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return actor, comp, scale


def face(actor, comp):
    fwd = (bone(comp, "ball_l") - bone(comp, "foot_l")) + (bone(comp, "ball_r") - bone(comp, "foot_r"))
    yaw = -math.degrees(math.atan2(fwd.y, fwd.x))
    actor.set_actor_rotation(unreal.Rotator(0, 0, yaw), False)
    return yaw


def setup():
    unreal.SystemLibrary.execute_console_command(WORLD, "r.TextureStreaming 0")
    floor = spawn(unreal.load_asset("/Engine/BasicShapes/Plane"), V(0, 0, -1), 0, 400.0)
    floor.static_mesh_component.set_material(0, unreal.load_asset("/Engine/EngineMaterials/DefaultMaterial"))
    sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, V(0, 0, 3000), unreal.Rotator(0, -42, 200))
    sun.light_component.set_editor_property("intensity", 6.0)
    spawned.append(sun)
    spawned.append(ACTORS.spawn_actor_from_class(unreal.SkyAtmosphere, V(0, 0, 0), unreal.Rotator(0, 0, 0)))
    sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, V(0, 0, 500), unreal.Rotator(0, 0, 0))
    sky.light_component.set_editor_property("intensity", 1.2)
    sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property("cubemap", unreal.load_asset("/Engine/MapTemplates/Sky/SunsetAmbientCubemap"))
    spawned.append(sky)
    rows = ART["champions"]
    y0 = -(len(rows) - 1) * 170 / 2
    for i, row in enumerate(rows):
        mesh = unreal.load_asset(row["mesh"].split(".")[0])
        entry = report["champions"].setdefault(row["profileId"], {})
        # Row A (x=0): idle. Row B (x=-900): the ChampionAttacks02 basic-attack clip at contact.
        for x, kind in ((0, "idle"), (-900, "attack")):
            actor, comp, scale = place(mesh, V(x, y0 + i * 170, 0), row["heightCm"])
            entry["yawFromFeet"] = round(face(actor, comp), 1)
            if kind == "idle":
                anim = unreal.load_asset(row["animations"]["idle"].split(".")[0])
                pose(comp, mesh, anim, 0.5)
            else:
                clip = MOTION_CLIP.get(row["loadoutPreset"], "slash")
                path = "/Game/Art/Characters/ChampionAttacks02/%s/A_%s_%s" % (row["attacksFolder"], row["attacksFolder"], clip)
                anim = unreal.load_asset(path)
                entry["attackClip"] = path if anim else None
                if anim:
                    pose(comp, mesh, anim, min(CONTACT[clip], float(anim.get_play_length()) - .01))
            entry["scale"] = round(scale, 4)
            origin, extent = actor.get_actor_bounds(False)
            entry[kind + "TopCm"] = round(origin.z + extent.z, 1)
        if row.get("mount"):
            mount = unreal.load_asset(row["mount"]["mesh"].split(".")[0])
            actor, comp, scale = place(mount, V(-60, y0 + len(rows) * 170 + 120, 0), row["mount"]["heightCm"])
            actor.set_actor_rotation(unreal.Rotator(0, 0, row["mount"].get("yaw", -90) + 180), False)
            report["mount"] = {"scale": round(scale, 4)}
    # Props lie in a line in front of the idle row at their loadout length.
    y = -400.0
    for name, prop in ART["props"].items():
        mesh = unreal.load_asset(prop["asset"].split(".")[0])
        size = mesh.get_bounds().box_extent.x * 2
        s = prop["lengthCm"] / size
        spawn(mesh, V(260, y, 0), 90.0, s)
        report.setdefault("props", {})[name] = {"scale": round(s, 3)}
        y += 200


def capture(tag, loc, look_at, fov=55.0):
    rt = unreal.RenderingLibrary.create_render_target2d(WORLD, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    direction = look_at - loc
    rot = unreal.MathLibrary.make_rot_from_x(direction)
    cap = ACTORS.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
    comp = cap.capture_component2d
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("fov_angle", fov)
    pp = comp.get_editor_property("post_process_settings")
    pp.set_editor_property("override_auto_exposure_method", True)
    pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    pp.set_editor_property("override_auto_exposure_bias", True)
    pp.set_editor_property("auto_exposure_bias", BIAS.get(tag, -1.0))
    comp.set_editor_property("post_process_settings", pp)
    comp.capture_scene()
    return (tag, rt, cap)


def export(pending):
    tag, rt, cap = pending
    unreal.RenderingLibrary.export_render_target(WORLD, rt, str(OUT), tag + ".png")
    cap.destroy_actor()
    report["captures"].append(str(OUT / (tag + ".png")))


BIAS = {}
state = {"start": time.monotonic(), "stage": 0, "handle": None}


def tick(_delta):
    try:
        elapsed = time.monotonic() - state["start"]
        if state["stage"] == 0:
            setup()
            state["stage"] = 1
            state["start"] = time.monotonic()
        elif state["stage"] == 1 and elapsed > 90:
            state["stage"] = 2
            state["shot"] = 0
            state["pending"] = None
            state["wait"] = 0
        elif state["stage"] == 2:
            state["wait"] += 1
            if state["pending"] is not None and state["wait"] >= 3:
                export(state["pending"])
                state["pending"] = None
            elif state["pending"] is None:
                if state["shot"] >= len(SHOTS):
                    finish()
                    return
                tag, loc, look, fov = SHOTS[state["shot"]]
                state["pending"] = capture(tag, V(*loc), V(*look), fov)
                state["shot"] += 1
                state["wait"] = 0
    except Exception as exc:
        report["errors"].append("%s\n%s" % (exc, traceback.format_exc()))
        finish()


def finish():
    unreal.unregister_slate_post_tick_callback(state["handle"])
    for actor in spawned:
        try:
            actor.destroy_actor()
        except Exception:
            pass
    (OUT / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_TRIPO_CHAMPION_LINEUP %s captures=%d dir=%s" % (
        "PASS" if not report["errors"] and len(report["captures"]) == len(SHOTS) else "FAIL",
        len(report["captures"]), OUT))
    unreal.SystemLibrary.quit_editor()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
