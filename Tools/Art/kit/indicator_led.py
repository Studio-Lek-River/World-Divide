import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
"""Round 2 worn steel box, large front-top chamfer and screwed raised bezel."""
from lighting_393_common import Builder,build_pair

def mesh(name,low):
    b=Builder(name)
    b.prism_y([(0,-.043),(.090,-.043),(.090,.026),(.075,.045),(0,.045)],-.120,.120,kind='steel')
    for y in (-.108,.108):b.screw((.090,y,.016),(1,0,0),.0055)
    # Raised bezel plate with a real recessed aperture, two end screws.
    for y in (-.106,.106):b.box((.047,y,-.045),(.070,.017,.009),.001,kind='steel')
    for x in (.016,.078):b.box((x,0,-.045),(.008,.195,.009),0,kind='steel')
    b.add([(.020,-.097,-.047),(.074,-.097,-.047),(.074,.097,-.047),(.020,.097,-.047)],[(3,2,1,0)],slot=1,kind='indicator_ribs')
    for y in (-.106,.106):b.screw((.047,y,-.0495),(0,0,-1),.0048)
    b.lathe((.042,-.120,0),(0,-1,0),[(0,.018),(.002,.018),(.0035,.0155)],n=16,kind='slotted')
    return b.finish()

def build(piece):return build_pair(piece,mesh)
