"""Render the Tripo town statics at yaw 0 from +X and from +Y to decide each slot's facade rotation.

UnrealEditor.exe <project> -ExecCmds="py Tools/RenderTripoFacingCheck.py" -unattended -RenderOffscreen
Town slots expect the street facade on local +X. Writes Saved/TripoFacingCheck/<stamp>/{plusX,plusY}.png.
"""
from datetime import datetime, timezone
from pathlib import Path
import json
import time
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
INTEGRATION = json.loads((PROJECT / "Saved" / "TripoBatch03Integration.json").read_text(encoding="utf-8"))
OUT = PROJECT / "Saved" / "TripoFacingCheck" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
OUT.mkdir(parents=True, exist_ok=True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Tripo/_GalleryTemp/L_TripoFacing")
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
NAMES = ["HouseA", "HouseB", "HouseC", "CastleKeep", "Watchtower", "MarketStallA", "MarketStallB", "Well", "Cart", "Barricade", "Fountain", "Banner"]
state = {"t": time.monotonic(), "stage": 0, "pending": None}


def setup():
    unreal.SystemLibrary.execute_console_command(WORLD, "r.TextureStreaming 0")
    floor = ACTORS.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Plane"), unreal.Vector(0, 0, -1), unreal.Rotator(0, 0, 0))
    floor.set_actor_scale3d(unreal.Vector(1000, 1000, 1))
    sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, -45, 225))
    sun.light_component.set_editor_property("intensity", 6.0)
    sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
    sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property("cubemap", unreal.load_asset("/Engine/MapTemplates/Sky/SunsetAmbientCubemap"))
    ACTORS.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    # uniform display height so every asset is readable in one row
    y = 0.0
    for name in NAMES:
        info = INTEGRATION["statics"][name]
        mesh = unreal.load_asset(info["mesh"])
        bb = mesh.get_bounding_box()
        size = bb.max - bb.min
        s = 450.0 / max(size.x, size.y)
        a = ACTORS.spawn_actor_from_object(mesh, unreal.Vector(0, y, 0), unreal.Rotator(0, 0, 0))
        a.set_actor_scale3d(unreal.Vector(s, s, s))
        label = ACTORS.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(300, y, 10), unreal.Rotator(90, 0, 180))
        label.text_render.set_editor_property("text", name)
        label.text_render.set_editor_property("world_size", 60)
        y += 600.0


def capture(tag, loc, look):
    rt = unreal.RenderingLibrary.create_render_target2d(WORLD, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = ACTORS.spawn_actor_from_class(unreal.SceneCapture2D, loc, unreal.MathLibrary.make_rot_from_x(look - loc))
    comp = cap.capture_component2d
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("fov_angle", 80)
    pp = comp.get_editor_property("post_process_settings")
    pp.set_editor_property("override_auto_exposure_method", True)
    pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    pp.set_editor_property("override_auto_exposure_bias", True)
    pp.set_editor_property("auto_exposure_bias", -1.0)
    comp.set_editor_property("post_process_settings", pp)
    comp.capture_scene()
    return tag, rt


SHOTS = [("plusX_left", unreal.Vector(2600, 1500, 900), unreal.Vector(0, 1500, 150)),
         ("plusX_right", unreal.Vector(2600, 5100, 900), unreal.Vector(0, 5100, 150))]


def tick(_d):
    if state["stage"] == 0:
        setup(); state["stage"] = 1; state["t"] = time.monotonic(); return
    if state["stage"] == 1 and time.monotonic() - state["t"] > 90:
        state["stage"] = 2; state["i"] = 0; state["wait"] = 0; return
    if state["stage"] == 2:
        state["wait"] += 1
        if state["pending"] and state["wait"] >= 3:
            tag, rt = state["pending"]
            unreal.RenderingLibrary.export_render_target(WORLD, rt, str(OUT), tag + ".png")
            state["pending"] = None
        elif not state["pending"]:
            if state["i"] >= len(SHOTS):
                unreal.log("CIRE_TRIPO_FACING_CHECK dir=%s" % OUT)
                unreal.SystemLibrary.quit_editor(); state["stage"] = 3; return
            state["pending"] = capture(*SHOTS[state["i"]]); state["i"] += 1; state["wait"] = 0


unreal.register_slate_post_tick_callback(tick)
