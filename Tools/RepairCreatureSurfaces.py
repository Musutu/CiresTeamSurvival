"""Repair copied creature BaseColorTex bindings, preserving all imported packages."""
from pathlib import Path
import hashlib,json,os
import unreal
ROOT=Path(__file__).resolve().parent.parent
DEST='/Game/Art/Characters/CreatureSurfaces02'
ROWS=[('bear','armored_bear_3d_model','SK_Bear',unreal.SkeletalMesh),('whisp','lantern_spirit_3d_model','SM_Whisp',unreal.StaticMesh),('evergrove_centaur','centaur_warrior_3d_model','SM_Centaur',unreal.StaticMesh)]
lib=unreal.EditorAssetLibrary
registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
content=Path('\\\\?\\'+str(ROOT/'Content')) if os.name=='nt' else ROOT/'Content'
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in content.rglob('*.uasset')}
result=[]
for profile,folder,name,kind in ROWS:
    source=f'/Game/TripoModels/{folder}/{folder}'
    output=f'{DEST}/{name}'
    if lib.does_asset_exist(output):raise RuntimeError('Refusing existing output: '+output)
    original=lib.load_asset(source)
    if not isinstance(original,kind):raise RuntimeError('Unexpected source type: '+source)
    candidates=[data.get_asset() for data in registry.get_assets_by_path(f'/Game/TripoModels/{folder}/Textures',recursive=True) if str(data.asset_name).lower().endswith('_basecolor')]
    if len(candidates)!=1 or not isinstance(candidates[0],unreal.Texture2D) or not candidates[0].get_editor_property('srgb'):raise RuntimeError('Need one verified sRGB base color for '+profile)
    texture=candidates[0]
    mesh=lib.duplicate_asset(source,output)
    prop='materials' if kind==unreal.SkeletalMesh else 'static_materials'
    slots=list(mesh.get_editor_property(prop));surfaces=[]
    if not slots:raise RuntimeError('Empty material slots: '+profile)
    for index,slot in enumerate(slots):
        material=slot.get_editor_property('material_interface')
        if not isinstance(material,unreal.MaterialInstanceConstant):raise RuntimeError('Expected imported PBR material instance: '+profile)
        names={str(n) for n in unreal.MaterialEditingLibrary.get_texture_parameter_names(material)}
        if 'BaseColorTex' not in names:raise RuntimeError('Missing PBR color parameter: '+profile)
        copy=lib.duplicate_asset(material.get_path_name(),f'{DEST}/MI_{profile}_{index}')
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(copy,'BaseColorTex',texture)
        unreal.MaterialEditingLibrary.update_material_instance(copy)
        if unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(copy,'BaseColorTex')!=texture:raise RuntimeError('Color assignment did not persist')
        if not lib.save_loaded_asset(copy,only_if_is_dirty=False):raise RuntimeError('Material save failed')
        slot.set_editor_property('material_interface',copy)
        surfaces.append({'slot':index,'source':material.get_path_name(),'copy':copy.get_path_name(),'baseColor':texture.get_path_name()})
    mesh.set_editor_property(prop,slots)
    if profile=='evergrove_centaur':mesh.set_editor_property('allow_cpu_access',True)
    if not lib.save_loaded_asset(mesh,only_if_is_dirty=False):raise RuntimeError('Mesh save failed')
    result.append({'profileId':profile,'source':source,'mesh':mesh.get_path_name(),'materials':surfaces})
for path,digest in before.items():
    if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest:raise RuntimeError('Existing main project package changed: '+path)
report={'passed':True,'originalPackagesUnchanged':len(before),'output':DEST,'assets':result}
(ROOT/'Saved/CreatureSurfacesBuild.json').write_text(json.dumps(report,indent=2)+'\n')
unreal.log('CIRE_CREATURE_SURFACES_PASS '+json.dumps(report))
