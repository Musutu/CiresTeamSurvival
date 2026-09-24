"""Author original supplementary environment meshes. --sources never launches UE.

Run inside an isolated UE Python builder after materials are staged. Imported
Tripo assets are not touched. Outputs only /Game/Art/Environment/Props01.
"""
from pathlib import Path
import math,json,random
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'Art/Environment/Props01Sources'
PACKAGE='/Game/Art/Environment/Props01'
MATERIALS={'Stone':'/Game/Art/Environment/Materials/M_AshenMasonry',
           'Timber':'/Game/Art/Environment/Materials/M_OldTimber',
           'Slate':'/Game/Art/Environment/Materials/M_RoofSlate',
           'Metal':'/Game/Art/Materials/M_Metal','Gold':'/Game/Art/Materials/M_Gold',
           'Ember':'/Game/Art/Environment/Materials/M_WindowGlow'}
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,k):return tuple(x*k for x in a)
def norm(v):return mul(v,1/max(1e-9,math.sqrt(sum(x*x for x in v))))
def cross(a,b):return(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
class Mesh:
    def __init__(self):self.vertices=[];self.faces=[]
    def poly(self,v,f,m):
        offset=len(self.vertices);self.vertices.extend(v)
        for face in f:
            for i in range(1,len(face)-1):self.faces.append((m,tuple(offset+k for k in (face[0],face[i],face[i+1]))))
    def box(self,c,s,m,bevel=0):
        x,y,z=c;a,b,d=mul(s,.5)
        if bevel:
            bevel=min(bevel,a*.4,b*.4,d*.4)
            rings=[]
            for height,cut in ((-d,bevel),(-d+bevel,0),(d-bevel,0),(d,bevel)):
                rings.append([(x+vx,y+vy,z+height) for vx,vy in [(-a+bevel+cut,-b+cut),(a-bevel-cut,-b+cut),(a-cut,-b+bevel+cut),(a-cut,b-bevel-cut),(a-bevel-cut,b-cut),(-a+bevel+cut,b-cut),(-a+cut,b-bevel-cut),(-a+cut,-b+bevel+cut)]])
            v=sum(rings,[]);f=[tuple(range(7,-1,-1)),tuple(range(24,32))]
            f.extend((i*8+j,i*8+(j+1)%8,(i+1)*8+(j+1)%8,(i+1)*8+j) for i in range(3) for j in range(8));self.poly(v,f,m);return
        self.poly([(x+sx*a,y+sy*b,z+sz*d) for sx,sy,sz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],m)
    def rod(self,a,b,r1,r2,m,n=12):
        direction=norm(sub(b,a));u=norm(cross(direction,(0,1,0) if abs(direction[1])<.9 else (1,0,0)));w=cross(direction,u)
        v=[add(p,add(mul(u,r*math.cos(i*2*math.pi/n)),mul(w,r*math.sin(i*2*math.pi/n)))) for p,r in ((a,r1),(b,r2)) for i in range(n)]
        self.poly(v,[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],m)
    def ring(self,z,inner,outer,height,m,n=24):
        v=[(r*math.cos(i*2*math.pi/n),r*math.sin(i*2*math.pi/n),h) for h in (z,z+height) for r in (inner,outer) for i in range(n)]
        f=[]
        for i in range(n):
            j=(i+1)%n;f.extend([(i,j,j+n,i+n),(i+2*n,i+3*n,j+3*n,j+2*n),(i+n,j+n,j+3*n,i+3*n),(j,i,i+2*n,j+2*n)])
        self.poly(v,f,m)
    def save(self,name):
        lines=['# Original Cire environment geometry; centimetres; Z up','mtllib CireProps.mtl']
        lines+=['v %.5f %.5f %.5f'%p for p in self.vertices]
        lines+=['vt %.5f %.5f'%(p[0]/150,p[2]/150) for p in self.vertices]
        previous=None
        for mat,face in self.faces:
            if mat!=previous:lines.append('usemtl '+mat);previous=mat
            lines.append('f '+' '.join(f'{i+1}/{i+1}' for i in face))
        path=OUT/(name+'.obj');path.write_text('\n'.join(lines),encoding='ascii')
        return {'name':name,'source':str(path),'vertices':len(self.vertices),'triangles':len(self.faces),'boundsMin':[min(p[d] for p in self.vertices) for d in range(3)],'boundsMax':[max(p[d] for p in self.vertices) for d in range(3)]}

def sources():
    OUT.mkdir(parents=True,exist_ok=True);models={}
    barrel=Mesh()
    # Twenty individually separated stave wedges; bowed side profile and iron bands.
    for i in range(20):
        a,b=(i+.04)*2*math.pi/20,(i+.96)*2*math.pi/20
        v=[(r*math.cos(t),r*math.sin(t),z) for z,r in ((0,34),(10,37),(47,42),(84,37),(94,34)) for t in (a,b)]
        barrel.poly(v,[(j,j+1,j+3,j+2) for j in range(0,8,2)],'Timber')
    barrel.rod((0,0,2),(0,0,8),34,36,'Timber',20);barrel.rod((0,0,87),(0,0,92),36,34,'Timber',20)
    for z,r in ((10,37),(26,40),(64,40),(82,37)):barrel.ring(z,r-.7,r+1.8,5,'Metal',40)
    for x in (-24,-12,0,12,24):barrel.box((x,0,94),(10,2*math.sqrt(max(0,33*33-x*x)),2),'Timber',.7)
    models['SM_CooperedBarrel']=barrel
    crate=Mesh()
    for side in (-1,1):
        for i in range(6):
            crate.box((-43+i*17,side*49,50),(16,7,94),'Timber',1.3)
            crate.box((side*49,-43+i*17,50),(7,16,94),'Timber',1.3)
        for z in (10,91):
            crate.box((0,side*55,z),(110,9,14),'Timber',2);crate.box((side*55,0,z),(9,110,14),'Timber',2)
        crate.rod((-43,side*56,16),(43,side*56,84),5,5,'Timber',4)
        for edge in (-1,1):
            crate.box((edge*52,side*55,50),(9,9,97),'Metal',1)
            for z in (15,85):crate.rod((edge*41,side*61,z),(edge*41,side*65,z),2,2,'Metal',8)
    for i in range(6):crate.box((-43+i*17,0,100),(16,101,7),'Timber',1.5)
    models['SM_BracedSupplyCrate']=crate
    brazier=Mesh()
    brazier.ring(113,33,41,9,'Metal');brazier.ring(150,57,64,9,'Metal',32)
    for i in range(12):
        a=i*2*math.pi/12;brazier.rod((33*math.cos(a),33*math.sin(a),115),(60*math.cos(a),60*math.sin(a),152),4,3,'Metal',8)
    brazier.rod((0,0,20),(0,0,118),14,9,'Metal',12);brazier.rod((0,0,6),(0,0,20),35,26,'Stone',8)
    for i in range(4):
        a=i*math.pi/2+.4;brazier.rod((12*math.cos(a),12*math.sin(a),67),(46*math.cos(a),46*math.sin(a),2),5,7,'Metal',8)
    brazier.rod((0,0,121),(0,0,127),38,45,'Ember',16)
    for i in range(7):
        a=i*2.4;brazier.rod((25*math.cos(a),25*math.sin(a),127),(18*math.cos(a+.7),18*math.sin(a+.7),141),7,3,'Ember',7)
    models['SM_ForgedBrazier']=brazier
    arch=Mesh()
    for side in (-1,1):
        for row in range(6):arch.box((side*253,0,row*54+27),(92,135,52),'Stone',4)
        arch.box((side*253,0,20),(122,172,40),'Stone',4);arch.box((side*253,0,325),(120,167,22),'Stone',3)
    for i in range(13):
        a=i*math.pi/13+.012;b=(i+1)*math.pi/13-.012
        v=[(r*math.cos(t),y,321+r*math.sin(t)) for y in (-69,69) for r in (205,298) for t in (a,b)]
        arch.poly(v,[(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)],'Stone')
    arch.box((0,-78,594),(64,24,90),'Metal',5)
    models['SM_VoussoirArch']=arch
    stairs=Mesh()
    for i in range(8):stairs.box((i*44,0,(i+1)*10),(44,360,(i+1)*20),'Stone',2)
    for side in (-1,1):
        stairs.box((154,side*196,45),(352,32,90),'Stone',2)
        for x in (-8,154,315):stairs.box((x,side*196,115),(42,42,230),'Stone',4)
        stairs.rod((-8,side*196,155),(315,side*196,265),10,10,'Metal',8)
    models['SM_CourtyardStair']=stairs
    tree=Mesh();rng=random.Random(5813)
    trunk=[(0,0,0),(9,-7,130),(-5,5,260),(20,8,390),(1,22,545)]
    for i,(a,b) in enumerate(zip(trunk,trunk[1:])):tree.rod(a,b,37-i*7,29-i*7,'Timber',11)
    for i in range(8):
        a=i*2*math.pi/8;tree.rod((20*math.cos(a),20*math.sin(a),40),(95*math.cos(a),95*math.sin(a),4),21,4,'Timber',8)
    for i in range(9):
        a=i*2.4;base=trunk[1+i%3];r=140+(i%3)*37
        elbow=(base[0]+r*.55*math.cos(a),base[1]+r*.55*math.sin(a),base[2]+58)
        end=(base[0]+r*math.cos(a),base[1]+r*math.sin(a),base[2]+130+rng.random()*40)
        tree.rod(base,elbow,13,8,'Timber',9);tree.rod(elbow,end,8,2,'Timber',8)
        branch=add(elbow,(65*math.cos(a+.8),65*math.sin(a+.8),105));tree.rod(elbow,branch,6,1,'Timber',7)
    models['SM_GravewoodOak']=tree
    obelisk=Mesh();obelisk.box((0,0,15),(175,175,30),'Stone',5);obelisk.box((0,0,40),(137,137,24),'Stone',4)
    obelisk.poly([(-48,-48,52),(48,-48,52),(48,48,52),(-48,48,52),(-28,-28,350),(28,-28,350),(28,28,350),(-28,28,350),(0,0,409)],[(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,8),(5,6,8),(6,7,8),(7,4,8)],'Stone')
    for z in (102,157,212,267):
        obelisk.box((0,-45+(z-52)*.067,z),(18,4,23),'Gold',2)
    obelisk.ring(55,61,65,9,'Metal',8);models['SM_OathObelisk']=obelisk
    marker=Mesh();marker.box((0,0,9),(78,78,18),'Stone',3);marker.rod((0,0,18),(0,0,236),7,5,'Metal',12)
    marker.box((0,0,245),(68,59,9),'Metal',2);marker.box((0,0,278),(45,41,58),'Ember',3)
    for x in (-25,25):
        for y in (-23,23):marker.rod((x,y,247),(x,y,309),3,3,'Metal',8)
    marker.poly([(-36,-32,310),(36,-32,310),(36,32,310),(-36,32,310),(0,0,345)],[(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],'Slate');models['SM_WatchLantern']=marker
    (OUT/'CireProps.mtl').write_text('\n'.join('newmtl '+n+'\nKd 0.4 0.4 0.4' for n in MATERIALS),encoding='ascii')
    report=[mesh.save(name) for name,mesh in models.items()]
    (OUT/'sources.json').write_text(json.dumps(report,indent=2));return report

def build(u):
    rows=sources();lib=u.EditorAssetLibrary;tools=u.AssetToolsHelpers.get_asset_tools()
    for row in rows:
        task=u.AssetImportTask();task.filename=row['source'];task.destination_path=PACKAGE;task.destination_name=row['name'];task.automated=True;task.replace_existing=True;task.save=True
        opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
        data=opts.static_mesh_import_data;data.combine_meshes=True;data.auto_generate_collision=True;data.convert_scene=False;data.convert_scene_unit=False;task.options=opts
        tools.import_asset_tasks([task]);mesh=lib.load_asset(PACKAGE+'/'+row['name'])
        if not isinstance(mesh,u.StaticMesh):raise RuntimeError('Failed static mesh '+row['name'])
        for i,slot in enumerate(mesh.get_editor_property('static_materials')):
            key=str(slot.get_editor_property('imported_material_slot_name'));mat=lib.load_asset(MATERIALS.get(key,MATERIALS['Stone']))
            if not mat:raise RuntimeError('Missing staged material '+key)
            mesh.set_material(i,mat)
        body=mesh.get_editor_property('body_setup')
        if body:body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        if not lib.save_loaded_asset(mesh,only_if_is_dirty=False):raise RuntimeError('Failed save '+row['name'])
        row['asset']=mesh.get_path_name();row['trianglesImported']=mesh.get_num_triangles(0)
        if not 8<row['trianglesImported']<30000:raise RuntimeError('Invalid triangle budget')
    (ROOT/'Saved/WorldPropsBuild.json').write_text(json.dumps({'models':rows,'status':'generated','originalTripoAssetsModified':False},indent=2))
    u.log('CIRE_WORLD_PROPS_PASS models='+str(len(rows)))
if __name__=='__main__':
    try:import unreal
    except ImportError:print(json.dumps({'sources':sources(),'launchedUnreal':False},indent=2))
    else:build(unreal)
