"""Orthographic geometry audit only; no source asset modification."""
import json
from pathlib import Path
import struct,zlib
ROOT=Path(__file__).resolve().parent.parent
j=json.loads((ROOT/'Saved/CreatureMotionInspection.json').read_text())
width,height=1500,620
pixels=bytearray([240]*(width*height*3))
def dot(x,y,color):
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            if 0<=x+dx<width and 0<=y+dy<height:
                p=((y+dy)*width+x+dx)*3;pixels[p:p+3]=bytes(color)
m=j['models']['centaur_warrior_3d_model']; v=m['sections'][0]['vertices']
for index,(a,b) in enumerate([(0,2),(1,2),(0,1)]):
    for p in v:
        c=max(0,min(200,int(p[2]*2)));dot(round(index*500+250+p[a]*5),round(565-(p[b] if b==2 else p[b]+50)*5),(c,60,220-c))
def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
raw=b''.join(b'\x00'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
(ROOT/'Saved/CreatureGeometry.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
