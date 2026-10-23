"""#392 Blender-only preparation, Cycles review and FBX evidence.
Usage: blender --factory-startup -b --python Tools/Art/kit/wall_lamp_render.py -- prepare|verify|render [01_WallLamp ...]
Writes only the five issue folders. Never exports FBX: Tools/Art/kit_blender.py owns exports.
"""
import bpy,bmesh,json,sys,math,hashlib,importlib.util
from pathlib import Path
from mathutils import Vector
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from wall_lamp_materials import ROOT,KIT,make_textures,apply_pbr,bake_finish,temperature_rgb
SPEC=ROOT/'Art/Shared/kit/kit_catalogue.json'
# The #392 pieces: the catalogue's pieces with a finish of their own.
PIECES=lambda:[p for p in json.loads(SPEC.read_text())['pieces'] if 'finish' in p]


def gpu():
    try:
        pref=bpy.context.preferences.addons['cycles'].preferences;pref.compute_device_type='OPTIX';pref.get_devices()
        for d in pref.devices:d.use=d.type!='CPU'
        if any(d.use for d in pref.devices):bpy.context.scene.cycles.device='GPU'
        print('CYCLES DEVICES',[(d.name,d.use) for d in pref.devices],flush=True)
    except Exception as e:print('CPU fallback',str(e),flush=True)

def load_module(piece):
    names={'01_WallLamp':'wall_lamp','02_CageWallLamp':'cage_wall_lamp','03_EntranceLight':'entrance_light','05_DirectionalSpot':'directional_spot','06_RoofFloodLight':'roof_flood_light'}
    spec=importlib.util.spec_from_file_location('author_piece',Path(__file__).parent/(names[piece['id'].split('/')[-1]]+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def basic_report(o):
    import numpy as np
    me=o.data;me.calc_loop_triangles();world=[o.matrix_world@v.co for v in me.vertices]
    lens=[o.matrix_world@me.vertices[v].co for f in me.polygons if f.material_index==1 for v in f.vertices]
    def bounds(pts):return [[min(v[i] for v in pts) for i in range(3)],[max(v[i] for v in pts) for i in range(3)]]
    uv=me.uv_layers[0];uvbad=0;density={0:[],1:[]}
    for t in me.loop_triangles:
        a,b,c=[uv.data[i].uv for i in t.loops]
        uvarea=abs((b-a).cross(c-a))*.5
        if uvarea<1e-12:uvbad+=1
        slot=me.polygons[t.polygon_index].material_index
        if t.area>1e-12:density[slot].append(math.sqrt(uvarea/t.area)*(2048 if slot==0 else 1024))
    return dict(triangles=len(me.loop_triangles),vertices=len(me.vertices),slots=[s.material.name for s in o.material_slots],bounds_m=bounds(world),lens_bounds_m=bounds(lens),origin_m=list(o.location),texel_density_px_per_m={['Housing','Lens'][k]:dict(min=min(v),median=float(np.median(v)),max=max(v)) for k,v in density.items()},uv_channels=[l.name for l in me.uv_layers],degenerate_geometry=sum(t.area<1e-12 for t in me.loop_triangles),degenerate_uv=uvbad)

def uv_overlap(o,channel=0):
    # Exact convex clipping of UV triangles, with a broadphase grid. Shared edges have zero area.
    me=o.data;me.calc_loop_triangles();uv=me.uv_layers[channel];ts=[];cells={};overlaps=[];seen=set()
    def area(p):return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(p,p[1:]+p[:1])))*.5 if len(p)>2 else 0
    def clip(poly,tri):
        signed=sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(tri,tri[1:]+tri[:1]));sgn=1 if signed>0 else -1
        for a,b in zip(tri,tri[1:]+tri[:1]):
            old=poly;poly=[]
            if not old:break
            def side(p):return sgn*((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))
            for p,q in zip(old,old[1:]+old[:1]):
                sp,sq=side(p),side(q)
                if sp>=-1e-12:poly.append(p)
                if (sp<0)<(sq<0) and abs(sp-sq)>1e-15:
                    t=sp/(sp-sq);poly.append((p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])))
        return poly
    for i,t in enumerate(me.loop_triangles):
        p=[tuple(uv.data[j].uv) for j in t.loops];ts.append(p);slot=me.polygons[t.polygon_index].material_index
        lo=[min(v[a] for v in p) for a in (0,1)];hi=[max(v[a] for v in p) for a in (0,1)]
        for x in range(int(lo[0]*64),int(hi[0]*64)+1):
            for y in range(int(lo[1]*64),int(hi[1]*64)+1):
                key=(slot,x,y)
                for j in cells.get(key,[]):
                    pair=(j,i)
                    if pair in seen:continue
                    seen.add(pair)
                    if area(clip(p,ts[j]))>1e-10:overlaps.append(pair)
                cells.setdefault(key,[]).append(i)
    return dict(overlapping_triangle_pairs=len(overlaps),examples=overlaps[:8],method=f'UV{channel} exact triangle clipping per material atlas; positive-area threshold 1e-10')

def prepare(piece):
    bpy.ops.wm.read_factory_settings(use_empty=True);name=piece['mesh'].replace('SM_Kit_','');out=KIT/piece['id'].split('/')[-1]
    for p in ('source','textures','review','evidence'):(out/p).mkdir(exist_ok=True)
    a,b=load_module(piece).build(piece);bpy.context.scene.cycles.device='CPU'
    textures=make_textures(piece,(a,b),out);apply_pbr(piece,out);bake_finish(piece,(a,b),out);apply_pbr(piece,out,use_finish=True)
    for surface,res in [('Housing',2048),('Lens',1024)]:textures.append(dict(path=f'textures/T_{name}_{surface}_Finish_Normal.png',size=[res,res],slot=surface,map='Finish_Normal',srgb=False))
    recipe=dict(piece=piece['id'],round=4,slots=['Housing','Lens'],textures=textures,orm='R=occlusion, G=roughness, B=metallic',normal_format='DirectX',normal_uv='FinishBake (UV1), dense per-face finish charts; same axis orientation as UV0; baked in UV0 tangent basis',normal_binding='ONE final Finish_Normal per slot on UV1. Cast grain, pits, Torx/ribs/optics microdetail and bevel normals are baked together. No stacking; source/bake_inputs is authoring-only.',housing=dict(base='weathered graphite powder coat',roughness_mid=.48,metallic='0 painted; up to .90 on worn fasteners and .86 on exposed chips',bevel_radius_m=.001),lens=dict(emission_strength=7.0 if 'Cage' in name else (6.0 if 'Directional' in name else 5.0),emission_kelvin=piece['light']['kelvin'],profile='LightProfile times LampLevel times emission_strength',roughness='.18-.45 effective ORM roughness (generator clamps to .18); flood clear coat roughness .025',coat_weight=1.0 if 'RoofFlood' in name else .32,coat_roughness=.025 if 'RoofFlood' in name else .15,ior=1.49,subsurface_weight=.08,subsurface_scale_m=.0015),frame='metres; origin at mount back (roof: underside); +X forward; Z up',light=piece['light'],source='deterministic, locally authored geometry and procedural PBR; producer reference controls design',approval='not approved; round 4 independent check; producer approval pending')
    recipe['housing']['weathering']='Mostly bright silver exposed metal on edges/fasteners; localized oxidation at fasteners, lower edges and gravity-directed drips. Cage has the strongest local bloom.'
    recipe['lod_textures']='Both LODs occupy disjoint charts in the same seven maps; both are painted and independently normal-baked.'
    recipe['lens']['tint_basis']='Producer-matched neutral-white display tint for the specified 4000 K floodlight; LightProfile is achromatic.' if 'RoofFlood' in name else 'Normalized linear RGB approximation at the prescribed Kelvin value.'
    if 'Cage' in name:recipe['lod_textures']='Identical back-plate surfaces share exact LOD0 UV0/UV1 coordinates. Other LOD charts remain disjoint; each LOD normal-baked.'
    else:recipe['housing']['edge_wear']='Continuous irregular rubbed edges with sparse wider chips; metric low-frequency mask, no grain threshold gating.'
    recipe['review_day']='Neutral sky plus SUN, concrete wall/roof support; LampLevel .05 (dim on) or 0 (off). Night LampLevel 1.'
    recipe['normal_binding']='ONE final Finish_Normal per slot on FinishBake UV1, using UV0 tangents. Cast grain, pits, Torx/ribs/optics microdetail and bevel normals are combined by baking. No stacking.'
    recipe['normal_authoring_inputs']='source/bake_inputs/*_Micro_Normal.png are generator intermediates, not runtime maps. Cage lens frost uses a 3D position-noise bake to avoid UV stretching.'
    recipe['lens']['display_tint']=list(temperature_rgb(piece['light']['kelvin']))
    (out/'material_recipe.json').write_text(json.dumps(recipe,indent=2)+'\n')
    for im in bpy.data.images:
        if im.source=='FILE':im.pack();im.filepath=bpy.path.relpath(im.filepath,start=str(out/'source'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.object.select_all(action='DESELECT');a.select_set(True);b.hide_set(True);b.hide_render=True;bpy.context.view_layer.objects.active=a
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    report=dict(lod0=basic_report(a),lod1=basic_report(b),uv0_lod0=uv_overlap(a),uv0_lod1=uv_overlap(b),uv1_lod0=uv_overlap(a,1),uv1_lod1=uv_overlap(b,1),textures=textures,bake='Cycles normal bake of geometry-edge Bevel + generated finish into FinishBake UV1; converted to DirectX',scope='Blender evidence only, export roundtrip follows Tools/Art/kit_blender.py')
    assert report['uv0_lod0']['overlapping_triangle_pairs']==0,report
    assert report['uv0_lod1']['overlapping_triangle_pairs']==0,report
    assert report['uv1_lod0']['overlapping_triangle_pairs']==report['uv1_lod1']['overlapping_triangle_pairs']==0,report
    for r in (report['lod0'],report['lod1']):assert r['degenerate_geometry']==r['degenerate_uv']==0,r
    (out/'evidence/verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PREPARED',piece['id'],report['lod0']['triangles'],report['lod1']['triangles'],flush=True)

def retint(piece):
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','')
    bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    apply_pbr(piece,out,use_finish=True)
    for im in bpy.data.images:
        if im.source=='FILE' and im.users:
            im.pack();im.filepath=bpy.path.relpath(im.filepath,start=str(out/'source'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    recipe=json.loads((out/'material_recipe.json').read_text())
    recipe['lens']['display_tint']=list(temperature_rgb(piece['light']['kelvin']))
    recipe['lens']['tint_basis']='Producer-matched neutral-white display tint at 4000 K; achromatic LightProfile.' if 'RoofFlood' in name else 'Normalized linear RGB approximation at the prescribed Kelvin value.'
    recipe['lod_textures']='Identical cage plate charts share exact UV0/UV1 coordinates; other charts disjoint; both LODs normal-baked.' if 'Cage' in name else 'Both LODs have separate non-overlapping charts in the same seven runtime maps. Both are painted and independently normal-baked.'
    (out/'material_recipe.json').write_text(json.dumps(recipe,indent=2)+'\n')

def verify(piece):
    out=KIT/piece['id'].split('/')[-1];records={}
    for suffix,budget in [('',piece['budgets']['lod0']),('_LOD1',piece['budgets']['lod1'])]:
        bpy.ops.wm.read_factory_settings(use_empty=True);path=out/(piece['mesh']+suffix+'.fbx');bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True)
        objs=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(objs)==1
        o=objs[0];r=basic_report(o);r['uv_overlap']=uv_overlap(o);r['finish_uv_overlap']=uv_overlap(o,1);r['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        assert r['triangles']<=budget and r['slots']==['Housing','Lens'],r
        assert r['degenerate_geometry']==r['degenerate_uv']==r['uv_overlap']['overlapping_triangle_pairs']==0,r
        assert r['uv_channels']==['UVMap','FinishBake'],r
        assert r['finish_uv_overlap']['overlapping_triangle_pairs']==0,r
        o.data.calc_tangents(uvmap='UVMap')
        tangent_error=max(max(abs(l.tangent.length-1),abs(l.normal.length-1),abs(l.normal.dot(l.tangent))) for l in o.data.loops)
        assert math.isfinite(tangent_error) and tangent_error<1e-4,(path.name,tangent_error)
        r['normal_tangent_orthonormal_max_error']=tangent_error
        r['imported_custom_normals']=o.data.has_custom_normals
        if piece['light']['type']!='Point':
            pitch=math.radians(piece['light']['rotation'][0]);direction=Vector((math.cos(pitch),0,math.sin(pitch)))
            normal_matrix=o.matrix_world.to_3x3().inverted().transposed()
            dots=[(normal_matrix@f.normal).normalized().dot(direction) for f in o.data.polygons if f.material_index==1]
            r['lens_normal_dot_light']={'minimum':min(dots),'maximum':max(dots)}
            assert max(dots)>.999,(path.name,dots)
            if 'DirectionalSpot' in piece['mesh'] or 'RoofFloodLight' in piece['mesh']:assert min(dots)>.999,(path.name,dots)
        records[path.name]=r
    evidence=json.loads((out/'evidence/verification.json').read_text());evidence['fbx_reimport']=records
    # Export frame should roundtrip in real metres; importer stores FBX unit scale in its transform.
    for suffix,key in [('', 'lod0'),('_LOD1','lod1')]:
        before=evidence[key]['bounds_m'];after=records[piece['mesh']+suffix+'.fbx']['bounds_m']
        assert max(abs(a-b) for xs,ys in zip(before,after) for a,b in zip(xs,ys))<.00001,(before,after)
    evidence['export_roundtrip']='passed: counts, slot order, UVs, origin and world bounds'
    name=piece['mesh'].replace('SM_Kit_','');bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    evidence['source_inventory']=[dict(name=o.name,type=o.type) for o in bpy.context.scene.objects]
    assert len(evidence['source_inventory'])==2 and all(o['type']=='MESH' for o in evidence['source_inventory'])
    evidence['uv1_lod0']=uv_overlap(bpy.data.objects[piece['mesh']],1)
    evidence['uv1_lod1']=uv_overlap(bpy.data.objects[piece['mesh']+'_LOD1'],1)
    evidence['source_reload']={k:basic_report(bpy.data.objects[piece['mesh']+suffix]) for k,suffix in [('lod0',''),('lod1','_LOD1')]}
    evidence['packed_images']=[dict(name=im.name,dimensions=list(im.size),packed=bool(im.packed_file)) for im in bpy.data.images if im.source=='FILE' and im.users>0]
    assert len(evidence['packed_images'])>=7 and all(im['packed'] for im in evidence['packed_images'])
    evidence['packed_pixels_match_files']=[]
    for im in bpy.data.images:
        if im.source=='FILE' and im.users>0:
            packed=hashlib.sha256(bytes(im.packed_file.data)).hexdigest();disk=hashlib.sha256(Path(bpy.path.abspath(im.filepath)).read_bytes()).hexdigest()
            assert packed==disk,('stale packed image',im.name)
            evidence['packed_pixels_match_files'].append(dict(name=im.name,sha256=packed))
    evidence['final_normal_binding']={}
    for surface in ['Housing','Lens']:
        nt=bpy.data.materials[surface].node_tree;images=[n.image.name for n in nt.nodes if n.type=='TEX_IMAGE']
        normals=[n for n in images if 'Normal' in n];assert len(normals)==1 and 'Finish_Normal' in normals[0]
        assert not any('Micro_Normal' in n for n in images)
        evidence['final_normal_binding'][surface]=dict(image=normals[0],sample_uv='FinishBake',tangent_uv='UVMap',microstructure_combined=True)

    for image in evidence['textures']:
        path=out/image['path'];im=bpy.data.images.load(str(path),check_existing=False)
        assert list(im.size)==image['size'],image
        image['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        image['actual_size']=list(im.size)
    (out/'evidence/verification.json').write_text(json.dumps(evidence,indent=2)+'\n');print('VERIFIED',piece['id'],flush=True)

def aim(o,p):o.rotation_euler=(Vector(p)-o.location).to_track_quat('-Z','Y').to_euler()

def render(piece,quick=False,match=False,lod_only=False):
    name=piece['mesh'].replace('SM_Kit_','');out=KIT/piece['id'].split('/')[-1]
    bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'));scene=bpy.context.scene;a=bpy.data.objects[piece['mesh']];b=bpy.data.objects[piece['mesh']+'_LOD1'];b.hide_render=True
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.seed=392;scene.cycles.use_denoising=True;scene.cycles.device='CPU';gpu()
    scene.render.image_settings.file_format='PNG';scene.render.resolution_x=1400;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    if quick:scene.render.resolution_x=800;scene.render.resolution_y=700;scene.cycles.samples=16
    scene.view_settings.view_transform='AgX'
    world=bpy.data.worlds.new('ReviewWorld');world.use_nodes=True;scene.world=world;bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs['Color'].default_value=(.24,.30,.42,1)
    preview=bpy.data.collections.new('REVIEW ONLY - never exported');scene.collection.children.link(preview)
    def move(q):
        for c in list(q.users_collection):c.objects.unlink(q)
        preview.objects.link(q)
    # Quiet concrete context makes construction and downward light spill legible.
    concrete=bpy.data.materials.new('REVIEW concrete');concrete.use_nodes=True;nt=concrete.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(.12,.13,.14,1);p.inputs['Roughness'].default_value=.86
    noise=nt.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=75;noise.inputs['Detail'].default_value=3
    bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.5;bump.inputs['Distance'].default_value=.002;nt.links.new(noise.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],p.inputs['Normal'])
    def cube(loc,size):
        bpy.ops.mesh.primitive_cube_add(size=1,location=loc);q=bpy.context.object;q.name='REVIEW support';q.scale=size;q.data.materials.append(concrete);move(q);return q
    roof='Roof' in name
    if roof:cube((0,0,-.035),(3,3,.065))
    else:cube((-.032,0,-.25),(.06,10,10.0))
    settings={'WallLamp':((.60,-.67,-.31),(.065,0,-.005),58),'CageWallLamp':((.56,-.30,.16),(.055,0,0),62),'EntranceLight':((.70,-.95,-.36),(.185,0,.035),58),'DirectionalSpot':((.90,-.69,.20),(.225,0,.07),55),'RoofFloodLight':((1.05,-1.30,.96),(0,0,.39),48)}
    if match:
        settings={'WallLamp':((.60,-.48,-.23),(.065,0,0),70),'CageWallLamp':((.60,-.32,.10),(.064,0,0),70),'EntranceLight':((.70,-.95,-.36),(.184,0,.033),70),'DirectionalSpot':((.95,-.60,-.03),(.232,0,.071),70),'RoofFloodLight':((1.15,-1.30,.31),(0,0,.354),70)}
        sizes={'WallLamp':(1500,1000),'CageWallLamp':(1000,1400),'EntranceLight':(1500,1000),'DirectionalSpot':(1300,1100),'RoofFloodLight':(1200,1300)}
        scene.render.resolution_x,scene.render.resolution_y=sizes[name]
        if quick:scene.render.resolution_percentage=55
        for q in preview.objects:
            if q.type=='MESH':q.hide_render=True
        bg.inputs['Color'].default_value=(.006,.009,.014,1)
    pos,target,lens=settings[name]
    camd=bpy.data.cameras.new('Reference camera');cam=bpy.data.objects.new('Reference camera',camd);preview.objects.link(cam);cam.location=pos;aim(cam,target);camd.lens=lens;scene.camera=cam
    if match:
        from bpy_extras.object_utils import world_to_camera_view
        target=Vector(target);offset=cam.location-target
        for step in range(14):
            pts=[world_to_camera_view(scene,cam,a.matrix_world@v.co) for v in a.data.vertices]
            extent=max(max(abs(q.x-.5),abs(q.y-.5)) for q in pts)/.46
            if abs(extent-1)<.005:break
            offset*=extent;cam.location=target+offset;aim(cam,target);bpy.context.view_layer.update()
    def area(n,pos,target,power,size,color=(1,1,1)):
        d=bpy.data.lights.new(n,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=color;q=bpy.data.objects.new(n,d);preview.objects.link(q);q.location=pos;aim(q,target);return q
    scale=2 if roof else 1
    key=area('REVIEW neutral key',(.65*scale,-.65*scale,.9*scale),target,45*scale,.8*scale)
    fill=area('REVIEW cool rim',(.10,1.0*scale,.65*scale),target,20*scale,.6*scale,(.63,.75,1))
    rim=area('REVIEW bright edge strip',(.05*scale,.45*scale,.35*scale),target,28*scale*scale,.22*scale,(.55,.69,1))
    rim.data.shape='RECTANGLE';rim.data.size_y=.70*scale
    glassbox=area('REVIEW glass reflection',(.75,.85,.27),(0,0,.51),8*scale,.28,(1,1,1))
    glassbox.data.shape='RECTANGLE';glassbox.data.size_y=.65
    if name!='RoofFloodLight':glassbox.data.energy=0
    if name=='WallLamp':rim.location=(.35,.65,.45);aim(rim,target)
    under=area('REVIEW warm bounce',(.23,-.05,-.28),target,2.0,.28,(1,.74,.50))
    if name not in ('WallLamp','EntranceLight'):under.data.energy=0
    sidebox=area('REVIEW worn-head reflector',(-.25,-.45,.25) if name in ('WallLamp','EntranceLight') else (.70,.35,.02),target,3.0,.60,(.86,.91,1))
    if not match:sidebox.hide_render=True
    r=basic_report(a);lo,hi=r['lens_bounds_m'];anchor=Vector([(u+v)/2 for u,v in zip(lo,hi)]);direction=Vector((1,0,0))
    pitch=piece['light'].get('rotation',[0])[0];direction=Vector((math.cos(math.radians(pitch)),0,math.sin(math.radians(pitch))))
    # Separate review light(s); the delivered mesh and Claude's catalogue retain the single prescribed light.
    lights=[]
    positions=[anchor]
    if roof:positions=[anchor+Vector((0,s*.195,0)) for s in (-1,1)]
    for i,anchor in enumerate(positions):
        if 'Cage' in name:
            d=bpy.data.lights.new('REVIEW fixture light','POINT');d.energy=1.5;d.shadow_soft_size=.04;q=bpy.data.objects.new('REVIEW fixture light',d);preview.objects.link(q);q.location=anchor
        else:q=area('REVIEW fixture light',anchor+direction*.008,anchor+direction,.8 if not roof else 4.0,.065)
        q.data.color=temperature_rgb(piece['light']['kelvin']);lights.append(q)
    sun_data=bpy.data.lights.new('REVIEW daylight sun','SUN');sun_data.energy=2.0;sun_data.angle=math.radians(8)
    sun=bpy.data.objects.new('REVIEW daylight sun',sun_data);preview.objects.link(sun);sun.rotation_euler=(math.radians(25),math.radians(55),math.radians(20))
    lamp=bpy.data.materials['Lens'].node_tree.nodes['LampLevel']
    camera_record=dict(location_m=pos,target_m=target,lens_mm=lens,engine='Cycles',samples=32,resolution=[1400,1200],context='Blender concrete support; reference-aligned three-quarter angle; not Unreal')
    modes=[('ref_match_day',1,False),('ref_match_night',1,True)] if match else [('pbr_day_on',1,False),('pbr_day_off',0,False),('pbr_night_on',1,True),('pbr_night_off',0,True)]
    for filename,on,night in modes:
        if quick and filename!=('ref_match_day' if match else 'pbr_day_off'):continue
        under.data.energy=(2.0 if on else .3) if name in ('WallLamp','EntranceLight') else 0
        under.data.color=(1,.74,.50) if on else (.7,.8,1)
        lamp.outputs[0].default_value=on if night else on*.05
        for q in lights:q.hide_render=not bool(on) or not night
        sun.hide_render=night
        for q in preview.objects:
            if q.type=='MESH':q.hide_render=night and match
        bg.inputs['Color'].default_value=(.006,.009,.014,1) if night and match else ((.24,.30,.42,1) if night else (.65,.72,.85,1))
        sidebox.hide_render=not night or not match
        glassbox.data.energy=(8*scale if night else 3*scale) if roof else 0
        key.data.energy=(5 if night else 0)*scale*scale;fill.data.energy=(1.5 if night else 0)*scale*scale;rim.data.energy=(4 if night else 0)*scale*scale;bg.inputs['Strength'].default_value=(.10 if match else .018) if night else .65
        if not night:under.data.energy=0
        scene.render.filepath=str(out/'review'/(('ref_match_quick.png' if match else 'shape_check.png') if quick else f'{filename}.png'))
        if not lod_only:bpy.ops.render.render(write_still=True)
        if not lod_only:print('RENDERED',piece['id'],filename,flush=True)
    if quick:return
    lamp.outputs[0].default_value=1
    for q in lights:q.hide_render=False
    scene.render.filepath=str(out/'review'/('ref_match_night.png' if match else 'pbr_night_on.png'));bpy.context.preferences.filepaths.save_version=0
    if not lod_only:bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}_{"ReferencePreview" if match else "Preview"}.blend'))
    if match:
        camera_record.update(round=4,day=dict(world_color_linear=[.65,.72,.85],world_strength=.65,sun_energy=2,sun_angle_degrees=8,sun_rotation_degrees=[25,55,20],lamp_level=.05,context='concrete roof' if roof else 'concrete wall'),night=dict(lamp_level=1,background='isolated product studio'),location_m=list(cam.location),target_m=list(target),lens_mm=lens,resolution=[scene.render.resolution_x,scene.render.resolution_y],context='Reference main-product angle: daylight wall/roof support, neutral sky and sun, lens 5%; night isolated studio, lens 100%; no branding')
        (out/'evidence/reference_cameras.json').write_text(json.dumps(camera_record,indent=2)+'\n');return
    # Small-size LOD pair from a fixed elevated RTS-like angle. This is art comparison, not a gameplay screenshot.
    for q in list(preview.objects):
        if q.type=='MESH':q.hide_render=True
    sun.hide_render=True;key.data.energy=45*scale;fill.data.energy=20*scale;bg.inputs['Color'].default_value=(.24,.30,.42,1);bg.inputs['Strength'].default_value=.30
    for q in lights:q.hide_render=True
    scene.render.resolution_x=360;scene.render.resolution_y=300;scene.cycles.samples=32
    target=Vector(target);cam.location=target+Vector((.9,-1.2,.85)).normalized()*max(.85,scale*.75);aim(cam,target);camd.type='ORTHO';camd.ortho_scale=.46 if name not in ('DirectionalSpot','RoofFloodLight') else (.6 if not roof else 1.05)
    for lod,ob in [(0,a),(1,b)]:
        a.hide_render=lod!=0;b.hide_render=lod!=1;scene.render.filepath=str(out/'review'/f'rts_lod{lod}.png');bpy.ops.render.render(write_still=True)
    if lod_only:return
    # True scale at the 260m switch: same 60-degree camera and frame for each LOD.
    scene.render.resolution_x=1920;scene.render.resolution_y=1080;camd.type='PERSP';camd.lens=camd.sensor_width/(2*math.tan(math.radians(30)))
    for distance in (12,150,260,450):
        cam.location=target+Vector((.9,-1.2,.85)).normalized()*distance;aim(cam,target)
        for lod in (0,1):
            a.hide_render=lod!=0;b.hide_render=lod!=1
            prefix='switch' if distance==260 else 'distance'
            scene.render.filepath=str(out/'review'/f'{prefix}_{distance}m_lod{lod}.png');bpy.ops.render.render(write_still=True)
    camera_record['rts_small']='360x300 orthographic detail comparison, deliberately enlarged; not actual 260m size';camera_record['switch']='1920x1080, horizontal FOV 60deg, camera-to-fixture 260m; no environment'
    (out/'evidence/cameras.json').write_text(json.dumps(camera_record,indent=2)+'\n')


def sheets(piece):
    import numpy as np
    from wall_lamp_materials import save_map
    out=KIT/piece['id'].split('/')[-1]
    def read(name):
        im=bpy.data.images.load(str(out/'review'/name),check_existing=False);im.colorspace_settings.name='Non-Color'
        arr=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(arr)
        return arr.reshape((im.size[1],im.size[0],4))[:,:,:3].copy()
    glyphs={'L':['10000','10000','10000','10000','10000','10000','11111'],'O':['01110','10001','10001','10001','10001','10001','01110'],'D':['11110','10001','10001','10001','10001','10001','11110'],'0':['01110','10001','10011','10101','11001','10001','01110'],'1':['00100','01100','00100','00100','00100','00100','01110']}
    def pair(a,b):
        h,w=a.shape[:2];canvas=np.full((h+28,w*2+16,3),.055,dtype=np.float32);canvas[:h,:w]=a;canvas[:h,w+16:]=b
        for text,x in [('LOD0',12),('LOD1',w+28)]:
            for char in text:
                for j,row in enumerate(glyphs[char]):
                    for i,v in enumerate(row):
                        if v=='1':canvas[h+20-j*2:h+22-j*2,x+i*2:x+i*2+2]=.85
                x+=12
        return canvas
    a,b=read('rts_lod0.png'),read('rts_lod1.png');save_map(out/'review/rts_LOD0_left_LOD1_right.png',pair(a,b))
    a,b=read('switch_260m_lod0.png'),read('switch_260m_lod1.png');diff=np.max(np.abs(a-b),axis=2)
    h,w=a.shape[:2];sl=np.s_[h//2-24:h//2+24,w//2-24:w//2+24]
    aa=np.repeat(np.repeat(a[sl],8,axis=0),8,axis=1);bb=np.repeat(np.repeat(b[sl],8,axis=0),8,axis=1)
    save_map(out/'review/switch_260m_crop_8x_LOD0_left_LOD1_right.png',pair(aa,bb))
    receipt=dict(scope='Blender-only 60deg horizontal FOV, 1920x1080, fixture-centre camera distance 260m, same 392 sample seed; not a gameplay validation',changed_pixels_gt_1_of_255=int(np.sum(diff>1/255+.0001)),max_display_rgb_delta=float(diff.max()),mean_display_rgb_delta=float(diff.mean()),crop='48x48 centre pixels, nearest-neighbour enlargement 8x, LOD0 left / LOD1 right',rts_pair='enlarged orthographic inspection, LOD0 left / LOD1 right; see cameras.json')
    (out/'evidence/lod_comparison.json').write_text(json.dumps(receipt,indent=2)+'\n');print('SHEETS',piece['id'],receipt['changed_pixels_gt_1_of_255'],flush=True)


def compare(piece):
    import numpy as np
    from wall_lamp_materials import save_map
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','')
    crops={'WallLamp':(50,110,1070,750),'CageWallLamp':(55,120,615,965),'EntranceLight':(60,100,1070,725),'DirectionalSpot':(100,90,960,760),'RoofFloodLight':(50,90,1000,970)}
    def read(path):
        im=bpy.data.images.load(str(path),check_existing=False);im.colorspace_settings.name='Non-Color';a=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(a);return a.reshape((im.size[1],im.size[0],4))[:,:,:3].copy()
    ref=read(out/'reference/producer_reference.png');x0,y0,x1,y1=crops[name];ref=ref[ref.shape[0]-y1:ref.shape[0]-y0,x0:x1]
    def resize(a,h):
        w=round(h*a.shape[1]/a.shape[0]);ys=np.linspace(0,a.shape[0]-1,h).astype(int);xs=np.linspace(0,a.shape[1]-1,w).astype(int);return a[ys[:,None],xs[None,:]]
    for mode in ['day','night']:
        a=resize(ref,650);b=resize(read(out/'review'/f'ref_match_{mode}.png'),650)
        canvas=np.full((650,a.shape[1]+b.shape[1]+18,3),.025,dtype=np.float32);canvas[:,:a.shape[1]]=a;canvas[:,a.shape[1]+18:]=b
        save_map(out/'review'/f'reference_left_round04_right_{mode}.png',canvas)
    (out/'evidence/reference_comparison.json').write_text(json.dumps(dict(reference='reference/producer_reference.png',reference_crop_xyxy=crops[name],order='producer reference crop on left, Round 4 reference-angle render on right',day='review/reference_left_round04_right_day.png',night='review/reference_left_round04_right_night.png',note='Only the main product image is cropped for comparison; the producer original is untouched.'),indent=2)+'\n')


def rear(piece):
    if 'RoofFloodLight' not in piece['mesh']:return
    out=KIT/piece['id'].split('/')[-1]
    bpy.ops.wm.open_mainfile(filepath=str(out/'source/RoofFloodLight_ReferencePreview.blend'));scene=bpy.context.scene
    cam=scene.camera;cam.location=(-1.4,-.7,.90);aim(cam,(0,0,.42));cam.data.lens=55
    settings={'REVIEW neutral key':((-1.0,-.7,1.1),45),'REVIEW cool rim':((-.6,.8,.8),22),'REVIEW bright edge strip':((.4,.7,.7),8)}
    for o in scene.objects:
        if o.type=='LIGHT':
            if o.name in settings:o.location=settings[o.name][0];o.data.energy=settings[o.name][1];aim(o,(0,0,.42))
            else:o.hide_render=True
    bpy.data.materials['Lens'].node_tree.nodes['LampLevel'].outputs[0].default_value=0
    scene.render.resolution_x=1200;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    scene.render.filepath=str(out/'review/rear_fins.png');bpy.ops.render.render(write_still=True)

def lodcheck(piece):render(piece,lod_only=True)
def shape(piece):render(piece,quick=True)
def refquick(piece):render(piece,quick=True,match=True)
def refmatch(piece):render(piece,match=True)

if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['prepare']
    for piece in PIECES():
        if len(args)>1 and piece['id'].split('/')[-1] not in args[1:]:continue
        globals()[args[0]](piece)
