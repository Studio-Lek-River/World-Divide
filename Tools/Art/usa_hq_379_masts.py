"""Issue 379: bounded mast variants from the approved master. Run in Blender.

Preserves the baseline checkpoint and all non-mast objects. Does not run on import.
"""
import bpy
import bmesh
import json
import math
import hashlib
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2] / 'Art/USA/buildings/usa_hq'
WORK = ROOT / 'issue379'
CORE_PREFIX = ('HQ17_Mast', 'HQ17_Antenna', 'HQ17_Aerial', 'HQ17_Secondary',
               'HQ17_AnchorBolt', 'HQ17_FeederTail', 'HQ17_PanelAntenna')
OPTIONS = [(24, 21, 1.5), (28, 24, 1.75), (32, 27, 2.0)]

def group(o):
    if o.type != 'MESH': return None
    if o.name.startswith('HQ17_AntennaFeeder2'): return None  # fixed rooftop cable return
    if o.name.startswith(CORE_PREFIX): return 'Core'
    if o.name.startswith('HQ19_ER_Relay_'): return 'East'
    return None

def tri_count(mesh):
    mesh.calc_loop_triangles()
    return len(mesh.loop_triangles)

def mesh_hash(o):
    # Source geometry and transforms, independently checked after modifications.
    return hashlib.sha256(repr((tuple(o.matrix_world),
        [tuple(v.co) for v in o.data.vertices],
        [tuple(p.vertices) for p in o.data.polygons],
        [s.material.name if s.material else None for s in o.material_slots])).encode()).hexdigest()

def simplified_primitive(o):
    """Authored radial LOD: four-sided closed rods, original endpoints and radius.
    Boxes stay boxes; no strut, rung, panel, bolt or antenna is removed.
    """
    src = o.data
    caps = [p for p in src.polygons if len(p.vertices)>4]
    if len(caps)==2 and len(caps[0].vertices)==len(caps[1].vertices) and len(src.vertices)==2*len(caps[0].vertices):
        rings=[]
        # Order cap vertices spatially about the rod axis, preserving extremal radius.
        centers=[sum((src.vertices[i].co for i in p.vertices),Vector())/len(p.vertices) for p in caps]
        axis=(centers[1]-centers[0]).normalized()
        u=(src.vertices[caps[0].vertices[0]].co-centers[0]).normalized(); v=axis.cross(u).normalized()
        for p,c in zip(caps,centers):
            radius=max((src.vertices[i].co-c).length for i in p.vertices)
            rings.extend([c+radius*(u*math.cos(a*math.pi/2)+v*math.sin(a*math.pi/2)) for a in range(4)])
        faces=[(3,2,1,0),(4,5,6,7)]+[(i,(i+1)%4,(i+1)%4+4,i+4) for i in range(4)]
        m=bpy.data.meshes.new(o.name+'_radialLOD');m.from_pydata(rings,[],faces)
        for mat in src.materials:m.materials.append(mat)
        return m
    return src.copy()

def deform(co, foot, old_tip, new_tip, width):
    # Preserve all mounting positions on the roof, broaden above the mounting zone.
    t=max(0,min(1,(co.z-foot.z)/0.8)); t=t*t*(3-2*t)
    w=1+(width-1)*t
    return Vector(((co.x-foot.x)*w,(co.y-foot.y)*w,(co.z-foot.z)*(new_tip-foot.z)/(old_tip-foot.z)))

def joined_mesh(obs, label, foot, tip, width, lod):
    # #379 adds six sections total to the established 74-section HQ (limit 80).
    # Reuse three existing instances; retain light panels as a separate surface.
    def surface(name):
        return name if name in ('HQ17_FramePaint','HQ17_TeamLight') else 'HQ17_DarkStructure'
    verts=[];faces=[];mi=[];order=sorted({surface(s.material.name) for o in obs for s in o.material_slots if s.material})
    for o in obs:
        m=simplified_primitive(o) if lod else o.data.copy()
        start=len(verts)
        old_tip=17.98 if group(o)=='Core' else 16.5
        verts.extend([deform(o.matrix_world@v.co,foot,old_tip,tip,width) for v in m.vertices])
        for p in m.polygons:
            faces.append(tuple(start+i for i in p.vertices));mi.append(order.index(surface(m.materials[p.material_index].name)))
        bpy.data.meshes.remove(m)
    mesh=bpy.data.meshes.new(label);mesh.from_pydata(verts,[],faces)
    for name in order:mesh.materials.append(bpy.data.materials[name])
    for p,idx in zip(mesh.polygons,mi):p.material_index=idx
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    obj=bpy.data.objects.new(label,mesh);bpy.context.scene.collection.objects.link(obj)
    # Small primitives use existing flat coatings; deterministic box-projected UVs.
    uv=mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i])); axes=[i for i in range(3) if i!=axis]
        for li in p.loop_indices:
            co=mesh.vertices[mesh.loops[li].vertex_index].co
            uv.data[li].uv=(co[axes[0]]/2,co[axes[1]]/2)
    return obj,order

def export(obj,path,socket=None):
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    original_socket_name = socket.name if socket else None
    if socket:
        socket.select_set(True)
        socket.name = 'SOCKET_' + socket.get('socket_name', 'MastTipCore' if 'MastCore' in obj.name else 'MastTipEast')
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','EMPTY'},
        use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,colors_type='NONE',
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',
        bake_space_transform=False,add_leaf_bones=False,bake_anim=False,use_custom_props=True,path_mode='AUTO')
    if socket:socket.name=original_socket_name

def main():
    assert Path(bpy.data.filepath).name=='USA_HQ_Final.blend'
    assert not any(o.name.startswith('SM_HQ22_Mast') for o in bpy.data.objects), 'Already built; load baseline to regenerate.'
    groups={key:[o for o in bpy.data.objects if group(o)==key] for key in ('Core','East')}
    unaffected={o.name:mesh_hash(o) for o in bpy.data.objects if o.type=='MESH' and not group(o)}
    dg=bpy.context.evaluated_depsgraph_get()
    masks={};manifest={'options':{},'source_objects':{},'baseline_source_sha256':hashlib.sha256((WORK/'baseline/USA_HQ_Final.blend').read_bytes()).hexdigest()}
    for key,obs in groups.items():
        points=[];faces=[];count=0
        for o in obs:
            ev=o.evaluated_get(dg);m=ev.to_mesh();count+=tri_count(m)
            offset=len(points)
            points.extend([list(o.matrix_world@v.co) for v in m.vertices])
            faces.extend([[offset+i for i in t.vertices] for t in m.loop_triangles]);ev.to_mesh_clear()
        masks[key]={'vertices':points,'faces':faces,'triangles':count,'objects':[o.name for o in obs]}
        manifest['source_objects'][key]={'count':len(obs),'triangles':count}
    (WORK/'mast_removal_mask.json').write_text(json.dumps(masks),encoding='utf8')
    corex=sum(v.co.x for v in bpy.data.objects['HQ17_AntennaWhip'].data.vertices)/24
    feet={'Core':Vector((corex,0,11.51)),'East':Vector((-10.9,19.95,7.7))}
    for core,east,width in OPTIONS:
        records=[]
        for key,tip in [('Core',core),('East',east)]:
            foot=feet[key]
            for lod in (0,1):
                name=f'SM_HQ22_Mast{key}_H{core}'+('_LOD1' if lod else '')
                obj,slots=joined_mesh(groups[key],name,foot,tip,width,lod)
                helper=f'MastTip{key}'+('' if core==28 and lod==0 else f'_H{core}_LOD{lod}')
                socket=bpy.data.objects.new(helper,None)
                bpy.context.scene.collection.objects.link(socket);socket.location=(0,0,tip-foot.z)
                socket['socket_name']=f'MastTip{key}'
                path=ROOT/'fbx'/f'{name}.fbx';export(obj,path,socket)
                local_lo=[min(v.co[i] for v in obj.data.vertices) for i in range(3)]
                local_hi=[max(v.co[i] for v in obj.data.vertices) for i in range(3)]
                records.append({'mesh':name,'lod':lod,'triangles':tri_count(obj.data),'slots':slots,
                    'fbx':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'foot_cm':[foot.x*100,-foot.y*100,foot.z*100],
                    'tip_cm':[foot.x*100,-foot.y*100,tip*100],
                    'local_bounds_m':[local_lo,local_hi], 'beacon_scale':width,'socket_name':f'MastTip{key}'})
                obj.location=foot;socket.parent=obj;socket.location=(0,0,tip-foot.z)
                obj.hide_render=lod==1 or core!=28;obj.hide_set(obj.hide_render)
                socket.hide_render=True;socket.hide_set(True)
        manifest['options'][str(core)]=records
    for obs in groups.values():
        for o in obs:bpy.data.objects.remove(o,do_unlink=True)
    remaining={o.name:mesh_hash(o) for o in bpy.data.objects if o.type=='MESH' and not o.name.startswith('SM_HQ22_Mast')}
    assert unaffected==remaining, 'Non-mast source changed'
    manifest['non_mast_meshes_unchanged']=len(unaffected)
    manifest['section_count_with_existing_HQ']=80
    manifest['mast_surface_mapping']='FramePaint and TeamLight preserved; other mast surfaces share existing HQ17_DarkStructure.'
    bpy.context.scene['issue379']='24/28/32 m core options; 28 m visible provisionally, awaiting producer choice.'
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'source/USA_HQ_Final.blend'))
    (WORK/'mast_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    print('Mast options:', {k:[(r['mesh'],r['triangles']) for r in v] for k,v in manifest['options'].items()})
    print('Unchanged non-mast objects:',len(unaffected))

if __name__=='__main__':main()
