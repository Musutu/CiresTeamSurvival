"""Read-only custom-form mesh/reference-pose inspection in isolated CreatureBuilder."""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parent.parent
def vec(v): return [float(v.x), float(v.y), float(v.z)]
def transform(t):
    q=t.rotation
    return {"position":vec(t.translation),"scale":vec(t.scale3d),"rotation":[q.x,q.y,q.z,q.w]}

def run():
    result={"readOnlyOriginalAssets":True,"models":{}}
    for name in ("armored_bear_3d_model","centaur_warrior_3d_model","lantern_spirit_3d_model"):
        mesh=unreal.load_asset(f"/Game/TripoModels/{name}/{name}")
        if not mesh: raise RuntimeError("Missing staged source "+name)
        b=mesh.get_imported_bounds() if isinstance(mesh,unreal.SkeletalMesh) else mesh.get_bounds()
        row={"class":mesh.get_class().get_name(),"mesh":mesh.get_path_name(),"origin":vec(b.origin),"extent":vec(b.box_extent)}
        result["models"][name]=row
        if isinstance(mesh,unreal.SkeletalMesh):
            component=unreal.SkeletalMeshComponent();component.set_skeletal_mesh_asset(mesh)
            rig=unreal.IKRigDefinition();controller=unreal.IKRigController.get_controller(rig)
            controller.set_skeletal_mesh(mesh)
            row["bones"]={}
            for i in range(component.get_num_bones()):
                n=str(component.get_bone_name(i));row["bones"][n]=transform(controller.get_ref_pose_transform_of_bone(n))
                row["bones"][n]["parent"]=str(component.get_parent_bone(n))
        else:
            sections=[]
            for index in range(mesh.get_num_sections(0)):
                positions,triangles,normals,uvs,tangents=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,index)
                sections.append({"vertices":[vec(p) for p in positions],"triangles":list(triangles),"normals":[vec(p) for p in normals],"uvs":[[v.x,v.y] for v in uvs]})
            row["sections"]=sections
    path=ROOT/"Saved/CreatureMotionInspection.json";path.write_text(json.dumps(result),encoding="utf-8")
    unreal.log("CIRE_CREATURE_INSPECTION_PASS models=3 file="+str(path))
run()
