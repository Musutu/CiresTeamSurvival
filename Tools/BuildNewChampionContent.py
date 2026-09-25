"""New-champion content: original prototype props and the Aetheri energy materials.

    UnrealEditor-Cmd.exe CiresTeamSurvival.uproject -run=pythonscript -script=Tools/BuildNewChampionContent.py -unattended -nullrhi -nosound -NoLiveCoding
    python Tools/BuildNewChampionContent.py --sources      # OBJ/MTL sources only (no Unreal)

Creates /Game/Art/NewChampions01:
  Materials/M_AetherEnergy  lit, emissive = Tint x Glow (the constructs' cores, crystals and halos tint it per field)
  Materials/M_AetherAlloy   gold-white metallic alloy with a faint Trim emissive (construct bodies)
  Materials/M_NC<name>      constant prop materials
  Props/SM_<name>           Flintlock, Falchion, ArcaneBlunderbuss, SpectralBlade, Glaive, GlaiveLauncher, AetherStaff, AetherHalberd
Geometry is authored here (original, centimetres, +X forward, +Z up, palm grip at the origin), written as OBJ under
Art/NewChampions01 and imported. Existing assets are replaced only with -CireNewChampionsReplace. Writes
Saved/NewChampionContent/report.json and logs CIRE_NEW_CHAMPION_CONTENT_PASS / _FAIL.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import sys

try:
    import unreal
except ImportError:
    unreal = None

PROJECT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT / "Tools"))
from BuildArmoryProps import Mesh, add, mul, require  # noqa: E402  (shared original prop geometry kit)
import BuildArmoryProps as Armory  # noqa: E402

OUTPUT = "/Game/Art/NewChampions01"
# name: (base colour, roughness, metallic, emission)
MATERIALS = {
    "Wood": ((.13, .06, .025), .78, 0, 0),
    "Leather": ((.045, .028, .022), .86, 0, 0),
    "Steel": ((.34, .38, .42), .3, .85, 0),
    "DarkSteel": ((.07, .08, .1), .35, .8, 0),
    "Brass": ((.43, .28, .08), .36, .72, 0),
    "Silver": ((.7, .72, .75), .22, .95, 0),
    "Alloy": ((.8, .7, .48), .28, .9, 0),
    "Aether": ((.35, .3, 1.0), .3, .1, 2.5),
    "Spirit": ((.2, .5, 1.0), .25, .1, 2.2),
    "Moon": ((.75, .85, 1.0), .3, .6, .4),
}
Armory.MATERIALS.update(MATERIALS)  # Mesh.summary() validates material names against this table


def bell(m, x0, x1, r0, r1, material, steps=6):
    for i in range(steps):
        a, b = x0 + (x1 - x0) * i / steps, x0 + (x1 - x0) * (i + 1) / steps
        m.rod((a, 0, 6), (b, 0, 6), r0 + (r1 - r0) * (i + .5) / steps, material, 12)


def crescent(m, angle, inner, outer, sweep, thickness, material, cx=0.0, cz=0.0):
    """A crescent blade in the X/Z plane, rotated by angle (degrees) about the origin."""
    pts = []
    for i in range(9):
        t = math.radians(angle - sweep / 2 + sweep * i / 8)
        pts.append((cx + math.cos(t) * outer, cz + math.sin(t) * outer))
    for i in range(9):
        t = math.radians(angle + sweep / 2 - sweep * i / 8 - 8)
        r = inner + (outer - inner) * .25 * math.sin(math.pi * i / 8)
        pts.append((cx + math.cos(t) * r, cz + math.sin(t) * r))
    m.prism(pts, thickness, material)


def geometry() -> dict[str, Mesh]:
    out = {}
    # Flintlock pistol: wooden grip down the fist, brass lock, long steel barrel forward (+X), flared muzzle.
    m = Mesh()
    m.rod((0, 0, -9), (2.5, 0, 4), 1.7, "Wood", 10)
    m.box((-1.5, 0, -10), (5, 3.4, 3), "Brass")
    m.box((4, 0, 6), (10, 3.2, 4.2), "Wood")
    m.box((3, 0, 6.5), (5, 3.6, 3), "Brass")
    m.box((1.5, 0, 9.5), (2, 1, 3.5), "DarkSteel")
    m.rod((6, 0, 7), (30, 0, 7), 1.25, "DarkSteel", 12)
    m.rod((28, 0, 7), (31, 0, 7), 1.9, "Brass", 12)
    m.ring((4, 0, 2), 2.2, .35, (0, 1, 0), "Brass", 12)
    out["Flintlock"] = m
    # Falchion: leather grip, brass guard, broad single-edged blade widening toward a clipped point.
    m = Mesh()
    Armory.shaft(m, -10, 2, 1.25)
    m.box((0, 0, 3), (15, 3, 2), "Brass")
    m.gem((0, 0, -12), (4, 4, 5), "Brass")
    m.prism([(-1.6, 4), (2.2, 4), (3.6, 26), (5.6, 50), (4.2, 62), (-0.4, 66), (-1.6, 56), (-1.6, 30)], .8, "Silver")
    out["Falchion"] = m
    # Arcane blunderbuss: dark stock, drum of glowing orbs, flared bell barrel with spikes.
    m = Mesh()
    m.rod((0, 0, -8), (2, 0, 4), 1.9, "DarkSteel", 10)
    m.box((-10, 0, 3), (16, 4, 6), "DarkSteel")
    m.box((6, 0, 6), (14, 5, 6), "DarkSteel")
    for k, (dx, dz) in enumerate(((-4, 11), (0, 12), (4, 11), (-2, 14), (2, 14))):
        m.gem((dx, 0, dz), (4.5, 4.5, 4.5), "Aether")
    bell(m, 12, 34, 2.2, 5.5, "DarkSteel")
    m.ring((34, 0, 6), 5.8, .6, (1, 0, 0), "Aether", 14)
    for sign in (-1, 1):
        m.prism([(14, 8 * 1.0), (26, 12), (22, 8)], 1, "Steel") if sign > 0 else m.prism([(14, 4), (26, 0), (22, 4)], 1, "Steel")
    out["ArcaneBlunderbuss"] = m
    # Spectral blade: dark hilt, spiked guard and a translucent-looking glowing arcane blade.
    m = Mesh()
    Armory.shaft(m, -10, 2, 1.2)
    m.prism([(-7, 2), (7, 2), (4, 6), (-4, 6)], 2.4, "DarkSteel")
    m.gem((0, 0, 4), (4, 3, 5), "Spirit")
    m.prism([(-2.6, 6), (2.6, 6), (1.6, 34), (4, 42), (0, 60), (-4, 42), (-1.6, 34)], .9, "Spirit")
    out["SpectralBlade"] = m
    # Glaive: three crescent blades around a hand hub (thrown / launched).
    m = Mesh()
    m.rod((0, -1.5, 0), (0, 1.5, 0), 4, "Leather", 12)
    m.ring((0, 0, 0), 6, .9, (0, 1, 0), "Steel", 16)
    for k in range(3):
        crescent(m, 90 + k * 120, 6, 22, 70, 1.0, "Silver")
    m.gem((0, 0, 0), (3, 3, 3), "Moon")
    out["Glaive"] = m
    # Glaive launcher (mounted): crossbow-like frame with a spring cradle and a loaded glaive.
    m = Mesh()
    m.rod((0, 0, -8), (2, 0, 4), 1.8, "Wood", 10)
    m.box((-6, 0, 4), (40, 5, 5), "Wood")
    m.box((14, 0, 7.5), (30, 3, 2), "DarkSteel")
    for sign in (-1, 1):
        pts = [(26, 0, 6), (22, sign * 12, 7), (14, sign * 22, 8)]
        for a, b in zip(pts, pts[1:]):
            m.rod(a, b, 1.4, "Steel", 8)
        m.rod(pts[-1], (-2, 0, 8), .35, "Leather", 6)
    for k in range(3):
        crescent(m, 90 + k * 120, 3, 11, 70, .7, "Silver", cx=30, cz=12)
    m.ring((30, 0, 12), 3, .5, (0, 1, 0), "Brass", 12)
    out["GlaiveLauncher"] = m
    # Aether staff: alloy shaft, twin rings and a floating energy crystal.
    m = Mesh()
    m.rod((0, 0, -60), (0, 0, 80), 1.9, "Alloy", 12)
    m.rod((0, 0, -8), (0, 0, 8), 2.3, "DarkSteel", 12)
    for z in (74, 80):
        m.ring((0, 0, z), 7, .8, (0, 0, 1), "Alloy", 14)
    m.gem((0, 0, 96), (8, 8, 22), "Aether")
    m.ring((0, 0, 96), 10, .6, (1, 0, 0), "Aether", 16)
    out["AetherStaff"] = m
    # Aether halberd: alloy pole, crescent axe with an energy edge, spike.
    m = Mesh()
    m.rod((0, 0, -70), (0, 0, 90), 1.9, "Alloy", 12)
    m.rod((0, 0, -8), (0, 0, 8), 2.3, "DarkSteel", 12)
    m.prism([(0, 70), (16, 64), (22, 78), (18, 92), (0, 88)], 2, "Alloy")
    m.prism([(18, 66), (24, 78), (20, 92), (22, 78)], 2.2, "Aether")
    m.prism([(-1.6, 90), (1.6, 90), (0, 112)], 2, "Alloy")
    m.gem((0, 0, 86), (5, 5, 7), "Aether")
    out["AetherHalberd"] = m
    return out


def write_sources(models):
    folder = PROJECT / "Art/NewChampions01"
    folder.mkdir(parents=True, exist_ok=True)
    for name, model in models.items():
        text = model.obj().replace("mtllib Armory.mtl", "mtllib NewChampions.mtl").replace("usemtl M_Armory", "usemtl M_NC")
        text = text.replace("# Original Cire armory prototype.", "# Original Cire new-champion prototype prop.")
        (folder / ("SM_" + name + ".obj")).write_text(text, encoding="ascii")
    lines = []
    for name, (color, roughness, metallic, emission) in MATERIALS.items():
        lines += ["newmtl M_NC" + name, "Kd %.5f %.5f %.5f" % color, "Pr %.4f" % roughness, "Pm %.4f" % metallic, ""]
    (folder / "NewChampions.mtl").write_text("\n".join(lines), encoding="ascii")
    (folder / "manifest.json").write_text(json.dumps(dict(artStatus="original local prototype", units="centimeters", grip=[0, 0, 0],
                                                          assets={n: m.summary() for n, m in models.items()}), indent=2) + "\n", encoding="utf-8")
    return folder


# ------------------------------------------------------------------------------------------ Unreal
def make(name, folder, cls, factory, replace):
    lib = unreal.EditorAssetLibrary
    path = f"{folder}/{name}"
    if lib.does_asset_exist(path):
        require(replace, "Refusing to overwrite " + path + " (pass -CireNewChampionsReplace)")
        return lib.load_asset(path)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)


def constant_material(name, replace):
    lib = unreal.MaterialEditingLibrary
    material = make("M_NC" + name, OUTPUT + "/Materials", unreal.Material, unreal.MaterialFactoryNew(), replace)
    lib.delete_all_material_expressions(material)
    color, roughness, metallic, emission = MATERIALS[name]
    rgb = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -350, 0)
    rgb.set_editor_property("constant", unreal.LinearColor(*color, 1))
    lib.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop, y in ((roughness, unreal.MaterialProperty.MP_ROUGHNESS, 120), (metallic, unreal.MaterialProperty.MP_METALLIC, 200)):
        node = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -350, y)
        node.set_editor_property("r", value)
        lib.connect_material_property(node, "", prop)
    if emission:
        glow = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -350, 280)
        glow.set_editor_property("constant", unreal.LinearColor(*(c * emission for c in color), 1))
        lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False), "Material save failed " + name)
    return material


def energy_material(replace):
    """M_AetherEnergy: emissive = Tint x Glow x 3 over a dark base; Tint/Glow are set per construct (CireTechConstructs)."""
    lib = unreal.MaterialEditingLibrary
    material = make("M_AetherEnergy", OUTPUT + "/Materials", unreal.Material, unreal.MaterialFactoryNew(), replace)
    lib.delete_all_material_expressions(material)
    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(.5, .42, 2.2, 1))
    glow = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 160)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 1.0)
    fres = lib.create_material_expression(material, unreal.MaterialExpressionFresnel, -600, 280)
    boost = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -600, 400)
    boost.set_editor_property("r", 3.0)
    mul1 = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -380, 60)
    lib.connect_material_expressions(tint, "", mul1, "A")
    lib.connect_material_expressions(glow, "", mul1, "B")
    rim = lib.create_material_expression(material, unreal.MaterialExpressionAdd, -380, 300)
    lib.connect_material_expressions(fres, "", rim, "A")
    lib.connect_material_expressions(boost, "", rim, "B")
    mul2 = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -180, 120)
    lib.connect_material_expressions(mul1, "", mul2, "A")
    lib.connect_material_expressions(rim, "", mul2, "B")
    lib.connect_material_property(mul2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -380, -120)
    base.set_editor_property("constant", unreal.LinearColor(.02, .02, .04, 1))
    lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -380, -200)
    rough.set_editor_property("r", .25)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False), "Energy material save failed")
    return material


def alloy_material(replace):
    """M_AetherAlloy: gold-white metal with a faint emissive Trim (tinted per construct)."""
    lib = unreal.MaterialEditingLibrary
    material = make("M_AetherAlloy", OUTPUT + "/Materials", unreal.Material, unreal.MaterialFactoryNew(), replace)
    lib.delete_all_material_expressions(material)
    base = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, -100)
    base.set_editor_property("parameter_name", "Base")
    base.set_editor_property("default_value", unreal.LinearColor(.78, .68, .46, 1))
    lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    metal = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 60)
    metal.set_editor_property("r", .9)
    lib.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 140)
    rough.set_editor_property("r", .3)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    trim = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 240)
    trim.set_editor_property("parameter_name", "Trim")
    trim.set_editor_property("default_value", unreal.LinearColor(.45, .4, 1.6, 1))
    fres = lib.create_material_expression(material, unreal.MaterialExpressionFresnel, -700, 380)
    mul = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -420, 300)
    lib.connect_material_expressions(trim, "", mul, "A")
    lib.connect_material_expressions(fres, "", mul, "B")
    lib.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False), "Alloy material save failed")
    return material


def import_prop(name, source, materials, replace):
    asset = "SM_" + name
    folder = OUTPUT + "/Props"
    if unreal.EditorAssetLibrary.does_asset_exist(folder + "/" + asset):
        require(replace, "Refusing to overwrite " + asset)
        unreal.EditorAssetLibrary.delete_asset(folder + "/" + asset)
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(source / (asset + ".obj")), destination_path=folder, destination_name=asset,
                           automated=True, replace_existing=True, save=True).items():
        task.set_editor_property(key, value)
    opts = unreal.FbxImportUI()
    for key, value in dict(import_mesh=True, import_materials=False, import_textures=False, import_as_skeletal=False,
                           mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
        opts.set_editor_property(key, value)
    data = opts.get_editor_property("static_mesh_import_data")
    for key, value in dict(combine_meshes=True, auto_generate_collision=False, convert_scene=False, convert_scene_unit=False).items():
        data.set_editor_property(key, value)
    task.set_editor_property("options", opts)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(folder + "/" + asset)
    require(isinstance(mesh, unreal.StaticMesh), "Import failed " + name)
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        label = str(slot.get_editor_property("imported_material_slot_name"))
        key = label.removeprefix("M_NC")
        require(key in materials, "Unexpected material slot " + label)
        mesh.set_material(index, materials[key])
    require(unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False), "Prop save failed " + name)
    bounds = mesh.get_bounds().box_extent * 2
    return dict(asset=str(mesh.get_path_name()), sizeCm=[round(float(bounds.x), 2), round(float(bounds.y), 2), round(float(bounds.z), 2)],
                triangles=mesh.get_num_triangles(0))


def run_unreal(replace: bool) -> dict:
    models = geometry()
    source = write_sources(models)
    report = dict(status="failed", props={}, materials=[])
    try:
        materials = {name: constant_material(name, replace) for name in MATERIALS}
        report["materials"] = [str(m.get_path_name()) for m in materials.values()]
        report["materials"].append(str(energy_material(replace).get_path_name()))
        report["materials"].append(str(alloy_material(replace).get_path_name()))
        for name in models:
            report["props"][name] = import_prop(name, source, materials, replace)
        for name, model in models.items():
            expected = model.summary()["sizeCm"]
            actual = report["props"][name]["sizeCm"]
            require(max(abs(a - b) for a, b in zip(actual, expected)) < .25, f"Import axis/unit mismatch {name}: {actual} vs {expected}")
        report["status"] = "pass"
        unreal.log("CIRE_NEW_CHAMPION_CONTENT_PASS props=%d materials=%d" % (len(report["props"]), len(report["materials"])))
    except Exception as error:  # noqa: BLE001
        report["error"] = str(error)
        unreal.log_error("CIRE_NEW_CHAMPION_CONTENT_FAIL " + str(error))
        raise
    finally:
        out = PROJECT / "Saved/NewChampionContent"
        out.mkdir(parents=True, exist_ok=True)
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    if unreal:
        tokens = unreal.SystemLibrary.get_command_line().lower().split()
        run_unreal("-cirenewchampionsreplace" in tokens)
    else:
        models = geometry()
        folder = write_sources(models)
        print(json.dumps({n: m.summary()["sizeCm"] for n, m in models.items()}, indent=1))
        print("sources:", folder)
