"""Read-only source inspection; writes issue195 receipts only. Run in Blender."""
import bpy, json, hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
OUT=ROOT/'issue195/phase_b'
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'source/USA_HQ_Final.blend'))
rows=[]
for o in bpy.data.objects:
    if o.type!='MESH': continue
    rows.append({'name':o.name,'bounds_m':[[min((o.matrix_world@v.co)[i] for v in o.data.vertices) for i in range(3)],[max((o.matrix_world@v.co)[i] for v in o.data.vertices) for i in range(3)]], 'slots':[s.material.name if s.material else None for s in o.material_slots]})
result={'source':bpy.data.filepath,'sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'mesh_count':len(rows),'relevant':[r for r in rows if any(s in str(r) for s in ('TeamPanel','Portal','Pillar','Mast','FixtureBlue'))],'helpers':{o.name:[list(row) for row in o.matrix_world] for o in bpy.data.objects if o.type=='EMPTY'}}
(OUT/'source_audit.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result)[:5000],flush=True)
