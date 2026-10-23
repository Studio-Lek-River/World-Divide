"""#392 / Round 2: single slotted arm, rear cap and clamp band, rolled mouth, deep reflector."""
import math,sys
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import Builder,pair,bolt,smooth_path

def slotted_arm(b,y,low):
    a=Vector((.038,y,-.064));c=Vector((.252,y,.154));d=(c-a).normalized();u=Vector((-d.z,0,d.x));length=(c-a).length;n=8 if low else 16
    def capsule(start,end,r):
        half=n//2
        return [(end+r*math.cos(-math.pi/2+math.pi*i/(half-1)),r*math.sin(-math.pi/2+math.pi*i/(half-1))) for i in range(half)]+[(start+r*math.cos(math.pi/2+math.pi*i/(half-1)),r*math.sin(math.pi/2+math.pi*i/(half-1))) for i in range(half)]
    outer=capsule(0,length,.026);inner=capsule(.064,length-.064,.010);vs=[]
    for dy in (-.004,.004):
        for poly in (outer,inner):vs += [a+d*x+u*z+Vector((0,dy,0)) for x,z in poly]
    fs=[]
    for i in range(n):
        j=(i+1)%n;fs += [(i,j,j+n,i+n),(i+2*n,i+3*n,j+3*n,j+2*n),(i,i+2*n,j+2*n,j),(i+n,j+n,j+3*n,i+3*n)]
    b.add(vs,fs)

def mesh(name,low):
    b=Builder(name);n=12 if low else 40;e=0 if low else .002
    b.box((.006,0,0),(.012,.100,.300),e)
    b.box((.025,-.025,-.064),(.035,.075,.060),e)
    slotted_arm(b,-.078,low)
    axis=Vector((math.cos(math.pi/6),0,-.5));back=Vector((.204,0,.180));up=Vector((.5,0,math.cos(math.pi/6)))
    b.lathe(back,axis,[(0,.055),(.004,.064),(.014,.068),(.076,.068),(.082,.074)],n=n)
    b.lathe(back,axis,[(.024,.069),(.028,.072),(.039,.072),(.042,.069)],n=n,cap=False,kind='band')
    b.box(back+axis*.033+up*.073,(.024,.018,.012),e,kind='clamp')
    b.lathe(back,axis,[(.080,.075),(.104,.080),(.216,.099),(.235,.102),(.242,.101),(.246,.097),(.241,.093),(.224,.091),(.166,.077)],n=n,cap=False,kind='hood')
    b.disc(back+axis*.195,axis,.079,n=n,kind='honeycomb')
    b.lathe((.038,-.095,-.064),(0,1,0),[(0,.022),(.080,.022)],n=8 if low else 20)
    b.lathe((.252,-.095,.154),(0,1,0),[(0,.021),(.045,.021)],n=8 if low else 20)
    for center,d,r in [((.017,0,-.121),(1,0,0),.009),((.017,0,.121),(1,0,0),.009),((.038,-.095,-.064),(0,-1,0),.016),((.252,-.095,.154),(0,-1,0),.014)]:
        if low:b.lathe(center,d,[(0,r),(.006,r)],n=4,kind='torx')
        else:bolt(b,center,d,r,n=20)
    tip=back-axis*.018+up*.045
    path=[(.025,.020,-.110),(.055,.022,-.091),(.088,.025,.020),(.117,.024,.194),(.155,.018,.281),(.190,.010,.279),tuple(tip)]
    b.tube(smooth_path(path,2 if low else 3),.0045,sides=4 if low else 8,kind='cable')
    for center,d in [((.010,.020,-.110),(1,0,0)),(back-axis*.008+up*.045,-axis)]:b.lathe(center,d,[(0,.009),(.017,.009)],n=4 if low else 8,kind='gland')
    return b.finish()

def build(piece):return pair(piece,mesh)
