"""#392 Round 4: reference cast straps, shared capsule profile, explicit LOD atlas and clear hoops."""
import math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import Builder,pair

def mesh(name,low):
    b=Builder(name)
    b.round_plate(0,.020,.140,.260,.012,steps=2,back=not low)
    for i,c in enumerate(b.charts):c['shared_lod_key']='cage_plate_'+str(i+int(low))
    for y in (-.054,.054):
        for z in (-.110,.110):b.screw((.020,y,z),(1,0,0),.0075)
    b.oval_surface([(.022,.050),(.026,.061),(.043,.061),(.050,.050)],.060,n=14 if low else 36,kind='castframe')
    rings=[(.038,.049),(.073,.045),(.102,.028),(.114,.004)]
    b.oval_surface(rings,.060,n=20 if low else 48,slot=1,kind='frost',cap='back' if low else True)
    if low:
        # Rear closure matches LOD0 occlusion; a two-triangle front closure sits behind the central strap.
        b.add([(.114,-.004,-.064),(.114,.004,-.064),(.114,.004,.064),(.114,-.004,.064)],[(0,1,2,3)],slot=1,kind='frost')
    # The round vertical bar converges to the cast frame at pointed V ends.
    p=[(.046,0,-.122),(.075,0,-.112),(.103,0,-.090),(.120,0,-.058),(.120,0,.058),(.103,0,.090),(.075,0,.112),(.046,0,.122)]
    b.tube(p,.0055,ribbon=True,ribbon_axis=(0,1,0),kind='castbar')
    for z in (-.050,.050):
        segments=4 if low else 8
        p=[(.042+.077*math.sin(math.pi*i/segments),.056*math.cos(math.pi*i/segments),z) for i in range(segments+1)]
        b.tube(p,.005,ribbon=True,ribbon_axis=(0,0,1),kind='castbar')
        for s in (-1,1):
            if low:
                b.add([(.052,s*.057+dy,z+dz) for dy,dz in [(-.010,-.011),(.010,-.011),(.010,.011),(-.010,.011)]],[(0,1,2,3)]);b.screw((.052,s*.057,z),(1,0,0),.007)
            else:b.lathe((.038,s*.057,z),(1,0,0),[(0,.011),(.016,.011)],n=8,kind='torx',cap='front')
    return b.finish()

def build(piece):return pair(piece,mesh)
