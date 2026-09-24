"""Create the authored supplementary-prop placement source, without running UE."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parent.parent
models=[('barrel','SM_CooperedBarrel',62,True),('crate','SM_BracedSupplyCrate',90,True),('brazier','SM_ForgedBrazier',68,True),('arch','SM_VoussoirArch',335,True),('stairs','SM_CourtyardStair',410,True),('oak','SM_GravewoodOak',245,False),('obelisk','SM_OathObelisk',125,True),('lantern','SM_WatchLantern',56,True)]
placements=[]
def place(model,anchor,x=0,y=0,z=0,**kw):placements.append(dict(model=model,anchor=anchor,x=x,y=y,z=z,**kw))
for side in (-1,1):
    place('crate','town',-155,side*690,yaw=side*7)
    place('crate','town',-245,side*635,z=102,scale=.7,yaw=side*-13)
    place('barrel','town',-30,side*740,yaw=23)
    place('barrel','town',100,side*695,yaw=74)
    place('brazier','town',380,side*810,scale=1.15)
    place('obelisk','town',-195,side*930,scale=.8)
    for i in range(9):place('lantern','perimeter',fraction=.05+i*.11,side=side,yaw=side*90)
    for i in range(6):place('brazier','perimeter',fraction=.08+i*.17,side=side,scale=1.05)
for tier in (1,2,3):
    place('arch','challenge',-730,0,tier=tier,yaw=90,scale=.8)
    place('obelisk','challenge',-430,0,tier=tier,scale=.75+tier*.12)
    place('brazier','challenge',430,0,tier=tier,scale=1.1)
for i in range(15):place('oak','outer_skyline',fraction=.018+i*.068,y=(-1 if i%2 else 1)*75,scale=.8+(i%4)*.13,yaw=(i*73)%360)
for i in range(5):
    place('arch','outer_skyline',fraction=.1+i*.2,yaw=90,scale=1.5)
    place('stairs','outer_skyline',x=190,fraction=.1+i*.2,yaw=180,scale=1.0)
for side in (-1,1):
    place('obelisk','spawn',-80,side*850,scale=1.5)
    place('brazier','spawn',-350,side*780,scale=1.2)
document={'schemaVersion':1,'description':'Original ash-city supplementary props. Applied symmetrically to both separated PvE lanes. Objects touching an edited route, challenge spawn, or town entrance are automatically omitted.','models':[dict(id=i,mesh=f'/Game/Art/Environment/Props01/{m}.{m}',radiusCm=r,collision=c) for i,m,r,c in models],'placements':placements,'landmarkRequests':[{'role':'town_gate','provider':'Tripo','status':'awaiting_root_import'},{'role':'watchtower','provider':'Tripo','status':'awaiting_root_import'},{'role':'challenge_shrine','provider':'Tripo','status':'awaiting_root_import'}]}
for row in placements:
    for key,low,high,default in [('x',-30000,30000,0),('y',-5000,5000,0),('z',-20,2000,0),('yaw',-360,360,0),('scale',.1,8,1),('fraction',0,1,0),('tier',0,3,0),('side',-1,1,0)]:
        assert low <= row.get(key,default) <= high, (key,row)
(ROOT/'Content/Data/EnvironmentPlacements.json').write_text(json.dumps(document,indent=2)+'\n')
print(json.dumps({'models':len(models),'placementRows':len(placements),'maximumInstances':len(placements)*2}))
