"""Source, aperture/eye and independent FBX checks. No rebuilding or Unreal edits."""
import argparse,json,math,sys
from pathlib import Path
sys.dont_write_bytecode=True
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round3 as r
import village_431_round3_common as c

def inspect(obj):
    obj.data.calc_loop_triangles();uv=obj.data.uv_layers['UVMap'].data;obj.data.calc_tangents(uvmap='UVMap')
    badgeo=0;baduv=0
    for t in obj.data.loop_triangles:
        a,b,d=[obj.data.vertices[i].co for i in t.vertices];badgeo+=((b-a).cross(d-a).length<1e-9)
        a,b,d=[uv[i].uv for i in t.loops];baduv+=(abs((b-a).cross(d-a))<1e-9)
    badtan=sum(l.tangent.length<.99 or not all(math.isfinite(x) for x in l.tangent) for l in obj.data.loops)
    assert badgeo==baduv==badtan==0,(obj.name,badgeo,baduv,badtan)
    return {'triangles':len(obj.data.loop_triangles),'slots':[m.name for m in obj.data.materials],'bad_geo':badgeo,'bad_uv':baduv,'bad_tangents':badtan,'uv_channels':[l.name for l in obj.data.uv_layers]}

def aperture(obj,key):
    spec=r.g.BUILDINGS[key];ops=r.r2.opening_list() if key=='HouseA' else r.openings(key)[0]
    tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in obj.data.loop_triangles],all_triangles=True);out=[]
    for i,(face,a,z,w,h,kind,floor) in enumerate(ops):
        axis,side,other=r.g.FACES[face];extent=spec['length']/2 if axis==0 else spec['width']/2
        oldeye=floor*3+1.5;eye=oldeye if z+.11<oldeye<z+h-.11 else z+h*.5
        # Check off-centre inside the clear sash, avoiding the central mullion.
        origin=Vector((0,0,eye));origin[axis]=side*(extent+.6);origin[other]=a+w*.25;direction=Vector((0,0,0));direction[axis]=-side
        hit,n,idx,dist=tree.ray_cast(origin,direction,20);depth=hit[axis]*side-extent if hit else None
        assert depth is None or depth<-.10,(obj.name,i,face,a,eye,depth)
        out.append({'index':i,'face':face,'along_m':a,'sill_m':z,'head_m':z+h,'contract_eye_m':oldeye,'proposed_eye_m':eye,'first_hit_depth_m':depth})
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--geometry',action='store_true');ap.add_argument('--dependencies',action='store_true');args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if args.dependencies:
        dependencies={}
        for key in c.TYPES+['Bridge']:
            bpy.ops.wm.open_mainfile(filepath=str(c.OUT/'source'/(key+'_baked.blend')));files=[]
            for image in bpy.data.images:
                if image.source!='FILE':continue
                path=Path(bpy.path.abspath(image.filepath)).resolve();assert path.exists() and path.is_relative_to(c.OUT/'textures'),(key,image.name,str(path))
                files.append(str(path.relative_to(c.OUT)))
            dependencies[key]=sorted(set(files))
        (c.OUT/'evidence/source_dependencies.json').write_text(json.dumps(dependencies,indent=2));print('All eight baked sources resolve external images inside round3/textures',flush=True);return
    report={};apertures={}
    for key in c.TYPES+['Bridge']:
        f=c.OUT/'source'/(key+('_geometry' if args.geometry else '_baked')+'.blend');bpy.ops.wm.open_mainfile(filepath=str(f))
        for obj in bpy.data.objects:
            if obj.type!='MESH' or obj.name.startswith('UCX_'):continue
            report[obj.name]=inspect(obj)
            if key!='Bridge' and obj.name in c.names(key)[:2]:apertures[obj.name]=aperture(obj,key)
    for key in c.TYPES+['Bridge']:
        for name in c.names(key):
            for end,folder in [('', 'fbx'),('_LOD1','fbx')]+([] if args.geometry else [('_MASTER','source')]):
                file=c.OUT/folder/(name+end+'.fbx');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(file))
                obj=next(o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith('UCX_'));rec=inspect(obj)
                # Budgets use the higher source/FBX count. Keep review-master
                # exporter differences explicit; production meshes must match.
                if end!='_MASTER':assert rec['triangles']==report[name+end]['triangles'],(name+end,rec,report[name+end])
                report[name+end]['reimport_triangle_delta']=rec['triangles']-report[name+end]['triangles']
                report[name+end]['reimport']=rec
    (c.OUT/'evidence'/('first_qa.json' if args.geometry else 'source_qa.json')).write_text(json.dumps({'meshes':report,'apertures':apertures},indent=2))
    print('QA PASS',len(report),'source/independent FBX meshes;',sum(len(v) for v in apertures.values()),'opening rays',flush=True)

if __name__=='__main__':main()
