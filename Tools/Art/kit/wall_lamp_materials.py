"""Shared #392 Blender authoring utilities. Scope: five fixtures only; no engine work.
Deterministic meshes use physical-scale UV charts packed without overlap. Import is inert.
"""
import bpy, bmesh, math, json, sys
from pathlib import Path
from mathutils import Vector, Matrix
ROOT=Path(__file__).resolve().parents[3]
KIT=ROOT/'Art/Shared/kit/lighting'

class Builder:
    def __init__(self,name):
        self.name=name; self.v=[]; self.f=[]; self.uv=[]; self.mi=[]; self.smooth=[]; self.charts=[]; self.marks=[]
    def chart(self,uvs,slot=0,kind='metal'):
        # Charts are in physical metres. Housing 512px/m, lens 2048px/m.
        pts=[p for face in uvs for p in face]; lo=[min(p[i] for p in pts) for i in (0,1)]
        hi=[max(p[i] for p in pts) for i in (0,1)]
        ci=len(self.charts)
        self.charts.append(dict(size=[max(hi[i]-lo[i],1e-6) for i in (0,1)],slot=slot,kind=kind))
        return ci,[[(p[0]-lo[0],p[1]-lo[1]) for p in face] for face in uvs]
    def add(self,vs,faces,slot=0,uvs=None,groups=None,smooth=False,kind='metal'):
        base=len(self.v); self.v.extend([tuple(v) for v in vs])
        planar=uvs is None
        if uvs is None:
            uvs=[]
            for face in faces:
                a,b,c=[Vector(vs[i]) for i in face[:3]]
                normal=(b-a).cross(c-a); drop=max(range(3),key=lambda i:abs(normal[i]))
                axes=[i for i in range(3) if i!=drop]
                uvs.append([tuple(vs[i][ax] for ax in axes) for i in face])
            groups=list(range(len(faces)))
        if groups is None: groups=[0]*len(faces)
        mapuv={}
        for g in dict.fromkeys(groups):
            inds=[i for i,n in enumerate(groups) if n==g]
            ci,local=self.chart([uvs[i] for i in inds],slot,kind)
            if planar and len(inds)==1:
                fi=inds[0];face=faces[fi];a,b,c=[Vector(vs[i]) for i in face[:3]];normal=(b-a).cross(c-a).normalized()
                drop=max(range(3),key=lambda i:abs(normal[i]));axes=[i for i in range(3) if i!=drop]
                origin=[min(vs[j][ax] for j in face) for ax in axes]
                self.charts[ci].update(axes=axes,project_origin=origin,plane_normal=list(normal),plane_point=list(a))
            if len(faces)==1:
                a,b,c=[Vector(vs[i]) for i in faces[0][:3]]
                self.charts[ci]['lock_normal']=list((b-a).cross(c-a).normalized())
            for fi,uv in zip(inds,local):mapuv[fi]=(ci,uv)
        start=len(self.f)
        for i,face in enumerate(faces):
            self.f.append(tuple(base+j for j in face));self.mi.append(slot)
            self.uv.append(mapuv[i]);self.smooth.append(smooth)
        return (base,start)
    def box(self,c,size,bevel=0,transform=None,slot=0,kind='metal',open_back=False,open_front=False):
        # Single-segment silhouette bevels; no modifier/hidden export dependency.
        bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
        for v in bm.verts:v.co=Vector([v.co[i]*size[i] for i in range(3)])
        if bevel:
            bmesh.ops.bevel(bm,geom=list(bm.edges),offset=bevel,segments=1,affect='EDGES')
        bm.verts.ensure_lookup_table();bm.faces.ensure_lookup_table()
        vs=[v.co.copy()+Vector(c) for v in bm.verts]
        local_vs=vs
        if transform:vs=[transform@v for v in vs]
        faces=[[v.index for v in f.verts] for f in bm.faces]
        if open_back:faces=[f for f in faces if not all(abs(local_vs[i].x-(c[0]-size[0]/2))<1e-7 for i in f)]
        if open_front:faces=[f for f in faces if not all(abs(local_vs[i].x-(c[0]+size[0]/2))<1e-7 for i in f)]
        self.add(vs,faces,slot,kind=kind);bm.free()
    def prism_y(self,profile,y0,y1,slot=0,kind='metal'):
        n=len(profile);vs=[(x,y,z) for y in (y0,y1) for x,z in profile]
        faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
        faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        self.add(vs,faces,slot,kind=kind)
    def tube(self,path,radius,sides=6,closed=False,slot=0,kind='metal',ribbon=False,ribbon_axis=None):
        path=[Vector(p) for p in path];vs=[];lens=[0.];previous_u=None
        if ribbon:sides=4
        for a,b in zip(path,path[1:]):lens.append(lens[-1]+(b-a).length)
        for i,p in enumerate(path):
            tangent=path[(i+1)%len(path)]-path[(i-1)%len(path)] if closed or 0<i<len(path)-1 else (path[1]-p if i==0 else p-path[-2])
            tangent.normalize();ref=Vector((1,0,0)) if abs(tangent.x)<.9 else Vector((0,1,0))
            u=tangent.cross(ref).normalized() if previous_u is None else (previous_u-tangent*previous_u.dot(tangent)).normalized()
            if ribbon_axis is not None:
                width_axis=Vector(ribbon_axis);u=(width_axis-tangent*width_axis.dot(tangent)).normalized()
            previous_u=u.copy();v=tangent.cross(u).normalized()
            for s in range(sides):
                if ribbon:
                    cu,cv=[(-1,-.4),(1,-.4),(1,.4),(-1,.4)][s];vs.append(p+radius*(u*cu+v*cv))
                else:vs.append(p+radius*(u*math.cos(2*math.pi*s/sides)+v*math.sin(2*math.pi*s/sides)))
        fs=[];uv=[];gs=[]
        for i in range(len(path) if closed else len(path)-1):
            j=(i+1)%len(path);l0=lens[i];l1=lens[j] if j else lens[-1]+(path[0]-path[-1]).length
            for s in range(sides):
                t=(s+1)%sides;fs.append((i*sides+s,i*sides+t,j*sides+t,j*sides+s))
                uv.append([(2*math.pi*radius*s/sides,l0),(2*math.pi*radius*(s+1)/sides,l0),(2*math.pi*radius*(s+1)/sides,l1),(2*math.pi*radius*s/sides,l1)]);gs.append(0)
        if not closed:
            for i,rev in [(0,True),(len(path)-1,False)]:
                fs.append(tuple(i*sides+s for s in (range(sides-1,-1,-1) if rev else range(sides))))
                uv.append([(radius*math.cos(2*math.pi*s/sides),radius*math.sin(2*math.pi*s/sides)) for s in (range(sides-1,-1,-1) if rev else range(sides))]);gs.append(i+1)
        start_chart=len(self.charts)
        self.add(vs,fs,slot,uv,gs,not ribbon,kind)
        if ribbon:self.charts[start_chart]['wear_x_lines']=[2*math.pi*radius*s/4 for s in range(5)]
    def lathe(self,center,axis,rings,n=16,slot=0,kind='metal',cap=True):
        # rings: distance along axis, radius. No zero-radius rings (caps are ngons).
        center=Vector(center);axis=Vector(axis).normalized();u=Vector((0,1,0))
        if abs(axis.dot(u))>.9:u=Vector((1,0,0))
        u=(u-axis*axis.dot(u)).normalized();v=axis.cross(u)
        vs=[center+axis*d+r*(u*math.cos(2*math.pi*s/n)+v*math.sin(2*math.pi*s/n)) for d,r in rings for s in range(n)]
        fs=[];uv=[];gs=[];arc=[0.]
        for (d,r),(e,t) in zip(rings,rings[1:]):arc.append(arc[-1]+math.hypot(e-d,t-r))
        for i in range(len(rings)-1):
            for s in range(n):
                t=(s+1)%n;fs.append((i*n+s,i*n+t,(i+1)*n+t,(i+1)*n+s))
                uv.append([(2*math.pi*rings[i][1]*s/n,arc[i]),(2*math.pi*rings[i][1]*(s+1)/n,arc[i]),(2*math.pi*rings[i+1][1]*(s+1)/n,arc[i+1]),(2*math.pi*rings[i+1][1]*s/n,arc[i+1])]);gs.append(0)
        if cap:
            for i,rev in [(0,True),(len(rings)-1,False)]:
                if cap=='front' and rev:continue
                seq=list(range(n-1,-1,-1) if rev else range(n));fs.append(tuple(i*n+s for s in seq))
                uv.append([(rings[i][1]*math.cos(2*math.pi*s/n),rings[i][1]*math.sin(2*math.pi*s/n)) for s in seq]);gs.append(i+1)
        start=len(self.f);start_chart=len(self.charts);self.add(vs,fs,slot,uv,gs,True,kind)
        if slot==0:self.charts[start_chart]['wear_lines']=arc
        if cap:
            self.smooth[-1]=False
            if cap!='front':self.smooth[-2]=False
    def strip(self,a,b,width,depth,slot=0):
        a,b=Vector(a),Vector(b);d=b-a
        rot=d.to_track_quat('Z','Y').to_matrix().to_4x4()
        self.box((0,0,0),(depth,width,d.length),transform=Matrix.Translation((a+b)/2)@rot,slot=slot)
    def oval(self,x,r,straight,n=20):
        # Each semicircle includes both endpoints: exact straight sides, no skipped quadrant.
        half=n//2
        return [(x,r*math.cos(math.pi*(k+j/(half-1))),r*math.sin(math.pi*(k+j/(half-1)))+(straight if k==0 else -straight)) for k in (0,1) for j in range(half)]
    def oval_surface(self,rings,straight,n=20,slot=0,kind='metal',cap=False):
        vs=[];uv=[];fs=[];gs=[]
        # Approximate capsule perimeter parameter, continuous for each meridian.
        for x,r in rings:vs+=self.oval(x,r,straight,n)
        perimeters=[]
        for j in range(len(rings)):
            ps=[Vector(v) for v in vs[j*n:(j+1)*n]];arc=[0.]
            for s in range(n):arc.append(arc[-1]+(ps[(s+1)%n]-ps[s]).length)
            perimeters.append(arc)
        meridian=[0.]
        for (x,r),(a,b) in zip(rings,rings[1:]):meridian.append(meridian[-1]+math.hypot(a-x,b-r))
        for j in range(len(rings)-1):
            for s in range(n):
                t=(s+1)%n;fs.append((j*n+s,j*n+t,(j+1)*n+t,(j+1)*n+s))
                uv.append([(perimeters[j][s],meridian[j]),(perimeters[j][s+1],meridian[j]),(perimeters[j+1][s+1],meridian[j+1]),(perimeters[j+1][s],meridian[j+1])]);gs.append(0)
        if cap:
            for j,rev in [(0,True),(len(rings)-1,False)]:
                if cap=='front' and rev:continue
                if cap=='back' and not rev:continue
                seq=list(range(n-1,-1,-1) if rev else range(n));fs.append(tuple(j*n+s for s in seq));uv.append([(vs[j*n+s][1],vs[j*n+s][2]) for s in seq]);gs.append(j+1)
        start_chart=len(self.charts);self.add(vs,fs,slot,uv,gs,True,kind)
        if slot==0:self.charts[start_chart]['wear_lines']=meridian
    def screw(self,point,normal,r=.007):self.marks.append((point,normal,r))
    def round_plate(self,x0,x1,width,height,radius,steps=3,kind='metal',back=True):
        outline=[]
        for k,(cy,cz) in enumerate([(width/2-radius,height/2-radius),(-width/2+radius,height/2-radius),(-width/2+radius,-height/2+radius),(width/2-radius,-height/2+radius)]):
            for j in range(steps+1):
                a=(k+j/steps)*math.pi/2;outline.append((cy+radius*math.cos(a),cz+radius*math.sin(a)))
        n=len(outline);vs=[(x,y,z) for x in (x0,x1) for y,z in outline]
        fs=([tuple(range(n-1,-1,-1))] if back else [])+[tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        start=len(self.f);self.add(vs,fs,kind=kind,smooth=True);self.smooth[start]=False
        if back:self.smooth[start+1]=False
    def disc(self,center,axis,radius,n=40,kind='honeycomb'):
        axis=Vector(axis).normalized();u=Vector((0,1,0));v=axis.cross(u)
        vs=[Vector(center)+radius*(u*math.cos(2*math.pi*i/n)+v*math.sin(2*math.pi*i/n)) for i in range(n)]
        coords=[(radius*math.cos(2*math.pi*i/n),radius*math.sin(2*math.pi*i/n)) for i in range(n)]
        self.add(vs,[tuple(range(n))],slot=1,uvs=[coords],kind=kind)
    def finish(self):
        # Explicit painted fasteners live in map space, not added screw geometry.
        for point,normal,r in self.marks:
            for c in self.charts:
                if 'axes' not in c:continue
                if abs(Vector(c['plane_normal']).dot(Vector(normal)))<.99:continue
                if abs((Vector(point)-Vector(c['plane_point'])).dot(Vector(c['plane_normal'])))>.0002:continue
                q=[point[ax]-v for ax,v in zip(c['axes'],c['project_origin'])]
                if all(-.0001<=q[i]<=c['size'][i]+.0001 for i in (0,1)):c.setdefault('screws',[]).append([*q,r])
        def pack(slot,density,prefix,res):
            ids=[i for i,c in enumerate(self.charts) if c['slot']==slot];x=y=.008;row=0;pad=4/res
            for i in sorted(ids,key=lambda i:self.charts[i]['size'][1],reverse=True):
                c=self.charts[i];w,h=[s*density for s in c['size']]
                if x+w+pad>.992:x=.008;y+=row+2*pad;row=0
                if y+h+pad>.992 or w>.98:return False
                c[prefix+'offset']=[x,y];c[prefix+'scale']=density;x+=w+2*pad;row=max(row,h)
            return True
        for slot,res,density in [(0,2048,.25),(1,1024,1.5 if 'RoofFlood' in self.name else (1.75 if 'CageWall' in self.name else 1.8))]:
            assert pack(slot,density,'',res),('UV0 atlas full',self.name,slot)
            finish=1.5 if slot==0 else density
            while not pack(slot,finish,'finish_',res):finish*=.94
        me=bpy.data.meshes.new(self.name);me.from_pydata(self.v,[],self.f);me.update()
        for name in ('Housing','Lens'):
            me.materials.append(bpy.data.materials.get(name) or bpy.data.materials.new(name))
        uv=me.uv_layers.new(name='UVMap')
        for face,entry,mat,smooth in zip(me.polygons,self.uv,self.mi,self.smooth):
            ci,coords=entry;c=self.charts[ci]
            face.material_index=mat;face.use_smooth=smooth
            for li,p in zip(face.loop_indices,coords):uv.data[li].uv=(p[0]*c['scale']+c['offset'][0],p[1]*c['scale']+c['offset'][1])
        # UV1 is a distinct finish-bake channel, using the same tangent parameterization as UV0.
        uv1=me.uv_layers.new(name='FinishBake')
        for face,entry in zip(me.polygons,self.uv):
            ci,coords=entry;c=self.charts[ci]
            for li,p in zip(face.loop_indices,coords):uv1.data[li].uv=(p[0]*c['finish_scale']+c['finish_offset'][0],p[1]*c['finish_scale']+c['finish_offset'][1])
        attr=me.attributes.new('atlas_chart','INT','FACE')
        for item,(ci,coords) in zip(attr.data,self.uv):item.value=ci
        me.uv_layers.active_index=0
        bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.normal_update()
        chart_layer=bm.faces.layers.int['atlas_chart']
        for face in bm.faces:
            expected=self.charts[face[chart_layer]].get('lock_normal')
            if expected and face.normal.dot(Vector(expected))<0:face.normal_flip()
        bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free();me.update()
        o=bpy.data.objects.new(self.name,me);bpy.context.collection.objects.link(o)
        o['chart_layout']=json.dumps(self.charts);o['frame']='metres; base underside Z=0 at origin, +X forward, Z up' if 'RoofFloodLight' in self.name else 'metres; mount back X=0, +X forward, Z up'
        return o

def pair(piece,fn):
    bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
    a=fn(piece['mesh'],False);b=fn(piece['mesh']+'_LOD1',True)
    pack_lod_pair(a,b)
    b.hide_render=True;b.hide_set(False)
    out=KIT/piece['id'].split('/')[-1]
    if (out/'material_recipe.json').exists():apply_pbr(piece,out,use_finish=True)
    return a,b

def pack_lod_pair(a,b):
    """Both LODs occupy disjoint charts in the SAME seven maps, baked on their own normals.
    Previously LOD1 sampled arbitrary LOD0 charts: non-overlap alone did not prove binding.
    """
    objects=(a,b); layouts=[json.loads(o['chart_layout']) for o in objects]
    charts=[c for cs in layouts for c in cs]
    def pack(slot,density,prefix,res):
        x=y=.008;row=0;pad=4/res
        ids=[i for i,c in enumerate(charts) if c['slot']==slot]
        for i in sorted(ids,key=lambda i:charts[i]['size'][1],reverse=True):
            c=charts[i];w,h=[s*density for s in c['size']]
            if x+w+pad>.992:x=.008;y+=row+2*pad;row=0
            if y+h+pad>.992 or w>.98:return False
            c['new_'+prefix+'offset']=[x,y];c['new_'+prefix+'scale']=density
            x+=w+2*pad;row=max(row,h)
        return True
    for slot,res,density in [(0,2048,.25),(1,1024,1.8)]:
        while not pack(slot,density,'',res):
            assert slot==1,'Housing must retain 512 px/m'
            density*=.94
        finish=1.5 if slot==0 else density
        while not pack(slot,finish,'finish_',res):finish*=.94
    # Identical cage plate surfaces share the exact LOD0 UV0/UV1 regions.
    shared={c['shared_lod_key']:c for c in layouts[0] if 'shared_lod_key' in c}
    for c in layouts[1]:
        if c.get('shared_lod_key') in shared:
            source=shared[c['shared_lod_key']]
            for key in ('offset','scale','finish_offset','finish_scale'):c['new_'+key]=source['new_'+key]
            c['shared_lod_alias']=True
    for o,cs in zip(objects,layouts):
        for face in o.data.polygons:
            c=cs[o.data.attributes['atlas_chart'].data[face.index].value]
            for channel,pre in [(0,''),(1,'finish_')]:
                for li in face.loop_indices:
                    uv=o.data.uv_layers[channel].data[li].uv
                    local=(uv-Vector(c[pre+'offset']))/c[pre+'scale']
                    uv[:]=local*c['new_'+pre+'scale']+Vector(c['new_'+pre+'offset'])
        for c in cs:
            for key in ('offset','scale','finish_offset','finish_scale'):c[key]=c.pop('new_'+key)
        o['chart_layout']=json.dumps(cs)
        o['atlas_contract']='Round 4b: identical cage plate charts shared exactly across LODs; other charts disjoint; both LODs normal-baked'

def bolt(b,center,axis,r=.012,n=8):
    b.lathe(center,axis,[(0,r*1.32),(.002,r*1.32)],n=n)
    b.lathe(Vector(center)+Vector(axis)*.002,axis,[(0,r),(.005,r)],n=6,kind='torx')

# ----------------------- Final Round 3 PBR -----------------------
def save_map(path,array):
    import numpy as np
    h,w=array.shape[:2]
    rgba=np.ones((h,w,4),dtype=np.float32);rgba[:,:,:3]=np.clip(array,0,1)
    im=bpy.data.images.new(path.stem,width=w,height=h,alpha=True,float_buffer=False)
    im.colorspace_settings.name='Non-Color';im.pixels.foreach_set(rgba.ravel())
    im.filepath_raw=str(path);im.file_format='PNG';im.save();bpy.data.images.remove(im)

def noise(rng,n,grid):
    import numpy as np
    # Bilinear, periodic lattice; deterministic at every size without external deps.
    a=rng.random((grid+1,grid+1),dtype=np.float32);q=np.linspace(0,grid,n,endpoint=False,dtype=np.float32)
    i=q.astype(int);t=q-i
    return ((1-t[:,None])*((1-t[None,:])*a[i[:,None],i[None,:]]+t[None,:]*a[i[:,None],i[None,:]+1])+t[:,None]*((1-t[None,:])*a[i[:,None]+1,i[None,:]]+t[None,:]*a[i[:,None]+1,i[None,:]+1]))

def make_textures(piece,obj,out):
    """Round 3: silver chips, localized oxidation, physical cast grain, edge chips, Torx marks and structured optics.
    UV0: BaseColor/ORM/profile. UV1: dense micro-normal bake input; not a second runtime normal.
    """
    import numpy as np
    name=piece['mesh'].replace('SM_Kit_','');objects=list(obj) if isinstance(obj,(tuple,list)) else [obj];charts=[c for o in objects for c in json.loads(o['chart_layout'])];textures=[]
    (out/'source/bake_inputs').mkdir(exist_ok=True)
    def paint(slot,res,finish=False):
        rng=np.random.default_rng(2392+sum(map(ord,name))+slot+int(finish)*41)
        broad=noise(rng,res,37);mid=noise(rng,res,511);grain=noise(rng,res,min(res,1301));fine=rng.random((res,res),dtype=np.float32)
        if slot==0:
            value=.115+.040*(broad-.5)+.038*(mid-.5)+.030*(grain-.5)
            color=np.stack([value*.94,value*.98,value*1.025],axis=2)
            rough=.48+.30*(broad-.5)+.18*(mid-.5);metal=np.zeros((res,res),dtype=np.float32)
            pits=np.zeros_like(grain)
            height=(grain-.5)*.000075+(fine-.5)*.000018-pits
            color*=1-np.clip(pits[:,:,None]*350,0,.45)
            ao=np.clip(1-pits*500,.5,1)
        else:
            tint=(.71,.63,.47) if 'Cage' in name else (.74,.76,.73)
            color=np.ones((res,res,3),dtype=np.float32)*np.array(tint);color*=.90+.12*mid[:,:,None]
            rough=.34+.16*(grain-.5);metal=np.zeros((res,res),dtype=np.float32)
            height=(grain-.5)*.00015+(fine-.5)*.000035;ao=np.ones((res,res),dtype=np.float32)
        profile=np.ones((res,res),dtype=np.float32);scale_image=np.ones((res,res),dtype=np.float32)
        for ci,c in enumerate(charts):
            if c['slot']!=slot or c.get('shared_lod_alias'):continue
            pre='finish_' if finish else '';scale=c[pre+'scale'];x,y=[int(v*res) for v in c[pre+'offset']]
            w,h=[max(1,int(v*scale*res)) for v in c['size']]
            yy,xx=np.mgrid[:h,:w].astype(np.float32);xx/=(scale*res);yy/=(scale*res)
            wmet,hmet=c['size'];edge=np.minimum.reduce([xx,wmet-xx,yy,hmet-yy]);dst=np.s_[y:y+h,x:x+w]
            local=mid[dst];g=grain[dst];f=fine[dst];scale_image[dst]=scale
            if slot==0:
                # Sparse, position-matched pits: same metric feature centres in UV0 and UV1.
                prng=np.random.default_rng(4392+sum(map(ord,name))+ci*1309)
                for pi in range(min(120,int(wmet*hmet*900))):
                    cx,cy=prng.random(2)*[wmet,hmet];radius=prng.uniform(.00035,.0011);depth=prng.uniform(.00008,.00028)
                    crater=np.exp(-((np.hypot(xx-cx,yy-cy)/radius)**4))
                    height[dst]-=depth*crater;color[dst]*=1-.55*crater[:,:,None];ao[dst]*=1-.35*crater
                # Broken polished edges over dark coating; coarse rust/dust and fine pits remain independent.
                width=.00045+.00115*local
                if c.get('wear_lines'):
                    crease=np.minimum.reduce([np.abs(yy-line) for line in c['wear_lines']]);edge=np.minimum(edge,crease*1.8)
                if c.get('wear_x_lines'):
                    crease=np.minimum.reduce([np.abs(xx-line) for line in c['wear_x_lines']]);edge=np.minimum(edge,crease)
                wear=np.clip((width-edge)/np.maximum(width,.0001),0,1)*np.clip((g-.25)*2.1,0,1)
                scratch=(np.sin(xx*1150+local*15)>.97)*(g>.74)*.18
                wear=np.maximum(wear,scratch)
                if c['kind']=='castframe':wear=np.maximum(wear,np.exp(-crease/.00055)*(.45+.35*g))
                if 'Cage' not in name:
                    # Continuous rubbing: metric low-frequency width variation and sparse chips.
                    # Fine grain must never gate the edge into a dotted/stitch pattern.
                    slow=.5+.22*np.sin(xx*29+yy*43+ci*1.7)+.16*np.sin(xx*83-yy*61+ci*.71)
                    width=.0012+.0012*slow
                    chips=np.zeros_like(edge)
                    crng=np.random.default_rng(7392+sum(map(ord,name))+ci*1309)
                    for _ in range(max(1,int((wmet+hmet)*5))):
                        cx,cy=crng.random(2)*[wmet,hmet]
                        radius=crng.uniform(.003,.008)
                        chips=np.maximum(chips,np.exp(-((xx-cx)**2+(yy-cy)**2)/(radius*radius)))
                    width+=.0022*chips
                    coverage=np.clip((width-edge)*(scale*res)+.5,0,1)
                    wear=coverage*(.48+.22*slow)
                rust=np.zeros_like(edge)
                if 'Cage' in name and c['kind'] in ('castframe','castbar'):
                    rust=np.exp(-edge/.0025)*np.clip((local-.57)*4,0,1)*.64
                # Gravity-local bottom bloom; polished chips remain silver on all other edges.
                if 2 in c.get('axes',[]) and c['project_origin'][c['axes'].index(2)]<-.045:
                    bottom=xx if c['axes'].index(2)==0 else yy
                    rust=np.maximum(rust,np.clip((.0025-bottom)/.0025,0,1)*np.clip((local-.72)*4,0,1)*(.24 if 'Cage' in name else .10))
                dust=np.clip((.007-edge)/.007,0,1)*np.clip((.56-local)*1.8,0,1)
                color[dst]=color[dst]*(1-wear[:,:,None])+np.array((.67,.69,.71))*wear[:,:,None]
                color[dst]=color[dst]*(1-rust[:,:,None])+np.array((.31,.145,.047))*rust[:,:,None]
                color[dst]=color[dst]*(1-.28*dust[:,:,None])+np.array((.21,.215,.22))*.28*dust[:,:,None]
                rough[dst]=rough[dst]*(1-wear)+(.24+.16*g)*wear+.20*dust+.18*rust
                metal[dst]=.86*wear*(1-rust);height[dst]-=.00022*wear;ao[dst]-=.14*dust
                if c['kind']=='hood':
                    inner=np.clip((yy/hmet-.56)*9,0,1);color[dst]*=1-.48*inner[:,:,None];rough[dst]+=.05*inner
                if c['kind']=='cable':
                    braid=np.sin((xx+yy)*4300)*np.sin((xx-yy)*4300)
                    height[dst]+=.00024*braid;color[dst]*=.48;rough[dst]=.55+.12*g
                if c['kind'] in ('band','gland','wornmetal'):color[dst]*=1.30;metal[dst]=np.maximum(metal[dst],.35);rough[dst]-=.05
                centers=list(c.get('screws',[]))
                if c['kind'] in ('bolt','torx','clamp') and .009<min(wmet,hmet)<.065 and max(wmet,hmet)<min(wmet,hmet)*1.45:centers.append([wmet/2,hmet/2,min(wmet,hmet)*.44])
                for cx,cy,r in centers:
                    dx=xx-cx;dy=yy-cy;rad=np.hypot(dx,dy);ang=np.arctan2(dy,dx)
                    bloom=np.exp(-((rad-r*1.22)/(r*.30))**2)*np.clip((local-.42)*2.0,0,1)*(.38 if 'Cage' in name else .16)
                    if 2 in c.get('axes',[]):
                        down=-(dx if c['axes'].index(2)==0 else dy);across=dy if c['axes'].index(2)==0 else dx
                        streak=np.exp(-(across/(r*.28))**2)*np.clip((down-r)/(r*.7),0,1)*np.exp(-np.maximum(down-r,0)/(r*2.5))
                        bloom=np.maximum(bloom,streak*(.30 if 'Cage' in name else .12))
                    color[dst]=color[dst]*(1-bloom[:,:,None])+np.array((.33,.16,.052))*bloom[:,:,None];rough[dst]+=.22*bloom
                    head=np.clip((r-rad)*5000,0,1);rim=np.exp(-((rad-r*.89)/(r*.10))**2)
                    # Rounded six-lobe Torx recess, with a raised, chipped circular head and washer seat.
                    star_limit=r*(.40+.10*np.cos(6*ang));star=np.clip((star_limit-rad)*8000,0,1)
                    seat=np.exp(-((rad-r*1.06)/(r*.11))**2)
                    head_color=np.array((.23,.25,.27))[None,None,:]+rim[:,:,None]*np.array((.34,.34,.34))
                    color[dst]=color[dst]*(1-head[:,:,None])+head_color*head[:,:,None]
                    color[dst]=color[dst]*(1-star[:,:,None])+np.array((.012,.010,.008))*star[:,:,None]
                    height[dst]+=.00045*head-.00065*star-.00015*seat
                    rough[dst]=rough[dst]*(1-head)+(.29+.12*g)*head;metal[dst]=np.maximum(metal[dst],head*.90);ao[dst]*=1-.55*star-.22*seat
            else:
                attenuation=.65+.35*np.clip(edge/.009,0,1);kind=c['kind']
                if kind in ('ribbed_y','prismatic_x'):
                    q=xx if kind=='ribbed_y' else yy
                    rib=(.5+.5*np.cos(q*2*math.pi/.0032))**.7
                    height[dst]+=.00032*rib;profile[dst]=attenuation*(.14+.86*rib);rough[dst]=.27+.13*(1-rib)
                elif kind=='honeycomb':
                    pitch=.0052;row=np.floor(yy/(pitch*.866));px=(xx+pitch*.5*(row%2))%pitch-pitch/2;py=yy%(pitch*.866)-pitch*.433
                    cell=np.clip((.00245-np.hypot(px,py))/.00065,0,1)
                    height[dst]+=.00045*(1-cell);color[dst]*=.38+.62*cell[:,:,None];profile[dst]=attenuation*(.12+.88*cell);rough[dst]=.16+.10*(1-cell)
                elif kind in ('led','glasscover'):
                    px=(xx%.0105)-.00525;py=(yy%.0105)-.00525;dot=np.exp(-((np.maximum(np.abs(px),np.abs(py))/.0039)**10))
                    field=np.ones_like(dot)
                    if kind=='glasscover':
                        # Glass over all three fields, with dark separators visible behind it.
                        frac=yy/hmet
                        field=(np.abs(frac-1/3)>.012)*(np.abs(frac-2/3)>.012)
                        height[dst]=(f-.5)*.000015
                    else:height[dst]+=.00024*(1-dot)
                    color[dst]*=(.28+.72*dot[:,:,None])*field[:,:,None];profile[dst]=attenuation*(.055+.945*dot)*field;rough[dst]=.045 if kind=='glasscover' else .22
                else:
                    sparkle=np.maximum(0,g-.77)*3.5;height[dst]+=.00023*sparkle
                    color[dst]*=1-.20*sparkle[:,:,None]
                    core=.36+.64*np.exp(-((xx/wmet-.48)/.42)**2)*np.exp(-((yy/hmet-.70)/.62)**2)
                    profile[dst]=attenuation*core*(.78+.22*g);rough[dst]=.25+.20*g
            if slot==1:
                if c['kind'] in ('ribbed_y','prismatic_x','honeycomb'):
                    centre=np.exp(-((xx/wmet-.5)/.55)**4)*np.exp(-((yy/hmet-.5)/.55)**4)
                    profile[dst]*=.38+.77*centre
        dy,dx=np.gradient(height);nx=-dx*scale_image*res;ny=dy*scale_image*res;nz=np.ones_like(nx);length=np.sqrt(nx*nx+ny*ny+1)
        normal=np.stack([nx/length*.5+.5,ny/length*.5+.5,nz/length*.5+.5],axis=2)
        orm=np.stack([np.clip(ao,0,1),np.clip(rough,.18,.9),np.clip(metal,0,1)],axis=2)
        return color,orm,normal,np.repeat(np.clip(profile,0,1)[:,:,None],3,axis=2)
    for slot,res,surface in [(0,2048,'Housing'),(1,1024,'Lens')]:
        color,orm,_,profile=paint(slot,res)
        if slot==1 and 'Cage' in name:
            # Bake the frosted globe's brighter core from its real X/Z positions, not a UV-space gradient.
            for obj in objects:
                obj.data.calc_loop_triangles();uv=obj.data.uv_layers[0]
                for tri in obj.data.loop_triangles:
                    if tri.material_index!=1:continue
                    ps=np.array([uv.data[i].uv[:] for i in tri.loops],dtype=np.float32)*res
                    lo=np.maximum(np.floor(ps.min(axis=0)).astype(int),0);hi=np.minimum(np.ceil(ps.max(axis=0)).astype(int),res-1)
                    if any(hi<=lo):continue
                    yy,xx=np.mgrid[lo[1]:hi[1]+1,lo[0]:hi[0]+1];xq=xx+.5;yq=yy+.5
                    a,b,c=ps;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
                    if abs(den)<1e-8:continue
                    wa=((b[1]-c[1])*(xq-c[0])+(c[0]-b[0])*(yq-c[1]))/den
                    wb=((c[1]-a[1])*(xq-c[0])+(a[0]-c[0])*(yq-c[1]))/den;wc=1-wa-wb;inside=(wa>=0)&(wb>=0)&(wc>=0)
                    vs=np.array([obj.data.vertices[i].co[:] for i in tri.vertices]);wx=wa*vs[0,0]+wb*vs[1,0]+wc*vs[2,0];wz=wa*vs[0,2]+wb*vs[1,2]+wc*vs[2,2]
                    core=np.exp(-((wz/.075)**4))*np.exp(-(((wa*vs[0,1]+wb*vs[1,1]+wc*vs[2,1])/.055)**4))
                    wy=wa*vs[0,1]+wb*vs[1,1]+wc*vs[2,1]
                    cx=np.floor(wx/.0035);cy=np.floor(wy/.0035);cz=np.floor(wz/.0035)
                    def fract(q):return q-np.floor(q)
                    h1=fract(np.sin(cx*91.3+cy*127.1+cz*311.7)*43758.5453);h2=fract(np.sin(cx*76.9+cy*269.5+cz*183.3)*37581.1359);h3=fract(np.sin(cx*173.1+cy*97.3+cz*264.9)*32789.763)
                    dx=wx-(cx+.15+.70*h3)*.0035;dy=wy-(cy+.15+.70*h1)*.0035;dz=wz-(cz+.15+.70*h2)*.0035
                    speck=np.exp(-((np.sqrt(dx*dx+dy*dy+dz*dz)/(.00065+.00055*h1))**4))*((h2>.98)|((h2>.94)&(wz<-.065)))
                    target=profile[lo[1]:hi[1]+1,lo[0]:hi[0]+1];values=(.035+.965*core)*(1-.24*speck)*(.96+.04*(.5+.5*np.sin(wx*9217+np.sin(wy*8371)+np.sin(wz*7393))))
                    target[inside]=np.repeat(values[:,:,None],3,axis=2)[inside]
                    target_color=color[lo[1]:hi[1]+1,lo[0]:hi[0]+1];glass=np.array((.71,.63,.47))*(1-.18*speck[:,:,None]);target_color[inside]=glass[inside]
    
        for mapname,array in [('BaseColor',color),('ORM',orm)]+([('LightProfile',profile)] if slot else []):
            path=(out/'source/bake_inputs' if mapname=='Micro_Normal' else out/'textures')/f'T_{name}_{surface}_{mapname}.png';save_map(path,array)
            textures.append(dict(path=str(path.relative_to(out)),size=[res,res],slot=surface,map=mapname,srgb=mapname=='BaseColor'))
        _,_,normal,_=paint(slot,res,True)
        save_map(out/'source/bake_inputs'/f'T_{name}_{surface}_Micro_Normal.png',normal)
    return textures

def temperature_rgb(kelvin):
    # Producer display tints: 4000 K flood optics read neutral white in the reference.
    return {3000:(1,.44,.16),3500:(1,.54,.27),4000:(.89,.94,1)}[kelvin]

def apply_pbr(piece,out,use_finish=False):
    name=piece['mesh'].replace('SM_Kit_','')
    for surface in ('Housing','Lens'):
        mat=bpy.data.materials[surface];mat.use_nodes=True;nt=mat.node_tree;nt.nodes.clear()
        p=nt.nodes.new('ShaderNodeBsdfPrincipled');p.location=(800,0)
        output=nt.nodes.new('ShaderNodeOutputMaterial');output.location=(1100,0);nt.links.new(p.outputs['BSDF'],output.inputs['Surface'])
        def tex(mapname,y,uv1=False):
            path=(out/'source/bake_inputs' if mapname=='Micro_Normal' else out/'textures')/f'T_{name}_{surface}_{mapname}.png'
            node=nt.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(path),check_existing=True);node.image.reload();node.location=(-700,y);node.label=mapname
            node.image.colorspace_settings.name='sRGB' if mapname=='BaseColor' else 'Non-Color'
            coord=nt.nodes.new('ShaderNodeUVMap');coord.uv_map='FinishBake' if uv1 else 'UVMap';coord.location=(-950,y);nt.links.new(coord.outputs['UV'],node.inputs['Vector'])
            return node
        base=tex('BaseColor',420);nt.links.new(base.outputs['Color'],p.inputs['Base Color'])
        orm=tex('ORM',80);sep=nt.nodes.new('ShaderNodeSeparateColor');sep.location=(-400,80);nt.links.new(orm.outputs['Color'],sep.inputs['Color'])
        nt.links.new(sep.outputs[1],p.inputs['Roughness']);nt.links.new(sep.outputs[2],p.inputs['Metallic'])
        normal=tex('Finish_Normal' if use_finish else 'Micro_Normal',-300,True)
        sep=nt.nodes.new('ShaderNodeSeparateColor');sep.location=(-400,-240);nt.links.new(normal.outputs['Color'],sep.inputs['Color'])
        invert=nt.nodes.new('ShaderNodeMath');invert.operation='SUBTRACT';invert.inputs[0].default_value=1;invert.location=(-180,-350);nt.links.new(sep.outputs[1],invert.inputs[1])
        combine=nt.nodes.new('ShaderNodeCombineColor');combine.location=(0,-240)
        for src,idx in [(sep.outputs[0],0),(invert.outputs[0],1),(sep.outputs[2],2)]:nt.links.new(src,combine.inputs[idx])
        nm=nt.nodes.new('ShaderNodeNormalMap');nm.uv_map='UVMap';nm.location=(200,-240);nt.links.new(combine.outputs[0],nm.inputs['Color'])
        if not use_finish:
            bevel=nt.nodes.new('ShaderNodeBevel');bevel.inputs['Radius'].default_value=.001 if surface=='Housing' else .0006;bevel.samples=4;bevel.location=(420,-240)
            nt.links.new(nm.outputs['Normal'],bevel.inputs['Normal']);nt.links.new(bevel.outputs['Normal'],p.inputs['Normal'])
            if surface=='Lens' and 'Cage' in name:
                coords=nt.nodes.new('ShaderNodeNewGeometry');noise=nt.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=1450;noise.inputs['Detail'].default_value=2
                bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.38;bump.inputs['Distance'].default_value=.00013
                nt.links.new(coords.outputs['Position'],noise.inputs['Vector']);nt.links.new(noise.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],bevel.inputs['Normal'])
        else:nt.links.new(nm.outputs['Normal'],p.inputs['Normal'])
        if surface=='Lens':
            profile=tex('LightProfile',-650);multiply=nt.nodes.new('ShaderNodeMath');multiply.operation='MULTIPLY';multiply.inputs[1].default_value=7.0 if 'Cage' in name else (6.0 if 'Directional' in name else 5.0);multiply.location=(220,-600)
            nt.links.new(profile.outputs['Color'],multiply.inputs[0]);level=nt.nodes.new('ShaderNodeValue');level.name='LampLevel';level.outputs[0].default_value=1;level.location=(200,-780)
            prod=nt.nodes.new('ShaderNodeMath');prod.operation='MULTIPLY';prod.location=(450,-600);nt.links.new(level.outputs[0],prod.inputs[0]);nt.links.new(multiply.outputs[0],prod.inputs[1]);nt.links.new(prod.outputs[0],p.inputs['Emission Strength'])
            kelvin=piece['light']['kelvin'];tint=temperature_rgb(kelvin)
            p.inputs['Emission Color'].default_value=(*tint,1)
            p.inputs['IOR'].default_value=1.49;p.inputs['Subsurface Weight'].default_value=.08;p.inputs['Subsurface Scale'].default_value=.0015
            p.inputs['Coat Weight'].default_value=1.0 if 'RoofFlood' in name else .32
            p.inputs['Coat Roughness'].default_value=.025 if 'RoofFlood' in name else .15
            geo=nt.nodes.new('ShaderNodeNewGeometry');geo.location=(300,450);nt.links.new(geo.outputs['Normal'],p.inputs['Coat Normal'])
        mat.diffuse_color=(.045,.050,.055,1) if surface=='Housing' else (.75,.72,.65,1)


def bake_finish(piece,obj,out):
    import numpy as np
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=8
    objects=list(obj) if isinstance(obj,(tuple,list)) else [obj]
    for obj in objects:obj.hide_set(False);obj.data.uv_layers.active_index=1
    targets=[]
    for surface,res in [('Housing',2048),('Lens',1024)]:
        mat=bpy.data.materials[surface];nt=mat.node_tree
        image=bpy.data.images.new('BAKE_'+surface,width=res,height=res,alpha=False,float_buffer=True);image.colorspace_settings.name='Non-Color'
        node=nt.nodes.new('ShaderNodeTexImage');node.image=image;nt.nodes.active=node;targets.append((surface,image))
    scene.render.bake.margin=3;scene.render.bake.use_clear=True;scene.render.bake.normal_space='TANGENT';scene.render.bake.normal_g='POS_Y'
    for index,obj in enumerate(objects):
        for other in objects:other.hide_render=other!=obj
        bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
        scene.render.bake.use_clear=index==0
        bpy.ops.object.bake(type='NORMAL',uv_layer='FinishBake')
    name=piece['mesh'].replace('SM_Kit_','')
    repairs={}
    for surface,image in targets:
        pixels=np.empty(len(image.pixels),dtype=np.float32);image.pixels.foreach_get(pixels);arr=pixels.reshape((image.size[1],image.size[0],4))[:,:,:3].copy();arr[:,:,1]=1-arr[:,:,1]
        # Bevel rays through intersecting hidden construction can reverse the tangent normal.
        # Keep the valid baked bevels; replace only invalid hemispheres with the authored
        # DirectX micro-detail normal at the identical UV1 coordinate (not a second runtime map).
        micro=bpy.data.images.load(str(out/'source/bake_inputs'/f'T_{name}_{surface}_Micro_Normal.png'),check_existing=False)
        micro.colorspace_settings.name='Non-Color'
        fallback=np.empty(len(micro.pixels),dtype=np.float32);micro.pixels.foreach_get(fallback)
        fallback=fallback.reshape((micro.size[1],micro.size[0],4))[:,:,:3]
        invalid=(arr[:,:,2]<.55)|(~np.isfinite(arr).all(axis=2))
        repairs[surface]=dict(invalid_or_uncovered_atlas_pixels=int(invalid.sum()),method='Invalid negative/grazing bevel normals and unused atlas background replaced by authored DirectX micro-normal on the same UV1; valid bevel samples preserved')
        arr[invalid]=fallback[invalid]
        save_map(out/'textures'/f'T_{name}_{surface}_Finish_Normal.png',arr)
    (out/'evidence/normal_bake_repair.json').write_text(json.dumps(repairs,indent=2)+'\n')
    for index,obj in enumerate(objects):obj.data.uv_layers.active_index=0;obj.hide_render=index!=0


def smooth_path(points,steps=3):
    """Catmull-Rom cable centreline; endpoints and assembly anchors are preserved."""
    ps=[Vector(p) for p in points];out=[]
    for i in range(len(ps)-1):
        a=ps[max(0,i-1)];b=ps[i];c=ps[i+1];d=ps[min(len(ps)-1,i+2)]
        for j in range(steps):
            t=j/steps;out.append(.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t))
    return out+[ps[-1]]
