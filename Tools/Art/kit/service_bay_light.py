import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
"""Round 2: recessed prismatic bay panel with white reflector and latch clips."""
from lighting_393_common import Builder,build_pair

def mesh(name,low):
    b=Builder(name);e=0 if low else .0012
    b.box((-.05,0,0),(.10,.575,.275),e,open_front=True)
    for y in (-.290,.290):b.box((.005,y,0),(.020,.020,.30),e)
    for z in (-.140,.140):b.box((.005,0,z),(.020,.56,.020),e)
    for y in (-.280,.280):b.box((.009,y,0),(.008,.006,.264),e)
    for z in (-.130,.130):b.box((.009,0,z),(.008,.554,.006),e)
    # Four sloped enamel tray walls and a panel 19mm behind the main trim face.
    vs=[(x,y,z) for x,hy,hz in [(-.004,.266,.116),(.011,.277,.127)] for y,z in [(-hy,-hz),(hy,-hz),(hy,hz),(-hy,hz)]]
    b.add(vs,[(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],kind='enamel')
    b.add([(-.004,-.266,-.116),(-.004,.266,-.116),(-.004,.266,.116),(-.004,-.266,.116)],[(0,1,2,3)],slot=1,kind='diamond')
    for sign in (-1,1):
        profile=[(.010,sign*.135),(.015,sign*.136),(.019,sign*.142),(.018,sign*.150),(.015,sign*.150),(.015,sign*.144),(.012,sign*.140)]
        b.prism_y(profile,-.0225,.0225,kind='latch')
    for y in (-.290,.290):
        for z in (-.140,.140):b.screw((.015,y,z),(1,0,0),.0045)
    return b.finish()

def build(piece):return build_pair(piece,mesh)
