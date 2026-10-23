"""#392 / Round 2: chamfered wall pack, all-around backplate and four-screw bezel."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import Builder,pair

def mesh(name,low):
    b=Builder(name);e=0 if low else .0015
    b.box((.003,0,0),(.006,.312,.140),e)
    b.prism_y([(.010,-.044),(.130,-.044),(.130,.030),(.098,.060),(.010,.060)],-.148,.148)
    for y in (-.132,.132):b.screw((.130,y,-.031),(1,0,0),.0035)
    for y in (-.140,.140):b.box((.072,y,-.055),(.116,.020,.020),e)
    for x in (.021,.123):b.box((x,0,-.055),(.014,.260,.020),e)
    for x in (.040,.105):
        for y in (-.140,.140):b.screw((x,y,-.065),(0,0,-1),.0058)
    b.box((.072,0,-.054),(.088,.250,.004),slot=1,kind='ribbed_y')
    for s in (-1,1):
        n=8 if low else 24
        b.lathe((.043,s*.149,0),(0,s,0),[(0,.014),(.0025,.014),(.003,.011),(.007,.011)],n=n,kind='torx')
    return b.finish()

def build(piece):return pair(piece,mesh)
