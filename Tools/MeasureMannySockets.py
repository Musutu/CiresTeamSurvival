"""UE 5.8 commandlet: read how the Fab animation packs hold their weapons and write Content/Data/WeaponSockets.json.

The GDH bundle, Gun & Sword and Crossbow clips were authored on the UE5 mannequin with the demo weapon on a socket of
the demo skeleton (SwordShield "Sword" / "Shield", Spear "Weapon_r", AxeV1 "Axe_r", DualSword "Sword_r" / "Sword_l",
...). For every animation set and hand this tool combines that socket with the demo weapon mesh, measured with
Tools/MeasureFabWeapons.py (handle point, handle axis, edge), into one grip frame in the mannequin's bone space:
  point = the grip point (cm), tip = handle -> business end (blade, head, spear point), edge = blade edge / axe bit;
  shields: point = strap centre, tip = up (the wide end), edge = face out (away from the forearm).
Two-handed sets also get the second hand: the off-hand bone relative to the main hand bone, averaged over the set's
idle clip (the spread over the attack clips is reported, not used).
The runtime (Source/CiresTeamSurvival/CireWeaponSockets.cpp) maps these frames onto every champion body through the
body's measured retarget rotation, so the weapon swings the way the clip was authored. Only numbers are written
(no pack content), so the output is committed; the packs stay local.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/MeasureMannySockets.py -unattended -nullrhi
Marker CIRE_MANNY_SOCKETS_PASS; report Saved/MannySockets.json.
"""
import json
import math
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
G = "/Game/GDHBundle"
GS = "/Game/Gun_and_Sword/Demo/Characters/Mannequins/Meshes"
CB = "/Game/CrossbowPackAnim"
# set -> hand -> (demo skeleton or skeletal mesh, socket, demo weapon mesh, MeasureFabWeapons kind)
REFS = {
    "sword_shield": {
        "hand_r": (G + "/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SK_Mannequin", "Sword", G + "/SwordShield/DEMO/StaticMesh/SM_Sword", "guard"),
        "hand_l": (G + "/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SK_Mannequin", "Shield", G + "/SwordShield/DEMO/StaticMesh/Shield_Heater", "shield"),
    },
    "one_hand": {"hand_r": (G + "/OneHandSword/DEMO/Characters_Sword/Mannequins/Meshes/SK_Mannequin_Sword", "Weapon", G + "/OneHandSword/DEMO/SampleSword/SM_Sword", "guard")},
    "two_hand": {"hand_r": (G + "/TwoHandSword/DEMO/Character_TwoHandedSword/Mannequins/Meshes/SK_Mannequin", "Weapon", G + "/TwoHandSword/DEMO/DemoSword/1/SM_Sword", "guard")},
    "dual": {
        "hand_r": (G + "/DualSword/DEMO/DualSwordCharacters/Mannequins/Meshes/SK_Mannequin", "Sword_r", G + "/DualSword/DEMO/StaticMesh/SM_Sword", "guard"),
        "hand_l": (G + "/DualSword/DEMO/DualSwordCharacters/Mannequins/Meshes/SK_Mannequin", "Sword_l", G + "/DualSword/DEMO/StaticMesh/SM_Sword", "guard"),
    },
    "spear": {"hand_r": (G + "/Spear/DEMO/Character_Spear/Mannequins/Meshes/SK_Mannequin", "Weapon_r", G + "/Spear/DEMO/StaticMesh/SM_Spear", "pole")},
    "axe": {"hand_r": (G + "/AxeV1/DEMO/Character/Mannequins/Meshes/SK_Mannequin", "Axe_r", G + "/AxeV1/DEMO/StaticMesh/Axe_small", "head")},
    "throw": {
        "hand_r": (G + "/DaggerCombatAnimationV1/DEMO/Character_Unarmed/Mannequins/Meshes/SK_Mannequin", "Dagger_r", G + "/DaggerCombatAnimationV1/DEMO/StaticMesh/SM_Dagger", "guard"),
        "hand_l": (G + "/DaggerCombatAnimationV1/DEMO/Character_Unarmed/Mannequins/Meshes/SK_Mannequin", "Dagger_l", G + "/DaggerCombatAnimationV1/DEMO/StaticMesh/SM_Dagger", "guard"),
    },
    "greatsword": {"hand_r": (G + "/GreatSword/DEMO/Character_GreatSword/Mannequins/Meshes/SK_Mannequin", "GreatSword", G + "/GreatSword/DEMO/StaticMesh/SM_GreatSword", "guard")},
    "katana": {"hand_r": (G + "/Katana/DEMO/Character/Mannequins/Meshes/SK_Mannequin", "Katana", G + "/Katana/DEMO/Katana/StaticMesh/SM_Katana", "guard")},
    # Gun & Sword (the gunblade): the demo SKM_Manny carries the sword and the pistol on sockets of the helper bones
    # Sword_Weapon_R / Gun_Weapon_L, fixed children of the hands in every clip; re-expressed in the hand below.
    "gun": {
        "hand_r": (GS + "/SKM_Manny", "Sword_Weapon_RSocket", GS + "/Sword", "guard"),
        "hand_l": (GS + "/SKM_Manny", "Gun_Weapon_LSocket", GS + "/Gun", "pistol"),
    },
    # Crossbow pack: the crossbow rides the animated w_crossbow bone. The LEFT hand holds its fore-end rigidly through
    # the attack and hit clips (the right hand moves between trigger and reload), so the authored grip is the left
    # hand's stock hold, averaged over the attack clips. No socket: the demo mesh sits on the bone itself.
    "crossbow": {"hand_l": (None, "w_crossbow", CB + "/Demo/Crossbow/Crossbow", "stock")},
}
# Weapon kinds measured from the demo hand instead of MeasureFabWeapons (the hand defines the grip of a gun):
#   pistol: tip = up the pistol grip (pinky -> index knuckle), edge = muzzle (the long axis away from the hand);
#   stock:  tip = muzzle (along the supporting knuckle line, index finger forward; the limbs are checked to sit there),
#           edge = up (from the supporting hand toward the mass).
# Frame "grip": "stock" tells the runtime to take the prop's fore-grip (WeaponGrips offHand / offAxis) with its handle
# axis as "up" (CireWeaponSockets::PropGrip).
# Sets whose second hand holds the same weapon: idle clip (the relation) and attack clips (spread report).
TWO_HANDED = ["two_hand", "spear", "greatsword"]
FAB_MAP = ROOT / "Art/Fab/FabAnimMap.json"
# Carried staffs (no pack holds a staff: the casters play the unarmed SpellCombat set). Held low at the side in the main
# hand, off the shoulder line and a little outward, upright with a slight forward lean; the second hand lets go so the
# staff never crosses the torso or the legs (Eric: "staffs pass through the body while moving"). carryAt = [forward,
# inward, drop] in arm lengths (negative inward = outward), carryUp = (forward, outward, up) weights. Overrides the
# WeaponGrips.json carry of these meshes (CireGrip).
STAFF_CARRY = {"carryAt": [0.22, -0.12, 0.74], "carryUp": [0.1, 0.0, 1.0], "oneHand": True}
CARRY = {m: STAFF_CARRY for m in ("SM_ArcaneStaff", "SM_RiftStaff", "SM_EmberStaff", "SM_GroveStaff", "SM_LanternStaff", "SM_AetherStaff")}


def load_measure():
    """MeasureFabWeapons.measure / verts without running its main()."""
    text = (ROOT / "Tools/MeasureFabWeapons.py").read_text(encoding="utf-8")
    text = text[: text.rindex("\nmain()")]
    ns = {"__file__": str(ROOT / "Tools/MeasureFabWeapons.py")}
    exec(compile(text, "MeasureFabWeapons.py", "exec"), ns)
    render_verts = ns["verts"]

    def verts(mesh):
        """Render vertices, or the editor mesh description when the demo mesh has no CPU access (Shield_Heater)."""
        try:
            return render_verts(mesh)
        except Exception:
            d = mesh.get_static_mesh_description(0)
            return [(p.x, p.y, p.z) for p in (d.get_vertex_position(u.VertexID(i)) for i in range(d.get_vertex_count()))]
    ns["verts"] = verts
    return ns["measure"]


def asset_mesh(path):
    """The skeletal mesh at path, or one using the skeleton at path (demo folders keep them side by side)."""
    a = u.load_asset(path)
    if isinstance(a, u.SkeletalMesh):
        return a
    if isinstance(a, u.Skeleton):
        reg = u.AssetRegistryHelpers.get_asset_registry()
        folder = path.rsplit("/", 1)[0]
        for d in reg.get_assets_by_path(folder, recursive=False):
            m = d.get_asset()
            if isinstance(m, u.SkeletalMesh) and m.get_editor_property("skeleton") == a:
                return m
    raise RuntimeError("no skeletal mesh for " + path)


def socket(mesh, name):
    for i in range(mesh.num_sockets()):
        s = mesh.get_socket_by_index(i)
        if str(s.get_editor_property("socket_name")).lower() == name.lower():
            return s
    raise RuntimeError("socket %s missing on %s" % (name, mesh.get_path_name()))


def vec(v):
    return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]


def V(a):
    return u.Vector(float(a[0]), float(a[1]), float(a[2]))


def far_tip(mesh_asset, handle, axis):
    """handle -> the far end of the handle axis (blade, head, spear tip), from the static mesh bounds."""
    b = mesh_asset.get_bounding_box()
    far, near = -1e9, 1e9
    for i in range(8):
        c = [b.max.x if i & 1 else b.min.x, b.max.y if i & 2 else b.min.y, b.max.z if i & 4 else b.min.z]
        d = sum((c[k] - handle[k]) * axis[k] for k in range(3))
        far, near = max(far, d), min(near, d)
    return axis if far >= -near else [-x for x in axis]


def set_clips(fab, set_name, bone):
    """The set's clips whose skeleton has bone (attack clips first)."""
    out = []
    for c in sorted(fab["clips"].values(), key=lambda c: c.get("kind") != "attack"):
        if c.get("set") != set_name:
            continue
        a = u.load_asset(c["path"].split(".")[0])
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, u.AnimPoseEvaluationOptions()) if a else None
        if pose and bone in [str(n) for n in u.AnimPoseExtensions.get_bone_names(pose)]:
            out.append((c, a))
    return out


def bone_in_hand(fab, set_name, bone, hand, report_key, report):
    """bone relative to hand, averaged over the set's attack clips; the spread is reported. The helper bones are fixed
    children of the hand (Gun & Sword) or held rigidly by it (the crossbow in the left hand)."""
    samples = []
    for c, a in set_clips(fab, set_name, bone):
        if c.get("kind") != "attack":
            continue
        for i in range(6):
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, a.get_play_length() * (i + .5) / 6, u.AnimPoseEvaluationOptions())
            samples.append(u.MathLibrary.make_relative_transform(u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD),
                                                                 u.AnimPoseExtensions.get_bone_pose(pose, hand, u.AnimPoseSpaces.WORLD)))
    if not samples:
        raise RuntimeError("no %s attack clip with bone %s" % (set_name, bone))
    n = len(samples)
    mean = u.Vector(sum(t.translation.x for t in samples) / n, sum(t.translation.y for t in samples) / n, sum(t.translation.z for t in samples) / n)
    q0 = samples[0].rotation
    acc = [0.0, 0.0, 0.0, 0.0]
    for t in samples:
        q = t.rotation
        sign = 1.0 if (q.x * q0.x + q.y * q0.y + q.z * q0.z + q.w * q0.w) >= 0 else -1.0
        acc = [acc[0] + sign * q.x, acc[1] + sign * q.y, acc[2] + sign * q.z, acc[3] + sign * q.w]
    l = math.sqrt(sum(x * x for x in acc)) or 1.0
    rot = u.Quat(acc[0] / l, acc[1] / l, acc[2] / l, acc[3] / l)
    spread_cm = max(math.sqrt((t.translation.x - mean.x) ** 2 + (t.translation.y - mean.y) ** 2 + (t.translation.z - mean.z) ** 2) for t in samples)
    spread_deg = max(math.degrees(2 * math.acos(min(1.0, abs(t.rotation.x * rot.x + t.rotation.y * rot.y + t.rotation.z * rot.z + t.rotation.w * rot.w)))) for t in samples)
    report[report_key] = {"weaponBone": bone, "hand": hand, "samples": n, "spreadCm": round(spread_cm, 2), "spreadDeg": round(spread_deg, 2)}
    return u.Transform(mean, rot.rotator(), u.Vector(1, 1, 1))


def hand_grip(fab, set_name, bone, hand, sock):
    """Demo hand in weapon-mesh space (pistol / stock kinds): fist centre and the pinky -> index knuckle line."""
    c, a = [x for x in set_clips(fab, set_name, bone) if x[0].get("kind") == "attack"][0]
    pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, a.get_play_length() * .5, u.AnimPoseEvaluationOptions())
    h = u.AnimPoseExtensions.get_bone_pose(pose, hand, u.AnimPoseSpaces.WORLD)
    mesh_world = u.MathLibrary.compose_transforms(sock, h)  # weapon mesh -> world (sock is already in hand space)
    side = hand[-2:]
    p = {}
    for b in (hand, "index_01" + side, "middle_01" + side, "pinky_01" + side):
        x = mesh_world.inverse_transform_location(u.AnimPoseExtensions.get_bone_pose(pose, b, u.AnimPoseSpaces.WORLD).translation)
        p[b] = [x.x, x.y, x.z]
    knuckles = [(p["index_01" + side][k] + p["middle_01" + side][k] + p["pinky_01" + side][k]) / 3 for k in range(3)]
    point = [.7 * knuckles[k] + .3 * p[hand][k] for k in range(3)]
    line = [p["index_01" + side][k] - p["pinky_01" + side][k] for k in range(3)]
    return point, line


def unit3(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]


def gun_frame(kind, weapon_path, point, line):
    """pistol / stock grip in weapon-mesh space: (handle point, tip, edge); see the kinds above."""
    m = u.load_asset(weapon_path)
    b = m.get_bounding_box()
    lo, hi = [b.min.x, b.min.y, b.min.z], [b.max.x, b.max.y, b.max.z]
    ext = [hi[k] - lo[k] for k in range(3)]
    try:
        pts = [(v.x, v.y, v.z) for v in u.ProceduralMeshLibrary.get_section_from_static_mesh(m, 0, 0)[0]]
    except Exception:
        d = m.get_static_mesh_description(0)
        pts = [(q.x, q.y, q.z) for q in (d.get_vertex_position(u.VertexID(i)) for i in range(d.get_vertex_count()))]
    mean = [sum(v[k] for v in pts) / len(pts) for k in range(3)]
    L = ext.index(max(ext))
    if kind == "pistol":
        muzzle = [0.0, 0.0, 0.0]
        muzzle[L] = 1.0 if hi[L] - point[L] >= point[L] - lo[L] else -1.0
        return point, unit3(line), muzzle
    # stock: the supporting hand wraps the stock with its knuckle line along it, index finger toward the muzzle; the
    # crossbow limbs (the widest cross-section) must sit at that end (checked, reported).
    line_u = unit3(line)
    stock = max(range(3), key=lambda k: abs(line_u[k]))
    muzzle = [0.0, 0.0, 0.0]
    muzzle[stock] = 1.0 if line_u[stock] >= 0 else -1.0
    third = [v for v in pts if (v[stock] - (lo[stock] + hi[stock]) / 2) * muzzle[stock] > ext[stock] / 6]
    back = [v for v in pts if (v[stock] - (lo[stock] + hi[stock]) / 2) * muzzle[stock] < -ext[stock] / 6]
    cross = [k for k in range(3) if k != stock]
    width = lambda vs: max((max(v[k] for v in vs) - min(v[k] for v in vs)) for k in cross) if vs else 0.0
    if width(third) < width(back):
        raise RuntimeError("crossbow limbs not at the index-finger end (front %.1f, back %.1f)" % (width(third), width(back)))
    thin = min(range(3), key=lambda k: ext[k])
    up = [0.0, 0.0, 0.0]
    up[thin] = 1.0 if mean[thin] - point[thin] >= 0 else -1.0
    return point, muzzle, up


def frame(set_name, hand, spec, measure, report, fab):
    skel_path, sock_name, weapon_path, kind = spec
    s = None
    if skel_path is None:
        # No socket: the demo mesh sits on the (animated) weapon bone itself.
        bone, sock = sock_name, u.Transform()
    else:
        mesh = asset_mesh(skel_path)
        s = socket(mesh, sock_name)
        bone = str(s.get_editor_property("bone_name"))
        sock = u.Transform(s.get_editor_property("relative_location"), s.get_editor_property("relative_rotation"), s.get_editor_property("relative_scale"))
    weapon_bone = bone
    if not bone.startswith(("hand_", "weapon_", "lowerarm_")):
        # Helper weapon bones (Sword_Weapon_R, Gun_Weapon_L, w_crossbow): re-express the socket in the holding hand.
        sock = u.MathLibrary.compose_transforms(sock, bone_in_hand(fab, set_name, bone, hand, "%s/%s/weaponBone" % (set_name, hand), report))
        bone = hand
    elif bone.startswith("weapon_"):
        # Dagger sockets sit on the mannequin's weapon_r / weapon_l helper bones: re-express them in the hand.
        clip = next(c["path"] for c in fab["clips"].values() if c.get("set") == set_name)
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(u.load_asset(clip.split(".")[0]), 0.0, u.AnimPoseEvaluationOptions())
        parent = "hand" + bone[len("weapon"):]
        rel = u.MathLibrary.make_relative_transform(u.AnimPoseExtensions.get_ref_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD),
                                                    u.AnimPoseExtensions.get_ref_bone_pose(pose, parent, u.AnimPoseSpaces.WORLD))
        sock = u.MathLibrary.compose_transforms(sock, rel)
        bone = parent
    weapon = u.load_asset(weapon_path)
    if kind in ("pistol", "stock"):
        handle, tip_m, edge_m = gun_frame(kind, weapon_path, *hand_grip(fab, set_name, weapon_bone, hand, sock))
        info = {"extent": None}
        grip = {"handle": [round(x, 3) for x in handle], "tip": [round(x, 4) for x in tip_m], "edge": edge_m, "kind": kind}
    else:
        info, grip = measure(weapon_path, kind)
        handle, axis, edge = grip["handle"], grip["axis"], grip["edge"]
    if kind in ("pistol", "stock"):
        point = sock.transform_location(V(handle))
        tip_v = sock.transform_direction(V(tip_m))
        edge_v = sock.transform_direction(V(edge_m))
        shield = False
    elif kind == "shield":
        # up = the wide end of the long axis; face out = away from the forearm (from the hand bone origin).
        out = sock.transform_location(V([(weapon.get_bounding_box().min.x + weapon.get_bounding_box().max.x) / 2,
                                        (weapon.get_bounding_box().min.y + weapon.get_bounding_box().max.y) / 2,
                                        (weapon.get_bounding_box().min.z + weapon.get_bounding_box().max.z) / 2]))
        thin = {"X": 0, "Y": 1, "Z": 2}[info["thin"]]
        n = [1.0 if k == thin else 0.0 for k in range(3)]
        nb = sock.transform_direction(V(n))
        if (out.x * nb.x + out.y * nb.y + out.z * nb.z) < 0:
            n = [-x for x in n]
        # wide end: the half of the long axis with the larger cross-section (heater: the top)
        prof = info.get("profile") or []
        up = axis
        if prof:
            half = len(prof) // 2
            if sum(prof[:half]) > sum(prof[half:]):
                up = [-x for x in axis]
        point = sock.transform_location(V(handle))
        tip_v = sock.transform_direction(V(up))
        edge_v = sock.transform_direction(V(n))
        shield = True
    else:
        tip = far_tip(weapon, handle, axis)
        point = sock.transform_location(V(handle))
        tip_v = sock.transform_direction(V(tip))
        edge_v = sock.transform_direction(V(edge))
        shield = False
    def unit(v):
        l = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z) or 1.0
        return u.Vector(v.x / l, v.y / l, v.z / l)
    tip_v = unit(tip_v)
    d = edge_v.x * tip_v.x + edge_v.y * tip_v.y + edge_v.z * tip_v.z
    edge_v = unit(u.Vector(edge_v.x - tip_v.x * d, edge_v.y - tip_v.y * d, edge_v.z - tip_v.z * d))
    out = {"bone": bone, "point": vec(point), "tip": vec(tip_v), "edge": vec(edge_v),
           "source": "%s:%s + %s" % ((skel_path or "-").rsplit("/", 1)[-1], sock_name, weapon_path.rsplit("/", 1)[1])}
    if shield:
        out["shield"] = True
    if kind == "stock":
        out["grip"] = "stock"
    if s is None:
        out["source"] = "%s bone (%s) + %s" % (sock_name, hand, weapon_path.rsplit("/", 1)[1])
    report["%s/%s" % (set_name, hand)] = {"socketBone": bone, "weaponBone": weapon_bone,
                                          "socketLoc": vec(s.get_editor_property("relative_location")) if s else None,
                                          "socketRot": [round(x, 3) for x in (s.get_editor_property("relative_rotation").pitch, s.get_editor_property("relative_rotation").yaw, s.get_editor_property("relative_rotation").roll)] if s else None,
                                          "weapon": weapon_path, "weaponExtentCm": info["extent"], "grip": grip, "frame": out}
    return out


def pose_rel(anim, t, off, main):
    opts = u.AnimPoseEvaluationOptions()
    pose = u.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
    a = u.AnimPoseExtensions.get_bone_pose(pose, off, u.AnimPoseSpaces.WORLD)
    b = u.AnimPoseExtensions.get_bone_pose(pose, main, u.AnimPoseSpaces.WORLD)
    return u.MathLibrary.make_relative_transform(a, b)


def off_hand(set_name, fab, report):
    loco = fab.get("locomotion", {}).get(set_name, {})
    idle = loco.get("idle")
    if not idle:
        return None
    anim = u.load_asset(idle.split(".")[0])
    n = 8
    rels = [pose_rel(anim, anim.get_play_length() * (i + .5) / n, "hand_l", "hand_r") for i in range(n)]
    locs = [r.translation for r in rels]
    mean = u.Vector(sum(l.x for l in locs) / n, sum(l.y for l in locs) / n, sum(l.z for l in locs) / n)
    spread = max(math.sqrt((l.x - mean.x) ** 2 + (l.y - mean.y) ** 2 + (l.z - mean.z) ** 2) for l in locs)
    q = rels[n // 2].rotation
    attack_spread = []
    for key, clip in fab["clips"].items():
        if clip.get("set") == set_name and clip.get("kind") == "attack":
            a = u.load_asset(clip["path"].split(".")[0])
            ls = [pose_rel(a, a.get_play_length() * (i + .5) / 6, "hand_l", "hand_r").translation for i in range(6)]
            attack_spread.append(round(max(math.sqrt((l.x - mean.x) ** 2 + (l.y - mean.y) ** 2 + (l.z - mean.z) ** 2) for l in ls), 1))
    report["%s/offHand" % set_name] = {"idle": idle, "idleSpreadCm": round(spread, 2), "attackMaxDistanceFromIdleCm": attack_spread}
    return {"main": "hand_r", "loc": vec(mean), "quat": [round(q.x, 5), round(q.y, 5), round(q.z, 5), round(q.w, 5)]}


def main():
    report = {}
    try:
        fab = json.loads(FAB_MAP.read_text(encoding="utf-8"))
        measure = load_measure()
        sets = {}
        for set_name, hands in REFS.items():
            for hand, spec in hands.items():
                if (spec[0] and not u.EditorAssetLibrary.does_asset_exist(spec[0])) or not u.EditorAssetLibrary.does_asset_exist(spec[2]):
                    report["%s/%s" % (set_name, hand)] = {"skipped": "pack not installed"}
                    continue
                try:
                    sets.setdefault(set_name, {})[hand] = frame(set_name, hand, spec, measure, report, fab)
                except Exception as error:
                    report["%s/%s" % (set_name, hand)] = {"skipped": str(error)}
            if set_name in TWO_HANDED and set_name in sets:
                try:
                    o = off_hand(set_name, fab, report)
                    if o:
                        sets[set_name]["offHand"] = o
                except Exception as error:
                    report["%s/offHand" % set_name] = {"skipped": str(error)}
        # One calibration clip per set: whichever the body was given first measures its retarget rotation.
        calibration, seen = [], set()
        for key, clip in fab["clips"].items():
            if clip.get("kind") == "attack" and clip["set"] not in seen:
                seen.add(clip["set"])
                calibration.append({"clip": key, "source": clip["path"]})
        (ROOT / "Content/Data/WeaponSockets.json").write_text(json.dumps({
            "schemaVersion": 1,
            "description": "Animation-authored weapon grips of the Fab animation packs (Tools/MeasureMannySockets.py; Docs/WeaponLoadouts.md 'Grip model'). "
                           "sets.<set>.<hand>: grip frame in UE5-mannequin bone space (cm): point, tip (handle -> business end), edge; shields: point = strap, tip = up, edge = face out. "
                           "sets.<set>.offHand: second-hand bone relative to the main hand bone. calibration: one retargeted clip per set and its mannequin source. "
                           "carry: side carry of the staffs (carryAt, carryUp, oneHand; overrides WeaponGrips.json).",
            "sets": sets, "calibration": calibration, "carry": CARRY}, indent=1) + "\n", encoding="utf-8")
        u.log("CIRE_MANNY_SOCKETS_PASS sets=%d" % len(sets))
    except Exception as error:
        import traceback
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_MANNY_SOCKETS_FAIL " + str(error))
    finally:
        (ROOT / "Saved/MannySockets.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
