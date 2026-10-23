"""Issue 393 scoped source, render and export verification. No FBX writes here."""
import sys,os,json,math,hashlib,importlib.util
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy,numpy as np
from mathutils import Vector
from wall_lamp_materials import ROOT,KIT,save_map
from wall_lamp_render import basic_report,uv_overlap,gpu,aim
from lighting_393_finish import textures,apply,tint,strength
SPEC=KIT/'_work_393/catalogue_393.json'
PIECES=json.loads(SPEC.read_text())['pieces']

def load(piece):
    sp=importlib.util.spec_from_file_location('fixture393',Path(__file__).parent/piece['module']);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);return m

def prepare(piece):
    bpy.ops.wm.read_factory_settings(use_empty=True);out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','')
    a,b=load(piece).build(piece);objects=[o for o in (a,b) if o];maps=textures(piece,objects,out)
    for im in list(bpy.data.images):
        if im.users==0:bpy.data.images.remove(im)
        elif im.source=='FILE':im.pack();im.filepath=bpy.path.relpath(im.filepath,start=str(out/'source'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.object.select_all(action='DESELECT');a.select_set(True);bpy.context.view_layer.objects.active=a
    if b:b.hide_render=True;b.hide_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    report=dict(piece=piece['id'],round=4,textures=maps,source_inventory=[o.name for o in objects],bake='Cycles geometry Bevel plus authored metric microfinish, baked to UV1, UV0 tangent parameterization; final maps DirectX')
    for i,o in enumerate(objects):
        r=basic_report(o);r['uv0_overlap']=uv_overlap(o,0);r['uv1_overlap']=uv_overlap(o,1)
        assert r['degenerate_geometry']==r['degenerate_uv']==r['uv0_overlap']['overlapping_triangle_pairs']==r['uv1_overlap']['overlapping_triangle_pairs']==0,r
        report['lod'+str(i)]=r
    recipe=dict(piece=piece['id'],round=4,slots=['Housing','Lens'],textures=maps,orm='R occlusion, G roughness, B metallic',normal_format='DirectX, no Unreal green flip',normal_binding='ONE Finish_Normal per slot; sample FinishBake UV1; retain UV0 tangents; no normal stacking',housing=dict(coating='approved 392 graphite powder coat; silver edge rubbing; sparse local rust/dust/pits',roughness_mid=.48,metallic='0 coating; up to .9 exposed fasteners',bevel_m=.001),lens=dict(display_tint=tint(piece),emission_strength=strength(piece),profile='p=LightProfile.r; hot=saturate((p-.55)/.35); emission=p*LampLevel*emission_strength*lerp(display_tint,hot_core_tint,hot)',hot_core_tint=(1,.48,.08) if 'Warning' in name else ((1,.22,.035) if 'Mast' in name else (1,1,1)),ior=1.49,coat_weight=.22 if 'Beacon' in name else .25,coat_roughness=.06 if 'Beacon' in name else .15,orm_roughness='beacon .12 to .265; opal .23 to .39',representation='continuous coloured opaque glass field, soft tinted interior repeated around circumference; no dark photographic backdrop/window' if 'Beacon' in name else 'ribbed opal diffuser'),light=piece.get('light','emissive only; no real light'),approval='Round 4; unapproved',lod_textures='Separate LOD charts in the same maps; every LOD painted and finish-baked',frame='metres, +X forward, Z up; origin base underside for 08/12/13, mounting plane for 07/09',uv_beacon='08 one dome island, U begins +X and increases towards +Y; 13 negative Y U<.5, positive Y U>=.5; seam/caps included in their dome chart')
    recipe['light_anchor']={"ServiceBayLight":{"anchor_m":[-0.004,0,0],"direction":[1,0,0],"text":"Rect, 300 cd, 4000 K, attenuation 12 m. Aperture 0.532 × 0.232 m; anchor (-0.004,0,0) m, direction +X. Mount pitch places it on the ceiling."},"WarningBeacon":{"anchor_m":[0,0,0.25],"direction":"omnidirectional","text":"Point, 80 cd, amber/2200 K, attenuation 8 m; anchor (0,0,0.25) m. Mirror faces +X at rest; Claude supplies the material sweep."},"IndicatorLED":{"anchor_m":[0.047,0,-0.047],"direction":[0,0,-1],"text":"Emissive only, no real light. Wall mounting frame faces +X; the actual underside diffuser faces −Z. Optical centre (0.047,0,-0.047) m."},"MastBeaconSingle":{"anchor_m":[0,0,0.395],"direction":"omnidirectional","text":"Emissive only, no real light. Tip mount; no side arm or building mast. Dome centre approximately (0,0,0.395) m."},"MastBeaconDual":{"anchor_m":[[0,-0.225,0.32625],[0,0.225,0.32625]],"direction":"omnidirectional","text":"Emissive only, no real light. Dome centres approximately (0,±0.225,0.32625) m. Negative-Y head U<0.5; positive-Y head U≥0.5, in both LODs."}}[name]
    if name=='IndicatorLED':recipe['housing']['coating']='worn bare/galvanised steel, neutral grey, pits and silver edge wear'
    if name=='WarningBeacon':recipe['housing']['coating']='near-black cast housing with local pits and silver wear'
    recipe['lens']['orm_roughness']='.12 to .265' if 'Beacon' in name else ('.24 to .34' if name=='ServiceBayLight' else '.23 to .39')
    (out/'material_recipe.json').write_text(json.dumps(recipe,indent=2)+'\n')
    (out/'evidence/verification.json').write_text(json.dumps(report,indent=2)+'\n');print('PREPARED',piece['id'],[basic_report(o)['triangles'] for o in objects],flush=True)

def verify(piece):
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');e=json.loads((out/'evidence/verification.json').read_text());records={}
    for i in range(1 if piece['class']=='S' else 2):
        suffix='' if i==0 else '_LOD1';p=out/piece.get('output','')/(piece['mesh']+suffix+'.fbx');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(p),use_custom_normals=True)
        obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(obs)==1;o=obs[0];r=basic_report(o)
        r['uv0_overlap']=uv_overlap(o,0);r['uv1_overlap']=uv_overlap(o,1)
        assert r['triangles']<=piece['budgets']['lod'+str(i)] and r['slots']==['Housing','Lens'],r
        assert r['degenerate_geometry']==r['degenerate_uv']==r['uv0_overlap']['overlapping_triangle_pairs']==r['uv1_overlap']['overlapping_triangle_pairs']==0,r
        assert r['uv_channels']==['UVMap','FinishBake'],r
        assert max(abs(v-w) for aa,bb in zip(e['lod'+str(i)]['bounds_m'],r['bounds_m']) for v,w in zip(aa,bb))<1e-5,r
        o.data.calc_tangents(uvmap='UVMap');r['tangent_error']=max(max(abs(l.tangent.length-1),abs(l.normal.length-1),abs(l.normal.dot(l.tangent))) for l in o.data.loops);assert r['tangent_error']<1e-4
        if name in ('ServiceBayLight','IndicatorLED'):
            axis=Vector((1,0,0) if name=='ServiceBayLight' else (0,0,-1));dots=[(o.matrix_world.to_3x3().inverted().transposed()@f.normal).normalized().dot(axis) for f in o.data.polygons if f.material_index==1];assert min(dots)>.999,(name,dots);r['emitting_normal_axis']=list(axis)
        r['sha256']=hashlib.sha256(p.read_bytes()).hexdigest();records[p.name]=r
    e['fbx_reimport']=records
    bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'));assert len(bpy.context.scene.objects)==(1 if piece['class']=='S' else 2)
    if 'Dual' in name:
        e['bar_construction']={}
        for suffix in ('','_LOD1'):
            o=bpy.data.objects[piece['mesh']+suffix];checks=[];cs=json.loads(o['chart_layout'])
            bolt_vertices={vi for f in o.data.polygons if cs[o.data.attributes['atlas_chart'].data[f.index].value]['kind'] in ('washer','torx','bolt') for vi in f.vertices}
            for y in (-.29,.29):
                vs=[o.data.vertices[i].co for i in bolt_vertices if abs(o.data.vertices[i].co.y-y)<.020]
                assert vs,'missing front bolts'
                bounds=[[min(v[j] for v in vs) for j in range(3)],[max(v[j] for v in vs) for j in range(3)]]
                assert bounds[0][2]>.070 and bounds[1][2]<.130 and bounds[1][0]>.102,('bar bolt anchor/relief',bounds)
                checks.append(bounds)
            glands=[v.co for v in o.data.vertices if v.co.z<.070 and min(math.hypot(v.co.x,v.co.y-y) for y in (-.225,.225))>.065]
            assert len(glands)>=12,'gland nuts must protrude below bar underside'
            e['bar_construction']['lod1' if suffix else 'lod0']=dict(front_bolt_bounds_m=checks,gland_vertices_below_bar=len(glands),bar_bottom_m=.070)
    e['packed_images']=[]
    for im in bpy.data.images:
        if im.source=='FILE' and im.users:
            assert im.packed_file,im.name;p=Path(bpy.path.abspath(im.filepath));sha=hashlib.sha256(p.read_bytes()).hexdigest();assert sha==hashlib.sha256(bytes(im.packed_file.data)).hexdigest();e['packed_images'].append(dict(name=im.name,size=list(im.size),sha256=sha))
    assert len(e['packed_images'])==7,e['packed_images']
    for t in e['textures']:t['sha256']=hashlib.sha256((out/t['path']).read_bytes()).hexdigest()
    nt=bpy.data.materials['Lens'].node_tree
    assert 'LensTint' in nt.nodes and 'HotCore' in nt.nodes and 'CoreColour' in nt.nodes
    assert abs(nt.nodes['HotCore'].inputs['From Min'].default_value-.55)<1e-5 and abs(nt.nodes['HotCore'].inputs['From Max'].default_value-.90)<1e-5
    assert any(n.type=='MATH' and n.operation=='MULTIPLY' and abs(n.inputs[1].default_value-strength(piece))<1e-5 for n in nt.nodes)
    e['lens_shader']=dict(tint=list(nt.nodes['LensTint'].outputs[0].default_value[:3]),hot_core_tint=list(nt.nodes['CoreColour'].inputs[2].default_value[:3]),hot_core_profile_range=[.55,.90],emission_strength=strength(piece),representation='opaque continuous optical backing; compact hot-core tint blend')
    e['normal_binding']={}
    for slot in ['Housing','Lens']:
        nt=bpy.data.materials[slot].node_tree;ns=[n for n in nt.nodes if n.type=='TEX_IMAGE' and 'Normal' in n.image.name];assert len(ns)==1 and 'Finish_Normal' in ns[0].image.name
        e['normal_binding'][slot]=dict(image=ns[0].image.name,sample_uv='FinishBake',tangent_uv='UVMap')
    if 'Dual' in name:
        e['dual_lens_u_ranges']={}
        for suffix in ('','_LOD1'):
            o=bpy.data.objects[piece['mesh']+suffix];uv=o.data.uv_layers[0];groups={}
            for side in [-1,1]:
                values=[uv.data[li].uv.x for f in o.data.polygons if f.material_index==1 and sum(o.data.vertices[v].co.y for v in f.vertices)*side>0 for li in f.loop_indices];groups[str(side)]=[min(values),max(values)]
            assert groups['-1'][1]<.5<=groups['1'][0];e['dual_lens_u_ranges']['lod1' if suffix else 'lod0']=groups
    e['export_roundtrip']='passed; independent FBX reimport, real metre bounds, slots, triangles, UV0/UV1, packed image byte equality'
    (out/'evidence/verification.json').write_text(json.dumps(e,indent=2)+'\n');print('VERIFIED',piece['id'],flush=True)

def render(piece,quick=False):
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'));scene=bpy.context.scene;a=bpy.data.objects[piece['mesh']];b=bpy.data.objects.get(piece['mesh']+'_LOD1')
    if b:b.hide_render=True;b.hide_set(False)
    scene.render.engine='CYCLES';scene.cycles.samples=64;scene.cycles.seed=393;scene.cycles.use_denoising=True;gpu();scene.render.image_settings.file_format='PNG';scene.render.resolution_x=1200;scene.render.resolution_y=1100;scene.render.resolution_percentage=100;scene.view_settings.view_transform='AgX'
    world=bpy.data.worlds.new('REVIEW world');world.use_nodes=True;scene.world=world;bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
    stand='Beacon' in name;target=Vector((0,0,.16 if 'Warning' in name else (.225 if 'Dual' in name else .28))) if stand else Vector((.02,0,0))
    offsets={'ServiceBayLight':(1,-.60,-.50),'IndicatorLED':(.65,-.60,-.35),'WarningBeacon':(1,-.26,.06),'MastBeaconSingle':(1,-1,.08),'MastBeaconDual':(1,-1,.10)}
    camd=bpy.data.cameras.new('REVIEW camera');cam=bpy.data.objects.new('REVIEW camera',camd);scene.collection.objects.link(cam);cam.location=target+Vector(offsets[name]);aim(cam,target);camd.lens=58;scene.camera=cam
    from bpy_extras.object_utils import world_to_camera_view
    for step in range(15):
        pts=[world_to_camera_view(scene,cam,v.co) for v in a.data.vertices];extent=max(max(abs(p.x-.5),abs(p.y-.5)) for p in pts)/.43
        if abs(extent-1)<.003:break
        cam.location=target+(cam.location-target)*extent;aim(cam,target);bpy.context.view_layer.update()
    close_location=list(cam.location)
    def area(n,pos,power,size):
        d=bpy.data.lights.new(n,'AREA');d.energy=power;d.shape='RECTANGLE';d.size=size;d.size_y=size*2.5;q=bpy.data.objects.new(n,d);scene.collection.objects.link(q);q.location=target+Vector(pos);aim(q,target);return q
    key=area('REVIEW key',(1,-1,1.4),30,.35);rim=area('REVIEW strip',(.3,1,.6),20,.08);fill=area('REVIEW fill',(-.3,-.5,.6),8,.25)
    sd=bpy.data.lights.new('REVIEW sun','SUN');sd.energy=2;sd.angle=math.radians(8);sun=bpy.data.objects.new('REVIEW sun',sd);scene.collection.objects.link(sun);sun.rotation_euler=(.4,.8,.3)
    # Separate context, absent from clean source and from all exported geometry.
    concrete=bpy.data.materials.new('REVIEW concrete');concrete.use_nodes=True;nt=concrete.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(.12,.13,.14,1);p.inputs['Roughness'].default_value=.85
    noise=nt.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=70;noise.inputs['Detail'].default_value=3;bn=nt.nodes.new('ShaderNodeBump');bn.inputs['Strength'].default_value=.4;bn.inputs['Distance'].default_value=.002;nt.links.new(noise.outputs['Fac'],bn.inputs['Height']);nt.links.new(bn.outputs['Normal'],p.inputs['Normal'])
    # A real opening around 07's recessed body; a solid wall would cover its rear box.
    supports=[]
    def box(loc,scale):
        bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name='REVIEW support';o.scale=scale;o.data.materials.append(concrete);supports.append(o)
    if name=='ServiceBayLight':
        box((-.06,-.66,0),(.12,.74,2));box((-.06,.66,0),(.12,.74,2));box((-.06,0,.57),(.12,.58,.84));box((-.06,0,-.57),(.12,.58,.84))
    elif stand:box((0,0,-.025),(2,2,.05))
    else:box((-.025,0,0),(.05,2,2))
    lamp=bpy.data.materials['Lens'].node_tree.nodes['LampLevel'];bsdf=next(n for n in bpy.data.materials['Lens'].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    lights=[]
    if name=='ServiceBayLight':
        q=area('REVIEW fixture',(.015,0,0),2,.25);q.location=(-.003,0,0);aim(q,(1,0,0));q.data.size=.556;q.data.size_y=.256;q.data.color=tint(piece);lights.append(q)
    elif name=='WarningBeacon':
        d=bpy.data.lights.new('REVIEW fixture','POINT');d.energy=.25;d.color=tint(piece);d.shadow_soft_size=.04;q=bpy.data.objects.new('REVIEW fixture',d);scene.collection.objects.link(q);q.location=(0,0,.25);lights.append(q)
    camera_record=dict(close_location_m=close_location,target_m=list(target),lens_mm=58,close_resolution=[1200,1100],distance_resolution=[1920,1080],distance_horizontal_fov_degrees=60,engine='Cycles',samples=64,seed=393,distance_samples=128,distance_denoising=False,context='Blender-only review; no Unreal/runtime evidence',day='neutral sky and sun, level .05 on / 0 off',night='studio environment, level 1 on / 0 off / .3 LowPower')
    def setup(night,on,context):
        lamp.outputs[0].default_value=on if night else on*.05
        bg.inputs['Color'].default_value=(.006,.009,.014,1) if night else (.65,.72,.85,1);bg.inputs['Strength'].default_value=.10 if night else .65
        sun.hide_render=night
        for q,power in [(key,4 if night else 12),(rim,4 if night else 8),(fill,1.5 if night else 5)]:q.data.energy=power*((3 if name=='IndicatorLED' else 2) if night else 1)
        for q in supports:q.hide_render=not context
        for q in lights:q.hide_render=not(night and on and (context or name=='ServiceBayLight'));q.data.energy=(2 if name=='ServiceBayLight' else .25)*on
    def shot(filename):scene.render.filepath=str(out/'review'/f'{filename}.png');bpy.ops.render.render(write_still=True);print('RENDERED',name,filename,flush=True)
    for night in (False,True):
        if quick and False:continue
        for on in (() if quick else (0,1)):setup(night,on,True);shot('pbr_'+('night' if night else 'day')+('_on' if on else '_off'))
        setup(night,1,not night);shot('ref_match_'+('night' if night else 'day'))
    if quick:return
    if stand:setup(True,.3,False);shot('lowpower_night')
    if name=='IndicatorLED':
        for label,color in [('blue',(.005,.08,1)),('red',(1,.005,.002)),('green',(.005,1,.025))]:
            bpy.data.materials['Lens'].node_tree.nodes['LensTint'].outputs[0].default_value=(*color,1);setup(True,1,True);shot('tint_'+label)
        bpy.data.materials['Lens'].node_tree.nodes['LensTint'].outputs[0].default_value=(*tint(piece),1)
    setup(True,1,False);scene.render.resolution_x=1920;scene.render.resolution_y=1080;scene.cycles.use_denoising=False;scene.cycles.samples=128
    camd.type='PERSP';camd.lens=camd.sensor_width/(2*math.tan(math.radians(30)));direction=Vector((.9,-1.2,.85)).normalized()
    for distance in (12,150,260,450):
        if False:continue
        cam.location=target+direction*distance;aim(cam,target)
        for i,o in enumerate([a,b] if b else [a]):
            a.hide_render=i!=0
            if b:b.hide_render=i!=1
            shot(('switch' if distance==260 and b is not None else 'distance')+f'_{distance}m_lod{i}')
    # Explicit lit/unlit far views, for glass visibility and bright-pixel judgement.
    for distance in (150,450):
        cam.location=target+direction*distance;aim(cam,target);a.hide_render=False
        if b:b.hide_render=True
        setup(False,0,False);shot(f'distance_day_{distance}m_lod0');setup(True,1,False)
    if b:
        scene.render.resolution_x=600;scene.render.resolution_y=500;scene.cycles.use_denoising=True;scene.cycles.samples=64;camd.type='ORTHO';camd.ortho_scale=max(a.dimensions)*1.35;cam.location=target+direction*2;aim(cam,target)
        for i in (0,1):a.hide_render=i!=0;b.hide_render=i!=1;shot(f'rts_lod{i}')
    (out/'evidence/cameras.json').write_text(json.dumps(camera_record,indent=2)+'\n')
    # Preserve review scene independently, clean source stays mesh-only.
    a.hide_render=False
    if b:b.hide_render=True
    camd.type='PERSP';camd.lens=58;cam.location=close_location;aim(cam,target);scene.render.resolution_x=1200;scene.render.resolution_y=1100;setup(True,1,False)
    bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}_Preview.blend'))

def reglaze(piece):
    from lighting_393_finish import optics
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    obs=[bpy.data.objects[piece['mesh']+suffix] for suffix in ('','_LOD1')]
    # Colour/specular revision only; geometry/UVs and baked finish normals are unchanged.
    optics(piece,obs,out);apply(piece,out,True)
    for im in list(bpy.data.images):
        if im.users==0:bpy.data.images.remove(im)
        elif im.source=='FILE':im.pack();im.filepath=bpy.path.relpath(im.filepath,start=str(out/'source'))
    bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/f'{name}.blend'))

def shape(piece):return render(piece,quick=True)

def sheets(piece):
    from lighting_393_finish import read
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','')
    def resize(a,h):
        w=round(a.shape[1]*h/a.shape[0]);return a[np.linspace(0,a.shape[0]-1,h).astype(int)[:,None],np.linspace(0,a.shape[1]-1,w).astype(int)[None,:]]
    ref=read(out/'reference'/('style_reference_04.png' if name=='ServiceBayLight' else 'producer_reference.png'))
    crops={'ServiceBayLight':(40,160,1080,770),'WarningBeacon':(70,110,600,1010),'IndicatorLED':(60,145,1050,760),'MastBeaconSingle':(160,180,480,730),'MastBeaconDual':(35,180,515,715)}
    x0,y0,x1,y1=crops[name];ref=ref[ref.shape[0]-y1:ref.shape[0]-y0,x0:x1]
    for mode in ('day','night'):
        aa=resize(ref,700);bb=resize(read(out/'review'/('tint_blue.png' if name=='IndicatorLED' and mode=='night' else f'ref_match_{mode}.png')),700);canvas=np.full((700,aa.shape[1]+bb.shape[1]+16,3),.025,np.float32);canvas[:,:aa.shape[1]]=aa;canvas[:,aa.shape[1]+16:]=bb;save_map(out/'review'/f'reference_left_round04_right_{mode}.png',canvas)
    if piece['class']!='S':
        def pair(a,b):
            h,w=a.shape[:2];canvas=np.full((h,w*2+16,3),.025,np.float32);canvas[:,:w]=a;canvas[:,w+16:]=b;return canvas
        save_map(out/'review/rts_LOD0_left_LOD1_right.png',pair(read(out/'review/rts_lod0.png'),read(out/'review/rts_lod1.png')))
        stats={}
        for d in (12,150,260,450):
            pre='switch' if d==260 else 'distance';a=read(out/'review'/f'{pre}_{d}m_lod0.png');b=read(out/'review'/f'{pre}_{d}m_lod1.png');delta=np.max(abs(a-b),axis=2);stats[str(d)]=dict(changed_pixels_gt_1_of_255=int((delta>1/255+.0001).sum()),max_display_delta=float(delta.max()))
            h,w=a.shape[:2];sl=np.s_[h//2-24:h//2+24,w//2-24:w//2+24];aa=np.repeat(np.repeat(a[sl],8,0),8,1);bb=np.repeat(np.repeat(b[sl],8,0),8,1);save_map(out/'review'/f'crop_{d}m_LOD0_left_LOD1_right.png',pair(aa,bb))
        (out/'evidence/lod_comparison.json').write_text(json.dumps(stats,indent=2)+'\n')
    if piece['class']=='S':
        for distance in (12,150,450):
            a=read(out/'review'/f'distance_{distance}m_lod0.png');h,w=a.shape[:2];crop=a[h//2-24:h//2+24,w//2-24:w//2+24];save_map(out/'review'/f'crop_{distance}m_lod0_8x.png',np.repeat(np.repeat(crop,8,0),8,1))
    colours={}
    for mode in ('night','day'):
        for distance in (150,450):
            file=f'distance_{distance}m_lod0.png' if mode=='night' else f'distance_day_{distance}m_lod0.png'
            a=read(out/'review'/file);h,w=a.shape[:2];crop=a[h//2-24:h//2+24,w//2-24:w//2+24]
            if mode=='day':save_map(out/'review'/f'crop_day_{distance}m_lod0_8x.png',np.repeat(np.repeat(crop,8,0),8,1))
            peak=crop.reshape((-1,3))[np.argmax(crop.max(2))]
            red=crop[:,:,0];coloured=(red>crop[:,:,1]*1.25)&(red>crop[:,:,2]*1.25)&(red-crop[:,:,2]>.01)
            colours[f'{mode}_{distance}m']=dict(peak_display_rgb=peak.tolist(),red_amber_pixels=int(coloured.sum()),scope='48x48 centre crop of native frame, no bloom; brightest day pixel can be background')
    (out/'evidence/distance_colour.json').write_text(json.dumps(colours,indent=2)+'\n')
    (out/'evidence/reference_comparison.json').write_text(json.dumps(dict(reference='style_reference_04.png' if name=='ServiceBayLight' else 'producer_reference.png',crop_xyxy=crops[name],order='reference LEFT, Round 4 RIGHT; 07 left is family finish reference, not its silhouette'),indent=2)+'\n')


def stable(piece):
    if piece['class']=='S':return
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}_Preview.blend'));scene=bpy.context.scene;gpu()
    # Always compare the current authoritative source, even before preview recapture.
    for suffix in ('','_LOD1'):bpy.data.objects.remove(bpy.data.objects[piece['mesh']+suffix],do_unlink=True)
    with bpy.data.libraries.load(str(out/'source'/f'{name}.blend'),link=False) as (src,dst):dst.objects=[piece['mesh'],piece['mesh']+'_LOD1']
    for obj in dst.objects:scene.collection.objects.link(obj)
    a=bpy.data.objects[piece['mesh']];b=bpy.data.objects[piece['mesh']+'_LOD1'];a.hide_set(False);b.hide_set(False);cam=scene.camera;cam.data.type='PERSP';cam.data.lens=cam.data.sensor_width/(2*math.tan(math.radians(30)));record=json.loads((out/'evidence/cameras.json').read_text());target=Vector(record['target_m']);direction=Vector((.9,-1.2,.85)).normalized()
    scene.render.resolution_x=1920;scene.render.resolution_y=1080;scene.render.resolution_percentage=100;scene.render.use_border=True;scene.render.use_crop_to_border=False;scene.render.border_min_x=896/1920;scene.render.border_max_x=1024/1920;scene.render.border_min_y=476/1080;scene.render.border_max_y=604/1080;scene.cycles.samples=512;scene.cycles.use_adaptive_sampling=False;scene.cycles.use_denoising=False;scene.cycles.seed=393
    from lighting_393_finish import read
    result={}
    for distance in (150,260):
        cam.location=target+direction*distance;aim(cam,target)
        for lod in (0,1):
            a.hide_render=lod!=0;b.hide_render=lod!=1;scene.render.filepath=str(out/'review'/f'stable_{distance}m_lod{lod}.png');bpy.ops.render.render(write_still=True)
        aa=read(out/'review'/f'stable_{distance}m_lod0.png');bb=read(out/'review'/f'stable_{distance}m_lod1.png');delta=np.max(abs(aa-bb),2);result[str(distance)]=dict(changed_pixels_gt_1_of_255=int((delta>1/255+.0001).sum()),max_display_delta=float(delta.max()))
    (out/'evidence/stable_distance_comparison.json').write_text(json.dumps(dict(scope='Blender-only 512 fixed samples without denoising/adaptive sampling; 128x128 centre render region at native 1920x1080 / 60deg; outside region unrendered',distances=result),indent=2)+'\n')

def audit(piece):
    sys.path.insert(0,str(ROOT/'Tools/Art'));import kit_blender
    out=KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');manifest=json.loads((KIT/'_work_393/manifest_393.json').read_text())[piece['id']]
    bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'))
    obs=[bpy.data.objects[piece['mesh']+s] for s in ([''] if piece['class']=='S' else ['','_LOD1'])]
    digests=[kit_blender.digest(o) for o in obs];assert digests==manifest['digest'],(piece['id'],digests,manifest['digest'])
    uv_contract={}
    if 'Warning' in name:
        for i,o in enumerate(obs):
            c=next(c for c in json.loads(o['chart_layout']) if c.get('dome'));uv_contract['lod'+str(i)]=dict(U0_at_positive_X=c['offset'][0],U_width=c['size'][0]*c['scale'],V0=c['offset'][1],V_height=c['size'][1]*c['scale'],angular_direction='+X toward +Y counterclockwise',dome_islands=1)
    if len(obs)==2:
        for o in obs:o.hide_set(False);o.hide_render=False;o.select_set(True)
        bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.join();combined={str(ch):uv_overlap(obs[0],ch) for ch in (0,1)};assert all(v['overlapping_triangle_pairs']==0 for v in combined.values()),combined
    else:combined='one LOD; see per-LOD checks'
    e=json.loads((out/'evidence/verification.json').read_text());e['source_geometry_digests']=digests;e['combined_lod_uv_overlap']=combined;e['lens_uv_contract']=uv_contract;(out/'evidence/verification.json').write_text(json.dumps(e,indent=2)+'\n')
    records=[]
    for p in sorted(out.rglob('*')):
        if not p.is_file() or 'archive' in p.relative_to(out).parts or p.suffix=='.log' or p.name=='delivery.json':continue
        records.append(dict(path=str(p.relative_to(out)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    data=dict(round=4,source_matches_runner=True,source_geometry_digests=digests,runner=manifest,combined_lod_atlas_uv_overlap=combined,uv_contract=uv_contract,artifacts=records,scope='Blender sources / Cycles renders / independent FBX import; no Unreal or producer approval')
    data['generator_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob('*.py') if p.name.startswith('lighting_393_') or p.name in ('service_bay_light.py','warning_beacon.py','indicator_led.py','mast_beacon_single_393.py','mast_beacon_dual.py')}
    (out/'evidence/delivery.json').write_text(json.dumps(data,indent=2)+'\n')
    print('AUDITED',piece['id'],flush=True)

if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:]
    for piece in PIECES:
        if len(args)>1 and piece['id'].split('/')[-1] not in args[1:]:continue
        globals()[args[0]](piece)
