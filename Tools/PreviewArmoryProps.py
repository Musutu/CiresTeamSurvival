"""Render source geometry for silhouette review without starting Unreal.

Uses Pillow from the bundled workspace Python. This does not establish in-game
materials, skeletal grip, animation, or lighting acceptance.
"""
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont
from BuildArmoryProps import geometry, MATERIALS, sub, dot, cross, norm


def main():
    models=geometry();width,height=2100,1140
    result=Image.new("RGB",(width,height),(19,23,28));draw=ImageDraw.Draw(result)
    font_path="C:/Windows/Fonts/segoeui.ttf"
    title=ImageFont.truetype(font_path,28);label=ImageFont.truetype(font_path,20);small=ImageFont.truetype(font_path,15)
    draw.text((24,14),"CIRE / ORIGINAL ARMORY PROTOTYPE / SOURCE GEOMETRY",font=title,fill=(232,221,191))
    draw.text((24,50),"Orthographic source preview • each tile fits its prop • red mark = palm grip • in-engine review still required",font=small,fill=(160,176,185))
    right=norm((.8,-.6,0));look=norm((.6,.8,.24));up=norm(cross(right,look));light=norm((.35,-.5,1))
    for index,(name,mesh) in enumerate(models.items()):
        x=(index%7)*300;y=85+(index//7)*520
        draw.rounded_rectangle((x+8,y,x+292,y+504),radius=12,fill=(29,35,41),outline=(57,67,73),width=1)
        points=[(dot(p,right),dot(p,up),dot(p,look)) for p in mesh.vertices]
        low=[min(p[a] for p in points) for a in (0,1)];high=[max(p[a] for p in points) for a in (0,1)]
        scale=min(245/max(1,high[0]-low[0]),415/max(1,high[1]-low[1]))
        center=((low[0]+high[0])/2,(low[1]+high[1])/2)
        def project(p):return x+150+(p[0]-center[0])*scale,y+230-(p[1]-center[1])*scale
        for face,material in sorted(mesh.faces,key=lambda f:sum(points[i][2] for i in f[0])/len(f[0])):
            a,b,c=(mesh.vertices[i] for i in face[:3]);normal=norm(cross(sub(b,a),sub(c,a)))
            # Source winding can include both front/back panels; use two-sided shading for QA.
            shade=.44+.56*abs(dot(normal,light));color=MATERIALS[material][0]
            rgb=tuple(min(255,int((v**(1/2.2))*255*shade)) for v in color)
            draw.polygon([project(points[i]) for i in face],fill=rgb)
        gx,gy=project((0,0,0));draw.ellipse((gx-3,gy-3,gx+3,gy+3),fill=(255,79,73))
        friendly=re.sub(r"(?<!^)(?=[A-Z])"," ",name)
        draw.text((x+18,y+448),friendly,font=label,fill=(231,226,208))
        size=mesh.summary()["sizeCm"]
        draw.text((x+18,y+478)," × ".join(str(round(v,1)) for v in size)+" cm",font=small,fill=(151,170,179))
    path=Path(__file__).resolve().parent.parent/"Art/Weapons/ArmoryPrototype01/source-preview.png"
    path.parent.mkdir(parents=True,exist_ok=True);result.save(path);print(path)


if __name__=="__main__":main()
