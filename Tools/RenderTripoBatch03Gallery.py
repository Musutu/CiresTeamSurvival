"""Render a verification gallery of the Tripo batch 03 monsters and town kit (editor Python, unsaved staging).

UnrealEditor.exe <project> -ExecCmds="py Tools/RenderTripoBatch03Gallery.py" -unattended -RenderOffscreen
Spawns the integrated assets far from the playable Citadel area, poses the monsters in their idle clip, captures
1920x1080 PNGs with SceneCapture2D into Saved/TripoBatch03Gallery/<stamp>/, measures scaled bounds and the
character facing (toe direction), traces the gate passages, writes report.json and quits without saving the map.
"""
from datetime import datetime, timezone
from pathlib import Path
import json
import math
import time
import traceback
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
INTEGRATION = json.loads((PROJECT / "Saved" / "TripoBatch03Integration.json").read_text(encoding="utf-8"))
OUT = PROJECT / "Saved" / "TripoBatch03Gallery" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
OUT.mkdir(parents=True, exist_ok=True)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TEMP_LEVEL = "/Game/Tripo/_GalleryTemp/L_TripoBatch03Gallery"
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(TEMP_LEVEL)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ORIGIN = unreal.Vector(0.0, 0.0, 0.0)
TOWN = (-6000.0, 0.0)
# Gameplay height targets (cm) before archetype scale; bosses are authored for their archetype multiplier.
TARGET_HEIGHT = {"HollowInfantry": 180, "HollowInfantryB": 182, "IronboundBruiser": 185, "HollowShieldbearer": 185,
                 "IronboundBruiserV2": 185, "HollowShieldbearerV2": 185,
                 "BlightCaster": 178, "BlightCasterB": 185, "BarbedHunter": 178, "BarbedHunterB": 180,
                 "GravemawPackLeader": 190, "HollowSiegebreaker": 200}
ARCHETYPE_SCALE = {"IronboundBruiser": 1.1, "HollowShieldbearer": 1.12, "IronboundBruiserV2": 1.1,
                   "HollowShieldbearerV2": 1.12, "GravemawPackLeader": 1.7,
                   "HollowSiegebreaker": 1.35}
report = {"directory": str(OUT), "monsters": {}, "statics": {}, "captures": [], "passages": {}, "errors": []}
spawned = []
town_actors = {}


def V(x, y, z):
    return unreal.Vector(ORIGIN.x + x, ORIGIN.y + y, ORIGIN.z + z)


def spawn(asset, loc, yaw=0.0, scale=1.0):
    actor = ACTORS.spawn_actor_from_object(asset, loc, unreal.Rotator(0, 0, yaw))
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    spawned.append(actor)
    return actor


def bone(comp, name):
    return comp.get_socket_location(name)


def setup():
    unreal.SystemLibrary.execute_console_command(WORLD, "r.TextureStreaming 0")
    floor = spawn(unreal.load_asset("/Engine/BasicShapes/Plane"), V(0, 0, -1), 0, 1200.0)
    floor.static_mesh_component.set_material(0, unreal.load_asset("/Engine/EngineMaterials/DefaultMaterial"))
    sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, V(0, 0, 3000), unreal.Rotator(0, -42, 200))
    sun.light_component.set_editor_property("intensity", 6.0)
    atmosphere = ACTORS.spawn_actor_from_class(unreal.SkyAtmosphere, V(0, 0, 0), unreal.Rotator(0, 0, 0))
    spawned.append(atmosphere)
    spawned.append(sun)
    sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, V(0, 0, 500), unreal.Rotator(0, 0, 0))
    sky.light_component.set_editor_property("intensity", 1.2)
    sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property("cubemap", unreal.load_asset("/Engine/MapTemplates/Sky/SunsetAmbientCubemap"))
    spawned.append(sky)

    # Monster line-up along Y at X=0, facing +X (toward the cameras).
    names = list(INTEGRATION["skeletal"])
    y = -(len(names) - 1) * 210 / 2
    for name in names:
        info = INTEGRATION["skeletal"][name]
        mesh = unreal.load_asset(info["mesh"])
        scale = TARGET_HEIGHT[name] / info["rawHeightCm"] * ARCHETYPE_SCALE.get(name, 1.0)
        actor = spawn(mesh, V(0, y, 0), 0.0, scale)
        comp = actor.skeletal_mesh_component
        facing = None
        try:
            fwd = (bone(comp, "ball_l") - bone(comp, "foot_l")) + (bone(comp, "ball_r") - bone(comp, "foot_r"))
            facing = -math.degrees(math.atan2(fwd.y, fwd.x))
            actor.set_actor_rotation(unreal.Rotator(0, 0, facing), False)
        except Exception as exc:
            report["errors"].append("%s facing: %s" % (name, exc))
        idle = unreal.load_asset(info["animations"].get("idle", ""))
        posed = False
        if idle:
            before = bone(comp, "hand_r")
            data = unreal.SingleAnimationPlayData()
            data.set_editor_property("anim_to_play", idle)
            data.set_editor_property("saved_position", 0.5)
            data.set_editor_property("saved_play_rate", 1.0)
            data.set_editor_property("saved_looping", True)
            data.set_editor_property("saved_playing", False)
            comp.set_editor_property("animation_data", data)
            comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            comp.set_update_animation_in_editor(True)
            try:
                comp.set_skinned_asset_and_update(mesh, True)
            except Exception as exc:
                report["errors"].append("%s reinit: %s" % (name, exc))
            comp.set_animation(idle)
            comp.override_animation_data(idle, True, True, 0.5, 1.0)
            comp.set_position(0.5, False)
            after = bone(comp, "hand_r")
            posed = (after - before).length() > 5.0
        bones = [str(b) for b in comp.get_all_socket_names()]
        report["monsters"][name] = {"posedIdle": posed, "meshScale": round(scale / ARCHETYPE_SCALE.get(name, 1.0), 4),
                                    "archetypeScale": ARCHETYPE_SCALE.get(name, 1.0),
                                    "facingYaw": round(facing, 2) if facing is not None else None,
                                    "hasBones": {b: b in bones for b in ("hand_r", "hand_l", "spine_03", "head", "foot_l", "ball_l")},
                                    "boneCount": len(bones)}
        y += 210 + (80 if ARCHETYPE_SCALE.get(name, 1) > 1.3 else 0)

    # Town kit: large pieces on a back row, props on a front row (+X is the facade side).
    row = {"large": [], "props": []}
    for name, info in INTEGRATION["statics"].items():
        (row["large"] if info["collision"] == "complex" and name not in ("MarketStallA", "MarketStallB", "Barricade") else row["props"]).append(name)
    for key, x0, gap in (("large", TOWN[0] - 6000, 700), ("props", TOWN[0], 250)):
        y = 0.0
        placed = []
        for name in row[key]:
            mesh = unreal.load_asset(INTEGRATION["statics"][name]["mesh"])
            actor = spawn(mesh, V(x0, 0, 0), 0.0, INTEGRATION["statics"][name]["suggestedScale"])
            origin, extent = actor.get_actor_bounds(False)
            placed.append((name, actor, extent))
        total = sum(e.y * 2 for _, _, e in placed) + gap * (len(placed) - 1)
        y = -total / 2
        for name, actor, extent in placed:
            actor.set_actor_location(V(x0, y + extent.y, 0), False, False)
            y += extent.y * 2 + gap
            origin, ext = actor.get_actor_bounds(False)
            town_actors[name] = actor


def measure():
    for name, actor in town_actors.items():
        origin, ext = actor.get_actor_bounds(False)
        report["statics"][name] = {"sizeCm": [round(ext.x * 2), round(ext.y * 2), round(ext.z * 2)],
                                   "bottomCm": round(origin.z - ext.z - ORIGIN.z, 1)}


FOOTPRINT = {"CastleGate": (1366, 2132), "Gatehouse": (1046, 2003)}


def trace_passage(name, slot_width, slot_height):
    """Emulate the town loader's footprint fit for a gate slot and line-trace its road passage (local X, Y=0)."""
    info = INTEGRATION["statics"].get(name)
    if not info:
        return
    mesh = unreal.load_asset(info["mesh"])
    bb = mesh.get_bounding_box()
    raw = bb.max - bb.min
    results = {}
    for yaw in (0, 90):
        sx, sy = (raw.x, raw.y) if yaw == 0 else (raw.y, raw.x)
        fit = min(FOOTPRINT[name][0] / sx, FOOTPRINT[name][1] / sy)
        actor = spawn(mesh, V(20000 + len(report["passages"]) * 6000, 0, 0), yaw, fit)
        loc = actor.get_actor_location()
        hits = []
        for dy in (-slot_width / 2 + 100, 0, slot_width / 2 - 100):
            for z in (60, 250, slot_height - 50):
                start = unreal.Vector(loc.x - 4000, loc.y + dy, loc.z + z)
                end = unreal.Vector(loc.x + 4000, loc.y + dy, loc.z + z)
                hit = unreal.SystemLibrary.line_trace_single(WORLD, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                             True, [], unreal.DrawDebugTrace.NONE, True)
                hits.append({"dy": dy, "z": z, "blocked": hit is not None and bool(hit.to_tuple()[0]) if hasattr(hit, "to_tuple") else bool(hit)})
        results["yaw%d" % yaw] = {"fitScale": round(fit, 3), "blockedRays": sum(1 for h in hits if h["blocked"]),
                                  "rays": len(hits), "detail": hits}
        actor.destroy_actor()
    report["passages"][name] = results


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


SHOTS = [
    ("01_monsters_lineup", (1250, 0, 250), (0, 0, 150), 78),
    ("02_monsters_left", (700, -650, 200), (0, -650, 120), 60),
    ("03_monsters_right", (800, 800, 260), (0, 800, 190), 60),
    ("04_town_buildings_overview", (4000, 0, 4500), (-12000, 0, 800), 70),
    ("05_town_buildings_left", (-12000 + 5500, -6500, 1400), (-12000, -6500, 900), 75),
    ("06_town_buildings_right", (-12000 + 5500, 6500, 1400), (-12000, 6500, 900), 75),
    ("07_town_props_row", (-6000 + 1500, 0, 600), (-6000, 0, 180), 75),
    ("08_town_side_overview", (-3000, -16000, 5500), (-9000, 0, 0), 60),
]
BIAS = {}
state = {"start": time.monotonic(), "stage": 0, "handle": None}


def tick(_delta):
    try:
        elapsed = time.monotonic() - state["start"]
        if state["stage"] == 0:
            setup()
            state["stage"] = 1
            state["start"] = time.monotonic()
        elif state["stage"] == 1 and elapsed > 150:
            measure()
            trace_passage("CastleGate", 900, 500)
            trace_passage("Gatehouse", 800, 500)
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
    unreal.log("CIRE_TRIPO_BATCH03_GALLERY %s captures=%d dir=%s" % (
        "PASS" if not report["errors"] and len(report["captures"]) == len(SHOTS) else "FAIL",
        len(report["captures"]), OUT))
    unreal.SystemLibrary.quit_editor()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
