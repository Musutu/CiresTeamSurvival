"""Champion-select content (run inside the editor commandlet):

  UnrealEditor-Cmd.exe CiresTeamSurvival.uproject -run=pythonscript -script=Tools/BuildDraftSelectContent.py -unattended -nosplash

1. /Game/UI/Draft/M_DraftCutout: UI-domain translucent material that stands the live 3D
   champion on the painted background. The draft stage renders the champion alone with
   post-process alpha, whose alpha channel is inverse opacity, so Opacity = (1 - A) x Fade,
   masked by the stage's depth capture (Depth.R < MaxDepth) because the renderer paints an
   opaque far "ground" below the horizon. Parameters: Figure, Depth (textures), Fade, MaxDepth.
2. Imports every Art/DraftBackgrounds/<id>.png as /Game/UI/Draft/Backgrounds/T_DraftBg_<id>
   (UI group, sRGB, capped at 2048 px, not streamed). <id> is a profile id or a body family
   (ether_golem, paladin, troll_berserker); see BackgroundId() in CireRosterHUD.cpp.

Writes Saved/DraftSelectContent/report.json and logs CIRE_DRAFT_SELECT_CONTENT_PASS/FAIL.
"""
import json
import os
import re

import unreal

MATERIAL_DIR = "/Game/UI/Draft"
MATERIAL = "M_DraftCutout"
BACKGROUND_DIR = "/Game/UI/Draft/Backgrounds"
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "Art", "DraftBackgrounds")


def build_material(report):
    edit = unreal.MaterialEditingLibrary
    path = MATERIAL_DIR + "/" + MATERIAL
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
        edit.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MATERIAL, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report["errors"].append("material creation failed")
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    def node(cls, x, y):
        return edit.create_material_expression(material, cls, x, y)

    def connect(a, output, b, input_):
        if not edit.connect_material_expressions(a, output, b, input_):
            report["errors"].append("cannot connect %s -> %s" % (output, input_))

    figure = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    figure.set_editor_property("parameter_name", "Figure")
    figure.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    fade = node(unreal.MaterialExpressionScalarParameter, -700, 260)
    fade.set_editor_property("parameter_name", "Fade")
    fade.set_editor_property("default_value", 1.0)
    inverse = node(unreal.MaterialExpressionOneMinus, -420, 160)
    connect(figure, "A", inverse, "")
    # Depth mask: anything farther than MaxDepth (the far "ground" the renderer paints below
    # the horizon) is dropped; the champion is always nearer.
    depth = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, 420)
    depth.set_editor_property("parameter_name", "Depth")
    # Colour sampler + engine default: the float render target set at runtime is read raw (not sRGB).
    depth.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    max_depth = node(unreal.MaterialExpressionScalarParameter, -1000, 640)
    max_depth.set_editor_property("parameter_name", "MaxDepth")
    max_depth.set_editor_property("default_value", 1.0e7)
    lower = node(unreal.MaterialExpressionSubtract, -760, 640)
    lower.set_editor_property("const_b", 120.0)
    connect(max_depth, "", lower, "A")
    step = node(unreal.MaterialExpressionSmoothStep, -560, 480)
    connect(lower, "", step, "Min")
    connect(max_depth, "", step, "Max")
    connect(depth, "R", step, "Value")
    near = node(unreal.MaterialExpressionOneMinus, -400, 480)
    connect(step, "", near, "")
    masked = node(unreal.MaterialExpressionMultiply, -300, 260)
    connect(inverse, "", masked, "A")
    connect(near, "", masked, "B")
    opacity = node(unreal.MaterialExpressionMultiply, -160, 220)
    connect(masked, "", opacity, "A")
    connect(fade, "", opacity, "B")
    # The sampler decodes the sRGB render target to linear, but the canvas writes this UI
    # material's output straight to the display-encoded back buffer: re-encode (gamma 1/2.2)
    # so the champion keeps the capture's true colour and contrast instead of going muddy.
    encode = node(unreal.MaterialExpressionPower, -300, -60)
    encode.set_editor_property("const_exponent", 1.0 / 2.2)
    connect(figure, "RGB", encode, "Base")
    if not edit.connect_material_property(encode, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        report["errors"].append("cannot connect final colour")
    if not edit.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
        report["errors"].append("cannot connect opacity")
    edit.recompile_material(material)
    report["expressions"] = edit.get_num_material_expressions(material)
    if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        report["errors"].append("material save failed")
    else:
        report["material"] = path


def import_backgrounds(report):
    if not os.path.isdir(SOURCE):
        report["notes"].append("no Art/DraftBackgrounds folder")
        return
    tasks = []
    for name in sorted(os.listdir(SOURCE)):
        match = re.fullmatch(r"([a-z][a-z0-9_]{0,63})\.png", name)
        if not match:
            continue
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SOURCE, name)
        task.destination_path = BACKGROUND_DIR
        task.destination_name = "T_DraftBg_" + match.group(1)
        task.replace_existing = True
        task.automated = True
        task.save = False
        tasks.append((match.group(1), task))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, t in tasks])
    for background, task in tasks:
        path = "%s/T_DraftBg_%s" % (BACKGROUND_DIR, background)
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(texture, unreal.Texture2D):
            report["errors"].append("import failed: " + background)
            continue
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("never_stream", True)
        texture.set_editor_property("max_texture_size", 2048)
        if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
            report["errors"].append("save failed: " + background)
            continue
        report["backgrounds"].append({"id": background, "asset": path,
                                      "width": texture.blueprint_get_size_x(), "height": texture.blueprint_get_size_y()})


def main():
    report = {"material": "", "backgrounds": [], "errors": [], "notes": []}
    build_material(report)
    # CIRE_DRAFT_MATERIAL_ONLY=1 rebuilds the material without re-importing the scenes.
    if os.environ.get("CIRE_DRAFT_MATERIAL_ONLY") != "1":
        import_backgrounds(report)
    out = os.path.join(PROJECT, "Saved", "DraftSelectContent")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
    unreal.log("CIRE_DRAFT_SELECT_CONTENT_{} material={} backgrounds={} errors={}".format(
        "PASS" if not report["errors"] and report["material"] else "FAIL", report["material"], len(report["backgrounds"]), len(report["errors"])))


main()
