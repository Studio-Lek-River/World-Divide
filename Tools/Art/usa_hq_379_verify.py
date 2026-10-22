"""Independent FBX reimport checks; cm-baked copies for native legacy preview importer."""
import bpy,json,hashlib,runpy
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
WORK=ROOT/'issue379';OUT=WORK/'preview_fbx';OUT.mkdir(exist_ok=True)
manifest=json.loads((WORK/'mast_manifest.json').read_text());checks=[]
for records in manifest['options'].values():
    for r in records:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=str(ROOT/r['fbx']))
        o=next(o for o in bpy.data.objects if o.type=='MESH');o.data.calc_loop_triangles()
        vs=[o.matrix_world@v.co for v in o.data.vertices]
        bounds=[[min(v[i] for v in vs) for i in range(3)],[max(v[i] for v in vs) for i in range(3)]]
        empties={o.name:list(o.matrix_world.translation) for o in bpy.data.objects if o.type=='EMPTY'}
        assert len(o.data.loop_triangles)==r['triangles']
        assert all(abs(bounds[j][i]-r['local_bounds_m'][j][i])<1e-4 for j in range(2) for i in range(3))
        socket='SOCKET_'+r['socket_name']
        assert socket in empties, 'Missing named socket empty'
        expected_tip=(r['tip_cm'][2]-r['foot_cm'][2])/100
        assert all(abs(a-b)<1e-4 for a,b in zip(empties[socket],(0,0,expected_tip))), 'Socket is off the rod tip'
        assert [m.name for m in o.data.materials]==r['slots']
        checks.append({'mesh':r['mesh'],'triangles':len(o.data.loop_triangles),'bounds_m':bounds,'empties_m':empties,
            'degenerate_triangles':sum(t.area<1e-12 for t in o.data.loop_triangles),'uv_layers':len(o.data.uv_layers)})
for name in ['SM_HQ_Shell','SM_HQ_ShellDetail','SM_HQ_ShellDetail_LOD1']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/'game'/f'{name}.fbx'))
    meshes=[o for o in bpy.data.objects if o.type=='MESH'];assert len(meshes)==1
    o=meshes[0];bpy.context.view_layer.objects.active=o;bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    o.data.calc_loop_triangles();triangles=len(o.data.loop_triangles)
    game=json.loads((ROOT/'game/game_meshes.json').read_text())
    key='SM_HQ_ShellDetail' if name.endswith('_LOD1') else name
    assert triangles==game['meshes'][key]['triangles'][1 if name.endswith('_LOD1') else 0],(name,triangles)
    tangent_check={}
    if name.endswith('_LOD1'):
        o.data.calc_tangents(uvmap=o.data.uv_layers.active.name)
        tangent_check={'zero_tangent_loops':sum(l.tangent.length<1e-4 for l in o.data.loops),'zero_bitangent_loops':sum(l.bitangent.length<1e-4 for l in o.data.loops)}
        assert not any(tangent_check.values()),tangent_check
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/f'{name}.fbx'),use_selection=True,object_types={'MESH'},
        use_mesh_modifiers=False,mesh_smooth_type='FACE',use_tspace=True,apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_anim=False)
    checks.append({'mesh':name,'triangles':triangles,'slots':[m.name for m in o.data.materials],
        'source_sha256':hashlib.sha256((ROOT/'game'/f'{name}.fbx').read_bytes()).hexdigest(),**tangent_check})
(WORK/'fbx_reimport_checks.json').write_text(json.dumps(checks,indent=2))
print('Verified FBX files',len(checks),flush=True)
