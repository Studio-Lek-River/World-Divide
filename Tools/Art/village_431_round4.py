"""Round4 parameterised village geometry. Guarded destructive rebuild of round4 only.
Reuses Round2 construction/QA helpers; never writes approved round1/round2.
"""
import argparse,json,math,random,shutil,sys
from pathlib import Path
sys.dont_write_bytecode=True
import bpy,bmesh
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round2 as r2
r1=r2.r1;g=r1.grey;ROOT=r1.ROOT
OUT=ROOT/'Art/Shared/village/issue431/round4'
SLOTS=r1.SLOTS
TYPES=['HouseA','HouseB','HouseC','Farmhouse','Barn','Workshop','Church']
STATES=['Pristine','Damaged','Rubble']
LOW=False
SPEC=None

def name_for(key,state):return 'SM_Village_'+key+('' if state=='Pristine' else '_'+state)
def ext(face):return SPEC['length']/2 if 'X' in face else SPEC['width']/2
def span(face):return SPEC['width']/2 if 'X' in face else SPEC['length']/2
def fb(b,face,a,z0,z1,half,d0,d1,slot):
    before=set(b.mesh.faces);b.box(*g.face_box(face,a,z0,z1,half,d0,d1,ext(face)),slot)
    if LOW:
        axis,side,other=g.FACES[face];remove=[]
        for f in set(b.mesh.faces)-before:
            front=all(abs(v.co[axis]-side*(ext(face)+d1))<.00001 for v in f.verts)
            back=all(abs(v.co[axis]-side*(ext(face)+d0))<.00001 for v in f.verts)
            if (d0<=-1 or slot=='Glass' or d1-d0<.04 or (slot=='Trim' and half<=.026)) and not front:remove.append(f)
            elif back:remove.append(f)
        bmesh.ops.delete(b.mesh,geom=remove,context='FACES_ONLY')

def prism(b,face,coords,slot='Brick',thickness=.3):
    axis,side,other=g.FACES[face];layers=[]
    for d in [-thickness,0]:
        vs=[]
        for a,z in coords:
            co=[0,0,z];co[axis]=side*(ext(face)+d);co[other]=a;vs.append(b.mesh.verts.new(co))
        layers.append(vs)
    fs=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
    for i in range(len(coords)):
        j=(i+1)%len(coords);fs.append(b.mesh.faces.new([layers[0][i],layers[0][j],layers[1][j],layers[1][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=SLOTS.index(slot)
    if not hasattr(b,'wall_solids'):b.wall_solids=[]
    b.wall_solids.append((axis,side,other,ext(face),thickness,coords))

def prune_internal_walls(b):
    # Whole-face containment inside another convex masonry strip, never a centre
    # sample. Removes hidden strip returns while preserving every external face.
    remove=[]
    for f in b.mesh.faces:
        points=[v.co+f.normal*.0001 for v in f.verts]
        for axis,side,other,extent,thick,poly in getattr(b,'wall_solids',[]):
            if not all(extent-thick-1e-6<=p[axis]*side<=extent+1e-6 for p in points):continue
            if not all(min(q[0] for q in poly)-1e-6<=p[other]<=max(q[0] for q in poly)+1e-6 and min(q[1] for q in poly)-1e-6<=p.z<=max(q[1] for q in poly)+1e-6 for p in points):continue
            def inside(p):
                crosses=[(q[0]-a[0])*(p.z-a[1])-(q[1]-a[1])*(p[other]-a[0]) for a,q in zip(poly,poly[1:]+poly[:1])]
                return all(x>=-1e-6 for x in crosses) or all(x<=1e-6 for x in crosses)
            if all(inside(p) for p in points):remove.append(f);break
    bmesh.ops.delete(b.mesh,geom=remove,context='FACES_ONLY')

def openings(key):
    spec=g.BUILDINGS[key];ops=[];changes=[]
    for i,item in enumerate(g.openings(spec)):
        face,a,z,w,h,kind,floor=item;old=(z,w,h)
        if floor==spec['storeys'] and kind=='window':
            eye=floor*3+1.5
            if key=='Barn':z,h=5.2,1.5
            else:h=max(h,eye+.3-z)
        if key=='Farmhouse' and floor==1 and 'Y' in face:z,h=3.15,1.5
        if key=='HouseC' and face=='+X' and kind=='window' and floor==0 and a<0:w=1.65;h=1.8;z=.5
        if (z,w,h)!=old:changes.append({'index':i,'face':face,'along_m':a,'old_sill_width_height_m':old,'new_sill_width_height_m':[z,w,h]})
        eye=floor*3+1.5
        proposed=eye if z<eye<z+h else z+h*.5
        ops.append((face,a,z,w,h,kind,floor))
        if proposed!=eye:changes.append({'index':i,'firing_eye_old_m':eye,'firing_eye_proposed_m':proposed,'reason':'Keep contracted high opening; raise firing eye inside it in integration'})
    return ops,changes

def ring(b,face,a,z,w,h,width=.075,d0=-.08,d1=0):
    axis,side,other=g.FACES[face];rings=[]
    for depth,inside in [(d0,False),(d1,False),(d0,True),(d1,True)]:
        inset=width if inside else 0;vs=[]
        for aa,zz in [(a-w/2+inset,z+inset),(a+w/2-inset,z+inset),(a+w/2-inset,z+h-inset),(a-w/2+inset,z+h-inset)]:
            co=[0,0,zz];co[axis]=side*(ext(face)+depth);co[other]=aa;vs.append(b.mesh.verts.new(co))
        rings.append(vs)
    fs=[]
    for i in range(4):
        j=(i+1)%4
        for x,y in ([(2,3),(1,3)] if LOW else [(0,1),(2,3),(0,2),(1,3)]):fs.append(b.mesh.faces.new([rings[x][i],rings[x][j],rings[y][j],rings[y][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=2

def clip(poly,level,above):
    out=[]
    for p,q in zip(poly,poly[1:]+poly[:1]):
        pin=p[1]>=level if above else p[1]<=level;qin=q[1]>=level if above else q[1]<=level
        if pin:out.append(p)
        if pin!=qin:
            t=(level-p[1])/(q[1]-p[1]);out.append((p[0]+t*(q[0]-p[0]),level))
    clean=[]
    for p in out:
        if not clean or math.dist(p,clean[-1])>.015:clean.append(p)
    if len(clean)>2 and math.dist(clean[0],clean[-1])<.015:clean.pop()
    return clean

def shell(b,ops,state):
    hl,hw=SPEC['length']/2,SPEC['width']/2;e,r=SPEC['eaves'],SPEC['ridge']
    bx0=-hl*.45;bx1=bx0+min(3,SPEC['length']*.25);bz0=.35;bz1=min(3.8,e-.65)
    for face in g.FACES:
        holes=[(a-w/2,a+w/2,z,z+h) for f,a,z,w,h,k,fl in ops if f==face]
        if state=='Damaged' and face=='-Y':
            # Course-stepped opening with independent edge offsets, not a rectangle.
            for j in range(7):
                z0=bz0+(bz1-bz0)*j/7;z1=bz0+(bz1-bz0)*(j+1)/7
                holes.append((bx0+[0,-.15,.10,-.22,.06,-.1,.16][j],bx1+[.12,-.08,.2,-.12,.05,.18,-.13][j],z0,z1))
        cuts=sorted(set([-span(face),0,span(face)]+[x for hole in holes for x in hole[:2]]))
        top=lambda a:r-abs(a)*(r-e)/hw if 'X' in face else e
        for a,c in zip(cuts,cuts[1:]):
            intervals=[(-.5,r)]
            for x0,x1,z0,z1 in holes:
                if x0<(a+c)/2<x1:intervals=[q for u,v in intervals for q in [(u,min(v,z0)),(max(u,z1),v)] if q[1]-q[0]>.02]
            for lo,hi in intervals:
                poly=clip(clip([(a,-.5),(c,-.5),(c,top(c)),(a,top(a))],lo,True),hi,False)
                if len(poly)>=3:prism(b,face,poly)
        # Dark damp course; retain door and breach cutouts.
        gaps=[(x0,x1) for x0,x1,z0,z1 in holes if z0<=.35]
        cuts=sorted(set([-span(face),span(face)]+[x for q in gaps for x in q]))
        for a,c in zip(cuts,cuts[1:]):
            if not any(x0<(a+c)/2<x1 for x0,x1 in gaps):fb(b,face,(a+c)/2,-.5,.42,(c-a)/2,0,.04,'Rubble')

def windows(b,ops,state,key):
    hl=SPEC['length']/2;bx0=-hl*.45;bx1=bx0+min(3,SPEC['length']*.25)
    for i,(face,a,z,w,h,kind,floor) in enumerate(ops):
        if state=='Damaged' and face=='-Y' and bx0-w/2<a<bx1+w/2 and z<3.8:continue
        broken=state=='Damaged' and i%3!=0;large=kind=='door' and w>2.5
        if not broken:
            ring(b,face,a,z,w,h,.11 if large else .075)
            if not LOW:ring(b,face,a,z+.075,w-.15,h-.15,.045,-.12,-.04)
            slot=('Wood' if key=='Barn' and floor==1 else 'Glass') if kind=='window' else ('Wood' if key=='Barn' else ('Trim' if large else 'Wood'))
            fb(b,face,a,z+.1,z+h-.1,w/2-.10,-.17,-.15,slot)
            if kind=='window':fb(b,face,a,z+.08,z+h-.08,.025,-.12,-.04,'Trim')
            if key=='Barn' and floor==1:
                fb(b,face,a,z+.4,z+.52,w/2-.1,-.15,-.11,'Trim')
                fb(b,face,a,z+h-.5,z+h-.38,w/2-.1,-.15,-.11,'Trim')
            if large:
                # Roller ribs / barn door ledgers, physical outline retained at LOD1.
                for zz in ([1.3,2.65] if key=='Barn' else [.65,1.3,1.95,2.6,3.25]):fb(b,face,a,z+zz-.035,z+zz+.035,w/2-.11,-.15,-.12,'Wood' if key=='Barn' else 'Trim')
        else:
            fb(b,face,a-w/2+.04,z,z+h*.65,.04,-.08,0,'Trim');fb(b,face,a+.1,z+h-.07,z+h,.32,-.08,0,'Trim')
        # Single closed dark backing is sufficient at LOD1; visible returns stay.
        fb(b,face,a,z,z+h,w/2,-1.05,-1.0,'Wood')
        if not LOW:
            for aa in [a-w/2+.025,a+w/2-.025]:fb(b,face,aa,z,z+h,.025,-1,-.3,'Wood')
            for zz in [z,z+h-.05]:fb(b,face,a,zz,zz+.05,w/2,-1,-.3,'Wood')
        if kind=='window':fb(b,face,a,z-.055,z-.015,w/2+.06,-.03,.11,'Rubble')
        fb(b,face,a,z+h+.025,z+h+.115,w/2+.10,-.015,.045,'Rubble')
        if kind=='door' and not large:
            fb(b,face,a,.02,.15,w/2+.17,0,.7,'Rubble')
            for zz in [.35,1.05,1.65]:fb(b,face,a,zz,zz+.32,.32,-.145,-.12,'Trim')
            fb(b,face,a,2.36,2.48,w/2+.25,0,.8,'Wood')
            fb(b,face,a+.8,1.7,1.96,.1,0,.12,'Trim')

def roofpoly(b,points,side):
    hw=SPEC['width']/2;rise=SPEC['ridge']-SPEC['eaves'];ang=math.atan2(rise,hw);layers=[]
    for dz in [-.09,.09]:layers.append([b.mesh.verts.new((x,side*t*math.cos(ang),SPEC['ridge']+.04-t*math.sin(ang)+dz)) for x,t in points])
    fs=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
    for i in range(len(points)):
        j=(i+1)%len(points);fs.append(b.mesh.faces.new([layers[0][i],layers[0][j],layers[1][j],layers[1][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=1

def roof(b,state,key):
    hl,hw=SPEC['length']/2,SPEC['width']/2;e,r=SPEC['eaves'],SPEC['ridge'];rise=r-e;ang=math.atan2(rise,hw);run=math.hypot(hw,rise)+(.18 if key=='Farmhouse' else .38)
    left,right=-hl-.35,hl+.35;h0=hl*.05;h1=h0+min(4,SPEC['length']*.3);end=run*.73
    if state=='Damaged' and key in ['Farmhouse','Church']:h0-=1.1;h1+=.8;end=run*.88
    for side in [-1,1]:
        if state=='Damaged' and side==-1:
            roofpoly(b,[(left,0),(h0,0),(h0-.45,.9),(h0+.45,2.0),(h0-.6,end-1.2),(h0+.55,end),(left,run)],side)
            roofpoly(b,[(h1,0),(right,0),(right,run),(h1-.55,end),(h1+.55,end-1.5),(h1-.35,1.4)],side)
            roofpoly(b,[(left,run),(h0+.55,end),(h0+1.2,end+.42),(h0+2.0,end-.35),(h1-1.2,end+.3),(h1-.55,end),(right,run)],side)
            for j in range(4):
                x=h0+.4+j*(h1-h0-.8)/3;t=end*.5
                b.turned((x,-t*math.cos(ang),r-t*math.sin(ang)),(.12,end*.85-(j%2)*.5,.18),(math.degrees(ang),0,j*1.4),'Wood')
                b.turned((x,-end*.9*math.cos(ang),r-end*.9*math.sin(ang)),(.3,.45,.08),(math.degrees(ang)+9,j*3,j*7),'Roof')
            for t in [.9,end*.65]:
                for x in [h0,h1]:b.turned((x,-t*math.cos(ang),r-t*math.sin(ang)),(.48,.07,.07),(math.degrees(ang),0,12),'Wood')
            b.box((h0,-hw+.4,e-.2),(h1,-.2,e-.04),'Wood')
        else:roofpoly(b,[(left,0),(right,0),(right,run),(left,run)],side)
        y=side*(hw+(.13 if key=='Farmhouse' else .30));ze=e-.19
        # Zinc gutters, fascia and downpipes share Trim, never the wood scan.
        b.box((left,y-.10,ze),(right,y+.10,ze+.06),'Trim')
        b.box((left,y+.07,ze+.04),(right,y+.10,ze+.18),'Trim')
        fascia=side*(hw+(.08 if key=='Farmhouse' else .20))
        b.box((left,fascia-.05,ze-.08),(right,fascia+.05,ze+.14),'Trim')
        for x in [-hl+.3,hl-.3]:
            b.box((x-.06,side*(hw+.07)-.06,.1),(x+.06,side*(hw+.07)+.06,e-.1),'Trim')
            b.box((x-.06,min(y,side*(hw+.07))-.06,e-.3),(x+.06,max(y,side*(hw+.07))+.06,e-.18),'Trim')
        for x in [left+.08,right-.08]:b.turned((x,side*run/2*math.cos(ang),r-run/2*math.sin(ang)),(.15,run,.19),(-side*math.degrees(ang),0,0),'Trim')
    ranges=[(left,h0),(h1,right)] if state=='Damaged' else [(left,right)]
    for a,c in ranges:b.box((a,-.13,r+.095),(c,.13,r+.225),'Roof')
    if key in ['HouseB','HouseC','Farmhouse']:
        x=-hl*.65;y=-hw*.23;bottom=r-abs(y)*rise/hw-.1
        b.box((x-.33,y-.3,bottom),(x+.33,y+.3,r+.65),'Brick');b.box((x-.4,y-.37,r+.65),(x+.4,y+.37,r+.76),'Rubble')

def church_tower(b,state):
    global SPEC
    nave=SPEC;side,height,spire=nave['tower'];cx=-nave['length']/2-side/2;top=height if state=='Pristine' else (height-6 if state=='Damaged' else height/3)
    # Hollow full-thickness tower with recessed belfry openings, masonry buttresses.
    tower_spec={'length':side,'width':side,'eaves':top,'ridge':top}
    oldverts=set(b.mesh.verts);SPEC=tower_spec
    towerops=[]
    if state=='Pristine':
        for face in g.FACES:towerops.append((face,0,20,1.8,3.0,'window',6))
    shell(b,towerops,'Pristine')
    if state=='Pristine':
        for face,a,z,w,h,k,f in towerops:
            ring(b,face,a,z,w,h,.13)
            fb(b,face,a,z,z+h,w/2,-.6,-.55,'Wood')
            for zz in [20.25,20.75,21.25,21.75,22.25,22.75]:fb(b,face,a,zz,zz+.12,.78,-.1,.08,'Trim')
        b.pyramid((0,0),side/2+.18,top,spire,'Roof')
        for zz in [8.5,18.5,25.6]:
            for face in g.FACES:fb(b,face,0,zz,zz+.22,side/2+.15,0,.16,'Rubble')
    else:
        rng=random.Random(431+int(top))
        for face in g.FACES:
            for j in range(5):fb(b,face,-side/2+(j+.5)*side/5,top-.05,top+rng.uniform(.12,.65),side/10,-.3,0,'Brick')
    for x in [-side/2,side/2]:
        for y in [-side/2,side/2]:b.box((x-.18,y-.18,-.5),(x+.18,y+.18,min(top,18.5)),'Brick')
    for v in set(b.mesh.verts)-oldverts:v.co.x+=cx
    SPEC=nave;b.collision.append(((cx-side/2,-side/2,-.5),(cx+side/2,side/2,top)))

def rubble(b,key,rng):
    hl,hw=SPEC['length']/2,SPEC['width']/2
    for face in g.FACES:
        n=max(5,int(span(face)*2/1.8))
        for j in range(n):
            if j%4==2:continue
            a=-span(face)+2*span(face)*j/n;c=-span(face)+2*span(face)*(j+1)/n;h=rng.uniform(.65,2.85)
            prism(b,face,[(a,-.5),(c,-.5),(c,h-.24),((a+c)/2,h+.08),(a,h)])
    for j in range(28):
        x=rng.uniform(-hl+.6,hl-.6);y=rng.uniform(-hw+.5,hw-.5);z=max(.4,1.3-math.hypot(x/hl,y/hw)*.6)
        r2.fragment(b,(x,y,z),(rng.uniform(.7,2.4),rng.uniform(.6,1.6),rng.uniform(.25,.7)),(rng.uniform(-25,25),rng.uniform(-25,25),rng.uniform(0,180)),['Brick','Brick','Roof','Wood','Rubble'][j%5])
    for x,y,a in [(-hl*.3,-hw*.2,22),(hl*.25,hw*.3,-24),(0,-hw*.4,13)]:
        b.turned((x,y,1.2),(SPEC['length']*.32,SPEC['width']*.3,.22),(a,9,x*8),'Roof')
        b.turned((x,y,1.0),(SPEC['length']*.3,.18,.24),(a,9,x*8),'Wood')
    b.turned((hl*.25,-hw*.3,1.0),(2.5,.3,1.8),(62,9,22),'Brick')
    r2.debris(b,rng,32,(hl*.92,hw*.92))
    b.collision=[((-hl,-hw,-.5),(hl,hw,1.3))]
    if key=='Church':church_tower(b,'Rubble')

def build(key,state,low=False):
    global SPEC,LOW
    SPEC=g.BUILDINGS[key];LOW=low;r2.LOW=low
    b=r2.Builder(name_for(key,state));rng=random.Random(431+TYPES.index(key));ops,changes=openings(key)
    if state=='Rubble':rubble(b,key,rng)
    else:
        shell(b,ops,state);windows(b,ops,state,key);roof(b,state,key)
        # Set-specific construction detail, retained in both LODs.
        if key=='HouseB':
            for face in ['+Y','-Y']:fb(b,face,0,-.5,SPEC['eaves'],.13,0,.08,'Rubble')
        if key=='Barn':
            for face in g.FACES:
                for a in [-span(face)+.1,span(face)-.1]:fb(b,face,a,.05,SPEC['eaves'],.09,0,.12,'Wood')
        if key=='Church':
            for face in ['+Y','-Y']:
                for a in [-9.5,-5.5,-1.5,2.5,6.5,9.5]:fb(b,face,a,-.5,6.8,.22,0,.45,'Brick')
            church_tower(b,state)
        g.body_collision(b,SPEC)
        if state=='Damaged':
            hl=SPEC['length']/2;center=-hl*.45+min(3,SPEC['length']*.25)/2
            r2.debris(b,rng,32,(2.2,.8),(center,-SPEC['width']/2-.5))
    if low:prune_internal_walls(b)
    obj,hulls=b.finish()
    if low:r2.simplify(obj)
    return obj,hulls

def qa(obj):
    uv=obj.data.uv_layers.get('UVMap') or obj.data.uv_layers.new(name='UVMap');obj.data.uv_layers.active=uv
    # Dominant-plane projection avoids degenerate scan UVs on every fragment.
    for p in obj.data.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i]));axes=[i for i in range(3) if i!=axis]
        for li in p.loop_indices:
            co=obj.data.vertices[obj.data.loops[li].vertex_index].co;period=1 if p.material_index==0 else 2
            uv.data[li].uv=(co[axes[0]]/period,co[axes[1]]/period)
            if p.material_index==1 and axis==2:
                slope=(SPEC['ridge']-SPEC['eaves'])/(SPEC['width']/2) if SPEC else 1
                uv.data[li].uv=(co.x/2,co.y*math.sqrt(1+slope*slope)/2)
    if 'Barn' in obj.name:
        for p in obj.data.polygons:
            axis=max(range(3),key=lambda i:abs(p.normal[i]))
            if p.material_index in [0,4] and axis!=2:
                horizontal=1 if axis==0 else 0
                for li in p.loop_indices:
                    co=obj.data.vertices[obj.data.loops[li].vertex_index].co;uv.data[li].uv=(co.z/2,co[horizontal]/2)
    rec=r1.validate(obj);obj.data.calc_tangents(uvmap='UVMap')
    rec['bad_tangents']=sum(l.tangent.length<.99 or not all(math.isfinite(c) for c in l.tangent) for l in obj.data.loops)
    if rec['bad_tangents']:
        print('TANGENT DIAGNOSTIC',obj.name,[(p.material_index,[obj.data.vertices[obj.data.loops[i].vertex_index].co[:] for i in p.loop_indices]) for p in obj.data.polygons if any(obj.data.loops[i].tangent.length<.99 for i in p.loop_indices)][:8],flush=True)
    assert rec['bad_tangents']==0
    rec['minimum_altitude_m']=min((lambda v:(v[1]-v[0]).cross(v[2]-v[0]).length/max((v[1]-v[0]).length,(v[2]-v[0]).length,(v[2]-v[1]).length))([obj.data.vertices[i].co for i in t.vertices]) for t in obj.data.loop_triangles)
    rec['bounds_m']=[[min(v.co[i] for v in obj.data.vertices) for i in range(3)],[max(v.co[i] for v in obj.data.vertices) for i in range(3)]]
    return rec

def carry_housea():
    global SPEC
    SPEC=g.BUILDINGS['HouseA'];bpy.ops.wm.open_mainfile(filepath=str(OUT.parent/'round2/source/HouseA_round2_baked.blend'))
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for o in list(bpy.data.objects):
        if o.type!='MESH' or o.name.startswith('UCX_') or o.name.endswith('_MASTER'):
            if o.name.endswith('_MASTER'):bpy.data.objects.remove(o,do_unlink=True)
            continue
        bm=bmesh.new();bm.from_mesh(o.data);seen=set()
        # Find disconnected construction solids before changing their whole profile.
        for v in bm.verts:
            if v in seen:continue
            stack=[v];group=set()
            while stack:
                q=stack.pop()
                if q in group:continue
                group.add(q);stack.extend(e.other_vert(q) for e in q.link_edges)
            seen|=group;faces={f for q in group for f in q.link_faces}
            if not faces:continue
            mat=next(iter(faces)).material_index
            lo=Vector(tuple(min(q.co[i] for q in group) for i in range(3)));hi=Vector(tuple(max(q.co[i] for q in group) for i in range(3)));size=hi-lo
            # Brown timber fascia was visually confused with gutters: zinc wrap.
            if mat==4 and size.x>1 and abs((lo.y+hi.y)/2)>4.65 and 5.3<lo.z<5.65:
                for f in faces:f.material_index=2
            # Slim all narrow sill/lintel slabs, preserving top/bottom attachment.
            if mat==5 and .035<size.z<.19 and lo.z<8 and min(size.x,size.y)<.4 and max(size.x,size.y)>.8:
                factor=.62
                for q in group:q.co.z=hi.z-(hi.z-q.co.z)*factor
        bm.to_mesh(o.data);bm.free();o.data.update()
        if o.data.uv_layers.get('BakeUV'):o.data.uv_layers.remove(o.data.uv_layers['BakeUV'])
    return [(o,[h for h in bpy.data.objects if h.name.startswith('UCX_'+o.name+'_') and h.name[len('UCX_'+o.name+'_'):].isdigit()]) for o in bpy.data.objects if o.type=='MESH' and not o.name.startswith('UCX_')]

def bridge_mesh(state,low):
    global SPEC
    SPEC={'length':30,'width':14,'eaves':1,'ridge':1}
    # Reuse the contract's span components and transforms, with six portable slots.
    g.SPAN_DROP=36.0
    old=g.Builder
    class BridgeBuilder(r1.Builder):
        def _tag(self,verts,slot):return super()._tag(verts,{'Concrete':'Brick','Asphalt':'Roof','Char':'Rubble','Rubble':'Rubble'}.get(slot,slot))
        def box(self,lo,hi,slot='Concrete',collides=False):
            if state=='Damaged' and slot=='Char':return
            if state=='Damaged' and slot=='Concrete' and abs(lo[2]+.38)<1e-5 and abs(hi[2]+.08)<1e-5 and hi[0]-lo[0]>29:
                # A real shallow crater, not a dark rectangle above an intact slab.
                for a,b,d,e in [(-15,-3,-7,7),(2,15,-7,7),(-3,2,-7,-2),(-3,2,3,7)]:super().box((a,d,-.38),(b,e,-.08),'Brick')
                super().box((-3,-2,-.38),(2,3,-.32),'Rubble');return
            return super().box(lo,hi,slot,collides)
    g.Builder=BridgeBuilder
    oldpar=g.bridge.parapet
    def parapet(b,x0,x1,side,bottom=0,collides=True):
        inner,outer=side*6.5,side*7
        b.box((x0,min(inner,outer),bottom),(x1,max(inner,outer),.88),'Brick')
        b.box((x0,min(inner,outer)-.05,.88),(x1,max(inner,outer)+.05,1),'Rubble')
    g.bridge.parapet=parapet
    try:obj,hulls=(g.span_damaged() if state=='Damaged' else g.span_destroyed())
    finally:g.Builder=old;g.bridge.parapet=oldpar
    # Additional break reinforcement, deck chunks and shallow crater rim.
    b=r1.Builder('bridge_detail');rng=random.Random(431)
    if state=='Damaged':
        outer=[(-3,-.8),(-2.1,-2),(.8,-2),(2,-1),(2,1.8),(.9,3),(-1.9,3),(-3,1.5)]
        inner=[(-.5+(x+.5)*.62,.5+(y-.5)*.62) for x,y in outer]
        for i in range(8):
            j=(i+1)%8
            vs=[b.mesh.verts.new((x,y,z)) for (x,y),z in [(outer[i],-.015),(outer[j],-.015),(inner[j],-.31),(inner[i],-.31)]]
            face=b.mesh.faces.new(vs);face.material_index=5
        # Fill the four rectangular-cut corners with asphalt so the outline is octagonal.
        for points in [[(-3,-2),(-2.1,-2),(-3,-.8)],[(2,-2),(2,-1),(.8,-2)],[(2,3),(.9,3),(2,1.8)],[(-3,3),(-3,1.5),(-1.9,3)]]:
            layers=[[b.mesh.verts.new((x,y,z)) for x,y in points] for z in [-.08,0]]
            fs=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
            for i in range(3):j=(i+1)%3;fs.append(b.mesh.faces.new([layers[0][i],layers[0][j],layers[1][j],layers[1][i]]))
            bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
            for f in fs:f.material_index=1
        for j in range(12):
            a=j*math.tau/12;x=-.5+2.65*math.cos(a);y=.5+2.65*math.sin(a)
            b.turned((x,y,.1),(.65,.4,.22),(rng.uniform(-15,15),rng.uniform(-15,15),j*30),'Rubble')
    else:
        for side in [-1,1]:
            for j,y in enumerate([-5.5,-3.2,-.6,2.3,5.1]):b.turned((side*(6.15+(j%2)*.18),y,-6.45),(.65,1.25,.28),(0,-side*36,j*4-8),'Rubble')
            for y in [-5,-3,-1,1,3,5]:
                # Thick enough for stable tangent frames; visibly bent 5 cm rods.
                b.turned((side*5.9,y,-6.05),(.95,.055,.055),(0,side*55,side*8),'Trim')
                b.turned((side*5.4,y,-5.65),(.55,.055,.055),(0,side*5,side*8),'Trim')
        for j in range(6):b.turned((rng.uniform(-3,3),rng.uniform(-5,5),-6.2),(1.4,.7,.6),(12,j*9,j*31),'Rubble')
    extra,_=b.finish();bm=bmesh.new();bm.from_mesh(obj.data);bm.from_mesh(extra.data);bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free();bpy.data.objects.remove(extra,do_unlink=True)
    if low:r2.simplify(obj)
    return obj,hulls

def main():
    global LOW,SPEC
    ap=argparse.ArgumentParser();ap.add_argument('--types',nargs='+',default=TYPES+['Bridge']);args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    for d in ['source','fbx','textures','evidence']:(OUT/d).mkdir(parents=True,exist_ok=True)
    report_path=OUT/'evidence/geometry_qa.json';report=json.loads(report_path.read_text()) if report_path.exists() else {}
    for key in args.types:
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
        r1.SURFACES['Roof']='RoofingTiles013A';r1.materials(OUT)
        objects=[]
        if key=='HouseA':objects=carry_housea()
        else:
            for state in (['Damaged','Destroyed'] if key=='Bridge' else STATES):
                for low in [False,True]:
                    obj,hulls=bridge_mesh(state,low) if key=='Bridge' else build(key,state,low)
                    if low:
                        # Remove same-name temporary UCX before renaming the reduced mesh.
                        for h in hulls:bpy.data.objects.remove(h,do_unlink=True)
                        hulls=[];obj.name=name_for(key,state)+'_LOD1' if key!='Bridge' else 'SM_Bridge_Span_'+state+'_LOD1'
                    objects.append((obj,hulls))
        for obj,hulls in objects:
            okey=obj.name;rec=qa(obj);rec['collision_boxes']=len(hulls)
            budget=(6000 if key=='Church' else 3000) if okey.endswith('_LOD1') else (25000 if key=='Church' else 12000)
            assert rec['triangles']<=budget,(okey,rec['triangles'],budget)
            assert rec['minimum_altitude_m']>.009,(okey,rec['minimum_altitude_m'])
            for h in hulls:h.hide_set(False)
            r1.export([obj]+hulls,OUT/'fbx'/(okey+'.fbx'));report[okey]=rec
            col=bpy.data.collections.new(okey);bpy.context.scene.collection.children.link(col)
            for o in [obj]+hulls:
                for old in list(o.users_collection):old.objects.unlink(o)
                col.objects.link(o)
            print('GEOMETRY',okey,rec['triangles'],flush=True)
        for img in bpy.data.images:
            if img.source=='FILE':
                dst=OUT/'textures'/Path(img.filepath).name
                if not dst.exists() and Path(bpy.path.abspath(img.filepath)).exists():shutil.copy2(bpy.path.abspath(img.filepath),dst)
                img.filepath='//../textures/'+dst.name
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source'/(key+'_geometry.blend')))
        report_path.write_text(json.dumps(report,indent=2))
        if key!='Bridge':(OUT/'evidence'/(key+'_openings.json')).write_text(json.dumps({'openings':r2.opening_list() if key=='HouseA' else openings(key)[0],'changes':[] if key=='HouseA' else openings(key)[1],'contract_garrison':g.garrison(g.BUILDINGS[key])},indent=2))
    # Additional set textures are copied byte-for-byte; no pack dependencies.
    for surface in ['Bricks097','Plaster001','Plaster006','Planks037A','Concrete031']:
        for kind in ['BaseColor','Normal','ORM']:shutil.copy2(ROOT/'Saved/Surfaces'/surface/(kind+'.png'),OUT/'textures'/(surface+'_'+kind+'.png'))

if __name__=='__main__':
    import village_431_round4_details as details
    details.install(sys.modules[__name__])
    main()
