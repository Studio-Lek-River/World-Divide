import bpy,json
from pathlib import Path
from mathutils.kdtree import KDTree
root=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
bpy.ops.wm.open_mainfile(filepath=str(root/'issue379/baseline/USA_HQ_Final.blend'))
dg=bpy.context.evaluated_depsgraph_get();records=[]
for o in bpy.data.objects:
    if o.type=='MESH' and o.name.startswith('HQ19_ER_Relay_'):
        ev=o.evaluated_get(dg);m=ev.to_mesh();m.calc_loop_triangles()
        deg=sum(1 for t in m.loop_triangles if t.area<1e-12)
        if deg:print('DEGENERATE',o.name,deg,flush=True)
        records.append((o.name,[o.matrix_world@v.co for v in m.vertices],len(m.loop_triangles)));ev.to_mesh_clear()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(root/'fbx/SM_HQ20_RoofSystems_WallCorrected.fbx'))
o=next(o for o in bpy.data.objects if o.type=='MESH');t=KDTree(len(o.data.vertices))
for i,v in enumerate(o.data.vertices):t.insert(o.matrix_world@v.co,i)
t.balance()
membership={}
for name,vs,tris in records:
    for v in vs:
        for _,idx,dist in t.find_range(v,.00015):membership.setdefault(idx,set()).add(name)
matched={name:0 for name,_,_ in records}
for p in o.data.polygons:
    common=set.intersection(*(membership.get(i,set()) for i in p.vertices))
    for name in common:matched[name]+=len(p.vertices)-2
print('DIFFERENCES',[(name,tris,matched[name]) for name,_,tris in records if tris!=matched[name]],flush=True)
print('MATCHED SUM',sum(matched.values()),flush=True)
result=[]
for name,vs,tris in records:
    missing=[t.find(v)[2] for v in vs if t.find(v)[2]>.00015]
    if missing:result.append({'name':name,'tris':tris,'verts':len(vs),'missing':len(missing),'maxdistance':max(missing)})
(root/'issue379/mask_diagnostics.json').write_text(json.dumps(result,indent=2))
print(result)
