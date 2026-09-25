"""Original centimeter-scale weapon props; default is read-only geometry/data preflight.

--sources writes reproducible OBJ/MTL source art. Unreal -CireArmoryBuild imports
only a fresh ArmoryPrototype01 folder. A separate -CireArmoryVerify process checks
saved mesh dimensions/materials and hashes of every preexisting Unreal package.
This script never modifies a character, skeleton, animation, or legacy weapon.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import shutil
import sys

try:
    import unreal
except ImportError:
    unreal = None

PROJECT = Path(__file__).resolve().parent.parent
OUTPUT = "/Game/Art/Weapons/ArmoryPrototype01"
MATERIALS = {
    "Wood": ((.15, .052, .019), .78, 0, 0),
    "Leather": ((.038, .024, .021), .86, 0, 0),
    "Steel": ((.32, .39, .43), .3, .85, 0),
    "Brass": ((.43, .26, .067), .36, .72, 0),
    "Stone": ((.14, .18, .16), .9, 0, 0),
    "Violet": ((.23, .065, .48), .28, .15, .6),
    "Ember": ((.7, .23, .045), .31, .1, .6),
    "Grove": ((.085, .32, .095), .5, 0, .15),
    "Light": ((.68, .52, .24), .35, .1, .5),
}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, k): return tuple(x*k for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]
def length(a): return math.sqrt(dot(a, a))
def norm(a): return mul(a, 1/max(length(a), 1e-10))


class Mesh:
    def __init__(self):
        self.vertices, self.faces = [], []

    def poly(self, vertices, faces, material):
        offset = len(self.vertices)
        self.vertices += vertices
        self.faces += [(tuple(offset+i for i in face), material) for face in faces]

    def box(self, center, size, material):
        x, y, z = center; a, b, c = mul(size, .5)
        self.poly([(x+sx*a, y+sy*b, z+sz*c) for sx, sy, sz in
                   [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],
                  [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)], material)

    def rod(self, start, end, radius, material, sides=10):
        direction = norm(sub(end, start))
        require(length(sub(end, start)) > .001, "Zero-length modeled rod")
        u = norm(cross(direction, (0,1,0) if abs(direction[1]) < .9 else (1,0,0))); w = cross(direction, u)
        points = [add(p, add(mul(u, radius*math.cos(i*2*math.pi/sides)), mul(w, radius*math.sin(i*2*math.pi/sides))))
                  for p in (start, end) for i in range(sides)]
        faces = [tuple(range(sides-1,-1,-1)), tuple(range(sides,2*sides))]
        faces += [(i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides)]
        self.poly(points, faces, material)

    def prism(self, outline, thickness, material):
        """A blade silhouette in X/Z, extruded along Y."""
        count = len(outline)
        vertices = [(x, y, z) for y in (-thickness/2, thickness/2) for x, z in outline]
        faces = [tuple(range(count)), tuple(range(2*count-1,count-1,-1))]
        faces += [(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
        self.poly(vertices, faces, material)

    def gem(self, center, size, material):
        x,y,z=center;a,b,c=mul(size,.5)
        self.poly([(x-a,y,z),(x,y-b,z),(x+a,y,z),(x,y+b,z),(x,y,z+c),(x,y,z-c)],
                  [(0,1,4),(1,2,4),(2,3,4),(3,0,4),(1,0,5),(2,1,5),(3,2,5),(0,3,5)], material)

    def ring(self, center, radius, wire, normal, material, segments=16):
        n=norm(normal);u=norm(cross(n,(0,0,1) if abs(n[2])<.9 else (0,1,0)));w=cross(n,u)
        points=[add(center,add(mul(u,radius*math.cos(i*2*math.pi/segments)),mul(w,radius*math.sin(i*2*math.pi/segments)))) for i in range(segments)]
        for a,b in zip(points,points[1:]+points[:1]):self.rod(a,b,wire,material,6)

    def summary(self):
        require(self.vertices and self.faces, "Empty weapon geometry")
        require(all(math.isfinite(x) for p in self.vertices for x in p), "Nonfinite vertex")
        minimum=[min(p[i] for p in self.vertices) for i in range(3)]
        maximum=[max(p[i] for p in self.vertices) for i in range(3)]
        for face, material in self.faces:
            require(material in MATERIALS and len(face)>=3 and all(0<=i<len(self.vertices) for i in face), "Invalid face/material")
            a,b,c=(self.vertices[i] for i in face[:3])
            require(length(cross(sub(b,a),sub(c,a))) > 1e-6, "Degenerate face")
        return dict(vertices=len(self.vertices), faces=len(self.faces), triangles=sum(len(f)-2 for f,_ in self.faces),
                    minimum=minimum, maximum=maximum, sizeCm=sub(maximum,minimum), materials=sorted({m for _,m in self.faces}))

    def obj(self):
        result=["# Original Cire armory prototype. Centimeters; +X forward, +Z up; palm grip at origin.", "mtllib Armory.mtl"]
        result += ["v %.6f %.6f %.6f"%p for p in self.vertices]
        # Explicit per-vertex UV indices avoid Interchange's missing-UV ensure.
        result += ["vt %.6f %.6f"%(p[0]*.01,p[2]*.01) for p in self.vertices]
        previous=None
        for face,material in self.faces:
            if material!=previous:result.append("usemtl M_Armory"+material);previous=material
            result.append("f "+" ".join(f"{i+1}/{i+1}" for i in face))
        return "\n".join(result)+"\n"


def shaft(mesh, bottom=-12, top=44, radius=1.6):
    mesh.rod((0,0,bottom),(0,0,top),radius,"Wood")
    mesh.rod((0,0,-7),(0,0,8),radius+.35,"Leather")
    for z in (-7,8):mesh.rod((0,0,z-.7),(0,0,z+.7),radius+.5,"Brass")


def staff(kind):
    m=Mesh();shaft(m,-64,79,1.9)
    m.rod((0,0,-68),(0,0,-62),2.6,"Steel")
    for z in (58,74):m.rod((0,0,z),(0,0,z+3),2.8,"Brass")
    if kind=="GroveStaff":
        for a,b in [((0,0,75),(2,5,88)),((2,5,88),(0,10,100)),((0,10,100),(-1,8,110)),((2,5,88),(1,-7,99))]:m.rod(a,b,2.5,"Wood")
        for y,z in ((10,94),(-7,96),(7,106)):
            m.gem((1,y,z),(2,9,16),"Grove")
    elif kind=="LanternStaff":
        for a,b in [((0,0,76),(0,0,98)),((0,0,98),(0,10,107)),((0,10,107),(0,22,103)),((0,22,103),(0,22,98))]:m.rod(a,b,2.1,"Brass")
        m.ring((0,22,96),2.1,.6,(1,0,0),"Steel",12)
        for z in (73,93):m.box((0,22,z),(13,13,2),"Brass")
        for x in (-5,5):
            for y in (17,27):m.rod((x,y,74),(x,y,92),.9,"Brass",6)
        m.gem((0,22,84),(7,7,16),"Light")
        m.gem((0,22,95),(15,15,7),"Brass")
    else:
        color={"ArcaneStaff":"Light","RiftStaff":"Violet","EmberStaff":"Ember"}[kind]
        m.ring((0,0,92),12,1.3,(1,0,0),"Brass")
        m.gem((0,0,92),(8,10,23),color)
        for sign in (-1,1):
            m.rod((0,sign*3,77),(0,sign*12,89),1.8,"Brass")
            if kind=="RiftStaff":m.gem((0,sign*12,98),(4,4,12),"Violet")
        if kind=="EmberStaff":m.gem((0,0,109),(5,7,16),"Ember")
    return m


def geometry():
    result={name:staff(name) for name in ("ArcaneStaff","RiftStaff","EmberStaff","GroveStaff","LanternStaff")}
    m=Mesh();shaft(m,-9,2,1.2);m.box((0,0,3),(13,3,2),"Brass")
    m.prism([(-2.6,4),(2.6,4),(1.8,23),(0,33),(-1.8,23)],1.6,"Steel");m.gem((0,0,-11),(5,5,6),"Brass");result["Dagger"]=m
    for name,top,width in (("WarAxe",47,24),("ThrowingAxe",30,17)):
        m=Mesh();shaft(m,-14,top+3,1.8 if name=="WarAxe" else 1.4)
        m.prism([(-3,top-6),(width*.5,top-6),(width,top-19),(width+2,top+6),(width*.55,top+13),(-3,top+8)],3.6,"Steel")
        m.rod((0,-3,top),(0,3,top),4,"Brass",8);m.prism([(-3,top-4),(-12,top-1),(-12,top+5),(-3,top+7)],4,"Steel")
        result[name]=m
    for name in ("WarHammer","PickHammer"):
        m=Mesh();shaft(m,-14,45,1.8);m.box((0,0,43),(19,12,12),"Steel")
        m.box((10,0,43),(5,14,14),"Brass")
        if name=="PickHammer":m.prism([(-8,49),(-23,48),(-36,30),(-20,39),(-8,39)],5,"Steel")
        else:m.box((-11,0,43),(6,14,14),"Steel")
        m.box((0,0,43),(4,13,13),"Brass");result[name]=m
    m=Mesh();shaft(m,-12,31,1.7)
    for i,center in enumerate([(0,0,34),(3,0,39),(7,0,43),(12,0,43),(17,0,41),(21,0,36)]):
        m.ring(center,3,.75,(0,1,0) if i%2==0 else (1,0,0),"Steel",12)
    m.gem((25,0,27),(19,19,19),"Steel")
    for direction in ((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)):
        center=add((25,0,27),mul(direction,10));m.gem(center,tuple(14 if abs(x) else 4 for x in direction),"Steel")
    result["Flail"]=m
    m=Mesh();shaft(m,-21,45,3.5);m.box((0,0,58),(26,31,64),"Stone")
    for z in (30,48,82):m.box((0,0,z),(28,33,4),"Brass")
    for y in (-8,8):m.gem((14,y,68),(5,8,7),"Grove")
    m.box((14,0,56),(3,4,13),"Brass");m.box((14,0,48),(3,17,3),"Brass")
    m.gem((0,0,93),(26,30,15),"Stone");result["Totem"]=m
    m=Mesh();m.box((-2,0,3),(66,6,8),"Wood");m.box((-25,0,0),(18,10,14),"Wood")
    m.rod((0,0,-8),(0,0,5),2,"Leather");m.box((6,0,7.3),(46,3,1),"Steel")
    for sign in (-1,1):
        points=[(20,0,4),(16,sign*14,4),(7,sign*28,5),(-4,sign*37,6)]
        for a,b in zip(points,points[1:]):m.rod(a,b,1.8,"Steel",8)
        m.rod(points[-1],(-21,0,7),.42,"Leather",6)
    m.box((18,0,4),(8,12,7),"Brass");m.ring((35,0,3),5,.9,(1,0,0),"Steel",12)
    m.ring((-2,0,-8),4,.8,(0,1,0),"Steel",12);result["Crossbow"]=m
    m=Mesh();m.rod((-21,0,0),(23,0,0),.5,"Wood",6)
    m.poly([(23,-2,-1),(23,2,-1),(23,2,1),(23,-2,1),(31,0,0)],[(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],"Steel")
    m.box((-17,0,0),(8,.4,5),"Leather");result["Bolt"]=m
    return result


def validate_loadouts(models):
    data=json.loads((PROJECT/"Content/Data/WeaponLoadouts.json").read_text(encoding="utf-8"))
    roster=json.loads((PROJECT/"Content/Data/ChampionRoster.json").read_text(encoding="utf-8"))
    require(data["schemaVersion"]==1,"Unsupported loadout schema")
    ids={r["id"] for r in roster["champions"]}
    require(set(data["profiles"])==ids,"Loadout/profile coverage mismatch")
    known=set(models)|{"legacy/"+n for n in ("Sword","Shield","Bow","Arrow","Lance")}
    # new-champions: props authored by Tools/BuildNewChampionContent.py.
    known|={"hunter/"+n for n in ("Flintlock","Falchion","ArcaneBlunderbuss","SpectralBlade","Glaive","GlaiveLauncher","AetherStaff","AetherHalberd")}
    for name,row in data["presets"].items():
        require(row["motion"] in ("none","melee","cast","bow","crossbow","throw") and len(row["parts"])<=6,"Invalid preset "+name)
        roles=[p.get("role","") for p in row["parts"]]
        require((not row["parts"]) if row["motion"]=="none" else roles.count("primary")==1,"Invalid primary "+name)
        require(roles.count("ammunition")==int(row["motion"] in ("bow","crossbow")),"Invalid ammunition "+name)
        for p in row["parts"]:
            require(p["asset"] in known and p["bone"] in ("hand_l","hand_r","pelvis","spine_03"),"Unknown prop or bone")
            size=p.get("scale",1);require(math.isfinite(size) and .35<=size<=2,"Bad weapon scale")
            for field,maximum in (("offsetCm",100),("rotation",360)):
                v=p.get(field,[0,0,0]);require(len(v)==3 and all(math.isfinite(n) and abs(n)<=maximum for n in v),"Invalid grip vector")
    for profile,preset in data["profiles"].items():
        require(preset in data["presets"],"Missing preset "+preset)
        if profile in ("bear","whisp") or profile.startswith("ether_golem_"):
            require(not data["presets"][preset]["parts"],"Natural-attack body has a humanoid weapon")
    for profile,options in data.get("previewOptions",{}).items():
        require(profile in ids and 2<=len(options)<=8 and len(set(options))==len(options),"Invalid preview options")
        require(options[0]==data["profiles"][profile] and all(n in data["presets"] for n in options),"Invalid preview default")
    return dict(profiles=len(ids),presets=len(data["presets"]),previewProfiles=len(data["previewOptions"]))


def write_sources(models):
    folder=PROJECT/"Art/Weapons/ArmoryPrototype01";folder.mkdir(parents=True,exist_ok=True)
    for name,model in models.items():(folder/("SM_"+name+".obj")).write_text(model.obj(),encoding="ascii")
    lines=[]
    for name,(color,roughness,metallic,emission) in MATERIALS.items():
        lines += ["newmtl M_Armory"+name,"Kd %.5f %.5f %.5f"%color,"Pr %.4f"%roughness,"Pm %.4f"%metallic,""]
    (folder/"Armory.mtl").write_text("\n".join(lines),encoding="ascii")
    (folder/"manifest.json").write_text(json.dumps(dict(artStatus="original local prototype",units="centimeters",grip=[0,0,0],
        assets={n:m.summary() for n,m in models.items()}),indent=2)+"\n",encoding="utf-8")
    return folder


def make_material(name):
    path=OUTPUT+"/Materials/M_Armory"+name
    require(not unreal.EditorAssetLibrary.does_asset_exist(path),"Refusing to overwrite "+path)
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Armory"+name,OUTPUT+"/Materials",unreal.Material,unreal.MaterialFactoryNew())
    require(material is not None,"Material creation failed")
    lib=unreal.MaterialEditingLibrary;color,roughness,metallic,emission=MATERIALS[name]
    rgb=lib.create_material_expression(material,unreal.MaterialExpressionConstant3Vector,-350,0)
    rgb.set_editor_property("constant",unreal.LinearColor(*color,1));lib.connect_material_property(rgb,"",unreal.MaterialProperty.MP_BASE_COLOR)
    for value,property_,y in ((roughness,unreal.MaterialProperty.MP_ROUGHNESS,120),(metallic,unreal.MaterialProperty.MP_METALLIC,200)):
        node=lib.create_material_expression(material,unreal.MaterialExpressionConstant,-350,y);node.set_editor_property("r",value)
        lib.connect_material_property(node,"",property_)
    if emission:
        glow=lib.create_material_expression(material,unreal.MaterialExpressionConstant3Vector,-350,280)
        glow.set_editor_property("constant",unreal.LinearColor(*(c*emission for c in color),1))
        lib.connect_material_property(glow,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False),"Material save failed")
    return material


def inspect_asset(name,model):
    mesh=unreal.load_asset(OUTPUT+"/SM_"+name)
    require(isinstance(mesh,unreal.StaticMesh),"Missing weapon mesh "+name)
    actual=mesh.get_bounds().box_extent*2
    size=(float(actual.x),float(actual.y),float(actual.z));expected=model.summary()["sizeCm"]
    require(max(abs(a-b) for a,b in zip(size,expected))<.15,"Import axis/unit mismatch "+name+": "+str(size))
    materials=[]
    for slot in mesh.get_editor_property("static_materials"):
        material=slot.get_editor_property("material_interface")
        require(material is not None and str(material.get_path_name()).startswith(OUTPUT+"/Materials/"),"Weapon material missing or wrong "+name)
        materials.append(str(material.get_path_name()))
    require(len(materials)>=1,"Weapon has no materials")
    # EditorStaticMeshLibrary relies on a subsystem absent in -run=pythonscript.
    # Query actual static mesh render data, also available in commandlets.
    require(mesh.get_num_lods()>=1,"Weapon has no render LOD: "+name)
    triangles=mesh.get_num_triangles(0)
    require(triangles>=model.summary()["triangles"]*.95,"Weapon lost triangles on import: "+name)
    return dict(asset=str(mesh.get_path_name()),sizeCm=size,materials=materials,triangles=triangles)


def run(build=False,verify=False,sources=False,recover_validation=False):
    models=geometry();summary={name:m.summary() for name,m in models.items()};coverage=validate_loadouts(models)
    report=dict(status="source_preflight_pass",artStatus="original local prototype",visualReviewAccepted=False,coverage=coverage,geometry=summary)
    if sources:write_sources(models)
    if not build and not verify:return report
    require(unreal is not None,"Build/verify require coordinated Unreal Python")
    import IntegrateTripoBatch as protection
    engine_content=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
    report["engineContent"]=str(engine_content)
    report_path=PROJECT/"Saved/ArmoryPrototype01/build.json"
    if verify:
        before=protection.read_json(report_path)
        require(before.get("status")=="built_needs_reload_validation" or
                (recover_validation and before.get("status")=="failed"),"Missing successful build report")
        report["revalidatedAfterBuildFailure"]=before.get("status")=="failed"
        require(Path(before["engineContent"]).resolve()==engine_content,"Verify must use the same isolated project as build")
        protection.validate_hashes(PROJECT/"Content",before["protectedHashes"])
        report["assets"]=[inspect_asset(n,m) for n,m in models.items()]
        protection.validate_hashes(PROJECT/"Content",before["protectedHashes"])
        # The no-DLL CreatureBuilder contains copied assets, not a shared mount.
        # Publish only this verified, self-contained new namespace; no overwrite.
        relative=OUTPUT[len("/Game/"):]
        source=engine_content/relative;destination=PROJECT/"Content"/relative
        if source.resolve()!=destination.resolve():
            files=[p for p in source.rglob("*") if p.is_file()]
            require(files and all(p.suffix.lower() in (".uasset",".uexp",".ubulk") for p in files),"Unexpected isolated output files")
            expected_packages={"SM_"+n+".uasset" for n in models}|{"Materials/M_Armory"+n+".uasset" for n in MATERIALS}
            require({p.relative_to(source).as_posix() for p in files if p.suffix==".uasset"}==expected_packages,"Isolated output package set differs from the authored armory")
            require(destination.resolve().is_relative_to((PROJECT/"Content").resolve()),"Output escapes Content")
            expected={str(p.relative_to(source)):protection.digest(p) for p in files}
            if destination.exists():
                actual={str(p.relative_to(destination)):protection.digest(p) for p in destination.rglob("*") if p.is_file()}
                require(actual==expected,"Refusing to replace existing real armory output")
            else:
                for path in files:
                    target=destination/path.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(path,target)
            protection.validate_hashes(destination,expected)
            report["publishedPackages"]=len(expected)
        protection.validate_hashes(PROJECT/"Content",before["protectedHashes"])
        report["status"]="saved_assets_verified";report["protectedPackages"]=len(before["protectedHashes"])
        protection.atomic_json(PROJECT/"Saved/ArmoryPrototype01/verify.json",report)
        unreal.log("CIRE_ARMORY_VERIFY_PASS meshes="+str(len(models)))
        return report
    output_disk=PROJECT/"Content"/OUTPUT[len("/Game/"):]
    require(not output_disk.exists() and not unreal.EditorAssetLibrary.does_directory_exist(OUTPUT),"Fresh ArmoryPrototype01 output required; nothing overwritten")
    report["protectedHashes"]=protection.original_hashes(PROJECT/"Content",OUTPUT[len("/Game/"):])
    source=write_sources(models)
    try:
        materials={name:make_material(name) for name in MATERIALS}
        for name,model in models.items():
            asset="SM_"+name;task=unreal.AssetImportTask()
            for key,value in dict(filename=str(source/(asset+".obj")),destination_path=OUTPUT,destination_name=asset,
                                  automated=True,replace_existing=False,save=True).items():task.set_editor_property(key,value)
            opts=unreal.FbxImportUI()
            for key,value in dict(import_mesh=True,import_materials=False,import_textures=False,import_as_skeletal=False,
                                  mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():opts.set_editor_property(key,value)
            data=opts.get_editor_property("static_mesh_import_data")
            for key,value in dict(combine_meshes=True,auto_generate_collision=False,convert_scene=False,convert_scene_unit=False).items():data.set_editor_property(key,value)
            task.set_editor_property("options",opts);unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            mesh=unreal.load_asset(OUTPUT+"/"+asset);require(isinstance(mesh,unreal.StaticMesh),"Import failed "+name)
            for index,slot in enumerate(mesh.get_editor_property("static_materials")):
                label=str(slot.get_editor_property("imported_material_slot_name"));key=label.removeprefix("M_Armory")
                require(key in materials,"Unexpected material slot "+label);mesh.set_material(index,materials[key])
            require(unreal.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False),"Weapon mesh save failed")
        report["assets"]=[inspect_asset(n,m) for n,m in models.items()]
        protection.validate_hashes(PROJECT/"Content",report["protectedHashes"])
        report["status"]="built_needs_reload_validation"
        unreal.log("CIRE_ARMORY_BUILD_PASS meshes="+str(len(models))+" materials="+str(len(materials)))
    except Exception as error:
        report["status"]="failed";report["error"]=str(error);raise
    finally:protection.atomic_json(report_path,report)
    return report


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build",action="store_true");parser.add_argument("--verify",action="store_true");parser.add_argument("--sources",action="store_true")
    parser.add_argument("--recover-validation",action="store_true",help="With --verify, fully validate saved output from a failed build without regeneration")
    args,_=parser.parse_known_args()
    if unreal:
        tokens=unreal.SystemLibrary.get_command_line().lower().split()
        args.build |= "-cirearmorybuild" in tokens;args.verify |= "-cirearmoryverify" in tokens
        args.recover_validation |= "-cirearmoryrecovervalidation" in tokens
        if str(PROJECT/"Tools") not in sys.path:sys.path.insert(0,str(PROJECT/"Tools"))
    require(not(args.build and args.verify),"Build and verify must use separate processes")
    try:
        result=run(args.build,args.verify,args.sources,args.recover_validation)
        print(json.dumps({k:v for k,v in result.items() if k not in ("protectedHashes","geometry")},indent=2))
    except Exception as error:
        if unreal:unreal.log_error("CIRE_ARMORY_FAIL "+str(error))
        raise
