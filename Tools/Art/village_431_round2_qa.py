"""Final portable QA and master UV repair; does not rebuild game geometry."""
import bpy,bmesh,json,sys,math
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round2 as r
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def main():
    out=r.OUT;bpy.ops.wm.open_mainfile(filepath=str(out/'source/HouseA_round2_baked.blend'))
    report={}
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for obj in list(bpy.data.objects):
        if obj.type!='MESH' or obj.name.startswith('UCX_'):continue
        # Reproject only master UV0 after beveling; unique bake UV1 is preserved.
        if obj.name.endswith('_MASTER'):
            # Remove submillimetre bevel junction remnants; preserve scaled UV0
            # and the bake atlas, then independently validate the FBX.
            bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.001);bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free();obj.data.update()
            obj.data.uv_layers.active_index=0;record=r.qa(obj)
            r.r1.export([obj],out/'source'/(obj.name+'.fbx'))
        else:record=r.qa(obj)
        record['uv_channels']=[l.name for l in obj.data.uv_layers]
        if not obj.name.endswith(('_MASTER','_LOD1','Rubble')):
            tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[p.vertices for p in obj.data.polygons],all_triangles=True)
            checks=[]
            for i,(face,a,z,w,h,k,fl) in enumerate(r.opening_list()):
                axis,side,other=r.g.FACES[face];extent=5.5 if axis==0 else 4.5
                origin=Vector((0,0,z+h/2));origin[axis]=side*(extent+.6);origin[other]=a+w/4
                direction=Vector((0,0,0));direction[axis]=-side
                hit,n,idx,dist=tree.ray_cast(origin,direction,15)
                depth=hit[axis]*side-extent if hit else None
                assert depth is None or depth<-.10,(obj.name,i,depth)
                checks.append({'index':i,'face':face,'along_m':a,'sill_m':z,'head_m':z+h,'first_hit_depth_m':depth})
            record['apertures']=checks
        report[obj.name]=record
    for col in bpy.data.collections:col.hide_viewport=col.name!='SM_Village_HouseA';col.hide_render=col.name!='SM_Village_HouseA'
    for img in bpy.data.images:
        if img.filepath and (out/'textures'/Path(img.filepath).name).exists():img.filepath='//../textures/'+Path(img.filepath).name
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/HouseA_round2_baked.blend'))
    # Independently reimport portable LOD0/LOD1 and master review FBXs.
    for f in list((out/'fbx').glob('*.fbx'))+list((out/'source').glob('*MASTER.fbx')):
        if f.stem.endswith('_WithLODs'):continue
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(f))
        obj=next(o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith('UCX_'))
        uv=obj.data.uv_layers.active.data;obj.data.calc_loop_triangles();obj.data.calc_tangents(uvmap='UVMap')
        bad_uv=sum(abs((uv[t.loops[1]].uv-uv[t.loops[0]].uv).cross(uv[t.loops[2]].uv-uv[t.loops[0]].uv))<1e-9 for t in obj.data.loop_triangles)
        bad=sum(not all(math.isfinite(c) for c in l.tangent) or l.tangent.length<.99 for l in obj.data.loops)
        report[f.stem]['reimport']={'triangles':len(obj.data.loop_triangles),'bad_uv':bad_uv,'bad_tangents':bad,'uv_channels':[l.name for l in obj.data.uv_layers]}
        if bad:
            print('BAD',f.name,[(l.index,l.tangent[:],obj.data.vertices[l.vertex_index].co[:],obj.data.corner_normals[l.index].vector[:]) for l in obj.data.loops if l.tangent.length<.99][:30],flush=True)
        assert bad==bad_uv==0,(f.name,bad,bad_uv)
    (out/'evidence/source_qa.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:(v['triangles'],v.get('reimport')) for k,v in report.items()},indent=2))
if __name__=='__main__':main()
