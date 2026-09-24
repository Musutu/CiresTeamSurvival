"""Original prototype combat art, authored locally; no cloud generation or paid assets.

Creates only CombatPrototype01 output. The imported meshes/skeletons and every
Preview01/02 locomotion asset are hash-checked and never saved. Animation poses
use analytic two-bone arm articulation in component space, converted back into
local quaternion tracks through the supported AnimDataController API. Imported
root scale100 and every local bone translation/scale remain intact.

All attacks last0.65s and release at0.25s. Lancer temporarily shares Ranger body.
Run using UE Python commandlet; -CireVerifyCombatArt performs read-only validation.
"""
from pathlib import Path
import hashlib
import json
import math
import unreal

ROOT = "/Game/Art/Characters/CombatPrototype01"
WEAPONS = "/Game/Art/Weapons/CombatPrototype01"
MODELS = {"Warden":"medieval_knight_armor_3d_model", "Ranger":"armored_archer_3d_model",
          "Scholar":"battlefield_healer_3d_model", "Lancer":"armored_archer_3d_model"}
# 40 keys intervals at60fps also divide exactly into the source compression30fps.
# Runtime maps this2/3-second storage clip onto the0.65-second action duration.
FRAMES = 40
RATE = 60

def require(value, message):
    if not value: raise RuntimeError(message)
def v(value): return (float(value.x), float(value.y), float(value.z))
def q(value): return (float(value.x), float(value.y), float(value.z), float(value.w))
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def mul(a,k): return tuple(x*k for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def length(a): return math.sqrt(dot(a,a))
def norm(a): return mul(a,1/max(length(a),1e-10))
def inv(a): return (-a[0],-a[1],-a[2],a[3])
def qm(a,b):
    av,bv=a[:3],b[:3]
    return (*add(add(mul(bv,a[3]),mul(av,b[3])),cross(av,bv)),a[3]*b[3]-dot(av,bv))
def rotate(a,b): return qm(qm(a,(*b,0)),inv(a))[:3]
def axis(a,angle):
    rad=math.radians(angle)*.5
    return (*mul(norm(a),math.sin(rad)),math.cos(rad))
def between(a,b):
    a,b=norm(a),norm(b)
    if dot(a,b)<-.9999: return axis(cross(a,(0,0,1)) if abs(a[2])<.9 else cross(a,(0,1,0)),180)
    out=(*cross(a,b),1+dot(a,b))
    return norm(out)
def slerp(a,b,t):
    if dot(a,b)<0: b=mul(b,-1)
    angle=math.acos(max(-1,min(1,dot(a,b))))
    if angle<1e-5:return norm(add(mul(a,1-t),mul(b,t)))
    return add(mul(a,math.sin((1-t)*angle)/math.sin(angle)),mul(b,math.sin(t*angle)/math.sin(angle)))

def options(mesh, mode="RAW"):
    result=unreal.AnimPoseEvaluationOptions()
    result.set_editor_property("evaluation_type",getattr(unreal.AnimDataEvalType,mode))
    result.set_editor_property("optional_skeletal_mesh",mesh)
    result.set_editor_property("should_retarget",False)
    result.set_editor_property("extract_root_motion",False)
    result.set_editor_property("incorporate_root_motion_into_pose",True)
    return result

def fk(locals_,parents):
    output={}
    for name,(pos,rot,scale) in locals_.items():
        parent=parents[name]
        if parent not in output:output[name]=(pos,rot,scale)
        else:
            pp,pq,ps=output[parent]
            output[name]=(add(pp,rotate(pq,tuple(a*b for a,b in zip(pos,ps)))),qm(pq,rot),tuple(a*b for a,b in zip(scale,ps)))
    return output

def set_world_rotation(locals_,parents,name,rotation):
    worlds=fk(locals_,parents)
    parent=parents[name]
    local=qm(inv(worlds[parent][1]),rotation) if parent in worlds else rotation
    pos,_,scale=locals_[name]
    locals_[name]=(pos,norm(local),scale)

def solve_arm(locals_,parents,side,target,hint,hand_rotation):
    upper,lower,hand=[part+"_"+side for part in ("upperarm","lowerarm","hand")]
    world=fk(locals_,parents)
    a,b,c=[world[n][0] for n in (upper,lower,hand)]
    l1,l2=length(sub(b,a)),length(sub(c,b))
    direction=norm(sub(target,a)); distance=min(max(length(sub(target,a)),abs(l1-l2)+.01),l1+l2-.01)
    bend=norm(sub(sub(hint,a),mul(direction,dot(sub(hint,a),direction))))
    along=(l1*l1-l2*l2+distance*distance)/(2*distance)
    elbow=add(add(a,mul(direction,along)),mul(bend,math.sqrt(max(0,l1*l1-along*along))))
    end=add(a,mul(direction,distance))
    set_world_rotation(locals_,parents,upper,qm(between(sub(b,a),sub(elbow,a)),world[upper][1]))
    world=fk(locals_,parents)
    set_world_rotation(locals_,parents,lower,qm(between(sub(world[hand][0],world[lower][0]),sub(end,world[lower][0])),world[lower][1]))
    set_world_rotation(locals_,parents,hand,hand_rotation)

def authored_poses(name,base,parents,reference,height):
    initial=fk(base,parents)
    forward=norm(add(sub(reference["ball_l"][0],reference["foot_l"][0]),sub(reference["ball_r"][0],reference["foot_r"][0])))
    forward=norm((forward[0],forward[1],0)); up=(0,0,1); right=cross(up,forward)
    chest=initial["spine_03"][0]
    def point(f,r,u):return add(chest,mul(add(add(mul(forward,f),mul(right,r)),mul(up,u)),height))
    def pose(kind):
        result=dict(base)
        # Visible anticipation comes from torso twist as well as hands; feet remain planted.
        twists={"Warden":(-24,22,12),"Ranger":(-14,-10,-8),"Scholar":(-15,12,5),"Lancer":(-28,25,15)}[name]
        phase={"windup":0,"release":1,"follow":2}[kind]
        for bone in ("spine_01","spine_02","spine_03"):
            world=fk(result,parents)
            set_world_rotation(result,parents,bone,qm(axis(up,twists[phase]/3),world[bone][1]))
        if name=="Warden":
            goals={"windup":(.01,.25,.20),"release":(.30,-.12,.02),"follow":(.16,-.19,-.10)}
            rh=point(*goals[kind]);lh=point(.14,-.23,-.015)
            sword_angles={"windup":-40,"release":70,"follow":115}
            rq=qm(axis(right,sword_angles[kind]),reference["hand_r"][1])
            lq=reference["hand_l"][1]
        elif name=="Ranger":
            lh=point(.34,-.035,.055)
            rh=point(*({"windup":(.015,.09,.095),"release":(-.03,.13,.09),"follow":(-.07,.16,.045)}[kind]))
            rq=reference["hand_r"][1];lq=reference["hand_l"][1]
        elif name=="Lancer":
            rh=point(*({"windup":(-.12,.20,.22),"release":(.36,.06,.10),"follow":(.30,-.035,-.07)}[kind]))
            lh=point(.09,-.20,-.05)
            rq=qm(axis(right, {"windup":-12,"release":0,"follow":18}[kind]),reference["hand_r"][1])
            lq=reference["hand_l"][1]
        else:
            rh=point(*({"windup":(.06,.15,.12),"release":(.34,.075,.075),"follow":(.25,.10,-.02)}[kind]))
            lh=point(*({"windup":(.07,-.14,.04),"release":(.23,-.17,.07),"follow":(.17,-.20,-.03)}[kind]))
            rq=qm(axis(right,55),reference["hand_r"][1]);lq=qm(axis(right,35),reference["hand_l"][1])
        solve_arm(result,parents,"r",rh,point(-.03,.32,.06),rq)
        solve_arm(result,parents,"l",lh,point(.03,-.33,-.08),lq)
        return result
    return [(0.,base),(.18,pose("windup")),(.25,pose("release")),(.36,pose("follow")),(.65,base)]

def sample(poses,time):
    for (a,left),(b,right) in zip(poses,poses[1:]):
        if time<=b+1e-7:
            alpha=max(0,min(1,(time-a)/(b-a)));alpha=alpha*alpha*(3-2*alpha)
            return {name:(pos,slerp(rot,right[name][1],alpha),scale) for name,(pos,rot,scale) in left.items()}
    return poses[-1][1]

def make_clip(name,mesh):
    source_name="Ranger" if name=="Lancer" else name
    idle=unreal.load_asset(f"/Game/Art/Characters/TripoRetarget/Preview02/{source_name}/Animations/MM_Idle_{source_name}")
    require(idle is not None,"Missing protected idle")
    component=unreal.SkeletalMeshComponent();component.set_skeletal_mesh_asset(mesh)
    names=[str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
    parents={n:str(component.get_parent_bone(n)) for n in names}
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(idle,0,options(mesh))
    base={};reference={}
    for n in names:
        value=unreal.AnimPoseExtensions.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL)
        base[n]=(v(value.translation),q(value.rotation),v(value.scale3d))
        value=unreal.AnimPoseExtensions.get_ref_bone_pose(pose,n,unreal.AnimPoseSpaces.WORLD)
        reference[n]=(v(value.translation),q(value.rotation),v(value.scale3d))
    require(max(abs(s-100) for s in base["root"][2])<.001,"Protected idle has wrong root scale")
    height=float(mesh.get_imported_bounds().box_extent.z*2)
    poses=authored_poses(name,base,parents,reference,height)
    path=f"{ROOT}/{name}/A_{name}_Attack"
    clip=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(idle.get_path_name(),path)
    require(clip is not None,"Could not create attack copy")
    controller=clip.get_editor_property("controller")
    controller.open_bracket("Author local prototype combat poses",False)
    try:
        controller.set_frame_rate(unreal.FrameRate(RATE,1),False)
        controller.set_number_of_frames(unreal.FrameNumber(FRAMES),False)
        keys=[sample(poses,i/FRAMES*.65) for i in range(FRAMES+1)]
        for bone in names:
            positions=[unreal.Vector(*frame[bone][0]) for frame in keys]
            rotations=[unreal.Quat(*frame[bone][1]) for frame in keys]
            scales=[unreal.Vector(*frame[bone][2]) for frame in keys]
            require(controller.set_bone_track_keys(bone,positions,rotations,scales,False),"Could not author "+bone)
    finally:controller.close_bracket(False)
    clip.set_editor_property("enable_root_motion",False)
    clip.set_editor_property("force_root_lock",True)
    require(unreal.EditorAssetLibrary.save_loaded_asset(clip,only_if_is_dirty=False),"Attack save failed")
    return clip

class Obj:
    def __init__(self):self.lines=[];self.count=0
    def poly(self,vertices,faces,material):
        self.lines.append("usemtl "+material)
        self.lines.extend("v %.6f %.6f %.6f"%p for p in vertices)
        # Interchange OBJ translator requires explicit UV indices even for solid materials.
        self.lines.extend("vt %.6f %.6f"%(p[0]*.01,p[2]*.01) for p in vertices)
        self.lines.extend("f "+" ".join(f"{self.count+i+1}/{self.count+i+1}" for i in face) for face in faces)
        self.count+=len(vertices)
    def box(self,center,size,material):
        x,y,z=center;a,b,c=mul(size,.5)
        self.poly([(x+sx*a,y+sy*b,z+sz*c) for sx,sy,sz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],
                  [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material)
    def rod(self,start,end,radius,material,sides=10):
        direction=norm(sub(end,start));u=norm(cross(direction,(0,1,0) if abs(direction[1])<.9 else (1,0,0)));w=cross(direction,u)
        vertices=[add(p,add(mul(u,radius*math.cos(i*2*math.pi/sides)),mul(w,radius*math.sin(i*2*math.pi/sides)))) for p in (start,end) for i in range(sides)]
        faces=[tuple(range(sides-1,-1,-1)),tuple(range(sides,2*sides))]
        faces += [(i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides)]
        self.poly(vertices,faces,material)

def build_weapons(saved):
    directory=saved/"CombatPrototypeSources";directory.mkdir(exist_ok=True)
    specs={}
    sword=Obj();sword.rod((0,0,-10),(0,0,3),1.65,"M_Dusk");sword.box((0,0,4),(22,4,3),"M_Gold")
    sword.poly([(-3,-.8,5),(3,-.8,5),(3,.8,5),(-3,.8,5),(-2,-.6,65),(2,-.6,65),(2,.6,65),(-2,.6,65),(0,0,77)],
               [(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,8),(5,6,8),(6,7,8),(7,4,8),(3,2,1,0)],"M_Metal")
    sword.rod((0,0,-13),(0,0,-10),2.8,"M_Gold");specs["Sword"]=sword
    shield=Obj();outline=[(-22,25),(22,25),(26,8),(19,-18),(0,-34),(-19,-18),(-26,8)]
    shield.poly([(x,y,z) for x in (-3,3) for y,z in outline],[tuple(range(7)),tuple(range(13,6,-1))]+[(i+7,(i+1)%7+7,(i+1)%7,i) for i in range(7)],"M_Slate")
    for a,b in zip(outline,outline[1:]+outline[:1]):shield.rod((3,a[0],a[1]),(3,b[0],b[1]),1.8,"M_Gold",6)
    shield.box((4,0,2),(2,4,38),"M_Gold");shield.box((4,0,7),(2,30,4),"M_Gold");specs["Shield"]=shield
    bow=Obj();points=[(-9,0,-52),(-1,0,-45),(8,0,-32),(11,0,-20),(3,0,-9),(0,0,0),(3,0,9),(11,0,20),(8,0,32),(-1,0,45),(-9,0,52)]
    for a,b in zip(points,points[1:]):bow.rod(a,b,1.7,"M_Gold",8)
    bow.rod((0,0,-8),(0,0,8),2.5,"M_Dusk");specs["Bow"]=bow
    arrow=Obj();arrow.rod((-52,0,0),(8,0,0),.55,"M_Gold",6)
    arrow.poly([(8,-1.8,-1.8),(8,1.8,-1.8),(8,1.8,1.8),(8,-1.8,1.8),(16,0,0)],[(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],"M_Metal")
    arrow.box((-45,0,0),(8,.4,6),"M_Slate");specs["Arrow"]=arrow
    lance=Obj();lance.rod((-90,0,0),(72,0,0),1.8,"M_Dusk");lance.rod((-12,0,0),(12,0,0),2.2,"M_Gold")
    lance.poly([(68,-4,0),(68,0,-2),(68,4,0),(68,0,2),(103,0,0)],[(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],"M_Metal");specs["Lance"]=lance
    created=[]
    for kind,obj in specs.items():
        asset="SM_Prototype"+kind; path=directory/(asset+".obj")
        material_file=directory/(asset+".mtl")
        material_file.write_text("\n".join("newmtl "+n+"\nKd 0.5 0.5 0.5" for n in ("M_Dusk","M_Gold","M_Slate","M_Metal")),encoding="ascii")
        path.write_text("# Original Cire combat prototype; centimeters; +X forward, +Z up\nmtllib "+material_file.name+"\n"+"\n".join(obj.lines),encoding="ascii")
        task=unreal.AssetImportTask();task.set_editor_property("filename",str(path));task.set_editor_property("destination_path",WEAPONS)
        task.set_editor_property("destination_name",asset);task.set_editor_property("automated",True);task.set_editor_property("replace_existing",True);task.set_editor_property("save",True)
        opts=unreal.FbxImportUI();opts.set_editor_property("import_mesh",True);opts.set_editor_property("import_materials",False);opts.set_editor_property("import_textures",False)
        opts.set_editor_property("import_as_skeletal",False);opts.set_editor_property("mesh_type_to_import",unreal.FBXImportType.FBXIT_STATIC_MESH)
        data=opts.get_editor_property("static_mesh_import_data");data.set_editor_property("combine_meshes",True);data.set_editor_property("auto_generate_collision",False)
        data.set_editor_property("convert_scene",False);data.set_editor_property("convert_scene_unit",False)
        task.set_editor_property("options",opts)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        mesh=unreal.load_asset(WEAPONS+"/"+asset);require(isinstance(mesh,unreal.StaticMesh),"Missing weapon import "+asset)
        slots=mesh.get_editor_property("static_materials")
        for index,slot in enumerate(slots):
            material_name=str(slot.get_editor_property("imported_material_slot_name"))
            material=unreal.load_asset("/Game/Art/Materials/"+material_name)
            if not material:material=unreal.load_asset("/Game/Art/Materials/M_Metal")
            mesh.set_material(index,material)
        require(unreal.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False),"Weapon save failed")
        created.append(str(mesh.get_path_name()))
    return created

def protected_hashes(content):
    paths=list((content/"Art/Characters/TripoRetarget").rglob("*.uasset"))
    for model in set(MODELS.values()):paths+=list((content/"TripoModels"/model).glob("*.uasset"))
    return {str(p.relative_to(content)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}

def validate(clip,mesh):
    require(abs(clip.get_play_length()-FRAMES/RATE)<.001,"Unexpected stored attack duration")
    require(clip.get_editor_property("skeleton")==mesh.get_editor_property("skeleton"),"Wrong attack skeleton")
    positions=[];rows=[]
    for mode in ("RAW","COMPRESSED"):
        for time in (0,.18,.25,.36,.65):
            pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(clip,time/.65*clip.get_play_length(),options(mesh,mode))
            require(unreal.AnimPoseExtensions.is_valid(pose),"Invalid attack pose")
            root=unreal.AnimPoseExtensions.get_bone_pose(pose,"root",unreal.AnimPoseSpaces.LOCAL)
            require(max(abs(s-100) for s in v(root.scale3d))<.01,"Attack root scale damaged")
            data={}
            for bone in ("head","foot_l","foot_r","hand_l","hand_r"):
                tr=unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
                p=v(tr.translation);require(all(math.isfinite(x) and abs(x)<300 for x in p),"Invalid attack bone "+bone);data[bone]=p
            require(45<data["head"][2]-min(data["foot_l"][2],data["foot_r"][2])<125,"Attack body proportions damaged")
            if mode=="RAW":positions.append(data["hand_r"])
            rows.append({"mode":mode,"time":time,"positions":data})
    require(length(sub(positions[1],positions[2]))>3,"Attack lacks a visible articulated release")
    return rows

def run():
    verify="-cireverifycombatart" in unreal.SystemLibrary.get_command_line().lower().split()
    saved=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    content=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    before=protected_hashes(content)
    report={"art_status":"original local prototype; Lancer temporarily shares Ranger body", "release_seconds":.25,"duration_seconds":.65,"originals_saved":False,"verify_only":verify,"attacks":{}}
    try:
        for name,model in MODELS.items():
            mesh=unreal.load_asset(f"/Game/TripoModels/{model}/{model}")
            clip=unreal.load_asset(f"{ROOT}/{name}/A_{name}_Attack") if verify else make_clip(name,mesh)
            require(clip is not None,"Missing combat clip")
            report["attacks"][name]={"asset":str(clip.get_path_name()),"samples":validate(clip,mesh)}
        if not verify:build_weapons(saved)
        report["weapons"]=[]
        expected={"Sword":(22,4,90),"Shield":(10,56,63),"Bow":(24,6,108),"Arrow":(68,4,6),"Lance":(193,8,5)}
        for kind,size in expected.items():
            weapon=unreal.load_asset(WEAPONS+"/SM_Prototype"+kind)
            require(isinstance(weapon,unreal.StaticMesh),"Missing weapon "+kind)
            bounds=weapon.get_bounds();actual=mul(v(bounds.box_extent),2)
            require(max(actual)<250 and max(actual)>40,"Weapon has invalid world dimensions "+kind)
            report["weapons"].append({"asset":str(weapon.get_path_name()),"size_cm":actual,
                                      "materials":[str(s.get_editor_property("material_interface").get_path_name()) for s in weapon.get_editor_property("static_materials")]})
        require(before==protected_hashes(content),"Protected original or locomotion asset bytes changed")
        report["protected_asset_count"]=len(before);report["protected_hashes_unchanged"]=True
        report["status"]="verified_needs_visual_review"
        unreal.log("CIRE_COMBAT_ART_PASS")
    except Exception as error:
        report["status"]="failed";report["error"]=str(error);raise
    finally:(saved/("CombatPrototypeVerification.json" if verify else "CombatPrototypeBuild.json")).write_text(json.dumps(report,indent=2),encoding="utf-8")

if __name__=="__main__":run()
