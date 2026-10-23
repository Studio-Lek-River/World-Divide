"""Issue 403 Round 4. Deterministic, scoped, destructive rebuild of rubble only.

Run: blender -b --python Tools/Art/usa_hq_rubble.py
No project imports, editor calls, providers, or writes outside the assigned rubble.
Blender +Y is shell garage side; FBX X-forward/Z-up converts it to Unreal -Y.
"""
import bpy, bmesh, math, json, hashlib, random, sys, argparse, struct, warnings
import numpy as np
from pathlib import Path
from mathutils import Vector, Euler, Matrix

REPO = Path(__file__).resolve().parents[2]
HQ = REPO / 'Art/USA/buildings/usa_hq'
OUT = HQ / 'rubble'
SEED = 40301
SLOTS = ['Debris', 'Scorch', 'Embers']
warnings.filterwarnings('ignore', category=DeprecationWarning)
STYLES = {
    'concrete': ((.70,.68,.62), .90, 0),
    'fracture': ((.57,.55,.49), .96, 0),
    'dust': ((.60,.57,.50), .98, 0),
    'paving': ((.51,.49,.44), .94, 0),
    'steel': ((.13,.15,.15), .64, .85),
    'rust': ((.24,.15,.09), .85, .72),
    'equipment': ((.28,.30,.25), .80, .48),
    'membrane': ((.17,.18,.17), .93, 0),
    'scorch': ((.065,.061,.052), .99, 0),
    'ivory': ((.79,.78,.71), .82, 0),
    'glass': ((.075,.13,.16), .24, .3),
    'ember': ((.14,.038,.008), .96, 0),
}

def write_json(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n', encoding='utf8')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

class Builder:
    def __init__(self, lod):
        self.lod=lod; self.v=[]; self.faces=[]; self.meta=[]; self.components=[]
        self.r=random.Random(SEED)

    def solid(self, name, vertices, faces, style='concrete', face_styles=None,orient=True):
        """Convex closed piece, outward winding independently checked at reimport."""
        vertices=[Vector(v) for v in vertices]
        lift=max(0,-.10-min(v.z for v in vertices))
        vertices=[v+Vector((0,0,lift)) for v in vertices]
        centre=sum(vertices,Vector())/len(vertices)
        start=len(self.v); first=len(self.faces)
        self.v.extend(tuple(v) for v in vertices)
        for i,f in enumerate(faces):
            f=list(f); pts=[vertices[j] for j in f]
            n=(pts[1]-pts[0]).cross(pts[2]-pts[0])
            if orient and n.dot(sum(pts,Vector())/len(pts)-centre)<0: f.reverse()
            st=face_styles[i] if face_styles else style
            self.faces.append(tuple(start+j for j in f))
            self.meta.append((st,name))
        self.components.append({'name':name,'first_face':first,'face_count':len(faces),
                                'vertex_start':start,'vertex_count':len(vertices)})

    def box(self,name,loc,size,style='concrete',rot=(0,0,0),top=None):
        m=Euler(rot).to_matrix(); p=Vector(loc)
        v=[p+m@Vector((x*size[0]/2,y*size[1]/2,z*size[2]/2))
           for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
                         (-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        f=[(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)]
        self.solid(name,v,f,style,[style]*5+[top or style])

    def beam(self,name,a,b,r,style='steel',sides=4):
        if self.lod==1 and sides==4 and r<.2:sides=3
        a,b=Vector(a),Vector(b); d=(b-a).normalized()
        u=d.cross(Vector((0,0,1)))
        if u.length<.01:u=d.cross(Vector((0,1,0)))
        u.normalize(); v=d.cross(u)
        verts=[p+r*(math.cos(i*math.tau/sides)*u+math.sin(i*math.tau/sides)*v)
               for p in (a,b) for i in range(sides)]
        faces=[tuple(range(sides-1,-1,-1)),tuple(range(sides,sides*2))]
        faces += [(i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides)]
        self.solid(name,verts,faces,style)

    def wall(self,name,a,b,heights,thick=.38):
        a,b=Vector((*a,0)),Vector((*b,0));d=b-a
        tangent=d.normalized();n=Vector((-d.y,d.x,0)).normalized()
        steps=max(2,math.ceil(d.length/4.0))
        angle=math.atan2(d.y,d.x)
        def piece(tag,t,width,z,height,depth=thick,style='concrete',offset=0):
            if height<.08:return
            p=a+d*t+n*offset+Vector((0,0,z+height/2))
            if True:
                # Thick extrusion with a sheared, chipped and stepped fracture top.
                # Broad broken steps, not the repeated narrow saw teeth of round 3.
                rr=random.Random(name+tag)
                shoulder=rr.uniform(.90,.96)
                profile=[(-.5,.03),(.5,0),(.5,height*.87),(.30,height*shoulder),
                         (.10,height*shoulder),(-.08,height),
                         (-.34,height*.98),(-.5,height*.91)]
                m=Euler((0,0,angle)).to_matrix()
                verts=[p+m@Vector((xx*width,yy*depth,zz-height/2))
                       for yy in (-.5,.5) for xx,zz in profile]
                count=len(profile)
                faces=[tuple(range(count)),tuple(range(2*count-1,count-1,-1))]
                faces += [(j,j+count,(j+1)%count+count,(j+1)%count) for j in range(count)]
                self.solid(name+tag,verts,faces,'fracture',
                           ['ivory','concrete']+['fracture']*count,orient=False)
            else:
                self.box(name+tag,tuple(p),(width,depth,height),style,(0,0,angle))
        for i in range(steps):
            t=(i+.5)/steps;w=d.length/steps
            h=heights[min(int(t*len(heights)),len(heights)-1)]*self.r.uniform(.9,1.12)
            # Stepped crumbling, a few genuine holes with surviving piers/lintels.
            if i%4==2 and not name.startswith('core'):continue
            if h>2.5 and i%3==1 and w>1.2:
                piece(f'_sill{i}',t,w,0,1.36)
                for sign in (-1,1):
                    piece(f'_pier{i}_{sign}',t+sign*(w-.55)/(2*d.length),.55,.95,h-.95)
                if h>4.3:piece(f'_lintel{i}',t+.06,w*.84,2.8,h-2.8)
            else:
                piece(f'_body{i}',t,w,0,h)
            # Broken wall banks on BOTH faces; the wall emerges from the collapse.
            for side in (-1,1):
                p=a+d*t+n*side*.8+Vector((0,0,.65))
                self.slab(name+f'_bank{i}_{side}',tuple(p),(w*1.2,1.8,1.1),
                          (.12,side*.35,angle),'dust')

    def mound(self,name,x,y,sx,sy,h):
        """Closed angular crushed-fill shell. Identical shell in BOTH LODs.

        Offset concentric rings give broad, slumped shoulders, not a cone or an
        assortment of flat chips. Surface panels mask the coarse fill facets.
        """
        rng=random.Random(SEED+sum(map(ord,name)))
        count=20;vertices=[]
        for ring,(rad,z) in enumerate([(1,.08),(.79,h*.39),(.48,h*.82),(.19,h)]):
            for i in range(count):
                a=i*math.tau/count
                # Superellipse follows rectangular blocks, with rounded broken corners.
                cx=math.copysign(abs(math.cos(a))**.65,math.cos(a))
                cy=math.copysign(abs(math.sin(a))**.65,math.sin(a))
                jitter=rng.uniform(.92,1.04)
                vertices.append((x+cx*sx*rad*jitter,y+cy*sy*rad*jitter,
                                 z*rng.uniform(.86,1.1)))
        vertices.append((x+.24,y-.33,h*1.04))
        faces=[tuple(range(count-1,-1,-1))]
        for ring in range(3):
            for i in range(count):
                a=ring*count+i;b=ring*count+(i+1)%count;c=b+count;d=a+count
                faces.extend([(a,b,c),(a,c,d)])
        for i in range(count):faces.append((3*count+i,3*count+(i+1)%count,4*count))
        self.solid(name,vertices,faces,'dust',orient=False)
        
    def slab(self,name,loc,size,rot=(0,0,0),style='fracture',top=None,detail=False):
        # Clipped rectangular section, not a radial/spherical primitive.
        cut=self.r.uniform(.12,.32)
        poly=[(-.36,-.5),(.5-cut,-.5),(.5,-.5+cut),(.5,.27),
              (.32,.5),(-.5+cut*.6,.5),(-.5,.5-cut*.6),(-.5,-.24)]
        # Independent per-piece cuts preserve placement RNG and round-3 massing.
        if style not in ('glass','ember','scorch','membrane') and not name.startswith('court_paving'):
            rr=random.Random('outline:'+name)
            a,b,c,d=[rr.uniform(.025,.43) for _ in range(4)]
            poly=[(-.5+a,-.5),(.5-b,-.5),(.5,-.5+b),(.5,.5-c),
                  (.5-c,.5),(-.5+d,.5),(-.5,.5-d),(-.5,-.5+a)]
        if style=='glass':poly=[(-.5,-.4),(.5,-.5),(.17,.5)]
        elif style in ('ember','scorch') or name.startswith('court_paving'):
            poly=[(-.5,-.4),(.4,-.5),(.5,.3),(-.35,.5)]
        elif self.lod==1 and style not in ('membrane',):
            # Collapse two tiny edge chips; extremal corners/extents retained.
            poly=[poly[i] for i in ((0,1,2,4,6) if detail else (0,2,4,6))]
        count=len(poly)
        m=Euler(rot).to_matrix();c=Vector(loc)
        vv=[c+m@Vector((x*size[0]+z*.12*size[2],y*size[1],
                       z*size[2]))
            for z in (-.5,.5) for j,(x,y) in enumerate(poly)]
        faces=[tuple(range(count-1,-1,-1)),tuple(range(count,2*count))]+[(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
        styles=[style,top or style]+[style]*count
        if detail and self.lod==0:
            # Shallow broken-face relief; identical perimeter vertices at LOD1.
            refined=[];ss=[]
            for fi,f in enumerate(faces):
                if fi<2:
                    # Planar caps ear-clipped at export: no raised centre/fan pinch.
                    refined.append(f);ss.append(styles[fi]);continue
                pts=[vv[i] for i in f];mid=sum(pts,Vector())/len(pts)
                normal=(pts[1]-pts[0]).cross(pts[2]-pts[0]).normalized()
                if normal.dot(mid-c)<0:normal=-normal
                inner=[]
                for j,p in enumerate(pts):
                    inner.append(len(vv));vv.append(mid+(p-mid)*.74
                                                   +normal*min(.018,size[2]*.04))
                for j in range(len(f)):
                    refined.append((f[j],f[(j+1)%len(f)],inner[(j+1)%len(f)],inner[j]));ss.append(styles[fi])
                ci=len(vv);vv.append(sum((vv[k] for k in inner),Vector())/len(inner))
                for j in range(len(inner)):
                    tri=(inner[j],inner[(j+1)%len(inner)],ci)
                    if fi<2 and j==0:
                        vi=len(vv);vv.append(sum((vv[k] for k in tri),Vector())/3-normal*.012)
                        for k in range(3):
                            refined.append((tri[k],tri[(k+1)%3],vi));ss.append(styles[fi])
                    else:
                        refined.append(tri);ss.append(styles[fi])
            faces,styles=refined,ss
        self.solid(name,vv,faces,style,styles,orient=False)

    def rock(self,name,loc,size,detail=True,style='fracture'):
        angle=self.r.uniform(0,math.tau)
        self.slab(name,loc,size,(.18,.27,angle),style,detail=detail)

    def build(self,ember_positions=None):
        r=self.r
        self.ember_positions=[]
        # Chipped foundation: deliberate attached slab, not preview terrain.
        self.solid('buried_foundation',
            [(-18.9,-27.9,-.10),(18.9,-27.9,-.10),(18.9,27.9,-.10),(-18.9,27.9,-.10),
             (-18.65,-27.65,-.025),(18.65,-27.65,-.025),(18.65,27.65,-.025),(-18.65,27.65,-.025)],
            [(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)],'paving')
        nodes=[(-9,0,7.4,7.4,4.25),(-10,-20,7.5,6.5,3.9),(-10,20,7.5,6.5,4.1),
               (6,-19,9.0,7.3,2.9),(6,18,9.0,7.3,3.1),(-2,-10,5.3,4.3,2.4),(-2,10,5.3,4.3,2.6)]
        def fill_z(xx,yy):
            return max([.08]+[h*max(0,1-(abs(xx-x)/sx)**2.6-(abs(yy-y)/sy)**2.6)**.60
                              for x,y,sx,sy,h in nodes])
        for k,(x,y,sx,sy,h) in enumerate(nodes):
            self.mound(f'crushed_fill_{k}',x,y,sx,sy,h)
        # Octagonal core: footprint echoes the recognisable central tower.
        core=[(-16,-4.5),(-13,-7.1),(-5,-7.1),(-1.8,-4),(-1.8,4),(-5,7.1),(-13,7.1),(-16,4.5)]
        for i in range(8):
            a,b=core[i],core[(i+1)%8]
            if i==3:continue # entrance has collapsed inward
            h=5.1 if i in (0,6,7) else 4.6
            self.wall(f'core_wall_{i}',a,b,[h,h*.95,h,h*.85],.38)
        # Rear wings and low front wings. Avoid a generic rectangular perimeter wall.
        outlines=[('rear_east',[(-16.2,14),(-16.2,25.5),(-4,25.5),(-4,14)],5.2),
                  ('rear_west',[(-16.2,-25.5),(-16.2,-14),(-4,-14),(-4,-25.5)],4.8),
                  ('front_west',[(0,-25.3),(14.8,-25.3),(14.8,-13),(0,-13)],2.8),
                  ('garage',[(0,10.7),(0,24.6),(14.9,24.6)],2.5)]
        for name,pts,h in outlines:
            for i in range(len(pts)-1):
                self.wall(name+str(i),pts[i],pts[i+1],[h*.85,h*.55,h,h*.38,h*.67],.38)
        for y in (-10,10):
            self.wall('broken_connector'+str(y),(-3,y),(4,y*1.3),[2.2,1.1,2.6,.65],.42)
        # Half-standing portal, split lintel fallen inward, identifiable bay opening.
        self.wall('garage_portal_north',(14.75,11.0),(14.75,12.0),[5.5,5.8,5.1],.8)
        self.wall('garage_portal_south',(14.75,23.4),(14.75,24.5),[3.7,3.1,2.5],.8)
        self.box('portal_lintel_remaining',(14.7,13.8,5.1),(.9,4.3,.75),'concrete',(.12,0,0))
        self.beam('portal_twisted_crossmember',(14.5,14.7,4.8),(12.4,21.8,1.1),.19)
        self.box('fallen_roller_door',(9.7,18.4,.8),(4.6,8.8,.22),'equipment',(.16,.10,.23))
        # Fractured roof decks: thick exposed concrete with a thin torn membrane.
        for k,(x,y,sx,sy,h) in enumerate(nodes):
            for j in range(3):
                xx=x+r.uniform(-sx*.45,sx*.45);yy=y+r.uniform(-sy*.4,sy*.4)
                rot=(r.uniform(-.6,.6),r.uniform(-.65,.65),r.uniform(-.8,.8))
                size=(r.uniform(2.3,3.4),r.uniform(2.8,4.5),r.uniform(.3,.48))
                c=Vector((xx,yy,fill_z(xx,yy)+.12))
                m=Euler(rot).to_matrix()
                self.slab(f'roof_deck_{k}_{j}',tuple(c),size,rot,'fracture',top='concrete')
                self.slab(f'roof_membrane_{k}_{j}',tuple(c+m@Vector((0,0,size[2]/2+.025))),
                          (size[0]*.94,size[1]*.92,.055),rot,'membrane')
                # Snapped companion folded down into the heap, at a different pitch.
                fold=(rot[0]+.65,rot[1]-.3,rot[2])
                self.slab(f'snapped_roof_half_{k}_{j}',tuple(c+Vector((1.4,.3,-.45))),
                          (size[0]*.55,size[1]*.8,.34),fold,'fracture',top='membrane')
                for n in range(2):
                    p=c+m@Vector(((n-.5)*.75,size[1]*.5,0))
                    self.beam(f'slab_rebar_{k}_{j}_{n}',p,p+m@Vector((.18,.85,.15)),.035,'rust')
        # Leaning/fallen facade panels, pale exterior and broken concrete section.
        for i,(x,y) in enumerate([(-15,-17),(-5,23),(12,-23),(-14,5),(4,12),(12,22)]):
            self.slab(f'fallen_facade_panel{i}',(x,y,fill_z(x,y)+.25),(2.5,2.9,.36),
                      (.55 if i%2 else .12,.5,i*.8),'fracture',top='ivory')
        # Thick fractured slabs stacked on continuous fine fill, plus minor toe scatter.
        # Their centres follow the fill surface rather than sitting at floor height.
        for i in range(220):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            xx=x+r.uniform(-sx*.92,sx*.92);yy=y+r.uniform(-sy*.92,sy*.92)
            ss=r.uniform(1.35,3.25)
            z=fill_z(xx,yy)+r.uniform(.05,.34)
            self.slab(f'collapse_slab_{i}',(xx,yy,z),
                      (ss,ss*r.uniform(.60,1.15),r.uniform(.25,.40)),
                      (r.uniform(-.65,.65),r.uniform(-.62,.62),r.uniform(0,math.tau)),
                      'fracture',top='ivory' if i%7==0 else 'concrete',detail=True)
        for i in range(36):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            a=r.uniform(0,math.tau)
            xx=x+math.cos(a)*sx*.94;yy=y+math.sin(a)*sy*.94
            self.rock(f'toe_scatter_{i}',(xx,yy,fill_z(xx,yy)+.13),
                      (r.uniform(.3,1.0),r.uniform(.3,.8),r.uniform(.25,.4)),False)
        for i in range(36):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            xx=x+r.uniform(-sx*.8,sx*.8);yy=y+r.uniform(-sy*.8,sy*.8)
            self.slab(f'bank_aggregate_{i}',(xx,yy,fill_z(xx,yy)+.18),
                      (r.uniform(.5,1.2),r.uniform(.4,1.0),r.uniform(.25,.4)),
                      (.3,-.24,i*1.6),'fracture',detail=True)
        # Exposed steel bent into the rubble and reinforcing bars at broken wall tops.
        for i in range(35):
            x,y,sx,sy,h=nodes[r.randrange(len(nodes))]
            a=(x+r.uniform(-4,4),y+r.uniform(-4,4),r.uniform(.6,1.7))
            b=(a[0]+r.uniform(1,4),a[1]+r.uniform(-2,2),r.uniform(1,3.5))
            self.beam(f'collapsed_I_web_{i}',a,b,.12)
            self.beam(f'collapsed_I_flange_{i}',Vector(a)+Vector((0,0,.14)),Vector(b)+Vector((0,0,.14)),.17)
        for i in range(30):
            # Short bent bars are embedded in the rear core's highest remnant.
            y=r.uniform(3.1,4.3);z=4.25
            self.beam(f'core_wall_rebar_{i}',(-16,y,z-.5),(-15.8+r.uniform(-.25,.25),y,z+.4),.032,'rust')
        # Fallen core mast: surviving tapering lattice, now across roof debris.
        start=Vector((-9,0,5.1)); end=Vector((3.7,8.6,2.4)); d=(end-start).normalized()
        u=d.cross(Vector((0,0,1))).normalized(); v=d.cross(u)
        rings=[]
        for j in range(9):
            p=start.lerp(end,j/8)+Vector((0,0,1.0*max(0,1-abs(j-4)/4))); radius=.95*(1-j/14)
            ring=[p+radius*(u*math.cos(k*math.tau/3)+v*math.sin(k*math.tau/3)) for k in range(3)]
            rings.append(ring)
            for k in range(3):
                self.beam(f'mast_ring_{j}_{k}',ring[k],ring[(k+1)%3],.10)
                if j:
                    self.beam(f'mast_rail_{j}_{k}',rings[j-1][k],ring[k],.16)
                    self.beam(f'mast_diagonal_{j}_{k}',rings[j-1][k],ring[(k+1)%3],.07)
        for k in range(3):
            a=(-9+.5*math.cos(k*math.tau/3),.5*math.sin(k*math.tau/3),4.3)
            b=(-10+.4*math.cos(k*math.tau/3),.4*math.sin(k*math.tau/3),6.4-k*.3)
            self.beam('mast_snapped_root'+str(k),a,b,.14)
        # Second roof mast, short bent segment and antenna rods.
        for i in range(3):
            self.beam('east_mast_rail'+str(i),(-11+i*.32,19,1),(-5+i*.16,22,1.6),.055)
            self.beam('antenna_rod'+str(i),(3.2+i*.2,8.4,1.6),(5.4+i*.2,9.2,1.2),.028)
        # Detached broken dish: missing wedge, convex closed triangular facets.
        c=Vector((-.2,7.7,3.2)); m=Euler((.32,-.35,.3)).to_matrix()
        centre=c+m@Vector((0,0,-.45))
        for i in range(11):
            if i in (2,3):continue
            a=i*math.tau/12; b=(i+1)*math.tau/12
            top=[centre,c+m@Vector((1.6*math.cos(a),1.6*math.sin(a),.18)),
                 c+m@Vector((1.6*math.cos(b),1.6*math.sin(b),.18))]
            vv=top+[p+m@Vector((0,0,-.10)) for p in top]
            self.solid('broken_dish_'+str(i),vv,[(0,1,2),(5,4,3),(0,3,4,1),(1,4,5,2),(2,5,3,0)],'concrete')
        self.beam('dish_feed_bent',(-.2,7.7,1.1),(.6,8,2),.055)
        self.box('detached_radar_array',(-9,-4,4.0),(3.7,1.65,.18),'steel',(.4,.15,-.55))
        for i in range(5):
            self.box('radar_rib'+str(i),(-10.2+i*.58,-4,4.15),(.09,1.7,.11),'steel',(.4,.15,-.55))
        # Crushed AC equipment, dark fan cavities and ripped ducts.
        for i,(x,y) in enumerate([(-11,21),(-7,-20),(7,-21),(3,15),(-12,-2)]):
            self.box(f'crushed_AHU_{i}',(x,y,fill_z(x,y)+.4),(2.7,1.5,.8),'equipment',(.18,.2,i*.4))
            self.box(f'fan_cavity_{i}',(x,y,fill_z(x,y)+.84),(1.1,1,.04),'scorch',(.18,.2,i*.4))
            for j in range(5):
                self.beam(f'AHU_grille_{i}_{j}',(x-.9+j*.35,y-.65,fill_z(x,y)+.6),(x-.9+j*.35,y+.65,fill_z(x,y)+.78),.037)
            self.beam(f'torn_duct_{i}',(x-1,y+.5,fill_z(x,y)+.1),(x-3,y+1.4,fill_z(x-3,y+1.4)),.36,'steel',6)
        # Courtyard paving remnants, toppled kerbs, glass and outward dust fans.
        for i in range(24):
            x=r.uniform(2,16);y=r.uniform(-10,9)
            self.slab(f'court_paving_fragment{i}',(x,y,.05),(r.uniform(.35,1.4),r.uniform(.3,1.2),.12),
                      (.04,.05,r.uniform(-.3,.3)),'paving')
        for i in range(30):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            self.slab(f'glass_shard{i}',(x+r.uniform(-4,4),y+r.uniform(-4,4),r.uniform(.4,1.3)),
                      (r.uniform(.25,.8),r.uniform(.15,.45),.025),(.2,.3,i*.7),'glass')
        for i,(x,y) in enumerate([(9,-6),(8,4),(15,0)]):
            self.box(f'court_planter_kerb{i}',(x,y,.2),(2.2,.25,.45),'ivory',(0,.12,.25*i))
            self.box(f'court_planter_char{i}',(x,y+.6,.08),(2,1,.14),'scorch')
        for i in range(14):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            p=(x+r.uniform(-4,4),y+r.uniform(-4,4),r.uniform(.5,1.3))
            self.beam(f'fragment_rebar{i}',p,Vector(p)+Vector((.5,.25,.45)),.035,'rust')
        # Discrete low ash volumes are their own slot; not coplanar decals.
        for i in range(24):
            x,y,sx,sy,h=nodes[i%len(nodes)]
            self.rock(f'charred_pocket_{i}',(x+r.uniform(-3,3),y+r.uniform(-3,3),.33),
                      (r.uniform(1.2,3),r.uniform(1,2.6),.6),False,'scorch')
        # Raycast the authored rubble so glow islands sit on an exposed crevice surface.
        from mathutils.bvhtree import BVHTree
        tree=BVHTree.FromPolygons([Vector(v) for v in self.v],self.faces)
        for i,(x,y) in enumerate([(-7,1),(-10,3),(-6,-3),(5,17),(3,-19),(-9,20),(-9,-21),(9,18)]):
            for j in range(4):
                xx=x+j*.33;yy=y+r.uniform(-.25,.25)
                hit=tree.ray_cast(Vector((xx,yy,8)),Vector((0,0,-1)))[0]
                # Use the lowest of fixed nearby exposed surfaces, avoiding roof-top icons.
                candidates=[]
                for ox,oy in [(0,0),(.8,.3),(-.7,-.4),(1.2,-.6)]:
                    hh=tree.ray_cast(Vector((xx+ox,yy+oy,8)),Vector((0,0,-1)))[0]
                    if hh is not None:candidates.append((hh.z,xx+ox,yy+oy))
                if candidates:
                    zz,xx,yy=min(candidates)
                    zz+=.075
                else:zz=.05
                if ember_positions is not None:xx,yy,zz=ember_positions[i*4+j]
                self.ember_positions.append((xx,yy,zz))
                self.rock(f'ember_{i}_{j}',(xx,yy,zz),(r.uniform(.5,1.1),r.uniform(.10,.22),.025),False,'ember')
        # The silhouette and frame are an explicit hard contract.
        assert all(z>=-.100001 for x,y,z in self.v)
        mesh=bpy.data.meshes.new('HQ_Rubble_Geometry_LOD'+str(self.lod))
        mesh.from_pydata(self.v,[],self.faces);mesh.update()
        obj=bpy.data.objects.new('SM_HQ_Rubble'+('_LOD1' if self.lod else ''),mesh)
        bpy.context.collection.objects.link(obj)
        for name in SLOTS:mesh.materials.append(bpy.data.materials[name])
        for p,(style,_) in zip(mesh.polygons,self.meta):
            p.material_index=2 if style=='ember' else 1 if style=='scorch' else 0
        return obj

def plane_records(obj,meta):
    records=[]
    for p,(style,name) in zip(obj.data.polygons,meta):
        pts=[obj.data.vertices[i].co for i in p.vertices]
        u=(pts[1]-pts[0]).normalized();n=p.normal;v=n.cross(u).normalized()
        coords=[Vector(((q-pts[0]).dot(u),(q-pts[0]).dot(v))) for q in pts]
        mn=Vector((min(q.x for q in coords),min(q.y for q in coords)))
        mx=Vector((max(q.x for q in coords),max(q.y for q in coords)))
        records.append({'face':p.index,'style':style,'name':name,'slot':p.material_index,
                        'uvlocal':[q-mn for q in coords],'size':mx-mn,
                        'origin':pts[0]+u*mn.x+v*mn.y,'u':u,'v':v})
    return records

def pack(records,size):
    ordered=sorted(records,key=lambda q:(-q['size'].y,-q['size'].x,q['face']))
    def attempt(scale,apply=False):
        x=y=row=0
        for q in ordered:
            w=max(3,math.ceil(q['size'].x*scale))+6
            h=max(3,math.ceil(q['size'].y*scale))+6
            if w>size:return False
            if x+w>size:x=0;y+=row;row=0
            if y+h>size:return False
            if apply:q['rect']=(x,y,w,h)
            x+=w;row=max(row,h)
        return True
    lo,hi=.01,100
    for _ in range(30):
        mid=(lo+hi)/2
        if attempt(mid):lo=mid
        else:hi=mid
    assert attempt(lo*.995,True)
    return lo*.995

def png(name,array,colorspace):
    h,w,_=array.shape
    im=bpy.data.images.new(name,width=w,height=h,alpha=False)
    im.colorspace_settings.name='Non-Color'
    rgba=np.ones((h,w,4),np.float32);rgba[:,:,:3]=np.clip(array,0,1)
    im.pixels.foreach_set(rgba.ravel());im.filepath_raw=str(OUT/'textures'/f'{name}.png')
    im.file_format='PNG';im.save()
    im.colorspace_settings.name=colorspace; im.filepath='//../textures/'+name+'.png'
    return im

def atlas(obj,meta):
    records=plane_records(obj,meta); uv=obj.data.uv_layers.new(name='UV0')
    texture_info={}; sizes=[2048,1024,512]
    for slot,size in enumerate(sizes):
        rr=[q for q in records if q['slot']==slot]; density=pack(rr,size)
        base=np.zeros((size,size,3),np.float32);base[:]=(.13,.12,.10)
        orm=np.zeros_like(base);orm[:]=(1,.95,0)
        normal=np.zeros_like(base);normal[:]=(.5,.5,1)
        emissive=np.zeros_like(base)
        rng=np.random.default_rng(SEED+slot)
        for q in rr:
            x,y,w,h=q['rect'];p=obj.data.polygons[q['face']]
            sx,sy=q['size']; innerw=w-6;innerh=h-6
            for li,co in zip(p.loop_indices,q['uvlocal']):
                uv.data[li].uv=((x+3+co.x/max(sx,1e-9)*innerw)/size,
                                (y+3+co.y/max(sy,1e-9)*innerh)/size)
            yy,xx=np.mgrid[0:h,0:w];fx=(xx-3)/innerw;fy=(yy-3)/innerh
            origin=np.array(q['origin']);u=np.array(q['u']);v=np.array(q['v'])
            world=origin[None,None,:]+fx[:,:,None]*sx*u+fy[:,:,None]*sy*v
            # World-continuous soot gradients, aggregate, cracks and pale outer dust.
            X,Y,Z=world[:,:,0],world[:,:,1],world[:,:,2]
            grain=rng.random((h,w),dtype=np.float32)
            coarse=(np.sin(X*8.1+np.sin(Y*3.2))*np.sin(Y*7.6+Z*3.1))*.5+.5
            burnt=np.maximum(np.exp(-((X+8)**2/34+Y**2/35)),
                             .8*np.exp(-((X-5)**2/25+(Y-18)**2/28)))
            for cx,cy in [(-10,-20),(-10,20),(6,-19)]:
                burnt=np.maximum(burnt,.85*np.exp(-((X-cx)**2/22+(Y-cy)**2/25)))
            col,rough,metal=STYLES[q['style']]
            color=np.array(col)[None,None,:]*(.97+.012*grain[:,:,None]+.012*coarse[:,:,None])
            if q['style'] in ('concrete','fracture','dust','paving','ivory'):
                soot=np.clip(burnt*1.38,0,.94)*(.90+.10*np.exp(-Z/4))
                if 'wall' in q['name'] or q['name'].startswith(('rear_', 'front_', 'garage')):
                    soot=np.maximum(soot,.60*np.exp(-Z/9))
                is_wall=any(t in q['name'] for t in ('wall','rear_','front_','garage'))
                streak=(.70+.30*np.sin(X*4+Y*5+np.sin(Z*.7)*.4)**2) if is_wall else np.ones_like(X)
                color*=1-.86*soot[:,:,None]*streak[:,:,None]
                if is_wall and q['style']=='concrete':color*=.68
                if q['style']=='ivory':
                    color*=1-.18*burnt[:,:,None]
                if q['style']=='paving':
                    grid=(np.mod(X+19,1.35)<.025)|(np.mod(Y+28,1.35)<.025)
                    color[grid]*=.94
                    rings=np.zeros_like(X)
                    for cx,cy in [(-9,0),(-10,-20),(-10,20),(6,-19),(6,18)]:
                        radius=np.sqrt((X-cx)**2+(Y-cy)**2)
                        rings=np.maximum(rings,np.exp(-((radius-5.8)/1.1)**2)*.22)
                    color*=1-rings[:,:,None]
                    # Pale fan-shaped dust deposits extending outward from each mass.
                    fan=np.zeros_like(X)
                    for cx,cy in [(-9,0),(-10,-20),(-10,20),(6,-19),(6,18)]:
                        dx=X-cx;dy=Y-cy
                        fan=np.maximum(fan,np.exp(-(dx*dx/42+dy*dy/32))*(.7+.3*np.sin(np.arctan2(dy,dx)*13)**2))
                    color=color*(1-.32*fan[:,:,None])+np.array((.68,.66,.59))[None,None,:]*.32*fan[:,:,None]
                pores=grain<.04;color[pores]*=.98
                aggregate=(grain>.94)&(coarse>.45);color[aggregate]*=1.02
                if q['style']=='fracture':
                    color*= (.94+.06*coarse[:,:,None])
                cracks=np.abs(np.sin(X*2.13+Y*.73+np.sin(Y*3)*.11))<.014
                color[cracks]*=.70 if q['style']=='paving' else .92
            elif q['style'] in ('steel','rust','equipment'):
                stain=(np.sin(X*13+Y*4)>.92)&(grain>.5)
                color[stain]=color[stain]*.6+np.array((.10,.045,.015))
            base[y:y+h,x:x+w]=color
            orm[y:y+h,x:x+w,0]=.88+.12*grain
            orm[y:y+h,x:x+w,1]=np.clip(rough+(grain-.5)*.09,0,1)
            orm[y:y+h,x:x+w,2]=metal
            normal[y:y+h,x:x+w,0]=.5+(grain-.5)*.065
            normal[y:y+h,x:x+w,1]=.5+(coarse-.5)*.065 # DirectX: green inverted in Blender graph
            if slot==2:
                edge=np.clip(np.minimum.reduce([fx,1-fx,fy,1-fy])*7,0,1)
                hot=np.clip((.28+.42*grain+coarse*.25)*edge,0,1)
                if p.normal.z<.3:hot*=.05
                emissive[y:y+h,x:x+w]=hot[:,:,None]
        prefix='T_HQ_Rubble_'+SLOTS[slot]
        maps={'BaseColor':png(prefix+'_BaseColor',base,'sRGB'),
              'Normal':png(prefix+'_Normal',normal,'Non-Color'),
              'ORM':png(prefix+'_ORM',orm,'Non-Color')}
        if slot==2:maps['Emissive']=png(prefix+'_Emissive',emissive,'Non-Color')
        mat=bpy.data.materials[SLOTS[slot]];mat.use_nodes=True
        ns=mat.node_tree.nodes;ns.clear();ln=mat.node_tree.links
        output=ns.new('ShaderNodeOutputMaterial');output.location=(680,0)
        bs=ns.new('ShaderNodeBsdfPrincipled');bs.location=(400,0);ln.new(bs.outputs['BSDF'],output.inputs['Surface'])
        nodes={}
        for i,(kind,im) in enumerate(maps.items()):
            node=ns.new('ShaderNodeTexImage');node.image=im;node.label=kind;node.location=(-700,300-i*260);nodes[kind]=node
        ln.new(nodes['BaseColor'].outputs['Color'],bs.inputs['Base Color'])
        sep=ns.new('ShaderNodeSeparateColor');sep.location=(-380,-200)
        ln.new(nodes['ORM'].outputs['Color'],sep.inputs[0]);ln.new(sep.outputs[1],bs.inputs['Roughness']);ln.new(sep.outputs[2],bs.inputs['Metallic'])
        # Delivered normal is DX; Blender preview converts green to OpenGL.
        split=ns.new('ShaderNodeSeparateColor');combine=ns.new('ShaderNodeCombineColor')
        inv=ns.new('ShaderNodeMath');inv.operation='SUBTRACT';inv.inputs[0].default_value=1
        ln.new(nodes['Normal'].outputs['Color'],split.inputs[0]);ln.new(split.outputs[0],combine.inputs[0])
        ln.new(split.outputs[1],inv.inputs[1]);ln.new(inv.outputs[0],combine.inputs[1]);ln.new(split.outputs[2],combine.inputs[2])
        nm=ns.new('ShaderNodeNormalMap');nm.uv_map='UV0';nm.inputs['Strength'].default_value=.4
        ln.new(combine.outputs[0],nm.inputs['Color']);ln.new(nm.outputs[0],bs.inputs['Normal'])
        if slot==2:
            tint=ns.new('ShaderNodeMixRGB');tint.blend_type='MULTIPLY';tint.inputs[0].default_value=1
            tint.inputs[2].default_value=(1,.105,.008,1);ln.new(nodes['Emissive'].outputs['Color'],tint.inputs[1])
            ln.new(tint.outputs[0],bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=7
        texture_info[SLOTS[slot]]={'resolution':[size,size],'atlas_faces':len(rr),'nominal_texels_per_m':density,
            'maps':{k:im.name+'.png' for k,im in maps.items()},'UV0':'Unique planar polygon islands, 3 pixel gutters; no tiling or overlaps.'}
    return records,texture_info

def transfer_uv(low,high,records,low_meta,high_meta):
    """Retained architecture copies exactly. Small rock faces use high atlas by nearest containing facet."""
    lookup={}
    for p,q in zip(high.data.polygons,records):
        lookup.setdefault(q['name'],[]).append((p,q))
    uv=low.data.uv_layers.new(name='UV0');used=set()
    for p,(_,name) in zip(low.data.polygons,low_meta):
        options=[pq for pq in lookup[name] if pq[0].index not in used]
        # For identical meshes matching face order, nearest polygon centre is exact.
        hp,q=min(options,key=lambda pq:(pq[0].center-p.center).length_squared)
        used.add(hp.index)
        x,y,w,h=q['rect'];size=[2048,1024,512][q['slot']]
        # Reduced small stones retain the same concrete map, remapped into one unique existing face island.
        pts=[low.data.vertices[i].co for i in p.vertices]
        u=(pts[1]-pts[0]).normalized();v=p.normal.cross(u).normalized()
        cc=[Vector(((t-pts[0]).dot(u),(t-pts[0]).dot(v))) for t in pts]
        mn=Vector((min(t.x for t in cc),min(t.y for t in cc)));mx=Vector((max(t.x for t in cc),max(t.y for t in cc)))
        for li,c in zip(p.loop_indices,cc):
            uv.data[li].uv=((x+3+(c.x-mn.x)/max(mx.x-mn.x,1e-8)*(w-6))/size,
                            (y+3+(c.y-mn.y)/max(mx.y-mn.y,1e-8)*(h-6))/size)

def surface_detail(obj):
    uv=obj.data.uv_layers.new(name='SurfaceDetail')
    for p in obj.data.polygons:
        pts=[obj.data.vertices[i].co for i in p.vertices]
        u=(pts[1]-pts[0]).normalized();v=p.normal.cross(u).normalized()
        for li,co in zip(p.loop_indices,pts):
            uv.data[li].uv=(co.dot(u)*2,co.dot(v)*2) # 0.5 m repeat, matching UV0 tangent orientation

def sharp_normal():
    size=2048
    yy,xx=np.mgrid[0:size,0:size].astype(np.float32)
    rng=np.random.default_rng(SEED+900)
    def noise(cells):
        grid=rng.random((cells,cells),dtype=np.float32)
        co=np.arange(size,dtype=np.float32)*cells/size
        index=np.floor(co).astype(int);frac=co-index;frac=frac*frac*(3-2*frac)
        a=grid[index[:,None]%cells,index[None,:]%cells]
        b=grid[index[:,None]%cells,(index[None,:]+1)%cells]
        c=grid[(index[:,None]+1)%cells,index[None,:]%cells]
        d=grid[(index[:,None]+1)%cells,(index[None,:]+1)%cells]
        return (a*(1-frac[None,:])+b*frac[None,:])*(1-frac[:,None])+(c*(1-frac[None,:])+d*frac[None,:])*frac[:,None]
    aggregate=noise(48);coarse=noise(12)
    height=(aggregate-.5)*.42+(coarse-.5)*.32
    height+=rng.random((size,size),dtype=np.float32)*.006
    dx=(np.roll(height,-1,1)-np.roll(height,1,1))*16
    dy=(np.roll(height,-1,0)-np.roll(height,1,0))*16
    normal=np.dstack((.5-dx,.5+dy,np.ones_like(dx)))
    old=bpy.data.images.get('T_HQ_Rubble_Debris_Normal')
    if old:bpy.data.images.remove(old,do_unlink=True)
    im=png('T_HQ_Rubble_Debris_Normal',normal,'Non-Color')
    mat=bpy.data.materials['Debris']
    tex=next(n for n in mat.node_tree.nodes if n.type=='TEX_IMAGE' and n.label=='Normal')
    tex.image=im
    coord=mat.node_tree.nodes.new('ShaderNodeUVMap');coord.uv_map='SurfaceDetail'
    mat.node_tree.links.new(coord.outputs['UV'],tex.inputs['Vector'])
    nm=next(n for n in mat.node_tree.nodes if n.type=='NORMAL_MAP');nm.uv_map='SurfaceDetail'
    nm.inputs['Strength'].default_value=.85
    grain=rng.random((size,size),dtype=np.float32)
    grit=.69+.25*aggregate+.12*coarse+.04*(grain-.5)
    detail=png('T_HQ_Rubble_Debris_DetailColor',np.repeat(grit[:,:,None],3,axis=2),'Non-Color')
    ns=mat.node_tree.nodes;ln=mat.node_tree.links
    tile=ns.new('ShaderNodeTexImage');tile.image=detail;tile.label='DetailColor (linear multiplier)'
    ln.new(coord.outputs['UV'],tile.inputs['Vector'])
    base=next(n for n in ns if n.type=='TEX_IMAGE' and n.label=='BaseColor')
    bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
    mix=ns.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=.65
    ln.new(base.outputs['Color'],mix.inputs[1]);ln.new(tile.outputs['Color'],mix.inputs[2])
    ln.new(mix.outputs['Color'],bs.inputs['Base Color'])


def stats(obj):
    m=obj.data;m.calc_loop_triangles();m.calc_tangents(uvmap='UV0')
    detail_bad=0
    if 'SurfaceDetail' in m.uv_layers:
        du=m.uv_layers['SurfaceDetail']
        for t in m.loop_triangles:
            a,b,c=[du.data[i].uv for i in t.loops]
            detail_bad+=abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-13
    assert detail_bad==0,('SurfaceDetail degenerates',detail_bad)
    coords=[obj.matrix_world@v.co for v in m.vertices]
    bounds=[[round(f(v[i] for v in coords)*100,5) for i in range(3)] for f in (min,max)]
    h=hashlib.sha256()
    for v in m.vertices:h.update(struct.pack('<3f',*v.co))
    for p in m.polygons:h.update(struct.pack('<'+str(len(p.vertices))+'I',*p.vertices));h.update(bytes([p.material_index]))
    uv=m.uv_layers['UV0']
    uv_bounds=[[float(f(loop.uv[i] for loop in uv.data)) for i in (0,1)] for f in (min,max)]
    uv_degenerate=0
    for t in m.loop_triangles:
        a,b,c=[uv.data[i].uv for i in t.loops]
        if abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-13:uv_degenerate+=1
    return {'vertices':len(m.vertices),'triangles':len(m.loop_triangles),'slots':[s.name for s in m.materials],
            'triangles_per_slot':{s:sum(t.material_index==i for t in m.loop_triangles) for i,s in enumerate(SLOTS)},
            'bounds_blender_cm':bounds,'bounds_unreal_cm':[[bounds[0][0],-bounds[1][1],bounds[0][2]],[bounds[1][0],-bounds[0][1],bounds[1][2]]],
            'origin_m':list(obj.location),'scale':list(obj.scale),'geometry_sha256':h.hexdigest(),
            'degenerate_triangles':sum(t.area<1e-10 for t in m.loop_triangles),
            'zero_tangent_loops':sum(l.tangent.length<.5 for l in m.loops),
            'zero_bitangent_loops':sum(l.bitangent.length<.5 for l in m.loops),
            'uv1_degenerate_triangles':int(detail_bad),'uv_channels':[u.name for u in m.uv_layers],'degenerate_uv_triangles':uv_degenerate,
            'uv0_bounds':uv_bounds,'uv0_out_of_range_loops':sum(any(c<0 or c>1 for c in loop.uv) for loop in uv.data)}

def export(obj,path):
    # Bake cm into a disposable export copy, leaving editable source in metres.
    copy=obj.copy();copy.data=obj.data.copy();bpy.context.collection.objects.link(copy)
    copy.data.transform(Matrix.Scale(100,4));old_unit=bpy.context.scene.unit_settings.scale_length
    bpy.context.scene.unit_settings.scale_length=.01
    bpy.ops.object.select_all(action='DESELECT');copy.select_set(True);bpy.context.view_layer.objects.active=copy
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},
        use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_anim=False,
        add_leaf_bones=False,path_mode='STRIP',use_custom_props=False)
    bpy.context.scene.unit_settings.scale_length=old_unit
    mesh=copy.data;bpy.data.objects.remove(copy,do_unlink=True);bpy.data.meshes.remove(mesh)

def independent_checks(expected):
    results={}
    for name,e in expected.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=str(OUT/(name+'.fbx')))
        meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(meshes)==1
        o=meshes[0];s=stats(o)
        assert s['triangles']==e['triangles'];assert s['slots']==SLOTS
        error=max(abs(s['bounds_blender_cm'][j][i]-e['bounds_blender_cm'][j][i]) for j in range(2) for i in range(3))
        assert error<.01,(name,error)
        assert not any(s[k] for k in ('degenerate_triangles','zero_tangent_loops','zero_bitangent_loops','degenerate_uv_triangles','uv0_out_of_range_loops'))
        # Topology, UV overlap and winding via a fresh imported mesh, no authored metadata.
        bm=bmesh.new();bm.from_mesh(o.data);bm.verts.ensure_lookup_table();bm.faces.ensure_lookup_table()
        inconsistent=set()
        for edge in bm.edges:
            loops=list(edge.link_loops)
            if len(loops)==2 and loops[0].vert==loops[1].vert:
                inconsistent.update(l.face.index for l in loops)
        visited=set();flipped=0;open_edges=sum(not e.is_manifold for e in bm.edges);volumes=[]
        for face in bm.faces:
            if face.index in visited:continue
            group=[];stack=[face];visited.add(face.index)
            while stack:
                f=stack.pop();group.append(f)
                for edge in f.edges:
                    for neighbour in edge.link_faces:
                        if neighbour.index not in visited:visited.add(neighbour.index);stack.append(neighbour)
            vol=sum(f.verts[0].co.dot(f.verts[i].co.cross(f.verts[i+1].co))/6 for f in group for i in range(1,len(f.verts)-1))
            volumes.append(vol)
            # A negative closed component volume is a globally inverted piece.
            if vol<=0:flipped+=len(group)
        bm.free()
        assert flipped==0 and open_edges==0 and not inconsistent,(name,flipped,open_edges,len(inconsistent))
        # UV triangles overlap test using a spatial hash, excluding shared edges.
        u=o.data.uv_layers['UV0'];grid={};overlaps=0
        def clip(poly,a,b):
            def cross(p):return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
            out=[]
            for i,p in enumerate(poly):
                q=poly[(i+1)%len(poly)];cp,cq=cross(p),cross(q)
                if cp>=-1e-12:out.append(p)
                if (cp>0 and cq<0) or (cp<0 and cq>0):
                    t=cp/(cp-cq);out.append((p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])))
            return out
        triangles=[]
        for t in o.data.loop_triangles:
            points=[tuple(u.data[li].uv) for li in t.loops]
            if (points[1][0]-points[0][0])*(points[2][1]-points[0][1])-(points[1][1]-points[0][1])*(points[2][0]-points[0][0])<0:points.reverse()
            triangles.append((t.material_index,points))
        for ti,(slot,points) in enumerate(triangles):
            lower=[min(p[j] for p in points) for j in (0,1)];upper=[max(p[j] for p in points) for j in (0,1)]
            cells=[(slot,x,y) for x in range(int(lower[0]*64),int(upper[0]*64)+1) for y in range(int(lower[1]*64),int(upper[1]*64)+1)]
            previous=set(k for cell in cells for k in grid.get(cell,[]))
            for k in previous:
                poly=list(points);other=triangles[k][1]
                for i in range(3):
                    if not poly:break
                    poly=clip(poly,other[i],other[(i+1)%3])
                if len(poly)>2:
                    area=abs(sum(poly[i][0]*poly[(i+1)%len(poly)][1]-poly[(i+1)%len(poly)][0]*poly[i][1] for i in range(len(poly))))/2
                    if area>1e-10:
                        overlaps+=1
            for cell in cells:grid.setdefault(cell,[]).append(ti)
        assert overlaps==0,(name,'UV overlap',overlaps)
        from io_scene_fbx import parse_fbx
        root,version=parse_fbx.parse(str(OUT/(name+'.fbx')))
        global_settings=next(el for el in root.elems if el.id==b'GlobalSettings')
        props=next(el for el in global_settings.elems if el.id==b'Properties70')
        frame={p.props[0].decode():(p.props[-1].decode() if isinstance(p.props[-1],bytes) else p.props[-1]) for p in props.elems if p.id==b'P'}
        geometry=next(el for el in next(el for el in root.elems if el.id==b'Objects').elems if el.id==b'Geometry')
        raw_vertices=np.array(next(el for el in geometry.elems if el.id==b'Vertices').props[0]).reshape(-1,3)
        raw_bounds=[raw_vertices.min(axis=0).tolist(),raw_vertices.max(axis=0).tolist()]
        assert abs(frame['UnitScaleFactor']-1)<1e-8,frame
        # FBX node rotation carries the axis conversion. Raw positions must already be cm.
        assert abs(max(abs(raw_vertices[:,0]))-1890)<.02,raw_bounds
        s.update({'flipped_faces':flipped,'inconsistent_winding_faces':len(inconsistent),
                  'fbx_global_settings':frame,'fbx_raw_vertex_bounds_cm':raw_bounds,
                  'nonmanifold_edges':open_edges,'closed_components':len(volumes),
                  'uv0_overlapping_triangle_pairs':overlaps,'max_cm_round_trip_error':error,
                  'single_static_mesh':True,'skeletons':sum(o.type=='ARMATURE' for o in bpy.context.scene.objects)})
        results[name]=s
    return results

def point(obj,target):
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()

def preview_material(name,color):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    bs=mat.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value=(*color,1)
    bs.inputs['Roughness'].default_value=.85
    return mat

def review(objects):
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32
    try:
        prefs=bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type='OPTIX';prefs.get_devices()
        for device in prefs.devices:device.use=device.type=='OPTIX'
        if any(d.use for d in prefs.devices):scene.cycles.device='GPU'
    except Exception as exc:print('Cycles CPU fallback:',exc,flush=True)
    scene.cycles.use_denoising=True;scene.render.resolution_x=1280;scene.render.resolution_y=720
    scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
    scene.view_settings.view_transform='AgX';scene.render.film_transparent=False
    scene.world=bpy.data.worlds.new('Neutral_review_world');scene.world.use_nodes=True
    bg=scene.world.node_tree.nodes.get('Background');bg.inputs[0].default_value=(.52,.59,.68,1);bg.inputs[1].default_value=.5
    bpy.ops.mesh.primitive_plane_add(size=2000,location=(0,0,-.13));ground=bpy.context.object;ground.name='REVIEW_ONLY_ground'
    mat=preview_material('REVIEW_ONLY_ground',(.19,.18,.16));ground.data.materials.append(mat)
    bpy.ops.object.light_add(type='SUN',location=(20,-30,50));sun=bpy.context.object;sun.name='REVIEW_ONLY_sun';sun.rotation_euler=(.45,-.38,-.6);sun.data.energy=3;sun.data.angle=.12
    bpy.ops.object.camera_add();camera=bpy.context.object;camera.name='ReviewCamera';scene.camera=camera
    camera.data.type='PERSP';camera.data.lens=31.17691454;camera.data.sensor_width=36;camera.data.clip_end=2500
    shots=[]
    def shot(name,dist=90,target=(0,0,1.2),az=-45,elev=55,lod=0,night=False,ortho=None):
        objects[0].hide_render=lod!=0;objects[1].hide_render=lod!=1
        a=math.radians(az);e=math.radians(elev)
        camera.location=Vector(target)+Vector((math.cos(a)*math.cos(e),math.sin(a)*math.cos(e),math.sin(e)))*dist
        point(camera,target);camera.data.type='ORTHO' if ortho else 'PERSP'
        if ortho:camera.data.ortho_scale=ortho
        bg.inputs[0].default_value=(.22,.30,.46,1) if night else (.52,.59,.68,1)
        bg.inputs[1].default_value=.35 if night else .5;sun.data.energy=.16 if night else 3
        scene.render.filepath=str(OUT/'review'/f'{name}.png');bpy.ops.render.render(write_still=True)
        shots.append({'file':name+'.png','engine':'Blender Cycles','samples':32,'distance_m':dist,
                      'target_blender_m':list(target),'camera_location_m':list(camera.location),
                      'rotation_euler':list(camera.rotation_euler),'horizontal_fov_deg':60,'lod':lod,'night':night,'ortho_width_m':ortho})
        print('REVIEW',name,flush=True)
    shot('final_neutral_rts',105,az=-45,elev=55)
    shot('final_neutral_reference',83,az=-32,elev=31)
    shot('night_rts',105,az=-45,elev=55,night=True)
    shot('night_reference',83,az=-32,elev=31,night=True)
    shot('rts_12m',12,target=(13,16,1.8),az=-45,elev=55)
    shot('rts_150m',150)
    shot('rts_150m_night',150,night=True)
    shot('rts_450m',450,lod=1)
    shot('lod0_260m',260,lod=0);shot('lod1_260m',260,lod=1)
    shot('lod0_249p6m',249.6,lod=0);shot('lod1_270p4m',270.4,lod=1)
    # Footprint is a preview-only closed curve, never exported.
    curve=bpy.data.curves.new('Footprint_38x56m','CURVE');curve.dimensions='3D';curve.bevel_depth=.065;curve.bevel_resolution=0
    s=curve.splines.new('POLY');s.points.add(3)
    for p,co in zip(s.points,[(-19,-28,.08),(19,-28,.08),(19,28,.08),(-19,28,.08)]):p.co=(*co,1)
    s.use_cyclic_u=True;outline=bpy.data.objects.new('REVIEW_ONLY_footprint',curve);scene.collection.objects.link(outline)
    m=preview_material('REVIEW_ONLY_outline',(1,.27,.025));outline.data.materials.append(m)
    shot('top_footprint',100,az=0,elev=90,ortho=106)
    outline.hide_render=True
    # Intact shell only, read-only import and matched neutral presentation.
    bpy.ops.import_scene.fbx(filepath=str(HQ/'game/SM_HQ_Shell.fbx'))
    intact=[o for o in bpy.context.selected_objects if o.type=='MESH'];assert len(intact)==1
    shell=intact[0];shell.name='REVIEW_ONLY_intact_shell';shell.location.y=-38
    neutral=preview_material('REVIEW_ONLY_intact_neutral',(.46,.46,.42))
    for i in range(len(shell.data.materials)):shell.data.materials[i]=neutral
    objects[0].location.y=38;objects[1].hide_render=True
    shot('intact_vs_rubble',153,target=(0,0,2),az=0,elev=52)
    objects[0].location.y=0;shell.hide_render=True
    # Save camera index and pixel difference for the exact 260 m pair.
    a=bpy.data.images.load(str(OUT/'review/lod0_260m.png'),check_existing=False)
    b=bpy.data.images.load(str(OUT/'review/lod1_260m.png'),check_existing=False)
    aa=np.array(a.pixels[:],dtype=np.float32).reshape(-1,4)[:,:3];bb=np.array(b.pixels[:],dtype=np.float32).reshape(-1,4)[:,:3]
    diff=np.abs(aa-bb)
    metric={'mean_absolute_linear_rgb':float(diff.mean()),'fraction_pixels_max_channel_diff_gt_0p02':float(np.mean(diff.max(axis=1)>.02)),
            'scope':'Full 1280x720 image, identical camera/light/sample seed, Cycles denoised; human review still required.'}
    write_json(OUT/'review/cameras.json',shots);write_json(OUT/'evidence/lod_render_comparison.json',metric)
    # Intact shell is deliberately not saved into the editable rubble source.
    bpy.data.objects.remove(shell,do_unlink=True)
    return shots,metric

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-review',action='store_true')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    for d in ['source','textures','review','evidence']:(OUT/d).mkdir(parents=True,exist_ok=True)
    previous=json.loads((OUT/'evidence/verification.json').read_text()) if (OUT/'evidence/verification.json').exists() else None
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
    bpy.context.preferences.filepaths.save_version=0
    for s in SLOTS:bpy.data.materials.new(s)
    builders=[Builder(i) for i in (0,1)]
    objects=[builders[0].build()]
    objects.append(builders[1].build(builders[0].ember_positions))
    fill_receipt=[]
    for hi,lo in zip([c for c in builders[0].components if c['name'].startswith('crushed_fill_')],
                     [c for c in builders[1].components if c['name'].startswith('crushed_fill_')]):
        hv=builders[0].v[hi['vertex_start']:hi['vertex_start']+hi['vertex_count']]
        lv=builders[1].v[lo['vertex_start']:lo['vertex_start']+lo['vertex_count']]
        assert hi['name']==lo['name'] and hv==lv
        fill_receipt.append({'name':hi['name'],'identical_LOD_vertices':True,
                             'bounds_m':[[f(v[i] for v in hv) for i in range(3)] for f in (min,max)]})
    records,textures=atlas(objects[0],builders[0].meta)
    transfer_uv(objects[1],objects[0],records,builders[1].meta,builders[0].meta)
    for obj in objects:surface_detail(obj)
    sharp_normal()
    textures['Debris']['maps']['DetailColor']='T_HQ_Rubble_Debris_DetailColor.png'
    textures['Debris']['DetailColor']='Linear tiled aggregate multiplier on UV1; lerp(1, DetailColor.rgb, 0.65) * UV0 BaseColor'
    textures['Debris']['Normal_UV_channel']=1
    for obj in objects:
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='EAR_CLIP')
        bm.to_mesh(obj.data);bm.free();obj.data.update()
    expected={o.name:stats(o) for o in objects}
    print('GEOMETRY',json.dumps(expected),flush=True)
    assert expected['SM_HQ_Rubble']['triangles']<=60000
    assert expected['SM_HQ_Rubble_LOD1']['triangles']<=15000
    for name,s in expected.items():
        assert s['bounds_blender_cm'][0][0]>=-1900 and s['bounds_blender_cm'][1][0]<=1900
        assert s['bounds_blender_cm'][0][1]>=-2800 and s['bounds_blender_cm'][1][1]<=2800
        assert s['bounds_blender_cm'][0][2]>=-20 and s['bounds_blender_cm'][1][2]<=800
        assert not any(s[k] for k in ('degenerate_triangles','degenerate_uv_triangles','zero_tangent_loops','zero_bitangent_loops'))
        export(bpy.data.objects[name],OUT/(name+'.fbx'))
    objects[1].hide_render=True;objects[1].hide_set(True)
    bpy.ops.object.select_all(action='DESELECT');objects[0].select_set(True);bpy.context.view_layer.objects.active=objects[0]
    for im in bpy.data.images:
        if im.source=='FILE':im.filepath='//../textures/'+Path(im.filepath).name
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/HQ_Rubble.blend'))
    # Independent reimport operates on empty factory scenes and never trusts source polygon normals.
    checks=independent_checks(expected)
    reproducibility={'seed':SEED,'algorithm':'sha256 of ordered float32 positions, polygon indices, slot indices',
                     'previous_run_available':previous is not None,
                     'same_geometry_as_previous_run':{name:previous['source'][name]['geometry_sha256']==s['geometry_sha256'] for name,s in expected.items()} if previous else None}
    verification={'issue':403,'round':4,'blender':bpy.app.version_string,'source':expected,'fbx_reimport':checks,
        'round3_collapse':{'continuous_fill_shells':fill_receipt,'stacked_primary_slabs':220,
             'small_bank_chunks':36,'minor_toe_scatter':36,'primary_slab_section_m':[.25,.40],
             'wall_body_section_m':.38,'core_standing_segments':7,
             'ember_seam_positions_identical_across_LODs':builders[0].ember_positions==builders[1].ember_positions,
             'notes':'Fill shells stay identical in LOD1; individual slabs retain mass with simpler corners/face relief.'},
        'textures':textures,'frame':{'blender':'metres, +X front, +Y garage side, Z up','fbx':'centimetres; X forward, Z up; UE garage -Y',
            'origin':'HQ slab ground centre (0,0,0)','matching_shell_sha256':sha(HQ/'game/SM_HQ_Shell.fbx')},
        'UV0_check':'Triangle clipping per slot after FBX reimport, shared edges excluded; zero positive-area intersections required.',
        'UV1':'SurfaceDetail: UV1 tiled Debris normal and DetailColor, 0.5 metre repeat; no finish bake. UV0 remains unique per slot.',
        'reproducibility':reproducibility,'unreal_tested':False,'collision':'None supplied; Claude must disable auto collision',
        'generator_sha256':sha(Path(__file__))}
    verification['texture_file_checks']={}
    verification['reference_inputs_sha256']={str(p.relative_to(REPO)).replace('\\','/'):sha(p) for p in [
        HQ/'usa_hq.json', HQ/'game/SM_HQ_Shell.fbx', HQ/'reference/candidate11_Reference1.png',
        HQ/'reference/candidate11_Reference2.png', HQ/'renders/final_neutral_rts.png', HQ/'renders/final_neutral_garage.png',
        HQ/'textures/T_HQ17_Coating_2m_BaseColor.png',HQ/'textures/T_HQ21_Paving_BaseColor.png',
        REPO/'docs/art/usa_hq/USA_HQ_SPEC.md',OUT/'BRIEF.md']}
    for p in sorted((OUT/'textures').glob('*.png')):
        raw=p.read_bytes();width,height=struct.unpack('>II',raw[16:24])
        assert max(width,height)<=2048
        verification['texture_file_checks'][p.name]={'actual_png_size':[width,height],'sha256':sha(p)}
    write_json(OUT/'evidence/verification.json',verification)
    recipe={'version':1,'slots':SLOTS,'shading':'Opaque Default Lit, one-sided; no Nanite, no collision, no vertex colors, no CPD dependencies',
        'UV0':{'tiling':False,'overlap':False,'mapping':'Unique planar face islands with 3px gutters at each slot resolution. LOD1 reuses existing islands.'},
        'UV1':{'name':'SurfaceDetail','purpose':'Tiled Debris normal and DetailColor, 0.5 metre repeat; not a FinishBake or lightmap','tiling':True,'overlap':True},'normal':'DirectX tangent-space; UE normal map compression; sRGB false; do not flip green in Unreal',
        'maps':textures,'texture_import':{'BaseColor':{'sRGB':True,'compression':'Default'},
              'Normal':{'sRGB':False,'compression':'Normalmap','flip_green':False},
              'ORM':{'sRGB':False,'compression':'Masks','channels':{'R':'Ambient Occlusion','G':'Roughness','B':'Metallic'}},
              'Emissive':{'sRGB':False,'compression':'Masks','channel':'R'},
              'DetailColor':{'sRGB':False,'compression':'Default','UV_channel':1,'expression':'BaseColorUV0 * lerp(1, DetailColorUV1.rgb, 0.65)'}},
        'mipmaps':'Keep generated mipmaps; at most 2048 per slot. NeverStream=true, following project building texture residency policy.',
        'materials':{'Debris':{'BaseColor':'BaseColor texture RGB on UV0 * lerp(1, DetailColor RGB on UV1, 0.65)','Normal':'texture RGB sampled with TexCoord index 1, strength 0.85','ORM':'texture RGB'},
                     'Scorch':{'BaseColor':'texture RGB','Normal':'texture RGB, strength 0.4','ORM':'texture RGB'},
                     'Embers':{'BaseColor':'texture RGB','ORM':'texture RGB','emissive_linear_color':[1,.105,.008],
                               'emissive_intensity':7,'emissive_expression':'Texture Emissive.R * float3(1,0.105,0.008) * 7',
                               'constant':True,'operational_or_lamp_multiplier':False}},
        'LOD':{'LOD0':'SM_HQ_Rubble.fbx','LOD1':'SM_HQ_Rubble_LOD1.fbx','switch_distance_cm':26000,'no_parts_removed':True,
               'simplification':'Fracture face relief and small corner chips simplified; small tubes 3-sided instead of 4-sided. All pieces retained. Mound shells and wall profiles exact; compare switch renders.'}}
    write_json(OUT/'material_recipe.json',recipe)
    write_json(OUT/'evidence/components.json',{n:b.components for n,b in zip(expected,builders)})
    if not args.skip_review:
        bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/HQ_Rubble.blend'))
        objects=[bpy.data.objects[n] for n in expected]
        image_checks=[]
        for image in bpy.data.images:
            if not image.users:continue
            path=Path(bpy.path.abspath(image.filepath))
            assert image.packed_file or path.is_file(),str(path)
            image_checks.append({'image':image.name,'relative_path':image.filepath,'packed':bool(image.packed_file),
                                 'size':list(image.size),'exists':path.is_file()})
        verification['source_reopen']={'mesh_objects':len(objects),'textures':image_checks,
                                      'scope':'Saved source reopened from disk before render; no external project dependencies.'}
        objects[1].hide_set(False)
        shots,metric=review(objects)
        verification['review']={'cycles_renders':len(shots),'lod_comparison':metric}
        write_json(OUT/'evidence/verification.json',verification)
    else:shots=[];metric=None
    paths=[OUT/'source/HQ_Rubble.blend',OUT/'SM_HQ_Rubble.fbx',OUT/'SM_HQ_Rubble_LOD1.fbx',OUT/'material_recipe.json']+sorted((OUT/'textures').glob('*.png'))
    hashes={str(p.relative_to(OUT)).replace('\\','/'):sha(p) for p in paths}
    write_json(OUT/'evidence/hashes.json',hashes)
    (OUT/'review/README.md').write_text('''# Issue 403 - Round 4 review\n\nAll images are Blender Cycles renders, not Unreal or gameplay evidence.\n\n- `final_neutral_rts.png`: three-quarter camera matched to final HQ RTS orientation (55 degree depression), framed at 105 m.\n- `final_neutral_reference.png`: lower reference presentation, front three-quarter, 31 degrees.\n- `night_rts.png`, `night_reference.png`: same cameras in low cool illumination, constant ember material.\n- `rts_12m.png`: closest camera aimed at the broken garage portal; the full building cannot fit a 12 m arm.\n- `rts_150m.png`, `rts_450m.png`: 60 degree horizontal lens, 16:9, true arm distances.\n- `rts_150m_night.png`: added true 150 m night review, same constant ember shader.\n- `lod0_260m.png`, `lod1_260m.png`: identical camera and lighting, manual LOD selection at the switch.\n- `lod0_249p6m.png`, `lod1_270p4m.png`: 4 percent inside/outside switch.\n- `top_footprint.png`: orange 38 x 56 m boundary, preview curve excluded from export.\n- `intact_vs_rubble.png`: original shell FBX (left) and rubble (right), same camera and neutral light. The intact shell uses a neutral override to compare massing, and excludes separate masts/moving equipment.\n\nExact camera transforms are in `cameras.json`. The intact FBX is read only and is not stored in the rubble blend.\nThe saved source is a clean two-LOD scene with editable texture graphs and relative textures, without preview terrain.\nRound 4 retains the round-3 fill and mast; planar ear-clipped caps remove fan pinching, broader stepped wall breaks remove spike fans, independent corner cuts vary slabs, and stronger interior soot preserves pale exterior dust. Ember intensity remains 7. The 150 m night view is rts_150m_night.png.\nUV0 remains unique per slot; UV1 SurfaceDetail intentionally tiles the Debris normal and linear DetailColor multiplier at 0.5 m. No FinishBake is claimed. See material_recipe.json for the exact Unreal graph.\nProcedural authored model is the game mesh; no separate dense approved master or finish-normal bake exists.\n''',encoding='utf8')
    counts=', '.join(f'{n}: {s["triangles"]:,} triangles' for n,s in expected.items())
    (OUT/'STATE.md').write_text(f'''# USA HQ rubble - Round 4\n\nREADY FOR PRODUCER REVIEW. Independent Round 4 model check complete; no producer approval claimed.\n\n{counts}. Three slots: Debris, Scorch, Embers.\n\nAuthority: BRIEF.md, intact game/SM_HQ_Shell.fbx, usa_hq.json and final_neutral_rts/reference.\nSource: source/HQ_Rubble.blend; generator: Tools/Art/usa_hq_rubble.py.\nTechnical checks: evidence/verification.json (independent FBX reimport, UV intersection, tangents, winding, bounds), evidence/hashes.json, evidence/round4_independent_check.json (separate fresh-process audit).\nReview: review/README.md. Round 4 fixes pinched slab caps, narrow wall teeth, repeated outlines and weak soot while retaining round-3 collapse, mast and ember intensity; visual acceptance remains Claude/producer review. UV1 SurfaceDetail now drives tiled Debris normal and DetailColor; consult material_recipe.json before importing. No Unreal editor or code work performed.\n\nNext: producer visual decision, then Claude integration if accepted. Unreal import, materials, distance switch, runtime lighting, disappearance and gameplay integration belong to Claude and remain untested.\n\nRebuild (overwrites only this rubble delivery, preserves BRIEF.md and ROUND_NOTES.md):\n`"D:\\Program Files\\Blender Foundation\\Blender 5.1\\blender.exe" -b --python Tools/Art/usa_hq_rubble.py`\n\nNo approval gates have been passed. No external or paid assets used.\n''',encoding='utf8')
    table='\n'.join(f'| `{p}` | `{h}` |' for p,h in hashes.items())
    (OUT/'CLAUDE_HANDOFF.md').write_text(f'''# Issue 403 - Round 4 intake\n\nREADY FOR PRODUCER REVIEW after Round 4 final check; not approved for production. Follow BRIEF.md.\n\nImport `SM_HQ_Rubble.fbx` as one static mesh, import `SM_HQ_Rubble_LOD1.fbx` into its LOD1. {counts}.\nNo skeleton, collision mesh or moving pieces. Disable automatic collision and Nanite; apply no scale or fit stretch. Origin is the intact HQ ground centre, +X front, Z up, scale 1. FBX contains centimetre geometry (UnitScaleFactor 1). Blender reimport uses object scale 0.01 to display those buffers in metres; Unreal import scale is 1. Blender garage is +Y, matching the source shell; Unreal garage is -Y after the same X-forward/Z-up export.\n\nSlots in exact order: **Debris, Scorch, Embers**, all used. Bind each to an opaque one-sided PBR material from `material_recipe.json`.\nBaseColor: sRGB on. DirectX Normal: sRGB off, Normalmap compression, no green flip. Normal strength: Debris 0.85, Scorch/Embers 0.4. ORM: sRGB off, Masks compression; R AO / G roughness / B metallic. Emissive mask: linear R, Masks compression. Keep mips and follow the established project building texture residency policy. Debris 2048, Scorch 1024, Embers 512. UV0 is a unique planar atlas per slot, no overlaps or tiling. UV1 SurfaceDetail holds tiled procedural Debris normal and DetailColor (0.5 m repeat), not a FinishBake or lightmap. Sample Debris Normal with TexCoord index 1, strength 0.85; DetailColor is linear/sRGB off, UV1, BaseColor = UV0 BaseColor * lerp(1, UV1 DetailColor, 0.65). All other maps use UV0. No finish-normal bake.\n\n**Embers:** constant linear `(1, 0.105, 0.008) * 7 * Emissive.R`. Do not multiply by Operational, LampOn, night, team colour or any lighting channel. No real lights or fire supplied. Source graph converts DX green for Blender only; Unreal uses the delivered map directly.\n\nLOD1 retains every named piece and the continuous fill shells; fracture face relief and slab corner profiles are simplified, and small tubes use triangular sections. Continuous mound shells and broken wall profiles remain exact. See the rendered switch pair for the practical silhouette comparison. Whole ruin switches at 26000 cm. See exact-camera pair in review/. Independent FBX evidence is Blender reimport, not Unreal import.\n\nReview limitations: closest view targets garage rather than fitting 56 m into 12 m; intact comparison is shell only with neutral material override; cameras reproduce presentation orientations from supplied images, not recovered editor transforms. No producer approval, engine import, light-state integration, runtime LOD/collision or sink testing claimed. Claude owns all Unreal/code work; the producer owns visual acceptance.\n\n## File receipt\n\n| File | SHA256 |\n|---|---|\n{table}\n\n## Continuation\n\nUse the repo-owned world-divide-building-production skill. Read rubble/STATE.md, BRIEF.md, ROUND_NOTES.md and review/README.md. Preserve the HQ source. Review the Round 4 renders and evidence/round4_independent_check.json; await producer acceptance before integration. Stay inside the rubble brief's write scope.\n''',encoding='utf8')
    print('CLEAN GENERATOR COMPLETE',counts,'LOD comparison',metric,flush=True)

if __name__=='__main__':main()



