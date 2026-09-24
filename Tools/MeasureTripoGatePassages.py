"""Measure the open road passage of the Tripo gate meshes as the town loader would place them.

UnrealEditor.exe <project> -ExecCmds="py Tools/MeasureTripoGatePassages.py" -unattended -RenderOffscreen
For each gate candidate and yaw, fits the mesh into the slot footprint exactly like CireEnvironmentProps
(uniform min of X/Y/Z ratios, centred, seated on the ground), line-traces along local X (the march road) with
complex collision across Y at several heights, and writes the widest clear interval to
Saved/TripoGatePassages.json. Uses an unsaved temporary level; quits when done.
"""
from pathlib import Path
import json
import time
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Tripo/_GalleryTemp/L_TripoGatePassages")
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
GATES = {  # slot -> (mesh, footprint X,Y,Z, required width, required height)
    "castle_gate": ("/Game/Tripo/Town/CastleGate/CTS_Town_CastleGate", (1366, 2132, 2378), 900, 500),
    "gatehouse": ("/Game/Tripo/Landmarks/Gatehouse/CTS_Landmark_Gatehouse", (1046, 2003, 1834), 800, 500),
    "castle_gate_v2": ("/Game/Tripo/Town/CastleGateV2/CTS_Town_CastleGateV2", (1366, 2132, 2378), 900, 500),
    "gatehouse_v2": ("/Game/Tripo/Town/GatehouseV2/CTS_Town_GatehouseV2", (1046, 2003, 1834), 800, 500),
    "gatehouse_with_castle_gate_v2": ("/Game/Tripo/Town/CastleGateV2/CTS_Town_CastleGateV2", (1046, 2003, 1834), 800, 500),
    "castle_gate_with_gatehouse_v2": ("/Game/Tripo/Town/GatehouseV2/CTS_Town_GatehouseV2", (1366, 2132, 2378), 900, 500),
}
report = {}
state = {"t": time.monotonic(), "done": False}


def measure():
    x0 = 0.0
    for slot, (path, fp, need_w, need_h) in GATES.items():
        mesh = unreal.load_asset(path)
        bb = mesh.get_bounding_box()
        size = bb.max - bb.min
        report[slot] = {}
        for yaw in (0, 90):
            sx, sy = (size.x, size.y) if yaw == 0 else (size.y, size.x)
            fit = min(fp[0] / sx, fp[1] / sy, fp[2] / size.z)
            actor = ACTORS.spawn_actor_from_object(mesh, unreal.Vector(x0, 0, 0), unreal.Rotator(0, 0, yaw))
            actor.set_actor_scale3d(unreal.Vector(fit, fit, fit))
            o, e = actor.get_actor_bounds(False)
            actor.set_actor_location(unreal.Vector(x0 - (o.x - x0), -o.y, -(o.z - e.z)), False, False)
            rows = {}
            for z in (60, 250, need_h - 30):
                clear = []
                for i in range(-44, 45):
                    dy = i * 25.0
                    hit = unreal.SystemLibrary.line_trace_single(
                        WORLD, unreal.Vector(x0 - 5000, dy, z), unreal.Vector(x0 + 5000, dy, z),
                        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
                    blocked = bool(hit.to_tuple()[0]) if hasattr(hit, "to_tuple") else bool(hit)
                    clear.append((dy, not blocked))
                # widest clear run containing y=0
                best, run = 0.0, []
                for dy, ok in clear:
                    run = run + [dy] if ok else []
                    if run and run[0] <= 0 <= run[-1]:
                        best = max(best, run[-1] - run[0] + 25)
                rows[str(z)] = best
            report[slot]["yaw%d" % yaw] = {"fit": round(fit, 3), "clearWidthAtHeightCm": rows,
                                           "passes": all(w >= need_w for w in rows.values()),
                                           "requiredWidthCm": need_w, "requiredHeightCm": need_h}
            actor.destroy_actor()
            x0 += 12000


def tick(_d):
    if state["done"] or time.monotonic() - state["t"] < 60:
        return
    state["done"] = True
    try:
        measure()
    except Exception as exc:
        report["error"] = str(exc)
    (PROJECT / "Saved" / "TripoGatePassages.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("CIRE_TRIPO_GATE_PASSAGES done")
    unreal.SystemLibrary.quit_editor()


unreal.register_slate_post_tick_callback(tick)
