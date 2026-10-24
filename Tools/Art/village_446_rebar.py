"""Non-destructive issue446 R1 fork of accepted source. Never rebuilds issue431."""
import sys,json,math,hashlib,shutil
from pathlib import Path
sys.dont_write_bytecode=True
import bpy,bmesh
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round1';FINAL=ROOT/'Art/Shared/village/final';NAME='SM_Bridge_Span_Destroyed'
sys.path.insert(0,str(ROOT/'Tools/Art'))
def load():
    bpy.ops.wm.open_mainfile(filepath=str(FINAL/'source/Bridge_baked.blend'))
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for o in bpy.data.objects:o.hide_set(False)
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
def bars_mesh():
    vs=[];fs=[];paths=[]
    def rod(points):
        # Two connected triangular-prism segments: 14 triangles, closed caps.
        points=[Vector(p) for p in points];rings=[]
        length=sum((b-a).length for a,b in zip(points,points[1:]))
        if length>1.48:points=[points[0]+(p-points[0])*(1.48/length) for p in points]
        tangent=(points[-1]-points[0]).normalized();u=tangent.cross(Vector((0,0,1))).normalized();w=tangent.cross(u).normalized()
        for p in points:
            ring=[]
            for k in range(3):
                a=math.tau*k/3+.3;ring.append(len(vs));vs.append(tuple(p+.03*(math.cos(a)*u+math.sin(a)*w)))
            rings.append(ring)
        fs.append(tuple(reversed(rings[0])));fs.append(tuple(rings[-1]))
        for a,b in zip(rings,rings[1:]):
            for i in range(3):j=(i+1)%3;fs.extend([(a[i],a[j],b[j]),(a[i],b[j],b[i])])
        paths.append([tuple(p) for p in points])
    for end in [-1,1]:
        # Peel the mat from above the waterline toward the break. Its final
        # segment turns down/inwards in the direction of the slab's fall.
        for j in range(9):
            fan=j-4;y=fan*.40;dx=(j%3)*.045;rise=.035*(j%3)
            rod([(end*(7.55+dx),y,-5.49+dx*.72),(end*(6.96+dx),y+fan*.06,-5.20+rise),(end*(6.39+dx),y+fan*.12,-5.58+rise)])
        # Mat torn out along both side faces where the slab is above water.
        for side in [-1,1]:
            for j in range(6):
                x=7.25+j*.32;z=-(15-x)*math.tan(math.radians(36))-.13
                reach=.79+.05*(j%3)
                rod([(end*x,side*6.96,z+.75),(end*(x-.30),side*(7.0+reach*.62),z+.85),(end*(x-.61-.05*(j%2)),side*(7.0+reach),z+.33-.06*(j%3))])
    mesh=bpy.data.meshes.new('R446_42_bent_bars');mesh.from_pydata(vs,[],fs);mesh.update()
    obj=bpy.data.objects.new('R446_bars',mesh);bpy.context.collection.objects.link(obj)
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    # Stable non-degenerate UV0 per triangle; scan is disabled for Trim.
    uv=mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        for i,li in enumerate(p.loop_indices):uv.data[li].uv=[(0,0),(1,0),(0,1)][i]
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
    (OUT/'evidence/protected_before.json').write_text(json.dumps(protected,indent=2))
    load();obj=bpy.data.objects[NAME];lod=bpy.data.objects[NAME+'_LOD1'];high=bpy.data.objects[NAME+'_MASTER'];hulls=[o for o in bpy.data.objects if o.name.startswith('UCX_'+NAME+'_')]
    keep=[obj,lod,high]+hulls
    for o in list(bpy.data.objects):
        if o not in keep:bpy.data.objects.remove(o,do_unlink=True)
    hull_sig={o.name:signature(o) for o in hulls};counts={o.name:len(o.data.polygons) for o in [obj,lod,high]}
    inherited={o.name:signature(o) for o in [obj,lod,high]}
    bars,paths=bars_mesh();bar_count=len(bars.data.polygons);assert bar_count==588
    for target in [obj,lod,high]:
        oldv=[tuple(v.co) for v in target.data.vertices];oldf=[(tuple(p.vertices),p.material_index) for p in target.data.polygons]
        bm=bmesh.new();bm.from_mesh(target.data);n=len(bm.faces);bm.from_mesh(bars.data)
        bm.faces.ensure_lookup_table()
        for face in list(bm.faces)[n:]:face.material_index=2
        bm.to_mesh(target.data);bm.free();target.data.update()
        assert [tuple(v.co) for v in target.data.vertices][:len(oldv)]==oldv
        assert [(tuple(p.vertices),p.material_index) for p in target.data.polygons][:len(oldf)]==oldf
    bpy.data.objects.remove(bars,do_unlink=True)
    # Repack this mesh only, then rebake from its retained chamfered master.
    select(obj)
    if obj.data.uv_layers.get('BakeUV'):obj.data.uv_layers.remove(obj.data.uv_layers['BakeUV'])
    obj.data.uv_layers.new(name='BakeUV');obj.data.uv_layers.active_index=1
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.0015,scale_to_bounds=True);bpy.ops.object.mode_set(mode='OBJECT');obj.data.uv_layers.active_index=0
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
    # Only new Trim texels receive a restrained warm rust edge. SurfaceTint stays
    # exactly (.10,.075,.055); all inherited atlas colouring follows round4.
    tint=bpy.data.images.load(str(OUT/'textures'/(NAME+'_Tint.png')),check_existing=False);tint.colorspace_settings.name='Non-Color';arr=np.array(tint.pixels[:],np.float32).reshape(2048,2048,4)
    for tri,sl,inside,points in bake.raster(obj):
        if tri.polygon_index<counts[NAME]:continue
        edge=.75 if tri.normal.z>.1 else .48
        arr[sl][:,:,:3][inside]=np.array([edge,edge*.91,edge*.79])
    bake.save_map(NAME+'_Tint',arr)
    # Match authored LOD1 UV1 via surface correspondence, using the same bake.
    obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles);tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
    layer=lod.data.uv_layers.get('BakeUV') or lod.data.uv_layers.new(name='BakeUV')
    for poly in lod.data.polygons:
        centre=poly.center;point,n,idx,dist=tree.find_nearest(centre);tri=tris[idx]
        for li in poly.loop_indices:
            co=lod.data.vertices[lod.data.loops[li].vertex_index].co
            value=barycentric_transform(co,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*obj.data.uv_layers['BakeUV'].data[i].uv,0)) for i in tri.loops]);layer.data[li].uv=value.xy
    lod.data.uv_layers.active_index=0
    report={}
    for target in [obj,lod,high]:
        report[target.name]=qa.inspect(target);report[target.name]['baseline_triangles']=counts[target.name];report[target.name]['added_triangles']=588
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
    (OUT/'evidence/build.json').write_text(json.dumps({'meshes':report,'bar_paths_m':paths,'bar_diameter_m':.06,'bar_prism_sides':3,'collision_unchanged':hull_sig,'inherited_geometry_unchanged':inherited,'bake':receipt},indent=2))
    fixpaths()
    # Fresh import validates exported geometry, UVs and tangents independently.
    for suffix,folder in [('', 'fbx'),('_LOD1','fbx'),('_MASTER','source')]:
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(OUT/folder/(NAME+suffix+'.fbx')))
        target=next(o for o in bpy.data.objects if o.type=='MESH' and not o.name.startswith('UCX_'));report[NAME+suffix]['fbx']=qa.inspect(target)
        assert report[NAME+suffix]['fbx']['triangles']==report[NAME+suffix]['triangles']
    (OUT/'evidence/source_qa.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
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
    for poly in lod.data.polygons:
        hit,n,idx,dist=tree.ray_cast(poly.center+poly.normal*.002,-poly.normal,.004)
        if hit is None:misses+=1;continue
        tri=tris[idx];uv=obj.data.uv_layers['BakeUV'].data
        expected=barycentric_transform(hit,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*uv[i].uv,0)) for i in tri.loops])
        luv=lod.data.uv_layers['BakeUV'].data;actual=sum((Vector((*luv[i].uv,0)) for i in poly.loop_indices),Vector())/len(poly.loop_indices)
        err=(expected-actual).length;errors.append(err)
        if poly.index>=984:bar_errors.append(err)
        if err>.001:worst.append((poly.index,err,tuple(poly.center),tri.polygon_index))
    error=max(errors);print('UV_AUDIT',error,'newbar',max(bar_errors),'worst',worst[:10],flush=True)
    assert max(bar_errors)<1e-4,(max(bar_errors),misses)
    result['LOD1_BakeUV_max_difference']=error;result['new_rebar_LOD1_BakeUV_max_difference']=max(bar_errors);result['LOD1_surface_samples']=len(errors);result['LOD1_surface_ray_misses']=misses;result['collision_hulls']=2
    protected=json.loads((OUT/'evidence/protected_before.json').read_text());assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in protected.items())
    result['protected_files_unchanged']=len(protected)
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
    else:inspect()
