"""Create the two aura materials in /Game/Art/FX/Auras (aura-vfx).

Run with the editor commandlet (engine Python inside Unreal):

  UnrealEditor-Cmd.exe CiresTeamSurvival.uproject -run=pythonscript -script=Tools/BuildAuraMaterials.py -unattended -nosplash

M_AuraCore  additive unlit, depth-tested, two-sided. Emissive = vertex colour x Boost with a
            slow rising shimmer band in world space; opacity = vertex alpha x depth fade.
M_AuraSoft  additive unlit camera/ground glows. Radial UV falloff x vertex alpha x depth fade.

Only these two new packages are created; existing materials are never modified. Running
again rebuilds them in place (they are owned by the aura system).
"""
import unreal

OUTPUT = "/Game/Art/FX/Auras"


def require(value, message):
    if not value:
        raise RuntimeError(message)


def build(name, soft):
    path = OUTPUT + "/" + name
    edit = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
        edit.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, OUTPUT, unreal.Material, unreal.MaterialFactoryNew())
    require(material is not None, "Material creation failed " + name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", False)

    def node(cls, x, y):
        return edit.create_material_expression(material, cls, x, y)

    def connect(a, output, b, input_):
        require(edit.connect_material_expressions(a, output, b, input_), "Cannot connect %s.%s -> %s" % (name, output, input_))

    vertex = node(unreal.MaterialExpressionVertexColor, -1200, -200)
    boost = node(unreal.MaterialExpressionScalarParameter, -1200, -40)
    boost.set_editor_property("parameter_name", "Boost")
    boost.set_editor_property("default_value", 2.2 if soft else 3.0)
    emissive = node(unreal.MaterialExpressionMultiply, -900, -160)
    connect(vertex, "", emissive, "A")
    connect(boost, "", emissive, "B")
    final_emissive = emissive
    if not soft:
        # Rising shimmer: 0.78 + 0.3 * sin(dot(world, k) - time * 4.5)
        world = node(unreal.MaterialExpressionWorldPosition, -1500, 200)
        k = node(unreal.MaterialExpressionConstant3Vector, -1500, 340)
        k.set_editor_property("constant", unreal.LinearColor(0.021, 0.017, 0.055, 0))
        dot = node(unreal.MaterialExpressionDotProduct, -1300, 240)
        connect(world, "", dot, "A")
        connect(k, "", dot, "B")
        time = node(unreal.MaterialExpressionTime, -1300, 380)
        speed = node(unreal.MaterialExpressionMultiply, -1150, 380)
        speed.set_editor_property("const_b", 4.5)
        connect(time, "", speed, "A")
        phase = node(unreal.MaterialExpressionSubtract, -1000, 280)
        connect(dot, "", phase, "A")
        connect(speed, "", phase, "B")
        wave = node(unreal.MaterialExpressionSine, -850, 280)
        wave.set_editor_property("period", 6.2831853)
        connect(phase, "", wave, "")
        amplitude = node(unreal.MaterialExpressionScalarParameter, -850, 420)
        amplitude.set_editor_property("parameter_name", "Shimmer")
        amplitude.set_editor_property("default_value", 0.3)
        scaled = node(unreal.MaterialExpressionMultiply, -700, 300)
        connect(wave, "", scaled, "A")
        connect(amplitude, "", scaled, "B")
        bias = node(unreal.MaterialExpressionAdd, -560, 300)
        bias.set_editor_property("const_b", 0.8)
        connect(scaled, "", bias, "A")
        shimmer = node(unreal.MaterialExpressionMultiply, -420, -100)
        connect(emissive, "", shimmer, "A")
        connect(bias, "", shimmer, "B")
        final_emissive = shimmer
    require(edit.connect_material_property(final_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "Cannot connect emissive " + name)

    fade = node(unreal.MaterialExpressionDepthFade, -200, 300)
    fade.set_editor_property("fade_distance_default", 16 if soft else 6)
    if soft:
        uv = node(unreal.MaterialExpressionTextureCoordinate, -1200, 500)
        center = node(unreal.MaterialExpressionConstant2Vector, -1200, 640)
        center.set_editor_property("r", 0.5)
        center.set_editor_property("g", 0.5)
        offset = node(unreal.MaterialExpressionSubtract, -1000, 540)
        connect(uv, "", offset, "A")
        connect(center, "", offset, "B")
        radius = node(unreal.MaterialExpressionLength, -860, 540)
        connect(offset, "", radius, "")
        twice = node(unreal.MaterialExpressionMultiply, -720, 540)
        twice.set_editor_property("const_b", 2)
        connect(radius, "", twice, "A")
        inverse = node(unreal.MaterialExpressionOneMinus, -580, 540)
        connect(twice, "", inverse, "")
        clamp = node(unreal.MaterialExpressionSaturate, -460, 540)
        connect(inverse, "", clamp, "")
        feather = node(unreal.MaterialExpressionPower, -340, 540)
        feather.set_editor_property("const_exponent", 2.2)
        connect(clamp, "", feather, "Base")
        opacity = node(unreal.MaterialExpressionMultiply, -340, 400)
        connect(feather, "", opacity, "A")
        connect(vertex, "A", opacity, "B")
        connect(opacity, "", fade, "Opacity")
    else:
        connect(vertex, "A", fade, "Opacity")
    require(edit.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY), "Cannot connect opacity " + name)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False), "Cannot save " + name)
    unreal.log("CIRE_AURA_MATERIAL_SAVED %s expressions=%d" % (path, edit.get_num_material_expressions(material)))


def main():
    if not unreal.EditorAssetLibrary.does_directory_exist(OUTPUT):
        unreal.EditorAssetLibrary.make_directory(OUTPUT)
    build("M_AuraCore", False)
    build("M_AuraSoft", True)
    unreal.log("CIRE_AURA_MATERIALS_PASS")


try:
    main()
except Exception as error:  # surfaced to the runner through the log marker
    unreal.log_error("CIRE_AURA_MATERIALS_FAIL " + str(error))
    raise
