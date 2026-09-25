"""UE 5.8 commandlet (paladin-hq): put the Polyphoria "Paladin RPG Set" plate body on the Iron Warden (knight) and
both Relic Paladins, and write their rows into Content/Data/ChampionArtBindings.fab.json (motion "humanoid").

The pack is modular (MetaHuman-compatible parts on a UE5 Manny skeleton) but ships a ready combined mesh,
SK_ma_pala_combine_a (chest + cloak + pants + boots + gloves + bracers, 34.8k vertices, 4K chest textures), so no
MetaHuman Creator assembly is needed. It is the LEADER of the champion mesh; the Polyphoria head and a paladin helm
are leader-posed parts. Every material is the pack's ID-masked "Base Complex" master, so each champion gets its own
colour identity from data: a vendor material instance per slot plus runtime vector overrides of the ID channels
(R/G/B/A/Y/M/C: tint RGB, blend in A) and per-channel metallic / roughness multipliers.

  knight             Iron Warden     burnished steel plate, midnight-blue cloak, dark leather, closed great helm
  paladin_righteous  Relic Paladin   crimson-lacquered plate, ember-gold trim, blood-red cloak, crested helm
  paladin_holy       Relic Paladin   ivory-white plate, bright gold filigree, white cloak, crested helm in gold

Animation: the body is FabAnimMap.json "fabBodies" -> folder PolyPaladin (sword & shield set, 8-way locomotion,
attacks, skills, cast, shout, hit, death, roll, jump), retargeted by Tools/RetargetFabAnimations.py
-CireFabAnimOnly=PolyPaladin. The row's locomotion is that BlendSpace; the runtime (UCireChampionArt) uses the row
only when the leader, every part and the BlendSpace exist locally, and never with -CireNoFab / -CireNoFabCreatures.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildFabPaladins.py -unattended -nullrhi
Log marker CIRE_FAB_PALADINS_PASS / _FAIL; report Saved/FabPaladins.json.
"""
import json
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
P = "/Game/Polyphoria/Polyphoria"
H = P + "/CharacterParts_HeavyArmour"
M = H + "/Materials"
LEADER = H + "/Meshes/UE5/Male/UpperBody/SK_ma_pala_combine_a"
HEAD = P + "/Base/Meshes/Male/SK_ma_meta_body_head_01"
HELM_A = H + "/Meshes/UE5/Male/Hats/SK_ma_tal_nrw_heavy_helm_paladin_a"
HELM_B = H + "/Meshes/UE5/Male/Hats/SK_ma_tal_nrw_heavy_helm_paladin_b"
LOCOMOTION = "/Game/FabDerived/Anim/PolyPaladin/BS_Fab_Locomotion_PolyPaladin"

# Leader slots of SK_ma_pala_combine_a.
CHEST, CHEST_CLOTH, CLOAK, TIGHTS, PANTS, GLOVES, BRACERS = (
    "lambert222", "Cloth", "lambert182", "paladin_", "lambert44", "glove_base", "pasted__pasted__lambert91")


def obj(path):
    path = path.split(".")[0]
    return "%s.%s" % (path, path.rsplit("/", 1)[1])


def mat(path, vectors=None, scalars=None):
    spec = {"base": obj(path)}
    if vectors:
        spec["vectors"] = vectors
    if scalars:
        spec["scalars"] = scalars
    return spec


# Colour identities. The whole set shares one ID-channel convention, read from debug renders (every channel painted
# a pure colour): R = the main plate (breastplate, pauldrons, greaves, helm shell), G = raised trim and filigree edges,
# B = straps and inner leather, A = sash / cloth wraps (and the helm wings), Y = small buckles and rivets,
# M = the hanging tabard, C = the scale / mail skirt. The cloak's R is its fabric. Each value is [r, g, b, blend].
def palette(plate, trim, leather, cloth, rivet, tabard, mail, metal=None, rough=None):
    vectors = {"R: Primary": plate, "G: Details 1": trim, "B: Details 2": leather, "A: Details 3 / Skin": cloth,
               "Y": rivet, "M": tabard, "C": mail}
    scalars = {}
    for ch, v in (metal or {}).items():
        scalars["%s: Metallic Multiplier" % ch] = v
    for ch, v in (rough or {}).items():
        scalars["%s: Roughness Multiplier" % ch] = v
    return vectors, scalars


IDENTITY = {
    # Iron Warden: burnished steel plate, dark silver trim, midnight-blue cloth and tabard, dark mail.
    "knight": dict(
        body=palette([.40, .42, .46, 1], [.30, .31, .34, 1], [.07, .055, .045, 1], [.018, .035, .13, 1],
                     [.55, .42, .22, 1], [.02, .045, .16, 1], [.32, .33, .35, 1],
                     metal={"R": 1, "G": 1, "C": 1}, rough={"R": .55, "G": .6}),
        cloak=[.012, .028, .11, 1],
        shield=[.02, .05, .19, 1],  # midnight-blue enamelled heater with steel trim: nothing like the Holy's ivory
        helm=palette([.40, .42, .46, 1], [.03, .07, .24, 1], [.07, .055, .045, 1], [.62, .64, .68, 1],
                     [.55, .42, .22, 1], [.02, .045, .16, 1], [.32, .33, .35, 1], metal={"R": 1, "A": 1}, rough={"R": .5})),
    # Relic Paladin (Righteous): crimson-lacquered plate, ember-gold filigree, blood-red cloak, blackened mail.
    "paladin_righteous": dict(
        body=palette([.40, .018, .016, 1], [1.0, .50, .12, 1], [.06, .02, .015, 1], [.22, .008, .008, 1],
                     [.95, .55, .15, 1], [.09, .004, .004, 1], [.13, .12, .12, 1],
                     metal={"R": .75, "G": 1, "C": 1}, rough={"R": .45, "G": .5}),
        cloak=[.30, .004, .01, 1],
        shield=[.20, .004, .004, 1],  # the white-based heater face needs a deeper crimson to read red, not pink
        helm=palette([.40, .018, .016, 1], [1.0, .50, .12, 1], [.06, .02, .015, 1], [1.0, .50, .12, 1],
                     [.95, .55, .15, 1], [.09, .004, .004, 1], [.13, .12, .12, 1], metal={"R": .75, "G": 1, "A": 1}, rough={"R": .45})),
    # Relic Paladin (Holy): ivory-white enamelled plate, bright gold filigree, white cloak, gilded mail.
    "paladin_holy": dict(
        body=palette([.86, .84, .78, 1], [1.0, .72, .28, 1], [.62, .56, .46, 1], [.88, .86, .80, 1],
                     [1.0, .72, .28, 1], [.84, .80, .68, 1], [.86, .64, .30, 1],
                     metal={"R": .35, "G": 1, "C": 1}, rough={"R": .6, "G": .45}),
        cloak=[.90, .88, .84, 1],
        helm=palette([.86, .84, .78, 1], [1.0, .72, .28, 1], [.62, .56, .46, 1], [1.0, .72, .28, 1],
                     [1.0, .72, .28, 1], [.84, .80, .68, 1], [.86, .64, .30, 1], metal={"R": .35, "G": 1, "A": 1}, rough={"R": .55})),
}


def identity(profile):
    """Leader slot specs and the helm spec for a champion: the neutral vendor instance of each piece, re-tinted."""
    ident = IDENTITY[profile]
    vec, sca = ident["body"]
    body = lambda base: mat(base, vec, sca)
    cloak_vec = dict(vec, **{"R: Primary": ident["cloak"]})
    return {
        CHEST: body(M + "/Chest/MI_ma_chest_heavy_05_a"),
        CHEST_CLOTH: body(M + "/Chest/MI_ma_chest_heavy_05_a"),
        CLOAK: mat(M + "/Chest/MI_ma_cloak_01_a_white", cloak_vec, {"R: Metallic Multiplier": 0}),
        TIGHTS: body(M + "/Shoes/MI_shoe_set_04_a_bright"),
        PANTS: body(M + "/Pants/MI_pants_medium_dark"),
        GLOVES: body(M + "/Gloves_Bracers/MI_longglove_01_a"),
        BRACERS: body(M + "/Gloves_Bracers/MI_bracers_heavy_04"),
    }, mat(M + ("/Helmet/MI_helm_paladin_b" if PALADINS_HELM[profile] == HELM_B else "/Helmet/MI_helm_paladin_bright"), *ident["helm"])


PALADINS_HELM = {
    "knight": HELM_B,             # the flat-topped great helm with the raised visor ridge
    "paladin_righteous": HELM_A,  # the winged great helm with the relic cross
    "paladin_holy": HELM_A,
}
PALADINS = {}
for _id, _helm in PALADINS_HELM.items():
    _leader, _helm_mat = identity(_id)
    PALADINS[_id] = dict(helm=_helm, leader=_leader, helm_mat=_helm_mat)


# Props per champion (Tools/MeasureFabWeapons.py measures their grips and writes WeaponLoadouts.fab.json "profiles"):
# token -> (mesh, grip kind, extra grip fields, material spec by slot). The set's own sword and heater shield.
PW = H + "/Meshes/UE5/Weapons"
MW = M + "/Weapoon"
# The pack's heater shield ships with broken reduction LODs (NaN render bounds: it never draws), so a clean LOD-0
# copy is derived into /Game/FabDerived (gitignored, like the retargeted clips) by derive_shield().
SHIELD_SRC = PW + "/SM_wp_shield_tri_01_a"
SHIELD = "/Game/FabDerived/Props/Polyphoria/SM_wp_shield_tri_01_a"
# The heater's face is convex toward -Y (its rim curls back toward +Y), so the measured back-face heuristic points the
# face inward: strap it face-out, and at 80% (the vendor shield spans a metre, shoulder to knee).
SHIELD_GRIP = {"edge": [0.0, -1.0, 0.0], "scale": .8}
MED = "/Game/Medieval_Weapons/Meshes"


def shield(base, ident):
    """The heater shield re-tinted like its bearer: face = plate colour, trim and emblem = the trim colour."""
    vec, sca = IDENTITY[ident]["body"]
    face = IDENTITY[ident].get("shield", vec["R: Primary"])
    return {"0": mat(MW + base, {"R: Primary": face, "G: Details 1": vec["G: Details 1"], "B: Details 2": vec["G: Details 1"]},
                     {"R: Metallic Multiplier": sca.get("R: Metallic Multiplier", 1)})}


WEAPONS = {
    "knight": {
        "legacy/Sword": (PW + "/SM_wp_1h_sword_03", "guard", {"tilt": 30}, {"0": mat(MW + "/MI_sword_simple")}),
        "legacy/Shield": (SHIELD, "shield", SHIELD_GRIP, shield("/MI_wp_shield_tri_01_a", "knight")),
    },
    # The Relic Paladins swing forged flanged maces (Ultimate Weapons pack) instead of the prototype chain flail.
    "paladin_righteous": {
        "Flail": (MED + "/SM_Mace_3", "head", {"tilt": 28}, None),
        "legacy/Shield": (SHIELD, "shield", SHIELD_GRIP, shield("/MI_wp_shield_tri_01_b", "paladin_righteous")),
    },
    "paladin_holy": {
        "Flail": (MED + "/SM_Mace_1", "head", {"tilt": 28}, None),
        "legacy/Shield": (SHIELD, "shield", SHIELD_GRIP, shield("/MI_wp_shield_tri_01_b", "paladin_holy")),
    },
}


def derive_shield(report):
    """Duplicate the heater shield into /Game/FabDerived, keep LOD 0 only, no Nanite, rebuild; verify finite bounds."""
    lib = u.EditorAssetLibrary
    if lib.does_asset_exist(SHIELD):
        lib.delete_asset(SHIELD)
    mesh = lib.duplicate_asset(SHIELD_SRC, SHIELD)
    if not isinstance(mesh, u.StaticMesh):
        raise RuntimeError("could not duplicate " + SHIELD_SRC)
    # One source model (LOD 0) and high-precision tangents: a new derived-data key, so the render data is rebuilt
    # instead of reusing the source's corrupt cached buffers.
    before = mesh.get_num_lods()
    mesh.set_num_source_models(1)
    sml = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) or u.EditorStaticMeshLibrary  # commandlets: the library
    settings = sml.get_lod_build_settings(mesh, 0)
    settings.set_editor_property("use_high_precision_tangent_basis", True)
    settings.set_editor_property("use_full_precision_u_vs", True)
    sml.set_lod_build_settings(mesh, 0, settings)
    try:
        nanite = mesh.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", False)
        mesh.set_editor_property("nanite_settings", nanite)
    except Exception:
        pass
    mesh.build(True) if hasattr(mesh, "build") else None
    removed = before - mesh.get_num_lods()
    b = mesh.get_bounds()
    ext = [b.box_extent.x, b.box_extent.y, b.box_extent.z]
    if not all(e == e and 1 < e < 200 for e in ext):
        raise RuntimeError("derived shield bounds are not sane: %s" % ext)
    lib.save_loaded_asset(mesh, False)
    report["shield"] = {"path": SHIELD, "lods": mesh.get_num_lods(), "removed": removed, "extent": [round(e, 1) for e in ext]}


def exists(path):
    return u.EditorAssetLibrary.does_asset_exist(path.split(".")[0])


def slot_names(mesh):
    return [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]


def check_spec(mesh, spec, errors, where):
    """Every slot exists on the mesh, every base material exists, every vector override names a real parameter."""
    names = slot_names(mesh)
    out = {}
    for slot, row in spec.items():
        if slot not in names:
            errors.append("%s: no slot %s" % (where, slot)); continue
        if not exists(row["base"]):
            errors.append("%s: missing %s" % (where, row["base"])); continue
        mi = u.load_asset(row["base"].split(".")[0])
        vec = {str(n) for n in u.MaterialEditingLibrary.get_vector_parameter_names(mi)}
        sca = {str(n) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(mi)}
        bad = [k for k in row.get("vectors", {}) if k not in vec] + [k for k in row.get("scalars", {}) if k not in sca]
        if bad:
            errors.append("%s %s: unknown parameters %s" % (where, slot, bad))
        out[slot] = dict(row, base=obj(row["base"]))
    return out


def main():
    report = {"status": "failed", "errors": [], "rows": {}}
    try:
        for p in (LEADER, HEAD, HELM_A, HELM_B):
            if not exists(p):
                raise RuntimeError("Polyphoria pack not installed: " + p)
        derive_shield(report)
        if not exists(LOCOMOTION):
            raise RuntimeError("run RetargetFabAnimations.py -CireFabAnimOnly=PolyPaladin first: " + LOCOMOTION)
        leader = u.load_asset(LEADER)
        head = u.load_asset(HEAD)
        b = leader.get_bounds()
        floor = b.origin.z - b.box_extent.z
        rows = []
        for profile, spec in PALADINS.items():
            errors = report["errors"]
            helm = u.load_asset(spec["helm"])
            hb = helm.get_bounds()
            top = hb.origin.z + hb.box_extent.z
            helm_slots = slot_names(helm)
            # Every helm section gets the chosen finish (helm A carries an unassigned second section).
            helm_spec = {s: spec["helm_mat"] for s in helm_slots}
            row = {
                "profileId": profile, "status": "custom_ready", "motion": "humanoid",
                "mesh": obj(LEADER), "locomotion": obj(LOCOMOTION),
                "heightCm": round(top - floor, 1), "meshScale": 1.0, "floorZ": round(floor, 2),
                "materials": check_spec(leader, spec["leader"], errors, profile + " body"),
                "parts": [
                    {"mesh": obj(HEAD)},
                    {"mesh": obj(spec["helm"]), "materials": check_spec(helm, helm_spec, errors, profile + " helm")},
                ],
                "source": "Fab Polyphoria Paladin RPG Set (licensed, local only): see Docs/FAB-PURCHASED.md",
            }
            rows.append(row)
            report["rows"][profile] = {"height": row["heightCm"], "floor": row["floorZ"], "head_slots": slot_names(head), "helm_slots": helm_slots}
        path = ROOT / "Content/Data/ChampionArtBindings.fab.json"
        doc = json.loads(path.read_text(encoding="utf-8"))
        keep = [r for r in doc.get("bindings", []) if r.get("motion") != "humanoid"]
        doc["bindings"] = keep + rows
        doc["description"] = ("Purchased Fab bodies for champions (Docs/FabIntegration.md): creature bodies from "
                              "Tools/BuildFabChampionCreatures.py, humanoid plate bodies from Tools/BuildFabPaladins.py. A row "
                              "replaces the committed art only when its meshes and locomotion exist locally; -CireNoFab / "
                              "-CireNoFabCreatures keep the committed art.")
        if report["errors"]:
            raise RuntimeError("; ".join(report["errors"]))
        path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        report["status"] = "pass"
        u.log("CIRE_FAB_PALADINS_PASS rows=%d" % len(rows))
    except Exception as error:
        import traceback
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_FAB_PALADINS_FAIL " + str(error))
    finally:
        (ROOT / "Saved").mkdir(exist_ok=True)
        (ROOT / "Saved/FabPaladins.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


if not globals().get("CIRE_IMPORT_ONLY"):
    main()
