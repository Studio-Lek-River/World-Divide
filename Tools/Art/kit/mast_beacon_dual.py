import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
"""Round 3 M-class dual, shared heads at producer-decided 0.75 scale."""
from lighting_393_common import Builder,build_pair,mast_head
from wall_lamp_materials import bolt

def mesh(name,low):
    b=Builder(name)
    for y in (-.225,.225):mast_head(b,y,low,scale=.75,dual=True)
    b.box((0,0,.100),(.18,.80,.060),0 if low else .0015,kind='plate_steel')
    for ch in b.charts:
        if ch['kind']=='plate_steel' and ch['size'][0]>.7:
            ch['screws']=[[ch['size'][0]*q,ch['size'][1]*.5,.014] for q in (.1375,.8625)]
    for y in (-.29,.29):
        if low:
            b.lathe((.090,y,.100),(1,0,0),[(0,.014),(.016,.014)],n=6,kind='bolt',cap='front')
        else:
            b.lathe((.090,y,.100),(1,0,0),[(0,.0185),(.003,.0185)],n=12,kind='washer')
            b.lathe((.093,y,.100),(1,0,0),[(0,.014),(.013,.014)],n=6,kind='torx')
    return b.finish()

def build(piece):return build_pair(piece,mesh)
