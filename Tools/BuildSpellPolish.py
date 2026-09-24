"""Create two fresh depth-tested spell materials, then separately reload/verify.

Run in the coordinated isolated CreatureBuilder project with -CireSpellPolishBuild
and a second process with -CireSpellPolishVerify. Default ordinary Python is a
read-only plan. Existing spell/audio/ground materials are never modified.
"""
from pathlib import Path
import json
import shutil
import sys

try:
    import unreal
except ImportError:
    unreal=None

PROJECT=Path(__file__).resolve().parent.parent
OUTPUT="/Game/Art/Effects/CireSpellPolish01"
NAMES=("M_SpellCore","M_SpellSoft")


def require(value,message):
    if not value:raise RuntimeError(message)


def build_material(name):
    edit=unreal.MaterialEditingLibrary
    require(not unreal.EditorAssetLibrary.does_asset_exist(OUTPUT+"/"+name),"Refusing existing material "+name)
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,OUTPUT,unreal.Material,unreal.MaterialFactoryNew())
    require(material is not None,"Material creation failed")
    material.set_editor_property("blend_mode",unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided",True)
    material.set_editor_property("disable_depth_test",False)
    def node(cls,x,y):return edit.create_material_expression(material,cls,x,y)
    def connect(a,output,b,input_):require(edit.connect_material_expressions(a,output,b,input_),"Cannot connect "+input_)
    vertex=node(unreal.MaterialExpressionVertexColor,-800,-100)
    require(edit.connect_material_property(vertex,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR),"Cannot connect emissive")
    fade=node(unreal.MaterialExpressionDepthFade,300,120)
    fade.set_editor_property("fade_distance_default",5 if name=="M_SpellCore" else 18)
    if name=="M_SpellCore":connect(vertex,"A",fade,"Opacity")
    else:
        uv=node(unreal.MaterialExpressionTextureCoordinate,-800,180)
        center=node(unreal.MaterialExpressionConstant2Vector,-800,340)
        center.set_editor_property("r",.5);center.set_editor_property("g",.5)
        subtract=node(unreal.MaterialExpressionSubtract,-580,180);connect(uv,"",subtract,"A");connect(center,"",subtract,"B")
        # Unreal shortens unnamed single input pins; empty name selects input zero.
        radius=node(unreal.MaterialExpressionLength,-400,180);connect(subtract,"",radius,"")
        twice=node(unreal.MaterialExpressionMultiply,-250,180);twice.set_editor_property("const_b",2);connect(radius,"",twice,"A")
        inverse=node(unreal.MaterialExpressionOneMinus,-100,180);connect(twice,"",inverse,"")
        clamp=node(unreal.MaterialExpressionSaturate,30,180);connect(inverse,"",clamp,"")
        feather=node(unreal.MaterialExpressionPower,150,180);feather.set_editor_property("const_exponent",2.4);connect(clamp,"",feather,"Base")
        opacity=node(unreal.MaterialExpressionMultiply,160,350);connect(feather,"",opacity,"A");connect(vertex,"A",opacity,"B")
        connect(opacity,"",fade,"Opacity")
    require(edit.connect_material_property(fade,"",unreal.MaterialProperty.MP_OPACITY),"Cannot connect opacity")
    edit.layout_material_expressions(material);edit.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False),"Cannot save "+name)


def inspect(name):
    material=unreal.load_asset(OUTPUT+"/"+name);edit=unreal.MaterialEditingLibrary
    require(isinstance(material,unreal.Material),"Missing material "+name)
    require(material.get_editor_property("blend_mode")==unreal.BlendMode.BLEND_ADDITIVE,"Wrong blending "+name)
    require(material.get_editor_property("two_sided") and not material.get_editor_property("disable_depth_test"),"Depth test/two sided invalid "+name)
    require(edit.get_material_property_input_node(material,unreal.MaterialProperty.MP_EMISSIVE_COLOR) is not None,"No emissive graph")
    require(edit.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY) is not None,"No opacity graph")
    count=edit.get_num_material_expressions(material)
    require(count==(2 if name=="M_SpellCore" else 11),"Incomplete saved material graph: "+name+" "+str(count))
    return dict(asset=str(material.get_path_name()),expressions=count,depthTest=True)


def run():
    if unreal is None:return dict(status="plan_only",materials=list(NAMES),output=OUTPUT,textureDependencies=0,originalMaterialsModified=False)
    if str(PROJECT/"Tools") not in sys.path:sys.path.insert(0,str(PROJECT/"Tools"))
    import IntegrateTripoBatch as protection
    tokens=unreal.SystemLibrary.get_command_line().lower().split()
    build="-cirespellpolishbuild" in tokens;verify="-cirespellpolishverify" in tokens
    resume="-cirespellpolishresume" in tokens
    require(build!=verify,"Choose exactly one build or separate verify commandlet")
    content=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
    report_path=PROJECT/"Saved/SpellPolish01/build.json"
    report=dict(visualReviewAccepted=False,engineContent=str(content),originalMaterialsModified=False)
    relative=OUTPUT[len("/Game/"):]
    if build:
        require(not (PROJECT/"Content"/relative).exists(),"Fresh real spell polish namespace required")
        if resume:
            before=protection.read_json(report_path)
            require(before["status"]=="failed" and Path(before["engineContent"]).resolve()==content,"No matching failed build to resume")
            report["protectedHashes"]=before["protectedHashes"]
            report["resumedMissingAssetsOnly"]=True
            protection.validate_hashes(PROJECT/"Content",report["protectedHashes"])
        else:
            require(not unreal.EditorAssetLibrary.does_directory_exist(OUTPUT),"Fresh isolated spell polish namespace required")
            report["protectedHashes"]=protection.original_hashes(PROJECT/"Content",relative)
        try:
            for name in NAMES:
                if resume and unreal.EditorAssetLibrary.does_asset_exist(OUTPUT+"/"+name):inspect(name)
                else:build_material(name)
            report["materials"]=[inspect(name) for name in NAMES]
            protection.validate_hashes(PROJECT/"Content",report["protectedHashes"])
            report["status"]="built_needs_reload_validation"
            unreal.log("CIRE_SPELL_POLISH_BUILD_PASS materials=2")
        except Exception as error:report.update(status="failed",error=str(error));raise
        finally:protection.atomic_json(report_path,report)
    else:
        before=protection.read_json(report_path)
        require(before["status"]=="built_needs_reload_validation" and Path(before["engineContent"]).resolve()==content,"Missing matching successful build")
        protection.validate_hashes(PROJECT/"Content",before["protectedHashes"])
        report["materials"]=[inspect(name) for name in NAMES]
        source=content/relative;destination=PROJECT/"Content"/relative
        require(destination.resolve().is_relative_to((PROJECT/"Content").resolve()),"Output escapes Content")
        expected={name+".uasset":protection.digest(source/(name+".uasset")) for name in NAMES}
        if source.resolve()!=destination.resolve():
            require({p.name for p in source.iterdir() if p.is_file()}==set(expected),"Unexpected output package")
            if destination.exists():
                require({p.name:protection.digest(p) for p in destination.iterdir() if p.is_file()}==expected,"Refusing differing real materials")
            else:
                destination.mkdir(parents=True)
                for filename in expected:shutil.copy2(source/filename,destination/filename)
        protection.validate_hashes(destination,expected);protection.validate_hashes(PROJECT/"Content",before["protectedHashes"])
        report.update(status="saved_materials_verified",protectedPackages=len(before["protectedHashes"]),publishedPackages=2)
        protection.atomic_json(PROJECT/"Saved/SpellPolish01/verify.json",report)
        unreal.log("CIRE_SPELL_POLISH_VERIFY_PASS materials=2")
    return {k:v for k,v in report.items() if k!="protectedHashes"}


if __name__=="__main__":
    try:print(json.dumps(run(),indent=2))
    except Exception as error:
        if unreal:unreal.log_error("CIRE_SPELL_POLISH_FAIL "+str(error))
        raise
