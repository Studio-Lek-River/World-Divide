import bpy,json,runpy
from pathlib import Path
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree
from mathutils import Vector
root=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
ns=runpy.run_path(str(Path(__file__).with_name('usa_hq_game_blender.py')))
bpy.ops.wm.read_factory_settings(use_empty=True)
name='SM_HQ20_RoofSystems_WallCorrected';o=ns['import_source'](name,ns['slot_instances']()[name])
r=json.loads((root/'issue379/mast_removal_mask.json').read_text())['East']
t=KDTree(len(r['vertices']))
for i,v in enumerate(r['vertices']):t.insert(v,i)
t.balance();b=BVHTree.FromPolygons(r['vertices'],r['faces'],all_triangles=True)
hits={v.index for v in o.data.vertices if t.find(v.co)[2]<.00015}
surf={v.index for v in o.data.vertices if b.find_nearest(v.co,.00015)[0] is not None}
counts={};slotcounts={};areas={}
for mode in ['vertices','surface','surface_center','center']:
    ps=[]
    for p in o.data.polygons:
        if mode=='vertices':yes=all(i in hits for i in p.vertices)
        elif mode=='surface':yes=all(i in surf for i in p.vertices)
        elif mode=='surface_center':yes=all(i in surf for i in p.vertices) and b.find_nearest(p.center,.00015)[0] is not None
        else:yes=b.find_nearest(p.center,.00015)[0] is not None
        if yes:ps.append(p)
    counts[mode]=sum(len(p.vertices)-2 for p in ps)
    areas[mode]=sum(p.area for p in ps)
    slotcounts[mode]={s.material.name:sum(len(p.vertices)-2 for p in ps if p.material_index==i) for i,s in enumerate(o.material_slots)}
areas['source']=sum((Vector(r['vertices'][b])-Vector(r['vertices'][a])).cross(Vector(r['vertices'][c])-Vector(r['vertices'][a])).length/2 for a,b,c in r['faces'])
(root/'issue379/maskcheck.json').write_text(json.dumps({'counts':counts,'areas':areas,'slots':slotcounts},indent=2))
print(counts)
