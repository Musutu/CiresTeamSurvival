"""Create a CPU-readable centaur copy; never modify the imported packages."""
from pathlib import Path
import hashlib,json
import unreal
ROOT=Path(__file__).resolve().parent.parent
SOURCE='/Game/TripoModels/centaur_warrior_3d_model/centaur_warrior_3d_model'
DEST='/Game/Art/Characters/CreatureMotion01/SM_CentaurMotion'
lib=unreal.EditorAssetLibrary
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'Content/TripoModels/centaur_warrior_3d_model').rglob('*.uasset')}
mesh=lib.load_asset(DEST) if lib.does_asset_exist(DEST) else lib.duplicate_asset(SOURCE,DEST)
if not isinstance(mesh,unreal.StaticMesh):raise RuntimeError('Centaur source is not a StaticMesh')
mesh.set_editor_property('allow_cpu_access',True)
if not lib.save_loaded_asset(mesh,only_if_is_dirty=False):raise RuntimeError('Could not save generated centaur')
vertices,triangles,_,_,_=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,0)
if len(vertices)<1000 or len(vertices)>30000 or not triangles:raise RuntimeError('Invalid centaur section budget')
for p,h in before.items():
    if hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h:raise RuntimeError('Imported package changed: '+p)
report={'status':'generated','asset':mesh.get_path_name(),'allowCpuAccess':bool(mesh.get_editor_property('allow_cpu_access')),'vertices':len(vertices),'triangles':len(triangles)//3,'originalPackagesUnchanged':True}
(ROOT/'Saved/CreatureMotionBuild.json').write_text(json.dumps(report,indent=2))
unreal.log('CIRE_CREATURE_CONTENT_PASS '+json.dumps(report))
