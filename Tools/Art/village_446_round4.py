"""Non-destructive issue446 R3 fork of accepted source. Never rebuilds issue431."""
import sys,json,math,hashlib,shutil,warnings
warnings.filterwarnings('ignore',category=DeprecationWarning)
from pathlib import Path
sys.dont_write_bytecode=True
import bpy,bmesh
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round4';FINAL=ROOT/'Art/Shared/village/final';NAME='SM_Bridge_Span_Destroyed'
sys.path.insert(0,str(ROOT/'Tools/Art'))
def load():
    bpy.ops.wm.open_mainfile(filepath=str(OUT.parent/'round3/source/Bridge_baked.blend'))
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for o in bpy.data.objects:o.hide_set(False)
    # Start from round2, remove only its 580 reinforcement triangles and retain
    # every accepted body vertex, face and hull. Re-expand source slot indices
    # for the unchanged round4 bake helpers (Rubble remains index5 until export).
    for o in bpy.data.objects:
        if o.type!='MESH' or o.name.startswith('UCX_'):continue
        count=2760 if o.name.endswith('_MASTER') else 984
        bm=bmesh.new();bm.from_mesh(o.data);bm.faces.ensure_lookup_table()
        tail={v for f in list(bm.faces)[count:] for v in f.verts}
        bmesh.ops.delete(bm,geom=list(tail),context='VERTS');bm.to_mesh(o.data);bm.free();o.data.update()
        assert len(o.data.polygons)==count
        mats=list(o.data.materials);indices=[p.material_index for p in o.data.polygons];o.data.materials.clear()
        for mat in [mats[0],mats[1],mats[2],mats[2],mats[2],mats[3]]:o.data.materials.append(mat)
        for p,index in zip(o.data.polygons,indices):p.material_index=5 if index==3 else index
def inspect():
    load();obj=bpy.data.objects[NAME];bm=bmesh.new();bm.from_mesh(obj.data);seen=set();parts=[]
    for v in bm.verts:
        if v in seen:continue
        group=set();stack=[v]
        while stack:
            q=stack.pop()
            if q in group:continue
            group.add(q);stack.extend(e.other_vert(q) for e in q.link_edges)
        seen|=group;faces={f for q in group for f in q.link_faces}
        parts.append({'slot':list(set(f.material_index for f in faces)),'verts':len(group),'min':[round(min(q.co[i] for q in group),3) for i in range(3)],'max':[round(max(q.co[i] for q in group),3) for i in range(3)]})
    print(json.dumps({'objects':[(o.name,len(o.data.polygons)) for o in bpy.data.objects if o.type=='MESH'],'materials':[m.name for m in obj.data.materials],'parts':parts},indent=2))
def select(obj):
    bpy.ops.object.select_all(action='DESELECT');obj.hide_set(False);obj.select_set(True);bpy.context.view_layer.objects.active=obj
def signature(obj):
    return hashlib.sha256(json.dumps({'v':[tuple(v.co) for v in obj.data.vertices],'f':[(tuple(p.vertices),p.material_index) for p in obj.data.polygons]},sort_keys=True).encode()).hexdigest()
BAR_META=[];UV_CHARTS=[];PATH_STATS=[];BAR_FACE_ENDS=[]
def bars_mesh():
    prior=json.loads((OUT.parent/'round3/evidence/build.json').read_text());vs=[];fs=[];paths=[];vertex_uv=[];BAR_META.clear();UV_CHARTS.clear();PATH_STATS.clear();BAR_FACE_ENDS.clear()
    for bar_index,(raw,meta) in enumerate(zip(prior['bar_paths_m'],prior['reinforcement'])):
        old=[Vector(p) for p in raw];root=old[0];hook=len(old)==5
        points=[p.copy() for p in old]
        if meta['kind']=='waterline mat':
            peers=sorted([m['root'][1] for m in prior['reinforcement'] if m['kind']=='waterline mat' and m['root'][0]*root.x>0])
            rank=peers.index(meta['root'][1])
            lo=(peers[rank-1]+root.y)/2+.14 if rank else root.y-.65
            hi=(peers[rank+1]+root.y)/2-.14 if rank<len(peers)-1 else root.y+.65
            aim=old[-2] if hook else old[-1]
            target_y=max(lo,min(hi,aim.y));dy=target_y-aim.y
            for q in points[1:]:
                t=(q.x-root.x)/(aim.x-root.x);q.y+=dy*t
        # Two neighbouring side bars visually shared an endpoint. Keep roots,
        # move the lower free tip outward by 28 cm and distribute over the bend.
        if bar_index in [19,41]:
            for j,q in enumerate(points[1:],1):q.x+=(-1 if root.x>0 else 1)*.28*j/(len(points)-1)
        tip=points[-2] if hook else points[-1];direction=tip-root;direction.z=0
        width=meta['width_m'];half=width*.5;height=width*.39
        # Apex-up open angle profile: two upper facets, no backfaces, 4 tris
        # per segment. Same width, roots, end reach and segment count as R2.
        length=0;lengths=[0]
        for a,b in zip(points,points[1:]):length+=(b-a).length;lengths.append(length)
        start=len(vs);cross=direction.normalized();across=Vector((0,0,1)).cross(cross)*half
        uvscale=.108/2.5;u0=(bar_index%8)*.125+.008;v0=.020+(bar_index//8)*.045
        profile_width=2*math.hypot(half,height)
        for p,distance in zip(points,lengths):
            vs.extend([tuple(p-across),tuple(p+Vector((0,0,height))),tuple(p+across)])
            vertex_uv.extend([(u0+distance*uvscale,v0+s*profile_width*uvscale) for s in [0,.5,1]])
        for i in range(len(points)-1):
            a=start+i*3;b=a+3;fs.extend([(a,b,b+1),(a,b+1,a+1),(a+1,b+1,b+2),(a+1,b+2,a+2)])
        BAR_FACE_ENDS.append(len(fs))
        paths.append([tuple(p) for p in points]);record=dict(meta);record.update(length_m=length,profile='apex-up V',ridge_height_m=height,hook=hook);BAR_META.append(record)
        PATH_STATS.append({'bar':bar_index,'max_control_point_shift_m':max((a-b).length for a,b in zip(points,old)),'root_shift_m':(points[0]-old[0]).length,'tip_shift_m':(points[-1]-old[-1]).length,'R3_length_m':meta['length_m'],'R4_length_m':length})
    mesh=bpy.data.meshes.new('R446_R4_angle_rebar');mesh.from_pydata(vs,[],fs);mesh.update()
    for poly in mesh.polygons:
        if poly.normal.z<0:poly.flip()
    mesh.update()
    obj=bpy.data.objects.new('R446_R4_bars',mesh);bpy.context.collection.objects.link(obj)
    uv=mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        chart={}
        for i,li in enumerate(p.loop_indices):
            uv.data[li].uv=[(0,0),(1,0),(0,1)][i];vi=mesh.loops[li].vertex_index
            chart[tuple(round(x,5) for x in mesh.vertices[vi].co)]=vertex_uv[vi]
        UV_CHARTS.append(chart)
    assert len(fs)==580,len(fs)
    return obj,paths
def build():
    import numpy as np
    import village_431_round4_bake as bake
    import village_431_round4_qa as qa
    import village_431_housea as h
    from mathutils.bvhtree import BVHTree
    from mathutils.geometry import barycentric_transform
    for f in ['source','fbx','textures','evidence']:(OUT/f).mkdir(parents=True,exist_ok=True)
    # Protection receipt includes the complete accepted delivery and greybox contract.
    protected={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in FINAL.rglob('*') if p.is_file()}
    for p in list((ROOT/'Art/Shared/village').glob('*.fbx'))+[ROOT/'Art/Shared/village/village.json']:
        protected[str(p.relative_to(ROOT))]=hashlib.sha256(p.read_bytes()).hexdigest()
    for previous in ['round1','round2','round3']:
        for folder in ['fbx','source','textures']:
            for p in (OUT.parent/previous/folder).glob('*'):
                if p.is_file():protected[str(p.relative_to(ROOT))]=hashlib.sha256(p.read_bytes()).hexdigest()
    (OUT/'evidence/protected_before.json').write_text(json.dumps(protected,indent=2))
    load();obj=bpy.data.objects[NAME];lod=bpy.data.objects[NAME+'_LOD1'];high=bpy.data.objects[NAME+'_MASTER'];hulls=[o for o in bpy.data.objects if o.name.startswith('UCX_'+NAME+'_')]
    keep=[obj,lod,high]+hulls
    for o in list(bpy.data.objects):
        if o not in keep:bpy.data.objects.remove(o,do_unlink=True)
    hull_sig={o.name:signature(o) for o in hulls};counts={o.name:len(o.data.polygons) for o in [obj,lod,high]}
    inherited={o.name:signature(o) for o in [obj,lod,high]}
    bars,paths=bars_mesh();bar_count=len(bars.data.polygons);assert 0 < bar_count <= 600
    for target in [obj,lod,high]:
        oldv=[tuple(v.co) for v in target.data.vertices];oldf=[(tuple(p.vertices),p.material_index) for p in target.data.polygons]
        bm=bmesh.new();bm.from_mesh(target.data);n=len(bm.faces);bm.from_mesh(bars.data)
        bm.faces.ensure_lookup_table()
        for face in list(bm.faces)[n:]:face.material_index=2
        bm.to_mesh(target.data);bm.free();target.data.update()
        assert [tuple(v.co) for v in target.data.vertices][:len(oldv)]==oldv
        assert [(tuple(p.vertices),p.material_index) for p in target.data.polygons][:len(oldf)]==oldf
    # Smooth the upper half-cylinder shading across the two geometric facets.
    # A narrow curved highlight replaces the broad flat white panel reflection.
    for target in [obj,lod,high]:
        normals=[tuple(n.vector) for n in target.data.corner_normals]
        for poly in target.data.polygons:
            if poly.index<counts[target.name]:continue
            poly.use_smooth=True
            chart=UV_CHARTS[poly.index-counts[target.name]]
            for li in poly.loop_indices:
                vi=target.data.loops[li].vertex_index;co=target.data.vertices[vi].co
                # Locate this vertex in its saved three-point section.
                best=None
                for record,points in zip(BAR_META,paths):
                    for j,pt in enumerate(points):
                        d=(Vector(pt)-co).length
                        if best is None or d<best[0]:best=(d,record,points,j)
                _,record,points,j=best
                tangent=Vector(points[min(j+1,len(points)-1)])-Vector(points[max(j-1,0)])
                tangent.normalize()
                up=Vector((0,0,1));up-=tangent*up.dot(tangent);up.normalize()
                across=Vector((0,0,1)).cross(Vector(points[-2] if record['hook'] else points[-1])-Vector(points[0]));across.normalize()
                lateral=(co-Vector(points[j])).dot(across)/(record['width_m']*.5)
                normal=up*.87+across*lateral*.50;normal.normalize();normals[li]=tuple(normal)
        target.data.normals_split_custom_set(normals)
    bpy.data.objects.remove(bars,do_unlink=True)
    # Repack this mesh only, then rebake from its retained chamfered master.
    select(obj)
    if obj.data.uv_layers.get('BakeUV'):obj.data.uv_layers.remove(obj.data.uv_layers['BakeUV'])
    obj.data.uv_layers.new(name='BakeUV');obj.data.uv_layers.active_index=1
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.0015,scale_to_bounds=True);bpy.ops.object.mode_set(mode='OBJECT');obj.data.uv_layers.active_index=0
    # Reserve a separated, generously padded atlas shelf for the rebar so the
    # dark steel cannot mip into pale concrete/background texels. Broad facets
    # have continuous charts across their bends and about 6-8 texels per facet.
    packed=obj.data.uv_layers['BakeUV'].data
    for poly in obj.data.polygons:
        if poly.index<counts[NAME]:
            for li in poly.loop_indices:packed[li].uv.y=packed[li].uv.y*.70+.30
        else:
            chart=UV_CHARTS[poly.index-counts[NAME]]
            for li in poly.loop_indices:
                co=obj.data.vertices[obj.data.loops[li].vertex_index].co;packed[li].uv=chart[tuple(round(x,5) for x in co)]
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=4;scene.cycles.device='CPU';scene.render.bake.margin=2;scene.render.bake.use_selected_to_active=True;scene.render.bake.cage_extrusion=.035;scene.render.bake.max_ray_distance=.08
    for o in keep:o.hide_render=o not in [obj,high]
    links=[]
    for mat in set(list(obj.data.materials)+list(high.data.materials)):
        shader=mat.node_tree.nodes.get('Principled BSDF')
        for link in list(shader.inputs['Normal'].links):links.append((mat,link.from_socket,link.to_socket));mat.node_tree.links.remove(link)
    maps=[]
    for kind in ['Normal','AO']:
        image=bpy.data.images.new(NAME+'_work'+kind,width=2048,height=2048,alpha=False);image.colorspace_settings.name='Non-Color';nodes=[]
        for mat in obj.data.materials:
            node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;mat.node_tree.nodes.active=node;nodes.append((mat,node))
        select(high);obj.select_set(True);bpy.context.view_layer.objects.active=obj
        if kind=='Normal':bpy.ops.object.bake(type='NORMAL',normal_space='OBJECT',uv_layer='BakeUV')
        else:bpy.ops.object.bake(type='AO',uv_layer='BakeUV')
        pixels=np.array(image.pixels[:],np.float32).reshape(2048,2048,4)
        if kind=='Normal':pixels[:,:,1]=1-pixels[:,:,1]
        maps.append(pixels)
        for mat,node in nodes:mat.node_tree.nodes.remove(node)
        bpy.data.images.remove(image)
    for mat,a,b in links:mat.node_tree.links.new(a,b)
    bake.OUT=OUT;receipt=bake.finish_maps(obj,'Bridge',*maps)
    finish=paint_finish(obj,bake,counts[NAME]);receipt['round4_finish']=finish
    # Match authored LOD1 UV1 via surface correspondence, using the same bake.
    obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles);tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
    layer=lod.data.uv_layers.get('BakeUV') or lod.data.uv_layers.new(name='BakeUV')
    for poly in lod.data.polygons:
        if poly.index>=counts[NAME+'_LOD1']:
            base_poly=obj.data.polygons[counts[NAME]+poly.index-counts[NAME+'_LOD1']]
            for li,bi in zip(poly.loop_indices,base_poly.loop_indices):layer.data[li].uv=obj.data.uv_layers['BakeUV'].data[bi].uv
            continue
        centre=poly.center;point,n,idx,dist=tree.find_nearest(centre);tri=tris[idx]
        for li in poly.loop_indices:
            co=lod.data.vertices[lod.data.loops[li].vertex_index].co
            value=barycentric_transform(co,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*obj.data.uv_layers['BakeUV'].data[i].uv,0)) for i in tri.loops]);layer.data[li].uv=value.xy
    lod.data.uv_layers.active_index=0
    transfer_master_tint(obj,high,counts[NAME],counts[NAME+'_MASTER'])
    report={}
    for target in [obj,lod,high]:
        report[target.name]=qa.inspect(target);report[target.name]['baseline_triangles']=counts[target.name];report[target.name]['added_triangles']=bar_count
        # Export compact stable four-slot contract; keep source nodes for viewing.
        materials=list(target.data.materials);indices=[p.material_index for p in target.data.polygons]
        target.data.materials.clear()
        for i in [0,1,2,5]:target.data.materials.append(bpy.data.materials.get(['Brick','Roof','Trim','x','x','Rubble'][i]) or bpy.data.materials.new(['Brick','Roof','Trim','x','x','Rubble'][i]))
        for poly,i in zip(target.data.polygons,indices):poly.material_index={0:0,1:1,2:2,5:3}[i]
        file=OUT/('source' if target==high else 'fbx')/(target.name+'.fbx');h.export([target]+(hulls if target==obj else []),file)
        target.data.materials.clear()
        for mat in materials:target.data.materials.append(mat)
        for poly,i in zip(target.data.polygons,indices):poly.material_index=i
    assert hull_sig=={o.name:signature(o) for o in hulls}
    # Drop unused inherited material datablocks, including the other bridge state.
    for material in list(bpy.data.materials):
        if material.users==0:bpy.data.materials.remove(material)
    for image in list(bpy.data.images):
        if image.users==0:bpy.data.images.remove(image)
    # Resolve every retained source texture within this round's folder.
    for image in bpy.data.images:
        if image.source!='FILE':continue
        old=Path(bpy.path.abspath(image.filepath));dest=OUT/'textures'/old.name
        if old.exists() and not dest.exists():shutil.copy2(old,dest)
        if dest.exists():image.filepath=str(dest)
    for mat in obj.data.materials:
        for node in mat.node_tree.nodes:
            if node.type=='TEX_IMAGE' and node.image and node.image.name.startswith(NAME+'_Tint'):
                node.image=bpy.data.images.load(str(OUT/'textures'/(NAME+'_Tint.png')),check_existing=False);node.image.colorspace_settings.name='Non-Color'
    # Remove unreferenced inherited image datablocks from the saved single-mesh source.
    for image in list(bpy.data.images):
        if image.users==0:bpy.data.images.remove(image)
    for image in bpy.data.images:
        if image.source=='FILE' and Path(image.filepath).exists():image.filepath='//../textures/'+Path(image.filepath).name
    for o in [lod,high]+hulls:o.hide_render=True;o.hide_set(True)
    select(obj);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/Bridge_baked.blend'),relative_remap=False)
    (OUT/'evidence/build.json').write_text(json.dumps({'meshes':report,'bar_paths_m':paths,'reinforcement':BAR_META,'bar_geometry':'single-sided apex-up angle profile, 4 triangles/segment','path_changes_from_R3':PATH_STATS,'collision_unchanged':hull_sig,'inherited_geometry_unchanged':inherited,'bake':receipt},indent=2))
    fixpaths()
    # Fresh import validates exported geometry, UVs and tangents independently.
    for suffix,folder in [('', 'fbx'),('_LOD1','fbx'),('_MASTER','source')]:
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(OUT/folder/(NAME+suffix+'.fbx')))
        target=next(o for o in bpy.data.objects if o.type=='MESH' and not o.name.startswith('UCX_'));report[NAME+suffix]['fbx']=qa.inspect(target)
        assert report[NAME+suffix]['fbx']['triangles']==report[NAME+suffix]['triangles']
    (OUT/'evidence/source_qa.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
def paint_finish(obj,bake,base_faces):
    import numpy as np
    arrays=[]
    for end in ['Tint','BakeNormal','BakeAO']:
        image=bpy.data.images.load(str(OUT/'textures'/(NAME+'_'+end+'.png')),check_existing=False);image.colorspace_settings.name='Non-Color';arrays.append(np.array(image.pixels[:],np.float32).reshape(2048,2048,4))
    tint,normal,ao=arrays;bar_mask=np.zeros((2048,2048),bool);painted=0
    root_lines={}
    def random_field(a,b,scale):
        u=a/scale;v=b/scale;ix=np.floor(u);iy=np.floor(v);fx=u-ix;fy=v-iy
        fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy)
        def h(x,y):return np.mod(np.sin(x*127.1+y*311.7)*43758.5453,1)
        return (h(ix,iy)*(1-fx)+h(ix+1,iy)*fx)*(1-fy)+(h(ix,iy+1)*(1-fx)+h(ix+1,iy+1)*fx)*fy
    for end in [-1,1]:
        roots=sorted([bar['root'] for bar in BAR_META if bar['kind']=='waterline mat' and bar['root'][0]*end>0],key=lambda p:p[1]);root_lines[end]=np.array(roots)
    for tri,sl,inside,p in bake.raster(obj):
        x,y,z=p[:,:,0],p[:,:,1],p[:,:,2]
        if tri.polygon_index>=base_faces:
            # Linear data values, not sRGB paint. No tan border. Facet lighting
            # provides the highlight; only a restrained warm pigment variation.
            bar_index=next(i for i,end in enumerate(BAR_FACE_ENDS) if tri.polygon_index-base_faces<end)
            variation=.82+.30*((bar_index*.61803398875)%1)
            shade=(.115+.015*np.sin(x*4.7+y*3.9+z*7.3))*variation
            color=shade[...,None]*np.array([1,.24,.045]);tint[sl][:,:,:3][inside]=color[inside]
            ao[sl][:,:,:3][inside]=.80;bar_mask[sl]|=inside
            continue
        if tri.material_index not in [0,1,5]:continue
        end=-1 if np.mean(x)<0 else 1;roots=root_lines[end]
        # Broad, angular fracture silhouette: independently varied upper/lower
        # boundaries, clustered spalls and a narrow wet margin. No dot stencil.
        anchor=np.interp(y,roots[:,1],abs(roots[:,0]))-.12
        knots=np.array([-7.2,-6.5,-5.9,-5.35,-4.6,-4.25,-3.8,-3.1,-2.35,-1.9,-1.1,-.45,.2,.85,1.55,2.1,2.65,3.4,4.05,4.8,5.35,6.0,6.6,7.2])
        widths=np.array([.24,.32,.63,.27,.22,.51,.68,.29,.38,.59,.23,.31,.65,.48,.26,.70,.45,.24,.57,.30,.22,.61,.36,.25])
        if end>0:widths=np.roll(widths,7)
        width=np.interp(y,knots,widths)
        offset=np.interp(y,knots,np.sin(np.arange(len(knots))*2.37+end)*.085)
        outer=anchor+offset+width*.72;inner=anchor+offset-width*.28
        edge=np.minimum(outer-abs(x),abs(x)-inner)
        band=np.clip(edge/.014,0,1)
        plane=-(15-abs(x))*math.tan(math.radians(36));band*=np.clip((.87-abs(z-plane))/.17,0,1)
        # Large joined angular plates, not stippled grain. Each plate has a
        # stable tone and slope; dark fissures separate them at different scales.
        rng=np.random.default_rng(4406+(end+1)*17)
        best=np.full(x.shape,1e9);second=best.copy();plate=np.zeros(x.shape);slopex=np.zeros(x.shape);slopey=np.zeros(x.shape)
        for k in range(54):
            cy=rng.uniform(-7.4,7.4);cx=float(np.interp(cy,roots[:,1],abs(roots[:,0])))-.1+rng.uniform(-.30,.30)
            dx=(abs(x)-cx)*rng.uniform(.85,1.35);dy=(y-cy)*rng.uniform(.8,1.4)
            dist=abs(dx+.35*dy)+abs(dy-.2*dx)
            closer=dist<best;second=np.where(closer,best,np.minimum(second,dist))
            tone=rng.uniform(.22,.52);sx=rng.uniform(-.34,.34);sy=rng.uniform(-.25,.25)
            plate=np.where(closer,tone,plate);slopex=np.where(closer,sx,slopex);slopey=np.where(closer,sy,slopey);best=np.minimum(best,dist)
        fissure=np.clip((second-best)/.035,0,1)
        # A few deeper root-group spalls join the fracture band.
        depth=np.zeros(x.shape)
        for cy,radius in ([(-5.7,.38),(-2.1,.44),(2.5,.48),(5.6,.30)] if end<0 else [(-4.0,.43),(-.4,.34),(2.0,.49),(5.7,.29)]):
            cx=np.interp(cy,roots[:,1],abs(roots[:,0]))+.05
            pit=np.clip(1-(abs(abs(x)-cx)/radius+abs(y-cy)/(radius*1.5)),0,1)
            depth=np.maximum(depth,pit)
        band=np.maximum(band,depth*np.clip((.87-abs(z-plane))/.17,0,1))
        # Side-face roots retain individual torn pockets.
        for bar in BAR_META:
            if bar['kind']!='torn side' or bar['root'][0]*end<0:continue
            d=p-np.array(bar['root']);shape=abs(d[:,:,0])/.31+abs(d[:,:,1])/.19+abs(d[:,:,2])/.26
            band=np.maximum(band,np.clip((1-shape)/.08,0,1))
        # Wet concrete immediately above the actual +11 cm preview water surface.
        wet=np.clip((z+5.89)/.025,0,1)*np.clip((-5.77-z)/.065,0,1)
        wet*=np.clip((.85-abs(z-plane))/.15,0,1)
        mask=inside&((band>.001)|(wet>.001))
        if not np.any(mask):continue
        grain=random_field(x+y*.23,y-x*.19,.027)
        value=(plate*(.70+.30*fissure)+.035*(grain-.5))*(1-.50*depth)
        mottled=value[...,None]*np.array([1,.94,.84])
        weights=band[...,None];current=tint[sl][:,:,:3]
        painted_color=current*(1-weights)+mottled*weights
        painted_color*=1-wet[...,None]*.48
        current[mask]=painted_color[mask]
        n=np.array(tri.normal);noise=np.stack([slopex,slopey,(grain-.5)*.1],axis=-1)
        noise-=np.sum(noise*n,axis=-1)[...,None]*n
        detail=n+noise;detail/=np.linalg.norm(detail,axis=-1)[...,None];detail[:,:,1]*=-1
        encoded=(detail+1)/2;cur=normal[sl][:,:,:3];cur[mask]=(cur*(1-weights)+encoded*weights)[mask]
        cur=ao[sl][:,:,:3];value=(.58+.22*fissure-.15*depth)[...,None]
        cur[mask]=(cur*(1-weights)+value*weights)[mask];painted+=int(mask.sum())
    # The complete rebar charts and their gutters are steel-coloured, avoiding
    # the pale edge contamination that made R2 appear grey/tan in close views.
    mask=bar_mask.copy()
    for _ in range(16):
        for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
            valid=np.roll(mask,shift,axis)&~mask;valid[574:,:]=False
            for array in arrays:array[valid]=np.roll(array,shift,axis)[valid]
            mask[valid]=True
    for end,array in zip(['Tint','BakeNormal','BakeAO'],arrays):bake.save_map(NAME+'_'+end,array)
    values=tint[:,:,:3][bar_mask]
    return {'spall_band_nominal_along_slab_m':[.20,.70],'spall_band_painted_texels':painted,'steel_tint_data_min':values.min(axis=0).tolist(),'steel_tint_data_max':values.max(axis=0).tolist(),'rebar_atlas_shelf_y':[0,.28],'rebar_gutter_dilation_steps':16,'roughness':'unchanged scalar; aggregate object-normal/AO paint changes surface response'}
def transfer_master_tint(obj,high,base_faces=984,high_faces=2760):
    from mathutils.bvhtree import BVHTree
    from mathutils.geometry import barycentric_transform
    obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles)
    tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
    layer=high.data.uv_layers.get('BakeUV') or high.data.uv_layers.new(name='BakeUV')
    for poly in high.data.polygons:
        if poly.index>=high_faces:
            base=obj.data.polygons[base_faces+poly.index-high_faces]
            for li,bi in zip(poly.loop_indices,base.loop_indices):layer.data[li].uv=obj.data.uv_layers['BakeUV'].data[bi].uv
            continue
        point,n,idx,dist=tree.find_nearest(poly.center);tri=tris[idx]
        for li in poly.loop_indices:
            co=high.data.vertices[high.data.loops[li].vertex_index].co
            uv=barycentric_transform(co,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*obj.data.uv_layers['BakeUV'].data[i].uv,0)) for i in tri.loops]);layer.data[li].uv=uv.xy
    high.data.uv_layers.active_index=0
def master_uv_fix():
    import village_431_housea as h
    import village_431_round4_qa as qa
    checkpoint=OUT/'checkpoints/master_uv_before';checkpoint.mkdir(parents=True,exist_ok=True)
    for file in ['Bridge_baked.blend',NAME+'_MASTER.fbx']:
        if not (checkpoint/file).exists():shutil.copy2(OUT/'source'/file,checkpoint/file)
    bpy.ops.wm.open_mainfile(filepath=str(checkpoint/'Bridge_baked.blend'))
    for col in bpy.data.collections:col.hide_viewport=False
    high=bpy.data.objects[NAME+'_MASTER'];obj=bpy.data.objects[NAME];high.hide_set(False);obj.hide_set(False)
    before=signature(high);transfer_master_tint(obj,high);assert signature(high)==before
    materials=list(high.data.materials);indices=[poly.material_index for poly in high.data.polygons];high.data.materials.clear()
    for slot in ['Brick','Roof','Trim','Rubble']:high.data.materials.append(bpy.data.materials.get(slot) or bpy.data.materials.new(slot))
    for poly,index in zip(high.data.polygons,indices):poly.material_index=index
    h.export([high],OUT/'source'/(NAME+'_MASTER.fbx'))
    high.data.materials.clear()
    for mat in materials:high.data.materials.append(mat)
    for poly,index in zip(high.data.polygons,indices):poly.material_index=index
    assert signature(high)==before
    for image in bpy.data.images:
        if image.source=='FILE':image.filepath='//../textures/'+Path(image.filepath).name
    high.hide_set(True);select(obj);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/Bridge_baked.blend'),relative_remap=False)
    records=json.loads((OUT/'evidence/source_qa.json').read_text());records[NAME+'_MASTER'].update(qa.inspect(high))
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(OUT/'source'/(NAME+'_MASTER.fbx')))
    target=next(o for o in bpy.data.objects if o.type=='MESH');records[NAME+'_MASTER']['fbx']=qa.inspect(target)
    (OUT/'evidence/source_qa.json').write_text(json.dumps(records,indent=2));print('MASTER UV FIX EXPORTED; GEOMETRY UNCHANGED',flush=True)
def audit():
    import village_431_round4_qa as qa
    load();original={}
    for o in bpy.data.objects:
        if o.name in [NAME,NAME+'_LOD1',NAME+'_MASTER'] or o.name.startswith('UCX_'+NAME+'_'):
            original[o.name]={'vertices':[tuple(v.co) for v in o.data.vertices],'faces':[(tuple(p.vertices),p.material_index) for p in o.data.polygons],'matrix':[list(row) for row in o.matrix_world]}
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/Bridge_baked.blend'));result={'meshes':{},'source_images':[]}
    for name,prior in original.items():
        o=bpy.data.objects[name];verts=[tuple(v.co) for v in o.data.vertices];faces=[(tuple(p.vertices),p.material_index) for p in o.data.polygons]
        normal_faces=[(v,5 if mat==3 and len(o.data.materials)==4 else mat) for v,mat in faces]
        assert verts[:len(prior['vertices'])]==prior['vertices'] and normal_faces[:len(prior['faces'])]==prior['faces']
        assert [list(row) for row in o.matrix_world]==prior['matrix']
        if name.startswith('UCX_'):assert len(verts)==len(prior['vertices']) and len(faces)==len(prior['faces']);continue
        r=qa.inspect(o);r['inherited_geometry_exactly_unchanged']=True
        r['minimum_triangle_altitude_m']=min((lambda a,b,c:(b-a).cross(c-a).length/max((b-a).length,(c-a).length,(c-b).length))(*[o.data.vertices[i].co for i in t.vertices]) for t in o.data.loop_triangles)
        if not name.endswith('_MASTER'):assert r['minimum_triangle_altitude_m']>.009
        result['meshes'][name]=r
    for image in bpy.data.images:
        if image.source=='FILE' and image.users:
            path=Path(bpy.path.abspath(image.filepath)).resolve();assert path.exists() and path.is_relative_to(OUT/'textures'),(image.name,str(path))
            result['source_images'].append(str(path.relative_to(OUT)))
    obj=bpy.data.objects[NAME];lod=bpy.data.objects[NAME+'_LOD1']
    from mathutils.bvhtree import BVHTree
    from mathutils.geometry import barycentric_transform
    obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles)
    tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
    # The accepted LOD1 has a different triangulation. Compare the persisted
    # atlas coordinates on the shared surfaces rather than face-index identity.
    errors=[];bar_errors=[];misses=0;worst=[]
    def face_key(mesh,poly):
        return (tuple(sorted(tuple(round(x,5) for x in mesh.vertices[i].co) for i in poly.vertices)),tuple(round(x,4) for x in poly.normal))
    new_charts={face_key(obj.data,poly):{tuple(round(x,5) for x in obj.data.vertices[obj.data.loops[li].vertex_index].co):tuple(obj.data.uv_layers['BakeUV'].data[li].uv) for li in poly.loop_indices} for poly in obj.data.polygons if poly.index>=984}
    for poly in lod.data.polygons:
        if poly.index>=984:
            chart=new_charts[face_key(lod.data,poly)]
            for li in poly.loop_indices:
                key=tuple(round(x,5) for x in lod.data.vertices[lod.data.loops[li].vertex_index].co)
                bar_errors.append(math.dist(chart[key],lod.data.uv_layers['BakeUV'].data[li].uv))
            continue
        hit,n,idx,dist=tree.ray_cast(poly.center+poly.normal*.002,-poly.normal,.004)
        if hit is None:misses+=1;continue
        tri=tris[idx];uv=obj.data.uv_layers['BakeUV'].data
        expected=barycentric_transform(hit,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*uv[i].uv,0)) for i in tri.loops])
        luv=lod.data.uv_layers['BakeUV'].data;actual=sum((Vector((*luv[i].uv,0)) for i in poly.loop_indices),Vector())/len(poly.loop_indices)
        err=(expected-actual).length;errors.append(err)
        if err>.001:worst.append((poly.index,err,tuple(poly.center),tri.polygon_index))
    error=max(errors);print('UV_AUDIT',error,'newbar',max(bar_errors),'worst',worst[:10],flush=True)
    assert max(bar_errors)<1e-4,(max(bar_errors),misses)
    result['LOD1_BakeUV_max_difference']=error;result['new_rebar_LOD1_BakeUV_max_difference']=max(bar_errors);result['LOD1_surface_samples']=len(errors);result['LOD1_surface_ray_misses']=misses;result['collision_hulls']=2
    protected=json.loads((OUT/'evidence/protected_before.json').read_text());assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in protected.items())
    result['protected_files_unchanged']=len(protected)
    image=bpy.data.images.load(str(OUT/'textures'/(NAME+'_Tint.png')),check_existing=False);image.colorspace_settings.name='Non-Color'
    import numpy as np
    pixels=np.array(image.pixels[:],np.float32).reshape(2048,2048,4);samples=[];normals=[];uv=obj.data.uv_layers['BakeUV'].data
    for poly in obj.data.polygons:
        if poly.index<984:continue
        centre=sum((uv[i].uv for i in poly.loop_indices),Vector((0,0)))/len(poly.loop_indices)
        samples.append(pixels[min(2047,int(centre.y*2048)),min(2047,int(centre.x*2048)),:3]);normals.append(poly.normal.z)
    samples=np.array(samples);assert samples.max()<.18,(samples.min(),samples.max());assert min(normals)>0,min(normals)
    result['actual_rebar_atlas_centroid_samples']={'count':len(samples),'min_rgb':samples.min(axis=0).tolist(),'max_rgb':samples.max(axis=0).tolist(),'minimum_facet_normal_z':min(normals)}
    records=json.loads((OUT/'evidence/source_qa.json').read_text())
    for name,rec in result['meshes'].items():
        for field in ['triangles','slots','bad_geo','bad_uv','bad_tangents','uv_channels']:records[name][field]=rec[field]
    (OUT/'evidence/source_qa.json').write_text(json.dumps(records,indent=2))
    (OUT/'evidence/independent_audit.json').write_text(json.dumps(result,indent=2));print('AUDIT PASS',flush=True)
def fixpaths():
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/Bridge_baked.blend'))
    # Match the FBX's four slots and drop other-state fake-user datablocks.
    for obj in bpy.data.objects:
        if obj.type!='MESH' or obj.name.startswith('UCX_'):continue
        if len(obj.data.materials)==6:
            mats=list(obj.data.materials);indices=[p.material_index for p in obj.data.polygons];obj.data.materials.clear()
            for i in [0,1,2,5]:obj.data.materials.append(mats[i])
            for poly,i in zip(obj.data.polygons,indices):poly.material_index={0:0,1:1,2:2,5:3}[i]
    live_mats={m for o in bpy.data.objects if o.type=='MESH' for m in o.data.materials}
    for mat in list(bpy.data.materials):
        if mat not in live_mats:bpy.data.materials.remove(mat)
    live_images={n.image for m in live_mats if m and m.use_nodes for n in m.node_tree.nodes if n.type=='TEX_IMAGE' and n.image}
    for image in list(bpy.data.images):
        if image not in live_images:bpy.data.images.remove(image)
    for image in bpy.data.images:
        if image.source=='FILE':
            dest=OUT/'textures'/Path(image.filepath).name
            assert dest.exists(),str(dest)
            image.filepath='//../textures/'+dest.name
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/Bridge_baked.blend'),relative_remap=False)
if __name__=='__main__':
    if '--build' in sys.argv:build()
    elif '--audit' in sys.argv:audit()
    elif '--fixpaths' in sys.argv:fixpaths()
    elif '--master-uv' in sys.argv:master_uv_fix()
    else:inspect()


