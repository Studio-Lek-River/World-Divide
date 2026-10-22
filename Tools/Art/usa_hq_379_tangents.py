"""Inspect the shell-detail LOD1 export's geometric and UV degeneracy."""
import bpy,json,collections,sys,hashlib,runpy,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(ROOT/'game/SM_HQ_ShellDetail_LOD1.fbx'))
o=next(o for o in bpy.data.objects if o.type=='MESH');m=o.data;m.calc_loop_triangles();uv=m.uv_layers.active.data
report={'triangles':len(m.loop_triangles),'geometric_area_counts':{},'uv_area_counts':{},'bad_by_material':{},'samples':[]}
for eps in (1e-18,1e-16,1e-14,1e-12,1e-10,1e-8,1e-6):
    report['geometric_area_counts'][str(eps)]=sum(t.area<eps for t in m.loop_triangles)
    report['uv_area_counts'][str(eps)]=0
bad=collections.Counter()
for t in m.loop_triangles:
    a,b,c=[uv[i].uv.copy() for i in t.loops];det=abs((b-a).cross(c-a))/2
    for eps in report['uv_area_counts']:
        if det<float(eps):report['uv_area_counts'][eps]+=1
    if det<1e-10 or t.area<1e-12:
        bad[m.materials[t.material_index].name]+=1
        if len(report['samples'])<30:report['samples'].append({'area':t.area,'uv_area':det,'material':m.materials[t.material_index].name,'coords':[list(m.vertices[i].co) for i in t.vertices],'uv':[list(v) for v in (a,b,c)]})
report['bad_by_material']=dict(bad)
m.calc_tangents(uvmap=m.uv_layers.active.name)
report['zero_tangent_loops']=sum(l.tangent.length<1e-4 for l in m.loops)
report['zero_bitangent_loops']=sum(l.bitangent.length<1e-4 for l in m.loops)
report['zero_tangent_samples']=[{'loop':l.index,'normal':list(m.corner_normals[l.index].vector),'uv':list(uv[l.index].uv),'material':m.materials[p.material_index].name,'face_normal':list(p.normal)} for p in m.polygons for l in [m.loops[i] for i in p.loop_indices] if l.tangent.length<1e-4][:40]
(ROOT/'issue379/tangent_diagnostics.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
if '--fix' in sys.argv:
    source=ROOT/'game/SM_HQ_ShellDetail_LOD1.fbx';backup=ROOT/'issue379/baseline/SM_HQ_ShellDetail_LOD1_pre_tangent_fix.fbx'
    if not backup.exists():shutil.copy2(source,backup)
    lib=runpy.run_path(str(Path(__file__).with_name('usa_hq_game_blender.py')))
    bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    geometry=lambda:hashlib.sha256(repr(([tuple(v.co) for v in o.data.vertices],[(tuple(p.vertices),p.material_index) for p in o.data.polygons])).encode()).hexdigest()
    before=geometry()
    faces=lambda:{(tuple(sorted(tuple(o.data.vertices[i].co) for i in p.vertices)),p.material_index) for p in o.data.polygons}
    original_faces=faces();fixed=lib['repair_detail_tangent_uvs'](o);assert faces().issubset(original_faces)
    o.name='SM_HQ_ShellDetail_LOD1';lib['export'](o,str(source))
    o.data.calc_tangents(uvmap=o.data.uv_layers.active.name)
    counts=lib['per_slot'](o);game=json.loads((ROOT/'game/game_meshes.json').read_text())
    game['meshes']['SM_HQ_ShellDetail']['triangles'][1]=sum(counts.values())
    game['meshes']['SM_HQ_ShellDetail']['slot_triangles'][1]=counts
    (ROOT/'game/game_meshes.json').write_text(json.dumps(game,indent=1)+'\n')
    repair={'fixed_uv_faces':fixed,'removed_sliver_faces':o.get('issue379_removed_sliver_faces',0),'max_sliver_width_m':o.get('issue379_max_sliver_width_m',0),'surviving_faces_positions_and_materials_unchanged':True,'before_geometry_hash':before,'triangles':sum(counts.values()),'zero_tangent_loops_after':sum(l.tangent.length<1e-4 for l in o.data.loops),'zero_bitangent_loops_after':sum(l.bitangent.length<1e-4 for l in o.data.loops),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
    (ROOT/'issue379/tangent_repair.json').write_text(json.dumps(repair,indent=2));print(json.dumps(repair,indent=2))
