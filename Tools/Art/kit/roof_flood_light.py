"""#392 06 RoofFloodLight: twin three-panel heads, fins, U-yoke and roof base.
Origin is base underside; heads face +X pitched 15 degrees down.
"""
import math,sys
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import Builder,pair,bolt,smooth_path

def mesh(name,low):
    b=Builder(name);e=0 if low else .002
    b.box((0,0,.008),(.250,.400,.016),e)
    b.box((0,0,.098),(.090,.090,.164),e)
    b.box((0,0,.186),(.100,.800,.025),e)
    for y in (-.387,.387):b.box((0,y,.350),(.092,.026,.330),e)
    for y in (-.195,.195):
        tf=Matrix.Translation((0,y,.510))@Matrix.Rotation(math.radians(15),4,'Y')
        # Casting with an inset front gasket, four screwed frame rails and three windows.
        b.box((-.019,0,0),(.083,.300,.380),e,transform=tf,open_front=low)
        for yy in (-.139,.139):b.box((.033,yy,0),(.025,.022,.378),e,transform=tf,kind='bezel',open_back=low)
        for zz in (-.177,.177):b.box((.033,0,zz),(.025,.256,.024),e,transform=tf,kind='bezel',open_back=low)
        for zz in (-.055,.055):
            if low:
                b.add([tf@Vector((.044,yy,zz+dz)) for yy,dz in [(-.128,-.0045),(.128,-.0045),(.128,.0045),(-.128,.0045)]],[(0,1,2,3)])
            else:b.box((.035,0,zz),(.018,.256,.009),transform=tf)
        # Three LED fields are represented behind the opaque clear-coated cover.
        # Continuous clear-coated cover over the three LED fields, inside the screwed bezel.
        vs=[tf@Vector((.042,yy,zz)) for yy,zz in [(-.128,-.164),(.128,-.164),(.128,.164),(-.128,.164)]]
        b.add(vs,[(0,1,2,3)],slot=1,kind='glasscover')
        # Sixteen thin, flat full-width cooling plates on BOTH LODs.
        # Only the buried root is open; tip and side thickness are real geometry.
        for fi in range(16):
            zz=-.165+.022*fi
            profile=[(-.057,zz-.0015),(-.115,zz-.0015),(-.115,zz+.0015),(-.057,zz+.0015)]
            vs=[tf@Vector((x,yy,z)) for yy in (-.153,.153) for x,z in profile]
            b.add(vs,[(3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6)],kind='fin')
        for yy in (-.129,.129):
            for zz in (-.163,.163):
                c=tf@Vector((.047,yy,zz));d=tf.to_3x3()@Vector((1,0,0))
                if low:b.lathe(c,d,[(0,.009),(.004,.009)],n=4,kind='bolt',cap='front')
                else:bolt(b,c,d,.008,n=8)
        # One yoke pivot per outside side of the pair; bearing enters the casting.
        s=-1 if y<0 else 1;c=(0,s*.405,.510);d=(0,s,0)
        b.lathe((0,s*.335,.510),d,[(0,.017),(.070,.017)],n=6 if low else 12)
        if low:b.lathe(c,d,[(0,.021),(.010,.021)],n=6,kind='bolt')
        else:bolt(b,c,d,.021,n=24)
        path=[(-.025,y,.308),(-.041,y,.256),(-.030,y*.7,.213),(-.020,y*.3,.193)]
        b.tube(smooth_path(path,2 if low else 3),.006,sides=3 if low else 8,kind='cable')
        b.lathe(path[0],(0,0,1),[(0,.011),(.026,.011)],n=4 if low else 8,kind='gland')
    for x in (-.091,.091):
        for y in (-.156,.156):
            if low:b.lathe((x,y,.016),(0,0,1),[(0,.012),(.010,.012)],n=4,kind='bolt')
            else:bolt(b,(x,y,.016),(0,0,1),.012,n=8)
    return b.finish()

def build(piece):return pair(piece,mesh)
