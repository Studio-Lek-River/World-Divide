"""Explicit #446 final handoff. --merge prepares; --deliver writes only nine allowed files."""
import sys,json,hashlib,shutil
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art/Shared/village/issue446/round4';FINAL=ROOT/'Art/Shared/village/final'
NAME='SM_Bridge_Span_Destroyed'
FILES=['fbx/'+NAME+'.fbx','fbx/'+NAME+'_LOD1.fbx','source/'+NAME+'_MASTER.fbx','source/Bridge_baked.blend']+['textures/'+NAME+'_'+s+'.png' for s in ['Tint','BakeNormal','BakeAO']]
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,d):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,indent=2),encoding='utf8')

def merge():
    import bpy
    # Keep every unaffected object, mesh, UV, material and collection in the
    # accepted multi-state Bridge source. Only append the three revised objects.
    bpy.ops.wm.open_mainfile(filepath=str(FINAL/'source/Bridge_baked.blend'))
    def sig(o):
        d={'matrix':[list(x) for x in o.matrix_world],'type':o.type}
        if o.type=='MESH':
            m=o.data;d.update(vertices=[list(v.co) for v in m.vertices],faces=[(list(p.vertices),p.material_index,p.use_smooth) for p in m.polygons],uv={u.name:[list(q.uv) for q in u.data] for u in m.uv_layers},materials=[x.name for x in m.materials])
        return hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
    names=[NAME+s for s in ['','_LOD1','_MASTER']]
    untouched={o.name:sig(o) for o in bpy.data.objects if o.name not in names}
    for n in names:bpy.data.objects.remove(bpy.data.objects[n],do_unlink=True)
    with bpy.data.libraries.load(str(OUT/'source/Bridge_baked.blend'),link=False) as (src,dst):
        assert all(n in src.objects for n in names);dst.objects=list(names)
    for o in dst.objects:bpy.context.scene.collection.objects.link(o)
    assert untouched=={n:sig(bpy.data.objects[n]) for n in untouched}
    stage=OUT/'delivery';(stage/'source').mkdir(parents=True,exist_ok=True);(stage/'textures').mkdir(exist_ok=True)
    for img in bpy.data.images:
        if img.source!='FILE':continue
        name=Path(img.filepath).name
        path=(OUT if name.startswith(NAME+'_') else FINAL)/'textures'/name
        assert path.exists(),str(path)
        shutil.copy2(path,stage/'textures'/name);img.filepath='//../textures/'+name
    for o in bpy.data.objects:o.hide_set(o.name!=NAME)
    bpy.ops.wm.save_as_mainfile(filepath=str(stage/'source/Bridge_baked.blend'),relative_remap=False)
    bpy.ops.wm.open_mainfile(filepath=str(stage/'source/Bridge_baked.blend'))
    assert untouched=={n:sig(bpy.data.objects[n]) for n in untouched}
    for img in bpy.data.images:
        if img.source=='FILE':assert Path(bpy.path.abspath(img.filepath)).exists()
    for n in names:
        assert len(bpy.data.objects[n].data.polygons)==read(OUT/'evidence/source_qa.json')[n]['triangles']
    write(OUT/'evidence/merged_source_audit.json',{'unaffected_object_signatures':untouched,'unaffected_objects_exactly_preserved':True,'reopened_and_dependencies_resolved':True,'source':str(stage/'source/Bridge_baked.blend')})
    print('MERGED SOURCE VERIFIED',len(untouched),'unaffected objects',flush=True)

def deliver():
    before=read(OUT/'evidence/final_before_hashes.json')
    assert {p.relative_to(FINAL).as_posix():sha(p) for p in FINAL.rglob('*') if p.is_file()}==before,'Final changed since start'
    q=read(OUT/'evidence/source_qa.json');u=read(OUT/'evidence/unreal_verified.json')
    assert read(OUT/'evidence/merged_source_audit.json')['unaffected_objects_exactly_preserved']
    for n in [NAME+s for s in ['','_LOD1','_MASTER']]:
        assert q[n]['triangles']==q[n]['fbx']['triangles']==u['meshes'][n]['triangles']
    backup=OUT/'accepted_backup'
    for rel in FILES+['hashes.json','manifest.json']:
        target=backup/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(FINAL/rel,target)
    for rel in FILES:
        source=OUT/('delivery/'+rel if rel=='source/Bridge_baked.blend' else rel)
        shutil.copy2(source,FINAL/rel)
        assert sha(source)==sha(FINAL/rel)
    manifest=read(backup/'manifest.json')
    for suffix in ['','_LOD1','_MASTER']:
        n=NAME+suffix;v=u['meshes'][n];entry=manifest['native_meshes'][n]
        entry.update(asset=v['path'],source_triangles=q[n]['triangles'],fbx_triangles=q[n]['fbx']['triangles'],unreal_triangles=v['triangles'],budget_triangles=v['triangles'],bounds_cm=v['bounds_cm'],slots=v['slots'],bindings=v['bindings'])
        record=dict(q[n]);record['reimport']=record.pop('fbx');record['reimport_triangle_delta']=0
        manifest['source_qa']['meshes'][n]=record
    write(FINAL/'manifest.json',manifest)
    hashes=read(backup/'hashes.json')
    for rel in FILES:
        hashes['files'][rel]={'bytes':(FINAL/rel).stat().st_size,'sha256':sha(FINAL/rel)}
    # Existing inventory hashes the manifest as well; update that receipt only.
    if 'manifest.json' in hashes['files']:
        hashes['files']['manifest.json']={'bytes':(FINAL/'manifest.json').stat().st_size,'sha256':sha(FINAL/'manifest.json')}
    write(FINAL/'hashes.json',hashes)
    current={p.relative_to(FINAL).as_posix():sha(p) for p in FINAL.rglob('*') if p.is_file()}
    changed={k:v for k,v in current.items() if before.get(k)!=v}
    assert set(changed)==set(FILES+['hashes.json','manifest.json'])
    assert set(before)==set(current)
    # Verify the scope of JSON edits independently of the writing loop.
    old=read(backup/'manifest.json');now=read(FINAL/'manifest.json')
    for suffix in ['','_LOD1','_MASTER']:
        n=NAME+suffix;now['native_meshes'][n]=old['native_meshes'][n];now['source_qa']['meshes'][n]=old['source_qa']['meshes'][n]
    assert now==old,'Unrelated manifest entries changed'
    oldh=read(backup/'hashes.json');newh=read(FINAL/'hashes.json')
    for rel in FILES+['manifest.json']:
        if rel in oldh['files']:newh['files'][rel]=oldh['files'][rel]
    assert oldh==newh,'Unrelated hash entries changed'
    write(OUT/'evidence/final_delivery.json',{'written':{k:{'sha256':v,'bytes':(FINAL/k).stat().st_size} for k,v in changed.items()},'unchanged':{k:v for k,v in current.items() if k not in changed},'unchanged_count':len(current)-len(changed),'manifest_other_entries_unchanged':True,'hash_other_entries_unchanged':True,'backup':str(backup),'total_files_before':len(before),'total_files_after':len(current)})
    print('DELIVERED',len(changed),'files;',len(current)-len(changed),'unchanged',flush=True)

def refresh_native():
    # Fresh R4/Final imports read the delivered paths. Update only the same
    # three destroyed-span records, then refresh the two JSON file receipts.
    receipt=read(OUT/'evidence/final_delivery.json')
    for rel,digest in receipt['unchanged'].items():assert sha(FINAL/rel)==digest
    native=read(OUT/'evidence/unreal_verified.json');m=read(FINAL/'manifest.json')
    for suffix in ['','_LOD1','_MASTER']:
        n=NAME+suffix;v=native['meshes'][n];m['native_meshes'][n].update(asset=v['path'],bindings=v['bindings'],bounds_cm=v['bounds_cm'])
    write(FINAL/'manifest.json',m)
    h=read(FINAL/'hashes.json')
    if 'manifest.json' in h['files']:h['files']['manifest.json']={'sha256':sha(FINAL/'manifest.json'),'bytes':(FINAL/'manifest.json').stat().st_size}
    write(FINAL/'hashes.json',h)
    for rel in receipt['written']:receipt['written'][rel]={'sha256':sha(FINAL/rel),'bytes':(FINAL/rel).stat().st_size}
    old=read(OUT/'accepted_backup/manifest.json');now=read(FINAL/'manifest.json')
    for suffix in ['','_LOD1','_MASTER']:
        n=NAME+suffix;now['native_meshes'][n]=old['native_meshes'][n];now['source_qa']['meshes'][n]=old['source_qa']['meshes'][n]
    assert now==old
    write(OUT/'evidence/final_delivery.json',receipt)
    print('FINAL NATIVE MAPPING REFRESHED',flush=True)

if __name__=='__main__':
    if '--merge' in sys.argv:merge()
    elif '--deliver' in sys.argv:deliver()
    elif '--refresh-native' in sys.argv:refresh_native()
    else:raise SystemExit('Choose explicit --merge or --deliver')
