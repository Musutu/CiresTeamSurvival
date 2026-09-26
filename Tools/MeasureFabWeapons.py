"""UE 5.8 commandlet: measure the Fab weapon meshes chosen as prop upgrades and write Content/Data/WeaponGrips.fab.json.

For each mesh it reads the render vertices (LOD 0), finds the long axis (largest bounds extent) and slices the mesh
along it into 2 cm bins with the cross-section size per bin. From that profile:
  * blades/hafts (sword, dagger, axe, hammer, spear): the handle is the thin run at the end opposite the heavy end
    (blade/head); handle = centre of that run, axis points from the grip toward the heavy end, radius = half the
    median grip thickness (+ wrap), edge = the wider of the two cross axes at the heavy end;
  * bows: grip at the middle of the long axis, edge = the belly (the side the string is NOT on);
  * shields: thinnest axis is the face normal; the strap sits on the back face centre;
  * crossbow: stock is the long axis; grip at the rear third; muzzle end = edge.
Keys in the output are full object paths (the packs reuse armory names like SM_WarHammer).
paladin-hq: Tools/BuildFabPaladins.py WEAPONS gives single champions their own prop per token (the Polyphoria
plate-body champions carry the set's sword and heater shield), written to WeaponLoadouts.fab.json "profiles" with an
optional material spec (UCireChampionArt::ApplyMaterialSpec); every other profile keeps "overrides".
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/MeasureFabWeapons.py -unattended -nullrhi
Marker CIRE_FAB_WEAPONS_PASS; report Saved/FabWeapons.json.
"""
import json
import math
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
V1, V2 = "/Game/Medieval_Weapons/Meshes", "/Game/Medieval_Weapons_VOL2/Meshes/VOL2"
# token -> (mesh, kind, extra grip fields)
WEAPONS = {
    "legacy/Sword": (V1 + "/SM_Sword_1", "guard", {"tilt": 30}),
    "legacy/Shield": (V1 + "/SM_Kite_Wood_Shield", "shield", {"scale": .62}),
    "legacy/Bow": (V2 + "/SM_Bow", "bow", {}),
    "legacy/Arrow": (V2 + "/SM_Arrow_Var1", "ammo", {}),
    "legacy/Lance": (V1 + "/SM_Spear", "pole", {"carry": True, "scale": .8}),
    "WarHammer": (V1 + "/SM_WarHammer", "head", {"tilt": 28}),
    "Dagger": (V1 + "/SM_Dagger_1", "guard", {"tilt": 20}),
    "WarAxe": (V1 + "/SM_Axe_1", "head", {"tilt": 25}),
    "ThrowingAxe": (V2 + "/SM_ThrowingAxe", "head", {"tilt": 25}),
}


def load_profile_weapons():
    """paladin-hq: profile -> token -> (mesh, grip kind, extra grip fields, material spec by slot)."""
    ns = {"CIRE_IMPORT_ONLY": True, "__file__": str(ROOT / "Tools/BuildFabPaladins.py")}
    exec(compile((ROOT / "Tools/BuildFabPaladins.py").read_text(encoding="utf-8"), "BuildFabPaladins.py", "exec"), ns)
    return ns["WEAPONS"]


def verts(mesh):
    out = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)[0]
    n = mesh.get_num_sections(0)
    for s in range(1, n):
        out += u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, s)[0]
    return [(v.x, v.y, v.z) for v in out]


def measure(path, kind):
    mesh = u.load_asset(path)
    pts = verts(mesh)
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    ext = [hi[i] - lo[i] for i in range(3)]
    order = sorted(range(3), key=lambda i: -ext[i])
    L, W, T = order  # long, wide, thin axes
    unit = lambda i, s=1.0: [s if k == i else 0.0 for k in range(3)]
    info = {"extent": [round(e, 1) for e in ext], "long": "XYZ"[L], "wide": "XYZ"[W], "thin": "XYZ"[T]}
    if kind == "shield":
        centre = [(lo[i] + hi[i]) / 2 for i in range(3)]
        # back face: the side of the thin axis with fewer vertices far from the centre plane (the boss/face is convex)
        front = sum(1 for p in pts if p[T] > centre[T]); back = len(pts) - front
        sign = -1.0 if front >= back else 1.0
        handle = list(centre); handle[T] = (lo[T] if sign < 0 else hi[T]) - sign * 1.5
        return info, {"grip": "shield", "handle": [round(x, 1) for x in handle], "axis": unit(L), "edge": unit(T, -sign), "radiusCm": 1.3}
    # profile along the long axis
    bins = {}
    for p in pts:
        b = int((p[L] - lo[L]) // 2)
        e = bins.setdefault(b, [1e9, -1e9, 1e9, -1e9])
        e[0] = min(e[0], p[W]); e[1] = max(e[1], p[W]); e[2] = min(e[2], p[T]); e[3] = max(e[3], p[T])
    nb = max(bins) + 1
    width = [max(bins[b][1] - bins[b][0], bins[b][3] - bins[b][2]) if b in bins else 0.0 for b in range(nb)]
    thick = [min(bins[b][1] - bins[b][0], bins[b][3] - bins[b][2]) if b in bins else 0.0 for b in range(nb)]
    centre_wt = lambda b: ((bins[b][0] + bins[b][1]) / 2, (bins[b][2] + bins[b][3]) / 2)
    info["profile"] = [round(w, 1) for w in width]
    if kind in ("bow", "ammo"):
        mid = nb // 2
        handle = [0.0, 0.0, 0.0]; handle[L] = lo[L] + mid * 2 + 1
        cw, ct = centre_wt(mid) if mid in bins else (0, 0); handle[W], handle[T] = cw, ct
        if kind == "ammo":
            return info, {"grip": "ammo", "handle": [round(x, 1) for x in handle], "axis": unit(L), "edge": unit(W), "radiusCm": .6}
        # belly: the limbs curve away from the string; the grip bulges toward the belly side on the wide axis
        tips = [centre_wt(b)[0] for b in (min(bins), max(bins))]
        belly = 1.0 if cw > sum(tips) / 2 else -1.0
        r = max(1.8, min(3.2, thick[mid] / 2 + .4))
        return info, {"handle": [round(x, 1) for x in handle], "axis": unit(L), "edge": unit(W, belly), "radiusCm": round(r, 2)}
    # blade / pole: the widest slice is the cross-guard (sword, dagger) or the head (axe, hammer, spear tip).
    m = max(range(nb), key=lambda i: width[i])
    heavy_high = m > nb / 2
    near_end = nb - 1 if heavy_high else 0          # end nearest the widest slice
    far_end = 0 if heavy_high else nb - 1
    if kind == "pole":
        g = int(round(far_end + (m - far_end) * .38))       # spear: held a little behind the middle
    elif kind == "guard":
        # guard in the middle third of the length: the handle runs from the guard to the SHORT side.
        short_end = 0 if m < nb - 1 - m else nb - 1
        heavy_high = short_end == 0                    # blade points away from the handle
        g = int(round((m + short_end) / 2 + (1 if short_end == 0 else -1) * 0))
        far_end = short_end
    else:
        # head at one end (axe/hammer): the fist sits near the butt of the haft.
        g = int(round(far_end + (m - far_end) * .2))
    run = [x for x in range(min(g, far_end), max(g, far_end) + 1)]
    handle = [0.0, 0.0, 0.0]; handle[L] = lo[L] + g * 2 + 1
    handle[W], handle[T] = centre_wt(g) if g in bins else (0, 0)
    grip_thick = sorted(thick[x] for x in range(max(0, g - 2), min(nb, g + 3)) if x in bins)
    r = max(1.2, min(3.0, grip_thick[len(grip_thick) // 2] / 2 + .35)) if grip_thick else 1.8
    k = max(2, int(nb * .3))
    heavy_bins = [x for x in range(max(0, m - 3), min(nb, m + 4)) if x in bins]
    ww = max(bins[i][1] - bins[i][0] for i in heavy_bins); tt = max(bins[i][3] - bins[i][2] for i in heavy_bins)
    edge_axis = W if ww >= tt else T
    # head asymmetry (axe bit / hammer face): the edge points to the side where the head extends further from the haft
    hc = centre_wt(g) if g in bins else (0, 0)
    far_pos = max(bins[i][1] if edge_axis == W else bins[i][3] for i in heavy_bins) - (hc[0] if edge_axis == W else hc[1])
    far_neg = (hc[0] if edge_axis == W else hc[1]) - min(bins[i][0] if edge_axis == W else bins[i][2] for i in heavy_bins)
    edge = unit(edge_axis, 1.0 if far_pos >= far_neg else -1.0)
    info["grip_bins"] = [run[0], run[-1]] if run else []
    # WeaponGrips convention (matches the prototype sword): the axis exits the fist on the thumb side, the blade/head
    # hangs on the pinky side (-axis). Poles (spear/lance) carry the tip on +axis like SM_PrototypeLance.
    toward_heavy = 1.0 if heavy_high else -1.0
    sign = toward_heavy if kind == "pole" else -toward_heavy
    return info, {"handle": [round(x, 1) for x in handle], "axis": unit(L, sign), "edge": edge, "radiusCm": round(r, 2)}


def main():
    report, weapons, overrides = {}, {}, {}
    try:
        for token, (path, kind, extra) in WEAPONS.items():
            if not u.EditorAssetLibrary.does_asset_exist(path):
                report[token] = {"skipped": "pack not installed"}; continue
            try:
                info, grip = measure(path, kind)
            except Exception as error:
                report[token] = {"skipped": "could not read mesh: %s" % error}; continue
            scale = extra.pop("scale", None) if "scale" in extra else None
            grip.update(extra)
            obj = "%s.%s" % (path, path.rsplit("/", 1)[1])
            weapons[obj] = grip
            overrides[token] = {"mesh": obj, **({"scale": scale} if scale else {})}
            report[token] = {"mesh": obj, "kind": kind, **info, "grip": grip}
        profiles = {}
        for profile, tokens in load_profile_weapons().items():
            for token, (path, kind, extra, materials) in tokens.items():
                if not u.EditorAssetLibrary.does_asset_exist(path):
                    report["%s/%s" % (profile, token)] = {"skipped": "pack not installed"}; continue
                obj = "%s.%s" % (path, path.rsplit("/", 1)[1])
                extra = dict(extra)
                scale = extra.pop("scale", None)
                if obj not in weapons:
                    info, grip = measure(path, kind)
                    grip.update(extra)
                    weapons[obj] = grip
                    report["%s/%s" % (profile, token)] = {"mesh": obj, "kind": kind, **info, "grip": grip}
                profiles.setdefault(profile, {})[token] = {"mesh": obj, **({"scale": scale} if scale else {}),
                                                           **({"materials": materials} if materials else {})}
        (ROOT / "Content/Data/WeaponGrips.fab.json").write_text(json.dumps({
            "schemaVersion": 1,
            "description": "Grip data for Fab weapon meshes (Tools/MeasureFabWeapons.py); keys are full object paths. Same fields as WeaponGrips.json.",
            "weapons": weapons}, indent=1) + "\n", encoding="utf-8")
        (ROOT / "Content/Data/WeaponLoadouts.fab.json").write_text(json.dumps({
            "schemaVersion": 1,
            "description": "Fab weapon props that replace a WeaponLoadouts.json asset token when the pack mesh exists locally (Docs/FabIntegration.md). Otherwise the token's own prop is used.",
            "overrides": overrides, "profiles": profiles}, indent=1) + "\n", encoding="utf-8")
        u.log("CIRE_FAB_WEAPONS_PASS weapons=%d" % len(weapons))
    except Exception as error:
        import traceback
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_FAB_WEAPONS_FAIL " + str(error))
    finally:
        (ROOT / "Saved/FabWeapons.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
