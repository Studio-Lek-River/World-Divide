"""HouseA Round2 recipe. Background Blender; writes round2 only, never round1.
Reuses approved Round1 envelope constants, replaces quality details. Safe to import.
"""
import argparse, json, math, random, shutil, sys
from pathlib import Path
sys.dont_write_bytecode=True
import bpy, bmesh
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_housea as r1
g=r1.grey
ROOT=r1.ROOT
OUT=ROOT/'Art/Shared/village/issue431/round2'
LOW=False
_facebox=r1.facebox
def facebox(b,face,a,z0,z1,half,d0,d1,slot):
    before=set(b.mesh.faces);_facebox(b,face,a,z0,z1,half,d0,d1,slot)
    if LOW and not (slot=='Brick' and d0==-.3):
        axis,side,other=g.FACES[face]
        remove=[]
        for f in set(b.mesh.faces)-before:
            # Dark backing and glazing are single facing planes; trim is open only
            # against the enclosing masonry, never along its visible outline.
            front=all(abs(v.co[axis]-side*((5.5 if axis==0 else 4.5)+d1))<.00001 for v in f.verts)
            back=all(abs(v.co[axis]-side*((5.5 if axis==0 else 4.5)+d0))<.00001 for v in f.verts)
            if (d0<=-1 or slot=='Glass' or d1-d0<.04) and not front:remove.append(f)
            elif back:remove.append(f)
        bmesh.ops.delete(b.mesh,geom=remove,context='FACES_ONLY')
r1.facebox=facebox

def opening_list():
    return [(f,a,z,w,1.8 if z==6 else h,k,fl) for f,a,z,w,h,k,fl in g.openings(g.BUILDINGS['HouseA'])]

class Builder(r1.Builder):
    def __init__(self,name):
        super().__init__(name);self.axis_boxes=[]
    def box(self,lo,hi,slot='Concrete',collides=False):
        self.axis_boxes.append((Vector(lo),Vector(hi)))
        return super().box(lo,hi,slot,collides)
    def finish(self):
        if LOW:
            self.mesh.normal_update();remove=[]
            for f in self.mesh.faces:
                pts=[v.co+f.normal*.0001 for v in f.verts]
                # Whole face containment in one convex solid, not a centre sample.
                if any(all(all(lo[i]-1e-6<=p[i]<=hi[i]+1e-6 for i in range(3)) for p in pts) for lo,hi in self.axis_boxes):remove.append(f)
            bmesh.ops.delete(self.mesh,geom=remove,context='FACES_ONLY')
        obj,hulls=super().finish()
        # Scanned Bricks085 has ~15 courses across one repeat: 0.975 m vertically.
        uv=obj.data.uv_layers.active.data
        for p in obj.data.polygons:
            for li in p.loop_indices:
                if p.material_index==0:uv[li].uv*=2
        return obj,hulls

def ring(b,face,a,z,w,h,width,d0,d1,slot='Trim'):
    axis,side,other=g.FACES[face];extent=5.5 if axis==0 else 4.5
    rings=[]
    for depth,inside in [(d0,False),(d1,False),(d0,True),(d1,True)]:
        inset=width if inside else 0
        points=[(a-w/2+inset,z+inset),(a+w/2-inset,z+inset),(a+w/2-inset,z+h-inset),(a-w/2+inset,z+h-inset)]
        vs=[]
        for aa,zz in points:
            co=[0,0,zz];co[axis]=side*(extent+depth);co[other]=aa;vs.append(b.mesh.verts.new(co))
        rings.append(vs)
    fs=[]
    for i in range(4):
        j=(i+1)%4
        for x,y in ([(2,3),(1,3)] if LOW else [(0,1),(2,3),(0,2),(1,3)]):fs.append(b.mesh.faces.new([rings[x][i],rings[x][j],rings[y][j],rings[y][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=r1.SLOTS.index(slot)

def shell(b,found,state):
    if state!='Damaged':r1.shell(b,found,state);return
    # Keep other approved faces; the damaged long wall has a course-stepped blast.
    faces=g.FACES.copy()
    try:
        del g.FACES['+Y'];r1.shell(b,found,'Pristine')
    finally:g.FACES.clear();g.FACES.update(faces)
    # Central windows expand into the breach, unchanged outside centre bay.
    holes=[(a-w/2,a+w/2,z,z+h) for f,a,z,w,h,k,fl in found if f=='+Y' and abs(a)>2]
    bands=[(-.5,.35,-1.35,1.36),(.35,1.0,-1.5,1.52),(1,1.9,-1.7,1.56),(1.9,2.9,-1.48,1.72),(2.9,3.8,-1.65,1.46),(3.8,4.65,-1.4,1.63),(4.65,5.18,-1.15,1.4),(5.18,5.8,-.8,.95)]
    for l,h,left,right in bands:
        for lo,hi in [(-5.5,left),(right,5.5)]:
            cuts=sorted(set([lo,hi]+[x for hole in holes for x in hole[:2] if lo<x<hi]))
            for a,c in zip(cuts,cuts[1:]):
                intervals=[(l,h)]
                for x0,x1,z0,z1 in holes:
                    if x0<(a+c)/2<x1:intervals=[q for u,v in intervals for q in [(u,min(v,z0)),(max(u,z1),v)] if q[1]-q[0]>.015]
                for u,v in intervals:r1.facebox(b,'+Y',(a+c)/2,u,v,(c-a)/2,-.3,0,'Brick')

def windows(b,found,state):
    for i,(face,a,z,w,h,kind,_) in enumerate(found):
        if state=='Damaged' and face=='+Y' and abs(a)<2:continue
        broken=state=='Damaged' and i%3!=0
        if not broken:
            ring(b,face,a,z,w,h,.075,-.08,0)
            if not LOW:ring(b,face,a,z+.075,w-.15,h-.15,.045,-.12,-.04)
            if kind=='window':
                r1.facebox(b,face,a,z+.08,z+h-.08,.025,-.12,-.04,'Trim')
            r1.facebox(b,face,a,z+.1,z+h-.1,w/2-.10,-.17,-.15,'Glass' if kind=='window' else 'Wood')
        else:
            # Missing sash and glass; one snapped jamb and lintel remain.
            r1.facebox(b,face,a-w/2+.04,z,z+h*.65,.04,-.08,0,'Trim')
            r1.facebox(b,face,a+.15,z+h-.07,z+h,.35,-.08,0,'Trim')
        # Dark room, with sealed reveals/backing inherited in the solid shell.
        r1.facebox(b,face,a,z,z+h,w/2,-1.05,-1.0,'Wood')
        if not LOW:
            for aa in [a-w/2+.025,a+w/2-.025]:r1.facebox(b,face,aa,z,z+h,.025,-1.0,-.3,'Wood')
            for zz in [z,z+h-.05]:r1.facebox(b,face,a,zz,zz+.05,w/2,-1.0,-.3,'Wood')
        if kind=='window':
            r1.facebox(b,face,a,z-.085,z-.02,w/2+.09,-.05,.17,'Rubble')
            if not LOW:r1.facebox(b,face,a,z-.1,z-.08,w/2+.04,.11,.15,'Wood')
        # Concrete lintels, full depth and dark enough to read at the primary view.
        r1.facebox(b,face,a,z+h+.025,z+h+.185,w/2+.18,-.02,.075,'Rubble')
        if kind=='door':
            r1.facebox(b,face,a,.02,.17,.78,0,.8,'Rubble')
            r1.facebox(b,face,a+.36,.9,1.05,.035,-.14,-.08,'Trim')
            for zz in [.35,1.02,1.65]:r1.facebox(b,face,a,zz,zz+.42,.32,-.145,-.12,'Trim')
            r1.facebox(b,face,a,2.38,2.50,.9,0,.85,'Wood')
            r1.facebox(b,face,a+.88,1.45,1.67,.13,0,.065,'Trim')
            r1.facebox(b,face,a-.92,1.7,2.0,.11,0,.16,'Wood')
            r1.facebox(b,face,a-.92,1.77,1.93,.085,.15,.18,'Glass')

def polyroof(b,points,side,slot='Roof',thick=.19):
    # Coordinates are ridge-along X and slope distance. Closed roof plate.
    angle=math.atan2(3.8,4.5);layers=[]
    for dz in [-thick/2,thick/2]:
        layers.append([b.mesh.verts.new((x,side*t*math.cos(angle),9.64-t*math.sin(angle)+dz)) for x,t in points])
    fs=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
    for i in range(len(points)):
        j=(i+1)%len(points);fs.append(b.mesh.faces.new([layers[0][i],layers[0][j],layers[1][j],layers[1][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=r1.SLOTS.index(slot)

def roof(b,state):
    if state!='Damaged':r1.roof(b,state)
    else:
        # Preserve ridge gap and lower hole placement, make every edge a broken course.
        run=math.hypot(4.5,3.8)+.38
        polyroof(b,[(-5.85,0),(5.85,0),(5.85,run),(-5.85,run)],-1)
        polyroof(b,[(-5.85,0),(-1.05,0),(-1.25,.6),(-.98,1.4),(-1.32,2.3),(-1.12,3.2),(-.82,4.1),(-5.85,run)],1)
        polyroof(b,[(3.3,0),(5.85,0),(5.85,run),(3.0,4.4),(3.5,3.6),(3.15,2.5),(3.42,1.5),(3.12,.6)],1)
        polyroof(b,[(-5.85,run),(-.82,4.1),(-.2,4.35),(.4,3.96),(1.0,4.5),(1.7,4.15),(2.4,4.6),(3.0,4.4),(5.85,run)],1)
        for j,x in enumerate([-.6,.35,1.25,2.15,2.95]):
            length=[3.3,4.25,2.8,3.7,2.5][j]
            b.turned((x,1.8,8.1),(.11+j*.012,length,.18),(-40.2,0,j*1.8-3),'Wood')
            if not LOW:b.turned((x+.03,2.8,7.35),(.05,.48,.10),(-51,0,j*7),'Wood')
        for j,t in enumerate([.8,1.7,2.6,3.6]):
            for x in [-1.18,3.25]:b.turned((x, t*.764,9.64-t*.645),(.48,.055,.055),(-40.2,0,(-1 if j%2 else 1)*14),'Wood')
        for j,x in enumerate([-.9,-.1,.8,1.8,2.8]):
            b.turned((x,3.25+(j%2)*.2,6.95),(.25,.44,.08),(-47,j*3,13-j*7),'Roof')
        b.box((-1.1,.15,5.65),(3.3,4.1,5.82),'Wood')
        # Retain the undamaged fascia/chimney/verge details without the old hole plates.
        temp=Builder('roof_details');r1.roof(temp,'Damaged')
        temp.mesh.faces.ensure_lookup_table()
        # Identify details by material and geometry outside the exposed roof field.
        for f in list(temp.mesh.faces):
            coords=[v.co for v in f.verts];center=f.calc_center_median()
            keep=(f.material_index==2 or (center.x<-3.05 and center.z>7.9 and center.y<-.3) or (f.material_index==1 and center.z>9.65))
            if keep:
                vs=[b.mesh.verts.new(v.co) for v in f.verts];nf=b.mesh.faces.new(vs);nf.material_index=f.material_index
        temp.mesh.free()
    if LOW:
        # One continuous cap per surviving ridge run, with identical top and ends.
        remove=[f for f in b.mesh.faces if f.material_index==1 and f.calc_center_median().z>9.65]
        bmesh.ops.delete(b.mesh,geom=remove,context='FACES_ONLY')
        for a,c in ([(-5.85,-1.15),(3.35,5.86)] if state=='Damaged' else [(-5.85,5.86)]):b.box((a,-.13,9.695),(c,.13,9.825),'Roof')

def extras(b,state):
    # Satellite bowl is a visible 60 cm silhouette, fixed mount on rear gable.
    seg=6 if LOW else 16
    made=bmesh.ops.create_uvsphere(b.mesh,u_segments=seg,v_segments=3 if LOW else 6,radius=1)
    from mathutils import Matrix
    for v in made['verts']:v.co=Vector((-5.73+v.co.x*.12,2.4+v.co.y*.34,4.85+v.co.z*.34))
    b._tag(made['verts'],'Trim')
    b.box((-5.75,2.35,4.30),(-5.45,2.45,4.75),'Wood')
    r1.facebox(b,'+X',3.2,.12,.56,.28,0,.12,'Wood')
    r1.facebox(b,'-X',-2.9,2.45,2.67,.23,0,.06,'Wood')
    if not LOW:
        for j in range(4):r1.facebox(b,'-X',-2.9,2.47+j*.045,2.49+j*.045,.2,.06,.09,'Trim')

def debris(b,rng,n,extent,center=(0,0)):
    for i in range(n):
        x=center[0]+rng.uniform(-extent[0],extent[0]);y=center[1]+rng.uniform(-extent[1],extent[1])
        slot=['Brick','Brick','Brick','Roof','Wood','Rubble'][i%6]
        size=(rng.uniform(.23,.8),rng.uniform(.18,.45),rng.uniform(.16,.32))
        if slot=='Wood':size=(rng.uniform(.9,1.8),.15,.18)
        angles=(rng.uniform(-24,24),rng.uniform(-24,24),rng.uniform(0,180))
        fragment(b,(x,y,size[2]/2+.05),size,angles,slot)

def fragment(b,center,size,angles,slot):
    if not LOW:b.turned(center,size,angles,slot);return
    from mathutils import Matrix,Euler
    mat=Matrix.Translation(center)@Euler(tuple(math.radians(a) for a in angles)).to_matrix().to_4x4()
    sx,sy,sz=(s/2 for s in size)
    vs=[b.mesh.verts.new(mat@Vector(p)) for p in [(-sx,-sy,-sz),(sx,-sy,-sz),(0,-sy,sz),(-sx,sy,-sz),(sx,sy,-sz),(0,sy,sz)]]
    fs=[b.mesh.faces.new([vs[i] for i in ids]) for ids in [(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)]]
    bmesh.ops.recalc_face_normals(b.mesh,faces=fs)
    for f in fs:f.material_index=r1.SLOTS.index(slot)

def build(state):
    name='SM_Village_HouseA'+('' if state=='Pristine' else '_'+state)
    b=Builder(name);rng=random.Random(431);found=opening_list()
    if state!='Rubble':
        shell(b,found,state);windows(b,found,state);roof(b,state);r1.drainage(b,state);extras(b,state)
        for face in g.FACES:
            span=4.5 if 'X' in face else 5.5
            ranges=[(-span,span)] if face!='+X' else [(-span,-.56),(.56,span)]
            if state=='Damaged' and face=='+Y':ranges=[(-span,-1.5),(1.55,span)]
            for a,c in ranges:r1.facebox(b,face,(a+c)/2,-.5,.5,(c-a)/2,0,.045,'Wood')
        b.collision=[((-5.5,-4.5,-.5),(5.5,4.5,5.8)),((-5.5,-2.25,5.8),(5.5,2.25,9.6))]
        if state=='Damaged':
            debris(b,rng,38,(2.5,.85),(0,4.9))
            for j in range(14):
                x=(-1.55 if j%2 else 1.58)+rng.uniform(-.14,.14);z=.5+(j//2)*.68
                fragment(b,(x,4.35,z),(.28,.32,.18),(0,rng.uniform(-20,20),0),'Brick')
    else:
        for face in g.FACES:
            span=4.5 if 'X' in face else 5.5
            for j in range(7):
                a=-span+2*span*j/7;c=-span+2*span*(j+1)/7
                if j in (2,4):continue
                h=rng.uniform(.65,2.85)
                # Real 30 cm wall with irregular closed broken top.
                r1.prism(b,face,[(a,-.5),(c,-.5),(c,h-.24),((a+c)/2,h+.08),(a,h)],'Brick')
                for k in range(2):r1.facebox(b,face,a+(c-a)*(.25+.5*k),h-.03,h+.14,.13,-.3,0,'Brick')
        for j in range(25):
            x=rng.uniform(-4.3,4.3);y=rng.uniform(-3.3,3.3);z=max(.3,1.1-math.hypot(x/5,y/4)*.5)
            b.turned((x,y,z),(rng.uniform(.8,2.5),rng.uniform(.6,1.8),rng.uniform(.3,.8)),(rng.uniform(-28,28),rng.uniform(-25,25),rng.uniform(0,180)),['Brick','Brick','Wood','Roof','Rubble'][j%5])
        for x,y,angle in [(-2,-1,24),(2,1,-27),(.5,-2,13)]:
            b.turned((x,y,1.2),(3.8,2.7,.22),(angle,12,x*20),'Roof')
            b.turned((x,y,1.0),(3.5,.18,.25),(angle,12,x*20),'Wood')
        # Fallen gable fragment with its full thickness.
        b.turned((2,-1.8,1.0),(2.8,.3,1.8),(62,9,22),'Brick')
        debris(b,rng,48,(5,4))
        b.collision=[((-5.5,-4.5,-.5),(5.5,4.5,1.3)),((-5,-3,1.3),(-3,3,2.7))]
    return b.finish()

def simplify(obj):
    # Hand-authored LOD uses one PVC ring, sealed room back instead of liners,
    # eight-sided dish. Dissolve coplanar mesh panels, never global decimation.
    bm=bmesh.new();bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
    bmesh.ops.dissolve_limit(bm,angle_limit=.001,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
    bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free();obj.data.update()

def qa(obj):
    uv=obj.data.uv_layers.active.data
    for p in obj.data.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i]));axes=[i for i in range(3) if i!=axis]
        for li in p.loop_indices:
            co=obj.data.vertices[obj.data.loops[li].vertex_index].co
            period=1 if p.material_index==0 else 2
            uv[li].uv=(co[axes[0]]/period,co[axes[1]]/period)
            if p.material_index==1 and axis==2:uv[li].uv=(co.x/2,co.y*math.sqrt(1+(3.8/4.5)**2)/2)
    rec=r1.validate(obj);obj.data.calc_tangents(uvmap='UVMap')
    rec['slot_triangles']={s:sum(1 for t in obj.data.loop_triangles if t.material_index==i) for i,s in enumerate(r1.SLOTS)}
    rec['bad_tangents']=sum(not all(math.isfinite(c) for c in l.tangent) or l.tangent.length<.99 for l in obj.data.loops)
    assert rec['bad_tangents']==0
    rec['minimum_altitude_m']=min((lambda vs:(vs[1]-vs[0]).cross(vs[2]-vs[0]).length/max((vs[1]-vs[0]).length,(vs[2]-vs[0]).length,(vs[2]-vs[1]).length))([obj.data.vertices[i].co for i in t.vertices]) for t in obj.data.loop_triangles)
    return rec

def main():
    global LOW,OUT
    ap=argparse.ArgumentParser();ap.add_argument('--output-root',type=Path,default=OUT);args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUT=args.output_root.resolve();assert OUT==ROOT/'Art/Shared/village/issue431/round2'
    for d in ['source','fbx','textures','evidence']:(OUT/d).mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.scene.unit_settings.system='METRIC'
    r1.SURFACES['Roof']='RoofingTiles013A';r1.materials(OUT)
    report={};objects=[]
    for state in ['Pristine','Damaged','Rubble']:
        LOW=False;obj,hulls=build(state);objects.append((obj,hulls));report[obj.name]=qa(obj);assert report[obj.name]['triangles']<=12000
        r1.export([obj]+hulls,OUT/'fbx'/(obj.name+'.fbx'))
        LOW=True;lod,lh=build(state);lod.name=obj.name+'_LOD1'
        for o in lh:bpy.data.objects.remove(o,do_unlink=True)
        simplify(lod);report[lod.name]=qa(lod)
        r1.export([lod],OUT/'fbx'/(lod.name+'.fbx'));objects.append((lod,[]))
    for obj,hulls in objects:
        col=bpy.data.collections.new(obj.name);bpy.context.scene.collection.children.link(col)
        for o in [obj]+hulls:
            for old in list(o.users_collection):old.objects.unlink(o)
            col.objects.link(o)
            if o!=obj:o.hide_set(True);o.hide_render=True
        if obj!=objects[0][0]:col.hide_viewport=True;col.hide_render=True
    for img in bpy.data.images:
        if img.source=='FILE':img.filepath='//../textures/'+Path(img.filepath).name
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/HouseA_round2.blend'))
    for f in sorted((OUT/'fbx').glob('*.fbx')):
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(f))
        obj=next(o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith('UCX_'))
        report[f.stem]['reimport']=qa(obj)
    (OUT/'evidence/mesh_manifest.json').write_text(json.dumps({'meshes':report,'openings':opening_list(),'brick_period_m':1.0,'roof_surface':'RoofingTiles013A'},indent=2))
    print(json.dumps({k:v['triangles'] for k,v in report.items()}),flush=True)

if __name__=='__main__':main()
