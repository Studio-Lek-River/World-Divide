"""Additive Phase B authoring. Never overwrites approved source or exports.
Run in background Blender; -- probe inspects beams; -- build creates own delivery.
"""
import bpy,bmesh,json,math,hashlib,sys
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
OUT=ROOT/'issue195/phase_b'; EXPORT=OUT/'fbx'
SLOTS=['HQ17_DarkStructure','HQ17_FramePaint','HQ17_TeamLight','HQ20_FixtureWarm','HQ195_BeaconAmber']
def digest(o):
    return hashlib.sha256(repr((tuple(o.matrix_world),[tuple(v.co) for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons],[s.material.name if s.material else None for s in o.material_slots])).encode()).hexdigest()
def open_source():bpy.ops.wm.open_mainfile(filepath=str(ROOT/'source/USA_HQ_Final.blend'))
def beam_probe():
    open_source(); dg=bpy.context.evaluated_depsgraph_get(); results=[]
    for side in (1,):
        for x,y,z in [(8,-14,7.5),(8,-13.5,7.5),(12,10.5,8.05)]:
            p=Vector((x,side*y,z));target=Vector((-1.294,0,5.75));d=(target-p).normalized()
            hit,co,n,idx,obj,mat=bpy.context.scene.ray_cast(dg,p,d,distance=(target-p).length+.05)
            down=bpy.context.scene.ray_cast(dg,p,Vector((0,0,-1)),distance=10)
            results.append({'start_m':list(p),'distance_m':(target-p).length,'hit':obj.name if hit else None,'hit_m':list(co) if hit else None,'support':down[4].name if down[0] else None,'support_m':list(down[1]) if down[0] else None})
    (OUT/'beam_probes.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
def facade_probe():
    open_source();dg=bpy.context.evaluated_depsgraph_get();manifest=json.loads((OUT/'fixture_manifest.json').read_text());rows=[]
    for name,h in manifest['helpers'].items():
        if 'Flood' not in name:continue
        cm=h['location_cm'];origin=Vector((cm[0]/100,-cm[1]/100,cm[2]/100))
        for y in (-2.5,0,2.5):
            for z in (4.8,5.75,6.65):
                target=Vector((-1.294,y,z));ray=(target-origin);hit,co,n,idx,o,m=bpy.context.scene.ray_cast(dg,origin,ray.normalized(),distance=ray.length+.15)
                rows.append({'helper':name,'target_m':list(target),'first_hit':o.name if hit else None,'first_hit_m':list(co) if hit else None,'distance_to_target_m':ray.length,'hit_distance_m':(co-origin).length if hit else None})
    (OUT/'facade_visibility.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows),flush=True)
def material(name):
    m=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    if name=='HQ195_BeaconAmber':
        m.use_nodes=True;p=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(.32,.095,.007,1);p.inputs['Roughness'].default_value=.25;p.inputs['Emission Color'].default_value=(2.4,1,.1,1);p.inputs['Emission Strength'].default_value=1
    return m
def mesh(name,vs,fs,mats,indices=None):
    m=bpy.data.meshes.new(name);m.from_pydata(vs,[],fs);m.update()
    for s in mats:m.materials.append(material(s))
    for p,i in zip(m.polygons,indices or [0]*len(fs)):p.material_index=i
    bm=bmesh.new();bm.from_mesh(m);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(m);bm.free()
    uv=m.uv_layers.new(name='UVMap')
    for p in m.polygons:
        axes=[i for i in range(3) if i!=max(range(3),key=lambda i:abs(p.normal[i]))]
        for li in p.loop_indices:
            co=m.vertices[m.loops[li].vertex_index].co;uv.data[li].uv=(co[axes[0]],co[axes[1]])
    o=bpy.data.objects.new(name,m);bpy.context.scene.collection.objects.link(o);return o
def box(name,center,size):
    c=Vector(center);v=[c+Vector((a*size[0]/2,b*size[1]/2,d*size[2]/2)) for a,b,d in [(-1,-1,-1),(-1,-1,1),(-1,1,-1),(-1,1,1),(1,-1,-1),(1,-1,1),(1,1,-1),(1,1,1)]]
    return mesh(name,v,[(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)],['HQ17_DarkStructure'])
def cylinder(name,rings,n,mat):
    vs=[(r*math.cos(i*math.tau/n),r*math.sin(i*math.tau/n),z) for z,r in rings for i in range(n)]
    fs=[tuple(range(n-1,-1,-1)),tuple((len(rings)-1)*n+i for i in range(n))]+[(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for j in range(len(rings)-1) for i in range(n)]
    return mesh(name,vs,fs,[mat])
def join(obs,name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs:o.hide_set(False);o.select_set(True)
    bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.join();o=obs[0];o.name=name
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');return o
def flood(lod):
    # Housing 35 cm wide, 25 high, 20 deep. +X lens normal.
    yz=[(-.155,-.125),(.155,-.125),(.175,-.105),(.175,.105),(.155,.125),(-.155,.125),(-.175,.105),(-.175,-.105)]
    vs=[(x,y*scale,z*scale) for x,scale in [(-.2,.90),(0,1),(.004,.84)] for y,z in yz]
    fs=[tuple(range(7,-1,-1))]+[(j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i) for j in range(2) for i in range(8)]+[tuple(range(16,24))]
    ids=[0]*(len(fs)-1)+[1]
    housing=mesh('HQ195_Flood_Housing_Lens',vs,fs,['HQ17_DarkStructure','HQ20_FixtureWarm'],ids)
    # U bracket is a single extruded profile; no microscopic bolts.
    yz2=[(-.205,.015),(-.185,.015),(-.185,-.185),(.185,-.185),(.185,.015),(.205,.015),(.205,-.215),(-.205,-.215)]
    vs=[(x,y,z) for x in (-.135,-.095) for y,z in yz2]
    fs=[tuple(range(7,-1,-1)),tuple(range(8,16))]+[(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    bracket=mesh('HQ195_Flood_U_Yoke',vs,fs,['HQ17_DarkStructure'])
    plate=box('HQ195_Flood_MountingPlate',(-.115,0,-.235),(.16,.22,.04))
    pivot=cylinder('HQ195_Flood_TiltAxle',[(-.21,.032),(.21,.032)],6,'HQ17_DarkStructure')
    pivot.rotation_euler[0]=math.pi/2;pivot.location=(-.115,0,0)
    return join([housing,bracket,plate,pivot],f'SM_HQ195_Flood'+('_LOD1' if lod else ''))
def beacon(lod):
    n=6 if lod else 8
    base=cylinder('HQ195_Beacon_GasketBase',[(0,.0775),(.028,.0775)],n,'HQ17_DarkStructure')
    dome=cylinder('HQ195_Beacon_AmberDome',[(.028,.072),(.100,.067),(.124,.035)],n,'HQ195_BeaconAmber')
    for p in dome.data.polygons:p.use_smooth=True
    return join([base,dome],f'SM_HQ195_Beacon'+('_LOD1' if lod else ''))
def export(o,path,helpers=()):
    bpy.ops.object.select_all(action='DESELECT');o.hide_set(False);o.select_set(True);bpy.context.view_layer.objects.active=o
    for h in helpers:h.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','EMPTY'},use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,colors_type='NONE',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',add_leaf_bones=False,bake_anim=False)
def count(o):o.data.calc_loop_triangles();return len(o.data.loop_triangles)
def build():
    OUT.mkdir(parents=True,exist_ok=True);EXPORT.mkdir(exist_ok=True);open_source()
    protected={o.name:digest(o) for o in bpy.data.objects if o.type=='MESH'}
    helpers=[];instances=[];fixture_obs=[]
    target=Vector((-1.294,0,5.75))
    for name,p,roof in [('West',(8,-12.85,7.5),7.09),('East',(12,10.5,8.05),7.63)]:
        d=(target-Vector(p)).normalized();q=Vector((1,0,0)).rotation_difference(d)
        o=flood(0);o.name='HQ195_Flood_'+name;o.rotation_mode='QUATERNION';o.rotation_quaternion=q;o.location=p
        bottom=Vector(p)+q@Vector((-.115,0,-.255))
        stem=box('HQ195_Flood_RoofStandoff',(bottom.x,bottom.y,(roof+bottom.z)/2),(.13,.13,bottom.z-roof+.012))
        o=join([o,stem],o.name);fixture_obs.append(o)
        h=bpy.data.objects.new('SOCKET_Flood_'+name,None);bpy.context.scene.collection.objects.link(h);h.location=Vector(p)+q@Vector((.004,0,0));h.rotation_mode='QUATERNION';h.rotation_quaternion=q;helpers.append(h)
        instances.append({'kind':'flood','name':name,'location_m':p,'quaternion_wxyz':list(q)})
    for name,y in [('South',24.384518),('North',10.784518)]:
        o=beacon(0);o.name='HQ195_GarageBeacon_'+name
        # Wall-mounted platform with a dome upright near each pillar's top.
        o.location=(15.68,y,6.42)
        shelf=box('HQ195_Beacon_WallBracket',(15.59,y,6.41),(.22,.16,.02));o=join([o,shelf],o.name);fixture_obs.append(o)
        h=bpy.data.objects.new('SOCKET_GarageBeacon_'+name,None);bpy.context.scene.collection.objects.link(h);h.location=(15.68,y,6.496);helpers.append(h)
        instances.append({'kind':'beacon','name':name,'location_m':[15.68,y,6.42]})
    assert all(digest(bpy.data.objects[n])==v for n,v in protected.items())
    helperdata={h.name:{'location_cm':[h.location.x*100,-h.location.y*100,h.location.z*100],'beam_direction':[ (h.rotation_quaternion@Vector((1,0,0))).x,-(h.rotation_quaternion@Vector((1,0,0))).y,(h.rotation_quaternion@Vector((1,0,0))).z] if 'Flood' in h.name else None} for h in helpers}
    fixture_total=sum(count(o) for o in fixture_obs)
    assert fixture_total<=432,fixture_total
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'USA_HQ_PhaseB.blend'))
    # Save fixtures as a full building-space assembly; one supplemental source.
    copies=[]
    for o in fixture_obs:
        c=o.copy();c.data=o.data.copy();bpy.context.scene.collection.objects.link(c);copies.append(c)
    f=join(copies,'SM_HQ195_Fixtures');export(f,EXPORT/'SM_HQ195_Fixtures.fbx',helpers)
    result={'approved_source_sha256':hashlib.sha256((ROOT/'source/USA_HQ_Final.blend').read_bytes()).hexdigest(),'protected_meshes_unchanged':len(protected),'fixture_triangles':fixture_total,'helpers':helperdata,'instances':instances,'batches':[]}
    # Rebatch the approved exported mast surfaces with additive fixtures. Original files read only.
    for lod in (0,1):
        suffix='_LOD1' if lod else '';pieces=[]
        for key,foot in [('Core',(-8.9871225,0,11.51)),('East',(-10.9,19.95,7.7))]:
            before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(ROOT/f'fbx/SM_HQ22_Mast{key}_H28{suffix}.fbx'))
            for o in set(bpy.data.objects)-before:
                if o.type=='MESH':o.location+=Vector(foot);pieces.append(o)
        c=f.copy();c.data=f.data.copy();bpy.context.scene.collection.objects.link(c);pieces.append(c)
        o=join(pieces,'SM_HQ195_StaticEquipment'+suffix)
        # Collapse duplicate slots only. Preserve every face's actual material.
        old=list(o.data.materials);idx=[SLOTS.index(m.name.removeprefix('MI_').split('.')[0]) for m in old];faceidx=[idx[p.material_index] for p in o.data.polygons]
        o.data.materials.clear()
        for s in SLOTS:o.data.materials.append(material(s))
        for p,i in zip(o.data.polygons,faceidx):p.material_index=i
        export(o,EXPORT/(o.name+'.fbx'),helpers)
        result['batches'].append({'mesh':o.name,'triangles':count(o),'slots':SLOTS})
    (OUT/'fixture_manifest.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
def parts():
    """Preferred instanced delivery: fixed supports rebatch with unchanged masts.
    Both small fixture types retain independent component material/power state.
    """
    r=json.loads((OUT/'fixture_manifest.json').read_text())
    bpy.ops.wm.read_factory_settings(use_empty=True)
    flood_obj=flood(0);beacon_obj=beacon(0)
    for o in (flood_obj,beacon_obj):
        export(o,EXPORT/(o.name+'.fbx'))
        export(o,EXPORT/(o.name+'_LOD1.fbx'))
    local_counts={'flood':count(flood_obj),'beacon':count(beacon_obj)}
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Fixtures_Library.blend'))
    supports=[]
    for inst in r['instances']:
        p=Vector(inst['location_m'])
        if inst['kind']=='flood':
            from mathutils import Quaternion
            q=Quaternion(inst['quaternion_wxyz']);bottom=p+q@Vector((-.115,0,-.255));roof=7.09 if inst['name']=='West' else 7.63
            supports.append(box('HQ195_Flood_RoofStandoff_'+inst['name'],(bottom.x,bottom.y,(roof+bottom.z)/2),(.13,.13,bottom.z-roof+.012)))
        else:supports.append(box('HQ195_Beacon_WallBracket_'+inst['name'],(15.59,p.y,6.41),(.22,.16,.02)))
    helpers=[]
    for name,h in r['helpers'].items():
        o=bpy.data.objects.new(name,None);bpy.context.scene.collection.objects.link(o);o.location=(h['location_cm'][0]/100,-h['location_cm'][1]/100,h['location_cm'][2]/100)
        if h['beam_direction']:o.rotation_mode='QUATERNION';o.rotation_quaternion=Vector((1,0,0)).rotation_difference(Vector((h['beam_direction'][0],-h['beam_direction'][1],h['beam_direction'][2])))
        helpers.append(o)
    batches=[]
    for lod in (0,1):
        suffix='_LOD1' if lod else '';pieces=[]
        for key,foot in [('Core',(-8.9871225,0,11.51)),('East',(-10.9,19.95,7.7))]:
            before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(ROOT/f'fbx/SM_HQ22_Mast{key}_H28{suffix}.fbx'))
            for o in set(bpy.data.objects)-before:
                if o.type=='MESH':o.location+=Vector(foot);pieces.append(o)
        for o in supports:
            c=o.copy();c.data=o.data.copy();bpy.context.scene.collection.objects.link(c);pieces.append(c)
        o=join(pieces,'SM_HQ195_MastsFixed'+suffix);old=list(o.data.materials);ids=[SLOTS.index(m.name.removeprefix('MI_').split('.')[0]) for m in old];faceidx=[ids[p.material_index] for p in o.data.polygons]
        o.data.materials.clear()
        for s in SLOTS[:3]:o.data.materials.append(material(s))
        for p,i in zip(o.data.polygons,faceidx):p.material_index=i
        export(o,EXPORT/(o.name+'.fbx'),helpers);batches.append({'mesh':o.name,'triangles':count(o),'slots':SLOTS[:3]})
    r['preferred_instanced_delivery']={'fixed_batches':batches,'shared_parts':{'flood':{'mesh':'SM_HQ195_Flood','triangles_per_instance':local_counts['flood'],'slots':['HQ17_DarkStructure','HQ20_FixtureWarm']},'beacon':{'mesh':'SM_HQ195_Beacon','triangles_per_instance':local_counts['beacon'],'slots':['HQ17_DarkStructure','HQ195_BeaconAmber']}},'fixed_support_triangles':48,'whole_hq_triangles':[349968,39432],'unique_instanced_sections':80,'supersedes':['SM_HQ195_StaticEquipment','SM_HQ195_StaticEquipment_LOD1']}
    (OUT/'fixture_manifest.json').write_text(json.dumps(r,indent=2));print(json.dumps(r['preferred_instanced_delivery']),flush=True)
if __name__=='__main__':
    action=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'probe'
    {'probe':beam_probe,'facade_probe':facade_probe,'build':build,'parts':parts}[action]()
