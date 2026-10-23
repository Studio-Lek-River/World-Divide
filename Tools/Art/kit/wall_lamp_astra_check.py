"""Independent Round 4 art/export checks; writes only assigned fixture evidence."""
import bpy,bmesh,sys,json,math,hashlib,struct
from pathlib import Path
from mathutils import Vector
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import wall_lamp_render as r

def digest(o):
    m=o.data;h=hashlib.sha256()
    for v in m.vertices:h.update(struct.pack('<3f',*v.co))
    for f in m.polygons:
        h.update(struct.pack('<2I',f.material_index,len(f.vertices)))
        for v in f.vertices:h.update(struct.pack('<I',v))
        h.update(bytes([f.use_smooth]))
    for uv in m.uv_layers:
        h.update(uv.name.encode())
        for d in uv.data:h.update(struct.pack('<2f',*d.uv))
    return h.hexdigest()

def geometry(o):
    m=o.data;m.calc_loop_triangles();m.calc_tangents(uvmap='UVMap')
    bad=[];tangent_error=0.;normal_error=0.
    for i,l in enumerate(m.loops):
        n=l.normal;t=l.tangent
        if not all(math.isfinite(v) for v in (*n,*t,l.bitangent_sign)):bad.append(i)
        normal_error=max(normal_error,abs(n.length-1))
        tangent_error=max(tangent_error,abs(t.length-1),abs(n.dot(t)))
    assert not bad and normal_error<1e-4 and tangent_error<1e-4,(o.name,bad,normal_error,tangent_error)
    charts=json.loads(o['chart_layout']);invalid=[];axis_error=0.
    for f in m.polygons:
        ci=m.attributes['atlas_chart'].data[f.index].value;c=charts[ci]
        for li in f.loop_indices:
            q=[]
            for ch,pre in [(0,''),(1,'finish_')]:
                q.append((m.uv_layers[ch].data[li].uv-Vector(c[pre+'offset']))/c[pre+'scale'])
            axis_error=max(axis_error,(q[0]-q[1]).length)
            if any(q[0][i]<-2e-6 or q[0][i]>c['size'][i]+2e-6 for i in (0,1)):invalid.append(f.index)
    assert not invalid and axis_error<2e-6,(o.name,invalid,axis_error)
    # Closed components must have positive signed volume. Open fin roots/covers are intentional.
    parent=list(range(len(m.vertices)))
    def find(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    for edge in m.edges:
        x,y=[find(i) for i in edge.vertices];parent[x]=y
    components={}
    for face in m.polygons:components.setdefault(find(face.vertices[0]),[]).append(face)
    closed=opened=0;negative=[]
    for faces in components.values():
        edges={};volume=0
        for face in faces:
            vs=list(face.vertices)
            for x,y in zip(vs,vs[1:]+vs[:1]):
                key=tuple(sorted((x,y)));edges[key]=edges.get(key,0)+1
            x,y,z=[m.vertices[i].co for i in vs];volume+=x.dot(y.cross(z))/6
        if all(n==2 for n in edges.values()):
            closed+=1
            if volume < -1e-12:negative.append(volume)
        else:opened+=1
    assert not negative,('inside-out closed components',o.name,negative)
    return dict(closed_components=closed,intentionally_open_components=opened,negative_closed_volumes=negative,digest=digest(o),triangles=len(m.loop_triangles),unit_normal_max_error=normal_error,orthonormal_tangent_max_error=tangent_error,uv0_uv1_physical_chart_max_error_m=axis_error,charts=len(charts),smooth_triangles=sum(f.use_smooth for f in m.polygons),hard_triangles=sum(not f.use_smooth for f in m.polygons))

def outline_delta(a,b,lens_only=False,distance=150):
    from mathutils.geometry import convex_hull_2d
    view=Vector((.9,-1.2,.85)).normalized();right=view.cross(Vector((0,0,1))).normalized();up=right.cross(view).normalized()
    centre=Vector((0,0,0));focal=1920/(2*math.tan(math.radians(30)))
    def hull(obj):
        ids={v for face in obj.data.polygons if not lens_only or face.material_index==1 for v in face.vertices}
        coords=[]
        for i in ids:
            v=obj.data.vertices[i].co-centre;depth=distance-v.dot(view)
            coords.append(Vector((v.dot(right)*focal/depth,v.dot(up)*focal/depth)))
        return [coords[i] for i in convex_hull_2d(coords)]
    x,y=hull(a),hull(b)
    def deviation(points,poly):
        farthest=0
        for p in points:
            nearest=1e9
            for a,b in zip(poly,poly[1:]+poly[:1]):
                d=b-a;s=max(0,min(1,(p-a).dot(d)/d.length_squared)) if d.length_squared else 0
                nearest=min(nearest,(p-(a+s*d)).length)
            farthest=max(farthest,nearest)
        return farthest
    return max(deviation(x,y),deviation(y,x))

def check(piece):
    name=piece['id'].split('/')[-1];out=r.KIT/name;repeats=[]
    for index in range(2):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        a,b=r.load_module(piece).build(piece)
        assert [m.name for m in a.data.materials]==[m.name for m in b.data.materials]==['Housing','Lens']
        repeats.append([digest(a),digest(b)])
    assert repeats[0]==repeats[1],name
    bpy.ops.wm.open_mainfile(filepath=str(out/'source'/(name.split('_',1)[1]+'.blend')))
    a=bpy.data.objects[piece['mesh']];b=bpy.data.objects[piece['mesh']+'_LOD1'];records=[geometry(o) for o in (a,b)]
    assert [rec['digest'] for rec in records]==repeats[0],('source/generator mismatch',name)
    outlines={str(d):{kind:outline_delta(a,b,lens_only=kind=='lens',distance=d) for kind in ('fixture','lens')} for d in (150,260)}
    assert max(v for sizes in outlines.values() for v in sizes.values())<.1,('outline change over 0.1 pixel',name,outlines)
    recipe=json.loads((out/'material_recipe.json').read_text())
    mats={};normals={}
    for slot,surface in enumerate(('Housing','Lens')):
        nt=bpy.data.materials[surface].node_tree;ims=[n for n in nt.nodes if n.type=='TEX_IMAGE']
        binding=[]
        for n in ims:
            assert n.image and n.image.packed_file
            expected='sRGB' if 'BaseColor' in n.image.name else 'Non-Color'
            assert n.image.colorspace_settings.name==expected
            uv=n.inputs['Vector'].links[0].from_node.uv_map
            assert uv==('FinishBake' if 'Normal' in n.image.name else 'UVMap')
            binding.append(dict(image=n.image.name,colorspace=expected,uv=uv,size=list(n.image.size)))
        normal=next(n for n in ims if 'Finish_Normal' in n.image.name).image
        pixels=list(normal.pixels);w,h=normal.size
        samples=[]
        for obj in (a,b):
            values=[]
            for f in obj.data.polygons:
                if f.material_index!=slot:continue
                uv=sum((obj.data.uv_layers[1].data[i].uv for i in f.loop_indices),Vector((0,0)))/len(f.loop_indices)
                x=min(w-1,max(0,int(uv.x*w)));y=min(h-1,max(0,int(uv.y*h)));p=pixels[(y*w+x)*4:(y*w+x)*4+3]
                values.append(p)
            assert all(p[2]>.48 for p in values),('unbaked normals',name,surface,obj.name,min(p[2] for p in values))
            samples.append(dict(lod=obj.name,triangle_centres_sampled=len(values),minimum_blue=min(p[2] for p in values)))
        normals[surface]=samples;mats[surface]=binding
    principled=next(n for n in bpy.data.materials['Lens'].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    actual=list(principled.inputs['Emission Color'].default_value)[:3]
    assert max(abs(x-y) for x,y in zip(actual,recipe['lens']['display_tint']))<1e-6
    lens=[f for f in a.data.polygons if f.material_index==1]
    direction=None
    if piece['light']['type']!='Point':
        pitch=math.radians(piece['light']['rotation'][0]);direction=Vector((math.cos(pitch),0,math.sin(pitch)))
        front=max(lens,key=lambda f:f.area if f.normal.dot(direction)>.99 else -1)
        assert front.normal.dot(direction)>.999,('lens aim',name,list(front.normal))
    cage_clearance=None
    if 'CageWallLamp' in piece['mesh']:
        from mathutils.bvhtree import BVHTree
        cage_clearance={}
        for obj in (a,b):
            cs=json.loads(obj['chart_layout']);tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[f.vertices for f in obj.data.polygons],all_triangles=True)
            tests=[]
            for y,z in [(0,0)]+[(y,z) for z in (-.05,.05) for y in (-.02,-.01,.01,.02)]:
                loc,normal,index,depth=tree.ray_cast(Vector((1,y,z)),Vector((-1,0,0)))
                kind=cs[obj.data.attributes['atlas_chart'].data[index].value]['kind']
                assert kind=='castbar',('cage bar buried in globe',obj.name,y,z,kind)
                tests.append(dict(y=y,z=z,visible_surface=kind,x=loc.x))
            for face in obj.data.polygons:
                if face.material_index!=1 or abs(face.center.x-.038)<1e-6:continue
                c=face.center;outward=Vector((c.x-.038,c.y,c.z-max(-.060,min(.060,c.z))))
                assert face.normal.dot(outward)>0,('inward globe',obj.name,face.index)
            cage_clearance[obj.name]=tests
    # Verify allowed shared charts vertex-by-vertex before excluding their duplicate occupancy.
    aliases=[];ac=json.loads(a['chart_layout']);bc=json.loads(b['chart_layout'])
    for ci,c in enumerate(bc):
        if not c.get('shared_lod_alias'):continue
        ai=next(i for i,q in enumerate(ac) if q.get('shared_lod_key')==c['shared_lod_key'])
        lookup={}
        for f in a.data.polygons:
            if a.data.attributes['atlas_chart'].data[f.index].value!=ai:continue
            for li in f.loop_indices:
                key=tuple(round(v,7) for v in a.data.vertices[a.data.loops[li].vertex_index].co)
                lookup[key]=[a.data.uv_layers[ch].data[li].uv.copy() for ch in (0,1)]
        error=0.;count=0
        for f in b.data.polygons:
            if b.data.attributes['atlas_chart'].data[f.index].value!=ci:continue
            count+=1
            for li in f.loop_indices:
                key=tuple(round(v,7) for v in b.data.vertices[b.data.loops[li].vertex_index].co)
                assert key in lookup,('shared chart geometry mismatch',c['shared_lod_key'])
                error=max(error,max((b.data.uv_layers[ch].data[li].uv-lookup[key][ch]).length for ch in (0,1)))
        assert error<2e-7,('shared plate UV mismatch',error)
        aliases.append(dict(key=c['shared_lod_key'],triangles=count,max_uv0_uv1_error=error))
    # Join unique chart occupancy only; per-LOD checks still forbid all overlap.
    copies=[]
    for o in (a,b):
        q=o.copy();q.data=o.data.copy();bpy.context.collection.objects.link(q);q.hide_set(False);copies.append(q)
        if o==b and aliases:
            bm=bmesh.new();bm.from_mesh(q.data);layer=bm.faces.layers.int['atlas_chart']
            bmesh.ops.delete(bm,geom=[f for f in bm.faces if bc[f[layer]].get('shared_lod_alias')],context='FACES')
            bm.to_mesh(q.data);bm.free();q.data.update()
    bpy.ops.object.select_all(action='DESELECT')
    for q in copies:q.select_set(True)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();merged=bpy.context.object
    shared=[r.uv_overlap(merged,ch) for ch in (0,1)]
    assert all(v['overlapping_triangle_pairs']==0 for v in shared),shared
    result=dict(shared_lod_plate_charts=aliases,cage_bar_visibility_rays=cage_clearance,projected_convex_outline_hausdorff_px=outlines,outline_method='Projected convex hulls at 1920x1080 / horizontal FOV60 / fixed RTS view; image pairs additionally inspect internal detail',round=4,scope='Independent Blender source/generator inspection; not Unreal acceptance',factory_reset_repeat_digests=repeats,source_equals_generator=True,lods=records,material_bindings=mats,final_normal_bake_coverage=normals,shared_two_lod_atlas_overlap=shared,emission_tint_linear=actual,kelvin=piece['light']['kelvin'],lens_normal_dot_light_direction=None if direction is None else front.normal.dot(direction),light_direction=None if direction is None else list(direction),approval='Producer approval pending')
    (out/'evidence/astra_technical.json').write_text(json.dumps(result,indent=2)+'\n')
    print('ASTRA TECHNICAL PASS',name,flush=True)


def visual_metrics(piece):
    import numpy as np
    from wall_lamp_materials import save_map
    out=r.KIT/piece['id'].split('/')[-1];records={}
    def read(path):
        im=bpy.data.images.load(str(path),check_existing=False);im.colorspace_settings.name='Non-Color'
        pixels=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(pixels)
        return pixels.reshape((im.size[1],im.size[0],4))[:,:,:3]
    for distance in (12,150,260,450):
        prefix='switch' if distance==260 else 'distance'
        if (out/'review'/f'stable_{distance}m_lod0.png').exists():prefix='stable'
        a,b=[read(out/'review'/f'{prefix}_{distance}m_lod{lod}.png') for lod in (0,1)]
        diff=np.max(np.abs(a-b),axis=2)
        verification=json.loads((out/'evidence/verification.json').read_text());bounds=verification['lod0']['bounds_m']
        diameter=math.sqrt(sum((hi-lo)**2 for lo,hi in zip(*bounds)))
        roi_radius=math.ceil(diameter*.5*1920/(2*math.tan(math.radians(30)))/distance)+4
        mask=np.zeros(diff.shape,dtype=bool);cy,cx=diff.shape[0]//2,diff.shape[1]//2
        mask[cy-roi_radius:cy+roi_radius,cx-roi_radius:cx+roi_radius]=True
        records[str(distance)]=dict(camera_distance_m=distance,source_prefix=prefix,frame=[1920,1080],horizontal_fov_degrees=60,comparison_roi_pixels=int(mask.sum()),roi_radius_pixels=roi_radius,changed_pixels_gt_1_of_255=int((diff>1/255+.0001).sum()),max_display_delta=float(diff.max()),mean_delta_over_comparison_roi=float(diff[mask].mean()) if mask.any() else 0)
        radius=128 if distance==12 else 24;scale=2 if distance==12 else 8;h,w=a.shape[:2]
        crop=np.s_[h//2-radius:h//2+radius,w//2-radius:w//2+radius]
        aa=np.repeat(np.repeat(a[crop],scale,axis=0),scale,axis=1);bb=np.repeat(np.repeat(b[crop],scale,axis=0),scale,axis=1)
        canvas=np.full((aa.shape[0],aa.shape[1]*2+16,3),.055,dtype=np.float32);canvas[:,:aa.shape[1]]=aa;canvas[:,aa.shape[1]+16:]=bb
        save_map(out/'review'/f'astra_{distance}m_LOD0_left_LOD1_right.png',canvas)
    (out/'evidence/astra_distance_comparison.json').write_text(json.dumps(dict(scope='Blender fixture-centre distances; both LODs forced for comparison, no runtime or master approval claim',records=records),indent=2)+'\n')
    print('ASTRA DISTANCE',piece['id'],records,flush=True)


def stable_distance(piece):
    if 'RoofFloodLight' not in piece['mesh']:return
    out=r.KIT/piece['id'].split('/')[-1]
    bpy.ops.wm.open_mainfile(filepath=str(out/'source/RoofFloodLight_Preview.blend'))
    scene=bpy.context.scene;r.gpu();scene.cycles.samples=512;scene.cycles.use_adaptive_sampling=False;scene.cycles.use_denoising=False;scene.cycles.seed=392
    for q in scene.objects:
        if q.name.startswith('REVIEW'):
            if q.type=='MESH' or 'fixture light' in q.name or 'daylight sun' in q.name:q.hide_render=True
    bpy.data.objects['REVIEW neutral key'].data.energy=90
    bpy.data.objects['REVIEW cool rim'].data.energy=40
    bg=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs['Color'].default_value=(.24,.30,.42,1);bg.inputs['Strength'].default_value=.3
    bpy.data.materials['Lens'].node_tree.nodes['LampLevel'].outputs[0].default_value=1
    a=bpy.data.objects[piece['mesh']];b=bpy.data.objects[piece['mesh']+'_LOD1'];target=Vector((0,0,.39));cam=scene.camera
    scene.render.resolution_x=1920;scene.render.resolution_y=1080;scene.render.resolution_percentage=100;cam.data.type='PERSP';cam.data.lens=cam.data.sensor_width/(2*math.tan(math.radians(30)))
    # Same native pixel scale; limit work to a 128x128 centre region. Outside it is unrendered.
    scene.render.use_border=True;scene.render.use_crop_to_border=False
    scene.render.border_min_x=.5-64/1920;scene.render.border_max_x=.5+64/1920
    scene.render.border_min_y=.5-64/1080;scene.render.border_max_y=.5+64/1080
    for distance in (150,260):
        cam.location=target+Vector((.9,-1.2,.85)).normalized()*distance;r.aim(cam,target)
        for lod in (0,1):
            a.hide_render=lod!=0;b.hide_render=lod!=1
            scene.render.filepath=str(out/'review'/f'stable_{distance}m_lod{lod}.png');bpy.ops.render.render(write_still=True)
    (out/'evidence/stable_distance_camera.json').write_text(json.dumps(dict(samples=512,denoising=False,adaptive_sampling=False,seed=392,frame=[1920,1080],native_pixel_render_region=[896,476,1024,604],crop_to_border=False,horizontal_fov=60,distances=[150,260],purpose='Resolve potential subpixel shading differences without 32-sample denoiser instability; pixels outside the centre region are unrendered'),indent=2)+'\n')

if __name__=='__main__':
    action=stable_distance if 'stable' in sys.argv else (visual_metrics if 'visual' in sys.argv else check)
    for piece in r.PIECES():action(piece)
