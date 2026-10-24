"""Non-rebuilding source QA and portable texture relinking for the Round 1 gate."""
import json
import math
import sys
from pathlib import Path
sys.dont_write_bytecode=True
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Art'))
import village_blender as grey

def main():
    out=ROOT/'Art/Shared/village/issue431/round1'
    bpy.ops.wm.open_mainfile(filepath=str(out/'source/HouseA_round1.blend'))
    result={}
    for obj in bpy.data.objects:
        if obj.type!='MESH' or obj.name.startswith('UCX_'):continue
        mesh=obj.data;mesh.calc_loop_triangles();mesh.calc_tangents(uvmap='UVMap')
        bad=sum(1 for loop in mesh.loops if not all(math.isfinite(c) for c in loop.tangent) or loop.tangent.length<.99)
        assert bad==0,(obj.name,bad)
        min_width=100
        for tri in mesh.loop_triangles:
            vs=[mesh.vertices[i].co for i in tri.vertices]
            area2=(vs[1]-vs[0]).cross(vs[2]-vs[0]).length
            min_width=min(min_width,area2/max((vs[0]-vs[1]).length,(vs[1]-vs[2]).length,(vs[2]-vs[0]).length))
        apertures=[]
        if not obj.name.endswith('Rubble'):
            tree=BVHTree.FromPolygons([v.co for v in mesh.vertices],[p.vertices for p in mesh.polygons],all_triangles=True)
            for i,(face,a,z,w,h,kind,floor) in enumerate(grey.openings(grey.BUILDINGS['HouseA'])):
                axis,side,other=grey.FACES[face];extent=5.5 if axis==0 else 4.5
                origin=Vector((0,0,z+h/2));origin[axis]=side*(extent+.6);origin[other]=a+w/4
                direction=Vector((0,0,0));direction[axis]=-side
                co,normal,index,distance=tree.ray_cast(origin,direction,15);hit=co is not None
                depth=(co[axis]*side-extent) if hit else None
                passed=not hit or depth<-.10
                assert passed,(obj.name,i,face,a,depth)
                apertures.append({'index':i,'face_blender':face,'along_m':a,'sill_m':z,'width_m':w,'height_m':h,'first_hit_depth_m':depth,'expanded_to_breach':obj.name.endswith('Damaged') and face=='+Y' and -1.6<a<1.7})
        result[obj.name]={'finite_unit_tangents':bad==0,'minimum_triangle_altitude_m':min_width,'aperture_ray_checks':apertures}
    for img in bpy.data.images:
        if img.source=='FILE' and Path(img.filepath).name.startswith(tuple(['Bricks085','RoofingTiles006','Planks023A','Concrete036'])):
            img.filepath='//../textures/'+Path(img.filepath).name
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/HouseA_round1.blend'))
    (out/'evidence/source_qa.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:{'tangents':v['finite_unit_tangents'],'min_altitude_m':v['minimum_triangle_altitude_m'],'openings_checked':len(v['aperture_ray_checks'])} for k,v in result.items()},indent=2))

if __name__=='__main__':main()
