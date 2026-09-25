"""Champion-select content (run inside the editor commandlet):

  UnrealEditor-Cmd.exe CiresTeamSurvival.uproject -run=pythonscript -script=Tools/BuildDraftSelectContent.py -unattended -nosplash

1. /Game/UI/Draft/M_DraftCutout: UI-domain translucent material that stands the live 3D
   champion on the painted background. The draft stage renders the champion alone with
   post-process alpha, whose alpha channel is inverse opacity, so Opacity = (1 - A) x Fade,
   masked by the stage's depth capture (Depth.R < MaxDepth) because the renderer paints an
   opaque far "ground" below the horizon. The figure is supersampled ~2x: an HLSL pass
   (COMPOSITE_HLSL) anti-aliases, un-premultiplies, sharpens and sRGB-encodes it.
   Parameters: Figure, Depth (textures), Fade, MaxDepth, Sharpen.
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


# champ-select-hq: one HLSL pass composites the supersampled figure (the stage renders it at ~2x the
# on-screen size). Per output pixel it takes a 4x4 grid of bilinear taps inside the pixel's footprint:
#   coverage = (1 - A) x near-depth mask, averaged over the taps  -> anti-aliased silhouette,
#              including the lower half where only the depth mask separates figure from the far ground;
#   colour   = sum(premultiplied RGB x mask) / sum(coverage)       -> no dark fringe around the figure;
#   Sharpen  = unsharp mask against the one-pixel ring             -> crisp armour and faces;
#   sRGB     = exact piecewise encode (the canvas writes UI material output raw).
COMPOSITE_HLSL = r"""
float2 px = ddx(UV), py = ddy(UV);
float3 sumC = 0; float sumW = 0;
for (int j = 0; j < 4; j++)
{
    for (int i = 0; i < 4; i++)
    {
        float2 uv = UV + px * ((i + 0.5) / 4.0 - 0.5) + py * ((j + 0.5) / 4.0 - 0.5);
        float4 c = Texture2DSampleLevel(Figure, FigureSampler, uv, 0);
        float d = Texture2DSampleLevel(Depth, DepthSampler, uv, 0).r;
        float m = 1.0 - smoothstep(MaxDepth - 120.0, MaxDepth, d);
        sumC += c.rgb * m; sumW += saturate(1.0 - c.a) * m;
    }
}
float coverage = sumW / 16.0;
float3 col = sumC / max(sumW, 1e-4);
float3 ringC = 0; float ringW = 0;
float2 ring[4] = { px, -px, py, -py };
for (int k = 0; k < 4; k++)
{
    float4 c = Texture2DSampleLevel(Figure, FigureSampler, UV + ring[k], 0);
    float d = Texture2DSampleLevel(Depth, DepthSampler, UV + ring[k], 0).r;
    float m = (1.0 - smoothstep(MaxDepth - 120.0, MaxDepth, d)) * saturate(1.0 - c.a);
    ringC += c.rgb * m; ringW += m;
}
if (ringW > 3.5) col = max(col + Sharpen * (col - ringC / ringW), 0);
float3 enc = lerp(col * 12.92, 1.055 * pow(max(col, 1e-6), 1.0 / 2.4) - 0.055, step(0.0031308, col));
return float4(saturate(enc), saturate(coverage) * Fade);
"""


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

    default = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    figure = node(unreal.MaterialExpressionTextureObjectParameter, -900, -100)
    figure.set_editor_property("parameter_name", "Figure")
    figure.set_editor_property("texture", default)
    # Colour sampler + engine default: the float depth render target set at runtime is read raw.
    depth = node(unreal.MaterialExpressionTextureObjectParameter, -900, 120)
    depth.set_editor_property("parameter_name", "Depth")
    depth.set_editor_property("texture", default)
    uv = node(unreal.MaterialExpressionTextureCoordinate, -900, 320)
    fade = node(unreal.MaterialExpressionScalarParameter, -900, 420)
    fade.set_editor_property("parameter_name", "Fade")
    fade.set_editor_property("default_value", 1.0)
    # Depth mask: anything farther than MaxDepth (the far "ground" the renderer paints below the
    # horizon) is dropped; the champion is always nearer.
    max_depth = node(unreal.MaterialExpressionScalarParameter, -900, 520)
    max_depth.set_editor_property("parameter_name", "MaxDepth")
    max_depth.set_editor_property("default_value", 1.0e7)
    sharpen = node(unreal.MaterialExpressionScalarParameter, -900, 620)
    sharpen.set_editor_property("parameter_name", "Sharpen")
    sharpen.set_editor_property("default_value", 0.45)
    inputs = [("Figure", figure), ("Depth", depth), ("UV", uv), ("Fade", fade), ("MaxDepth", max_depth), ("Sharpen", sharpen)]
    custom_inputs = []
    for name, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        custom_inputs.append(ci)
    composite = node(unreal.MaterialExpressionCustom, -500, 100)
    composite.set_editor_property("code", COMPOSITE_HLSL)
    composite.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    composite.set_editor_property("description", "DraftComposite")
    composite.set_editor_property("inputs", custom_inputs)
    for name, source in inputs:
        connect(source, "", composite, name)
    rgb = node(unreal.MaterialExpressionComponentMask, -250, 40)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, on)
    alpha = node(unreal.MaterialExpressionComponentMask, -250, 200)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(channel, on)
    connect(composite, "", rgb, "")
    connect(composite, "", alpha, "")
    if not edit.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        report["errors"].append("cannot connect final colour")
    if not edit.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY):
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
        # CIRE_DRAFT_BACKGROUNDS=a,b re-imports only those scenes (the others keep their saved assets).
        only = [v for v in os.environ.get("CIRE_DRAFT_BACKGROUNDS", "").split(",") if v]
        if only and match.group(1) not in only:
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
    # CIRE_DRAFT_MATERIAL_ONLY=1 rebuilds the material without re-importing the scenes;
    # CIRE_DRAFT_BACKGROUNDS_ONLY=1 imports scenes without touching the material.
    if os.environ.get("CIRE_DRAFT_BACKGROUNDS_ONLY") == "1":
        report["material"] = "(unchanged)"
    else:
        build_material(report)
    if os.environ.get("CIRE_DRAFT_MATERIAL_ONLY") != "1":
        import_backgrounds(report)
    out = os.path.join(PROJECT, "Saved", "DraftSelectContent")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
    unreal.log("CIRE_DRAFT_SELECT_CONTENT_{} material={} backgrounds={} errors={}".format(
        "PASS" if not report["errors"] and report["material"] else "FAIL", report["material"], len(report["backgrounds"]), len(report["errors"])))


main()
