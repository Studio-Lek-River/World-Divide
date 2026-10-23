"""Kit 04: simple ceiling strip light. Blender authoring only; no gameplay code.

Run Blender --background --python this_file -- [build|materials|render|verify].
Local frame: metres, mounting face X=0, emission +X, length Y, width Z.
Rebuild replaces ONLY this piece's own named files. Safe to import.
"""
import bpy
import bmesh
import json
import math
import sys
import hashlib
import shutil
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Art/Shared/kit/lighting/04_StripLight'
sys.path.insert(0,str(Path(__file__).resolve().parent))


def material(name, color, roughness, metallic=0, emission=0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = roughness
    p.inputs['Metallic'].default_value = metallic
    p.inputs['Emission Color'].default_value = (1, .73, .40, 1)
    p.inputs['Emission Strength'].default_value = emission
    m.diffuse_color = (*color, 1)
    return m


def prism(name, yz_profile, y0, y1, mat):
    # Profile tuples are depth X and width Z; extrude along fixture length Y.
    n = len(yz_profile)
    vs = [(x, y, z) for y in (y0, y1) for x, z in yz_profile]
    fs = [tuple(range(n-1, -1, -1)), tuple(range(n, n*2))]
    fs += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(vs, [], fs)
    me.materials.append(mat)
    me.update()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bm.to_mesh(me); bm.free(); me.update()
    uv = me.uv_layers.new(name='UVMap')
    # Uniform 512px/m on a future 2K trim atlas (0.25 UV/m).
    # Lens is allowed 4x density: 1024px/m here on the future 1K lens atlas.
    density = 1 if mat.name == 'Lens' else .25
    for p in me.polygons:
        dominant = max(range(3), key=lambda i: abs(p.normal[i]))
        axes = [i for i in range(3) if i != dominant]
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            uv.data[li].uv = (co[axes[0]]*density+.5, co[axes[1]]*density+.5)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def profile(x0, x1, z0, z1, cut=0):
    if not cut:
        return [(x0,z0),(x1,z0),(x1,z1),(x0,z1)]
    return [(x0,z0+cut),(x0+cut,z0),(x1-cut,z0),(x1,z0+cut),
            (x1,z1-cut),(x1-cut,z1),(x0+cut,z1),(x0,z1-cut)]


def build_mesh():
    housing = material('Housing', (.075,.085,.09), .52, .65)
    lens = material('Lens', (.82,.78,.65), .34, 0, 3)
    obs = []
    def part(name, p, a, b, m=housing):
        o = prism(name, p, a, b, m); obs.append(o); return o
    # Broad, legible layers: mounting spine, dark body, rim and diffuser.
    part('MountingSpine', profile(0,.023,-.071,.071,.005), -.575,.575)
    part('HousingPan', profile(.021,.061,-.09,.09,.008), -.60,.60)
    for s in (-1,1):
        za,zb = sorted((s*.073,s*.091))
        part('LongRim', profile(.051,.087,za,zb,.004), -.565,.565)
    # One solid milky diffuser. No tubes, micro-ribs, bolts or internal wiring.
    part('Lens', profile(.060,.108,-.073,.073,.011), -.553,.553,lens)
    for a,b in [(-.60,-.56),(.56,.60)]:
        part('EndCap', profile(.028,.091,-.091,.091,.007),a,b)
    # Distinctive two end retaining straps, bent around the underside.
    for y in (-.528,.528):
        for s in (-1,1):
            za,zb = sorted((s*.077,s*.090))
            part('RetainerSide',profile(.063,.120,za,zb),y-.010,y+.010)
        part('RetainerBridge',profile(.111,.124,-.086,.086,.004),y-.010,y+.010)
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs: o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    o = obs[0]; o.name='SM_Kit_StripLight'
    bpy.context.scene.cursor.location=(0,0,0)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    # Join preserves islands; keep exactly the two surface slots in fixed order.
    old = [s.material.name for s in o.material_slots]
    ids = [old[p.material_index] for p in o.data.polygons]
    o.data.materials.clear()
    o.data.materials.append(housing); o.data.materials.append(lens)
    for p,n in zip(o.data.polygons,ids): p.material_index = int(n=='Lens')
    return o


def report(o):
    me=o.data; me.calc_loop_triangles()
    bounds=[[min(v.co[i] for v in me.vertices),max(v.co[i] for v in me.vertices)] for i in range(3)]
    bad = [t.index for t in me.loop_triangles if t.area < 1e-12]
    bad_uv = 0
    uv=me.uv_layers.active
    for t in me.loop_triangles:
        a,b,c=[uv.data[i].uv for i in t.loops]
        if abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)) < 1e-12: bad_uv+=1
    return {'triangles':len(me.loop_triangles),'vertices':len(me.vertices),
            'slots':[s.material.name for s in o.material_slots],
            'dimensions_m':[b-a for a,b in bounds], 'bounds_m':bounds,
            'degenerate_triangles':len(bad),'degenerate_uv_triangles':bad_uv}


def build(piece):
    """The kit generator's entry (Tools/Art/kit_blender.py, #393): the approved model from source/StripLight.blend, as LOD0
    and, unchanged, as LOD1 (300 triangles, inside class M's LOD1 budget)."""
    with bpy.data.libraries.load(str(OUT/'source/StripLight.blend')) as (data_from,data_to):
        data_to.objects=[piece['mesh']]
    lod0=data_to.objects[0]
    bpy.context.scene.collection.objects.link(lod0)
    lod1=lod0.copy();lod1.data=lod0.data.copy();lod1.name=f"{piece['mesh']}_LOD1"
    bpy.context.scene.collection.objects.link(lod1)
    return lod0,lod1


def build_source():
    OUT.mkdir(parents=True,exist_ok=True)
    for p in ('source','review','evidence'): (OUT/p).mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system='METRIC'
    bpy.context.scene.unit_settings.scale_length=1
    o=build_mesh()
    if (OUT/'material_recipe.json').exists():
        from strip_light_materials import apply_materials
        apply_materials()
    r=report(o)
    assert r['triangles']<=400 and r['slots']==['Housing','Lens'],r
    assert not r['degenerate_triangles'] and not r['degenerate_uv_triangles'],r
    o['mount_frame']='X=0 mounting face, +X emission, Y length, Z width; metres'
    o['Lens_center_cm']=[8.4,0,0]
    o['LOD_policy']='Identical LOD0/LOD1: already under class M LOD1 budget; no loss at switch.'
    for suffix in ('','_LOD1'):
        bpy.ops.export_scene.fbx(filepath=str(OUT/f'SM_Kit_StripLight{suffix}.fbx'),
            use_selection=True,object_types={'MESH'},mesh_smooth_type='FACE',
            use_tspace=True,colors_type='NONE',apply_unit_scale=True,
            apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_anim=False)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'SM_Kit_StripLight.glb'),use_selection=True,export_format='GLB')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/StripLight.blend'))
    r.update({'mesh':o.name,'lod0':r['triangles'],'lod1':r['triangles'],
        'source_frame':'metres; origin mount centre, +X out, long axis Y, Z up',
        'lens_bounds_cm':[[6,10.8],[-55.3,55.3],[-7.3,7.3]],
        'preview_light':{'type':'Rect','location_cm':[10.7,0,0],'direction':[1,0,0],
            'source_width_cm':108,'source_height_cm':13,'candela':180,'kelvin':3500,
            'radius_cm':600,'cast_shadows':False,'indirect_lighting_intensity':0},
        'textures':[], 'approval':'pending producer appearance review',
        'integration':'Claude; catalogue, shared atlas binding, runtime power and flicker not authored here'})
    (OUT/'model_manifest.json').write_text(json.dumps(r,indent=2))
    print(json.dumps(r),flush=True)


def materials():
    from strip_light_materials import apply_materials, geometry_digest
    path=OUT/'source/StripLight.blend'
    backup=OUT/'source/StripLight_FlatMaterials.blend'
    if not backup.exists():shutil.copy2(path,backup)
    bpy.ops.wm.open_mainfile(filepath=str(path))
    o=bpy.data.objects['SM_Kit_StripLight']
    before=geometry_digest(o)
    fbx_before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*.fbx')}
    apply_materials()
    after=geometry_digest(o)
    assert before==after,'Material update must never change geometry/UVs/transforms'
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    bpy.ops.wm.open_mainfile(filepath=str(path))
    assert geometry_digest(bpy.data.objects['SM_Kit_StripLight'])==before
    fbx_after={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*.fbx')}
    assert fbx_before==fbx_after
    receipt={'geometry_before':before,'geometry_after_reload':before,'fbx_unchanged':fbx_before,
             'triangles':300,'material_slots':['Housing','Lens'],'uvs_unchanged':True,
             'source_backup':str(backup.relative_to(OUT))}
    (OUT/'evidence/material_only_verification.json').write_text(json.dumps(receipt,indent=2))
    manifest=json.loads((OUT/'model_manifest.json').read_text())
    manifest['textures']=json.loads((OUT/'material_recipe.json').read_text())['textures']
    manifest['material_recipe']='material_recipe.json'
    manifest['preview_light']['kelvin']=3500
    (OUT/'model_manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(receipt),flush=True)


def verify():
    results={}
    for suffix in ('','_LOD1'):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        p=OUT/f'SM_Kit_StripLight{suffix}.fbx'
        bpy.ops.import_scene.fbx(filepath=str(p),use_custom_normals=True)
        obs=[o for o in bpy.context.scene.objects if o.type=='MESH']
        assert len(obs)==1
        o=obs[0]
        # Importer may store unit conversion in object scale; report in world metres.
        bpy.context.view_layer.objects.active=o; o.select_set(True)
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        r=report(o)
        r['uv_channels']=[layer.name for layer in o.data.uv_layers]
        if (OUT/'evidence/finish_verification.json').exists():
            assert 'FinishBake' in r['uv_channels'],r
        assert r['triangles']<=400 and r['slots']==['Housing','Lens'],r
        assert max(r['dimensions_m'])>1.199 and max(r['dimensions_m'])<1.201,r
        assert not r['degenerate_triangles'] and not r['degenerate_uv_triangles'],r
        r['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
        results[p.name]=r
    (OUT/'evidence/fbx_reimport.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results),flush=True)


def aim(o,target): o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()


def render():
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/StripLight.blend'))
    scene=bpy.context.scene
    o=bpy.data.objects['SM_Kit_StripLight']
    o.rotation_euler[1]=math.pi/2; o.location=(0,0,2.7)
    # Preview-only room. It is never selected for export.
    concrete=material('PreviewConcrete',(.22,.24,.25),.9)
    def cube(name,loc,scale):
        bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
        q=bpy.context.object; q.name=name; q.scale=scale; q.data.materials.append(concrete)
    cube('PreviewCeiling',(0,0,2.76),(2.7,3,.12))
    cube('PreviewWall',(-.63,0,1.34),(.12,3,2.7))
    cube('PreviewFloor',(0,0,-.07),(2.7,3,.12))
    ld=bpy.data.lights.new('Lamp_RealLight','AREA'); ld.energy=36
    ld.color=(1,.779,.615); ld.shape='RECTANGLE'; ld.size=1.08; ld.size_y=.13
    light=bpy.data.objects.new('Lamp_RealLight',ld);scene.collection.objects.link(light)
    light.location=(0,0,2.59); light.rotation_euler[2]=math.pi/2
    # Area light emits local -Z, down, mounted at the diffuser.
    def fill(name,loc,power,size):
        d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size
        q=bpy.data.objects.new(name,d);scene.collection.objects.link(q);q.location=loc;aim(q,(0,0,2.5))
        return q
    key=fill('PreviewKey',(1.5,-1.4,2.3),90,2)
    rim=fill('PreviewFill',(.4,1.8,1.8),35,2)
    world=bpy.data.worlds.new('PreviewWorld');world.use_nodes=True;scene.world=world
    bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
    bg.inputs['Color'].default_value=(.10,.14,.2,1);bg.inputs['Strength'].default_value=.15
    camd=bpy.data.cameras.new('ReviewCamera');cam=bpy.data.objects.new('ReviewCamera',camd)
    scene.collection.objects.link(cam);scene.camera=cam;cam.location=(1.65,-1.55,1.65)
    aim(cam,(0,0,2.59));camd.lens=55
    scene.render.engine='CYCLES';scene.cycles.samples=64;scene.cycles.use_denoising=True
    try:
        pref=bpy.context.preferences.addons['cycles'].preferences
        pref.compute_device_type='OPTIX';pref.get_devices()
        gpu=[d for d in pref.devices if d.type!='CPU']
        for d in pref.devices:d.use=d.type!='CPU'
        if gpu:scene.cycles.device='GPU'
        print('Cycles GPU:',[d.name for d in gpu],flush=True)
    except Exception as e:print('Cycles CPU fallback',str(e),flush=True)
    size=3840 if '--4k' in sys.argv else 1600
    scene.render.resolution_x=size;scene.render.resolution_y=int(size*1000/1400);scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.view_settings.view_transform='AgX'
    level=bpy.data.materials['Lens'].node_tree.nodes['LampLevel']
    for name,on,night in [('pbr_day_on',True,False),('pbr_night_on',True,True),('pbr_day_off',False,False),('pbr_night_off',False,True)]:
        ld.energy=36 if on else 0
        level.outputs[0].default_value=int(on)
        key.data.energy=10 if night else 90;rim.data.energy=8 if night else 35
        bg.inputs['Strength'].default_value=.035 if night else .15
        scene.render.filepath=str(OUT/'review'/f'{name}.png');bpy.ops.render.render(write_still=True)
    # Source remains the clean model; preview is independently editable.
    ld.energy=36;level.outputs[0].default_value=1
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/StripLight_Preview.blend'))


def detail():
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/StripLight_Preview.blend'))
    scene=bpy.context.scene;cam=scene.camera
    cam.location=(.50,-.87,2.43);aim(cam,(0,-.39,2.63));cam.data.lens=62
    key=bpy.data.objects['PreviewKey'];key.location=(.25,-.8,2.48)
    key.data.energy=24;key.data.size=.45;aim(key,(0,-.38,2.65))
    bpy.data.objects['PreviewFill'].data.energy=12
    scene.render.resolution_x=2400;scene.render.resolution_y=1600
    scene.render.filepath=str(OUT/'review/finish_detail.png')
    bpy.ops.render.render(write_still=True)

if __name__=='__main__':
    from strip_light_finish import finish
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['build']
    {'build':build_source,'materials':materials,'finish':finish,'verify':verify,'render':render,'detail':detail}[args[0]]()
