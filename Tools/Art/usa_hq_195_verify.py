"""Independent FBX round trip and unchanged mast surface verification."""
import bpy,json,hashlib,math
from pathlib import Path
from collections import Counter
from mathutils import Vector
from mathutils.kdtree import KDTree
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq';OUT=ROOT/'issue195/phase_b'
def read(path):
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(path))
    return [o for o in bpy.data.objects if o.type=='MESH']
def triangles(obs,offset=(0,0,0)):
    rows=[]
    for o in obs:
        m=o.data;m.calc_loop_triangles()
        for t in m.loop_triangles:
            coords=[o.matrix_world@m.vertices[i].co+Vector(offset) for i in t.vertices]
            normal_matrix=o.matrix_world.to_3x3().inverted().transposed()
            normals=[tuple((normal_matrix@m.corner_normals[i].vector).normalized()) for i in t.loops]
            rows.append((m.materials[t.material_index].name.removeprefix('MI_').split('.')[0],[tuple(v) for v in coords],normals))
    return rows
results={}
for lod in (0,1):
    suffix='_LOD1' if lod else '';expected=[]
    for key,foot in [('Core',(-8.9871225,0,11.51)),('East',(-10.9,19.95,7.7))]:expected.extend(triangles(read(ROOT/f'fbx/SM_HQ22_Mast{key}_H28{suffix}.fbx'),foot))
    obs=read(OUT/f'fbx/SM_HQ195_MastsFixed{suffix}.fbx');actual=triangles(obs)
    tree=KDTree(len(actual))
    for i,(_,vs,_) in enumerate(actual):tree.insert(sum((Vector(v) for v in vs),Vector())/3,i)
    tree.balance();missing=[];max_error=0;max_normal_angle=0
    for s,vs,ns in expected:
        matches=[]
        for _,i,dist in tree.find_range(sum((Vector(v) for v in vs),Vector())/3,1e-4):
            a,b,bn=actual[i]
            if a==s:
                error=max(min((Vector(v)-Vector(w)).length for w in b) for v in vs)
                angle=max(math.degrees(Vector(n).angle(Vector(bn[min(range(3),key=lambda j:(Vector(v)-Vector(b[j])).length)]))) for v,n in zip(vs,ns))
                matches.append((error,angle))
        err,angle=min(matches) if matches else (1,180)
        max_normal_angle=max(max_normal_angle,angle)
        max_error=max(max_error,err)
        if err>1e-5:missing.append((s,vs,err))
    report={'triangles':len(actual),'mast_triangles':len(expected),'missing_or_changed_mast_triangles':len(missing),'max_mast_vertex_roundtrip_error_m':max_error,'max_mast_corner_normal_error_degrees':max_normal_angle,'slots':[m.name for m in obs[0].data.materials],'degenerate_triangles':0,'invalid_uv_faces':0,'helpers':{o.name:{'location_m':list(o.matrix_world.translation),'x_axis':list(o.matrix_world.to_3x3()@Vector((1,0,0)))} for o in bpy.data.objects if o.type=='EMPTY'}}
    for o in obs:
        m=o.data;m.calc_loop_triangles()
        for t in m.loop_triangles:
            if t.area<1e-12:report['degenerate_triangles']+=1
            if m.uv_layers:
                uv=[m.uv_layers.active.data[i].uv for i in t.loops];a,b=uv[1]-uv[0],uv[2]-uv[0]
                if abs(a.x*b.y-a.y*b.x)<1e-12:report['invalid_uv_faces']+=1
    assert not missing, missing[:2]
    assert report['degenerate_triangles']==0,report
    results[str(lod)]=report
    print('VERIFY',lod,report,flush=True)
parts={}
for name,expected in [('SM_HQ195_Flood',104),('SM_HQ195_Beacon',72)]:
    for suffix in ('','_LOD1'):
        obs=read(OUT/f'fbx/{name}{suffix}.fbx');ts=triangles(obs)
        assert len(ts)==expected,(name,len(ts));assert all(t.area>1e-12 for o in obs for t in o.data.loop_triangles)
        parts[name+suffix]={'triangles':len(ts),'slots':[m.name for m in obs[0].data.materials],'bounds_m':[[min((o.matrix_world@v.co)[i] for o in obs for v in o.data.vertices) for i in range(3)],[max((o.matrix_world@v.co)[i] for o in obs for v in o.data.vertices) for i in range(3)]]}
results['shared_parts']=parts
# Prove split/rebatch retained all 400 additive fixture triangles in world space.
expected_fixtures=triangles(read(OUT/'fbx/SM_HQ195_Fixtures.fbx'))
actual_all=triangles(read(OUT/'fbx/SM_HQ195_MastsFixed.fbx'))
manifest=json.loads((OUT/'fixture_manifest.json').read_text())
from mathutils import Quaternion
for inst in manifest['instances']:
    kind='Flood' if inst['kind']=='flood' else 'Beacon';obs=read(OUT/f'fbx/SM_HQ195_{kind}.fbx')
    for o in obs:
        o.location=inst['location_m']
        if inst['kind']=='flood':o.rotation_mode='QUATERNION';o.rotation_quaternion=Quaternion(inst['quaternion_wxyz'])
    bpy.context.view_layer.update();actual_all.extend(triangles(obs))
tree=KDTree(len(actual_all))
for i,(_,vs,_) in enumerate(actual_all):tree.insert(sum((Vector(v) for v in vs),Vector())/3,i)
tree.balance();errors=[]
for s,vs,_ in expected_fixtures:
    ds=[max(min((Vector(v)-Vector(w)).length for w in actual_all[i][1]) for v in vs) for _,i,d in tree.find_range(sum((Vector(v) for v in vs),Vector())/3,1e-4) if actual_all[i][0]==s]
    errors.append(min(ds) if ds else 1)
assert max(errors)<2e-5,max(errors) # float32 world/template round-trip; 0.02 mm tolerance
results['fixture_split_preservation']={'matched_triangles':len(errors),'max_position_error_m':max(errors)}
(OUT/'fbx_verification.json').write_text(json.dumps(results,indent=2))
hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [OUT/'USA_HQ_PhaseB.blend',OUT/'Fixtures_Library.blend',*sorted((OUT/'fbx').glob('*.fbx'))]}
(OUT/'delivery_hashes.json').write_text(json.dumps(hashes,indent=2))
