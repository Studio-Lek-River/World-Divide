"""#392 / Round 2: 35cm projection along +X, 20cm wide; downward prismatic lens."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import Builder,pair

def mesh(name,low):
    b=Builder(name);e=0 if low else .0015
    b.round_plate(0,.012,.120,.250,.008,steps=1 if low else 3)
    b.screw((.012,0,-.080),(1,0,0),.010)
    b.box((.031,0,.061),(.038,.110,.070),e)
    b.screw((.031,-.055,.060),(0,-1,0),.009)
    # The length is X, not Y: head projects out from the back bracket.
    b.box((.190,0,.078),(.350,.200,.100),e)
    b.screw((.046,-.100,.068),(0,-1,0),.009)
    for y in (-.090,.090):b.box((.190,y,.024),(.350,.020,.020),e)
    for x in (.027,.353):b.box((x,0,.024),(.024,.160,.020),e)
    for x in (.062,.319):
        for y in (-.090,.090):b.screw((x,y,.014),(0,0,-1),.006)
    b.box((.366,0,.028),(.004,.200,.028))
    b.box((.190,0,.024),(.294,.158,.004),slot=1,kind='prismatic_x')
    return b.finish()

def build(piece):return pair(piece,mesh)
