"""Diagnostic only: isolate native Unreal tangent recomputation from UV collapse."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq/issue379'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(ROOT/'preview_fbx/SM_HQ_ShellDetail_LOD1.fbx'))
o=next(o for o in bpy.data.objects if o.type=='MESH')
bpy.context.view_layer.objects.active=o
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
if '--slivers' in sys.argv:
    m=o.data
    # Blender converts the FBX centimetres back to metres on import.
    faces={p.index for p in m.polygons if 2*p.area/max((m.vertices[a].co-m.vertices[b].co).length for a,b in p.edge_keys)<0.001}
    print('Slivers',len(faces),flush=True)
    m.vertices.foreach_set('select',[False]*len(m.vertices));m.edges.foreach_set('select',[False]*len(m.edges));m.polygons.foreach_set('select',[p.index in faces for p in m.polygons])
    bpy.context.scene.tool_settings.mesh_select_mode=(False,False,True)
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.delete(type='FACE');bpy.ops.object.mode_set(mode='OBJECT')
    name='TangentSliverDiagnostic'
else:
    for p in o.data.polygons:
        assert len(p.loop_indices)==3
        for i,uv in zip(p.loop_indices,[(0,0),(1,0),(0,1)]):o.data.uv_layers.active.data[i].uv=uv
    name='TangentDiagnostic'
bpy.ops.export_scene.fbx(filepath=str(ROOT/f'preview_fbx/{name}.fbx'),use_selection=True,object_types={'MESH'},mesh_smooth_type='FACE',use_tspace=True,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_anim=False)
