"""Round4 review receipt. Deterministic crop/QA; writes only issue446/round4."""
import sys,json,hashlib,math,html
from pathlib import Path
from PIL import Image
sys.dont_write_bytecode=True
import village_446_round2_report as crops_helper
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round4';NAME='SM_Bridge_Span_Destroyed'
crops_helper.OUT=OUT
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    q=read(OUT/'evidence/source_qa.json');old=read(OUT.parent/'round3/evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');audit=read(OUT/'evidence/independent_audit.json');build=read(OUT/'evidence/build.json')
    protected=read(OUT/'evidence/protected_before.json');delivery=read(OUT/'evidence/final_delivery.json');allowed={'Art/Shared/village/final/'+p for p in delivery['written']};assert all(sha(ROOT/f)==v for f,v in protected.items() if Path(f).as_posix() not in allowed)
    rows=[];files=[]
    for suffix,label in [('', 'LOD0'),('_LOD1','LOD1'),('_MASTER','Master')]:
        n=NAME+suffix;s=q[n];u=native['meshes'][n];assert s['triangles']==s['fbx']['triangles']==u['triangles']
        rows.append(f"| {label} | {old[n]['triangles']:,} | {s['triangles']:,} | {s['fbx']['triangles']:,} | {u['triangles']:,} | {s['triangles']-old[n]['triangles']:+,} |")
        files.append(('source/' if suffix=='_MASTER' else 'fbx/')+n+'.fbx')
        for tag in json.loads(u['source_tags']['AssetImportData']):
            package=ROOT/'Content'/u['path'].removeprefix('/Game/');file=(package.parent/tag['RelativeFilename']).resolve()
            assert file.is_relative_to(ROOT/'Art/Shared/village/final') and hashlib.md5(file.read_bytes()).hexdigest()==tag['FileMD5']
    files+=['source/Bridge_baked.blend']+['textures/'+NAME+'_'+s+'.png' for s in ['Tint','BakeNormal','BakeAO']]
    images={}
    for phase,views in [('before',['12m','150m','450m']),('before_master',['12m','150m','450m']),('before_switch',['249_6m','270_4m']),('after',['12m','150m','450m']),('master',['12m','150m','450m']),('switch',['249_6m','270_4m'])]:
        for tag in views:
            p=OUT/'evidence'/phase/(NAME+'_'+tag+'.png');data=read(p.with_suffix('.json'));assert data['cameraFOV']==60
            if 'switch' in phase:
                actual=math.dist([data['cameraLocation'][a] for a in 'xyz'],data['target_cm']);assert abs(actual-(24960 if tag=='249_6m' else 27040))<.1
                data.update(manual_separate_LOD_actor=True,bounds_centre_world_cm=data['target_cm'],camera_to_bounds_centre_cm=actual,distance_reference='Common LOD0 whole-building bounds centre');p.with_suffix('.json').write_text(json.dumps(data,indent=2))
            images[p.relative_to(OUT).as_posix()]={'sha256':sha(p),'dimensions':Image.open(p).size,'camera':data}
    for view in ['12m','150m','450m']:
        cameras=[read(OUT/'evidence'/phase/(NAME+'_'+view+'.json')) for phase in ['before','after','master']]
        for field in ['cameraLocation','cameraRotation','cameraFOV','target_cm','sun_rotation_deg']:assert all(c[field]==cameras[0][field] for c in cameras)
    crops={phase:crops_helper.crops(phase) for phase in ['before','after']};(OUT/'evidence/crops_2x/index.json').write_text(json.dumps(crops,indent=2))
    hashes={'files':{},'scripts':{},'protected_files_unchanged':len(protected)-len(allowed)}
    for folder in ['fbx','source','textures']:
        for p in sorted((OUT/folder).iterdir()):
            if p.suffix in ['.fbx','.blend','.png']:hashes['files'][p.relative_to(OUT).as_posix()]={'bytes':p.stat().st_size,'sha256':sha(p)}
    for p in (ROOT/'Tools/Art').glob('village_446_round4*.py'):hashes['scripts'][p.relative_to(ROOT).as_posix()]=sha(p)
    hashes['crop_helper']={'path':'Tools/Art/village_446_round2_report.py','sha256':sha(ROOT/'Tools/Art/village_446_round2_report.py')}
    (OUT/'hashes.json').write_text(json.dumps(hashes,indent=2));(OUT/'evidence/index.json').write_text(json.dumps(images,indent=2))
    manifest={'status':'Issue446 final round delivered under explicit final-copy authorization','mesh':NAME,'native_root':'/Game/Art/Candidates/Village/Rebar446/R4/Final','native':native,'source_qa':q,'source_audit':audit,'captures':images,'crops_2x':crops,'textures':{p.name:Image.open(p).size for p in (OUT/'textures').glob('*.png')},'before':'Accepted #431; fresh captures before changes','lod':'Separate LOD1; manual 249.6/270.4m comparison; same new angle-profile bars','waterline':'Accepted staging retained: nominal -600cm; preview top world+11cm relative to bridge world+600cm = -589cm'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    def pic(phase,view,label):
        rel=f'evidence/{phase}/{NAME}_{view}.png';return f'<figure><a href="{rel}"><img src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    def crop(phase,end,label):
        rel=crops[phase][end]['output'];return f'<figure><a href="{rel}"><img class="crop" src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    gallery=['<!doctype html><meta charset="utf-8"><title>#446 Round4 rebar</title><style>body{background:#202322;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{width:100%}.crop{width:auto;max-width:100%;image-rendering:pixelated}figcaption{padding:8px}h2{margin-top:2em}</style><h1>#446 Round4 — final realism and handoff</h1><p>Round3 150 m read accepted; this round preserves it. Before images show accepted #431. Native editor evidence, 60° FOV. <a href="REPORT.md">Report</a> · <a href="manifest.json">Manifest</a>. Click images for native pixels.</p>']
    for view,label in [('150m','150 m read held'),('12m','12 m against accepted #431')]:
        gallery += [f'<h2>{label}</h2><div class="pair">',pic('before',view,'Accepted #431 before'),pic('after',view,'Round4 after'),'</div>']
    for end in ['negative_X_far_end','positive_X_near_end']:
        gallery += [f'<h2>150 m, 2× pixel crop — {end}</h2><div class="pair">',crop('before',end,'Accepted #431'),crop('after',end,'Round4'),'</div>']
    for view in ['12m','150m','450m']:
        gallery += [f'<h2>Master against game — {view}</h2><div class="pair">',pic('master',view,'Review master'),pic('after',view,'LOD0 game mesh'),'</div>']
    gallery += ['<h2>Manual switch pair</h2><div class="pair">',pic('switch','249_6m','LOD0 at 249.6 m'),pic('switch','270_4m','Separate LOD1 at 270.4 m'),'</div><p>Editor views; automatic production LOD switching and gameplay are not tested.</p>']
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    file_rows='\n'.join(f"| [{p}]({p}) | `{hashes['files'][p]['sha256']}` |" for p in files)
    final=ROOT/'Art/Shared/village/final';before=read(OUT/'evidence/final_before_hashes.json')
    for rel,record in delivery['written'].items():assert sha(final/rel)==record['sha256']
    for rel,digest in delivery['unchanged'].items():assert sha(final/rel)==digest==before[rel]
    final_rows='\n'.join(f"| [{p}](../../final/{p}) | `{d['sha256']}` |" for p,d in delivery['written'].items())
    counts=[];bounds=[]
    for suffix,label in [('', 'LOD0'),('_LOD1','LOD1'),('_MASTER','Master')]:
        n=NAME+suffix;a=q[n];u=native['meshes'][n]
        counts.append(f"| {label} | {a['triangles']:,} | {a['fbx']['triangles']:,} | {u['triangles']:,} | +580 |")
        box=u['bounds_cm'];lo=[round(box['min'][k],3) for k in 'xyz'];hi=[round(box['max'][k],3) for k in 'xyz'];size=[round(box['max'][k]-box['min'][k],3) for k in 'xyz']
        bounds.append(f'| {label} | `{lo}` | `{hi}` | `{size}` |')
    actual=audit['actual_rebar_atlas_centroid_samples'];merged=read(OUT/'evidence/merged_source_audit.json')
    report=f'''# #446 — Round 4 of 4: final handoff

**Yes: the approved 150 m read held.** Both ends retain an irregular fringe over water, with distinct projecting bars in the matched 2× crops. Before means the accepted #431 asset, captured afresh before modelling. [Comparison gallery](REVIEW.html).

## Realism changes

Started from `round3/source/Bridge_baked.blend` and its 47 saved bar paths. Converging waterline tips were nudged into separate lanes; two side-bar tips were eased apart. Roots, widths, counts, outward reach and vertical droop remain. Three transverse pieces provide the intentional crossings; four returning hooks remain. [Tip separation measurements](evidence/separation.json).

The fracture band varies approximately 20–70 cm, with larger angular chips, deeper pockets at selected bar groups, fine aggregate beneath the larger shapes, and a thin dark wet margin above the water. Tint, object-normal and AO carry the finish. No concrete geometry was added.

Steel has varied warm rust pigment and curved shading across the two upper facets, replacing broad pale panels with a narrower highlight. The open V profile remains four triangles per segment. SurfaceTint is unchanged at `(0.10,0.075,0.055)`; parent, scalar and vector values match the accepted templates. The saved atlas's {actual['count']} sampled rebar face centroids range from `{[round(x,4) for x in actual['min_rgb']]}` to `{[round(x,4) for x in actual['max_rgb']]}` in linear mask-data RGB. Small lighting highlights remain; the supplied Unreal views are the visual evidence.

## Counts and final bounds

| Mesh | Blender | Fresh FBX reimport | Unreal | Increase over #431 |
|---|---:|---:|---:|---:|
{chr(10).join(counts)}

Counts equal round3; each game LOD is 20 triangles below the +600 allowance. Slots are `Brick`, `Roof`, `Trim`, `Rubble`. Nanite is off; LOD1 is a separate authored mesh. Two original UCX stub hulls and native collision data are unchanged; LOD0 uses `CTF_UseSimpleAsComplex`.

Bounds below are mesh-local centimetres in XYZ order, measured in Unreal after final-file import:

| Mesh | Minimum cm | Maximum cm | Size cm |
|---|---|---|---|
{chr(10).join(bounds)}

Deck-top-centre origin, +X along the span, Z up and unit scale remain. Export uses `FBX_SCALE_NONE`.

## Written to final

Exactly seven asset/source files and the two JSON inventories were replaced under the explicit round4 authorization:

| Path relative to `Art/Shared/village/final/` | SHA-256 |
|---|---|
{final_rows}

**{delivery['unchanged_count']} other final files are byte-identical**, including the damaged-span FBXs and atlas and every other mesh. No files were added or removed. [Before inventory](evidence/final_before_hashes.json) · [after receipt with every unchanged hash](evidence/final_delivery.json). Unrelated manifest and hash entries compare equal. Only the destroyed span's three mesh/source-QA records and affected file hashes changed.

The shared final `Bridge_baked.blend` was merged into the accepted multi-state source. All {len(merged['unaffected_object_signatures'])} unaffected objects, including damaged-span geometry, UVs, material references and transforms, were preserved and checked after reopening. [Merge audit](evidence/merged_source_audit.json). This round's `source/Bridge_baked.blend` is the isolated working source; `delivery/source/Bridge_baked.blend` is the exact multi-state file copied to final. All nine previous files are backed up under `accepted_backup/`.

## Paths and verification

Native authority: `/Game/Art/Candidates/Village/Rebar446/R4/Final/`, with three meshes in `Meshes/`, eight instances in `Materials/`, and three atlases in `Textures/`. Review01 and Review02 are superseded and retained. Existing preview only: `/Game/Art/Candidates/Village/L_VillagePreview`.

Meshes and atlases were imported from their delivered `final/` files. Source MD5s, counts, collision, material bindings and same-editor save/reload were verified. The log retains one refused overwrite of the earlier Review02 texture; the fresh R4/Final imports succeeded. Accepted `/Game/Art/Candidates/Village/Final/M_SetSurface` and scan textures remain dependencies. Material values, mipmaps and streaming policy are retained. Only this mesh's three 2048² atlases changed.

[Native receipt](evidence/unreal_verified.json) · [working-file hashes](hashes.json) · [round manifest](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [independent audit](evidence/independent_audit.json).

## Captures

- 150 m accepted #431 [before](evidence/before/{NAME}_150m.png) / [after](evidence/after/{NAME}_150m.png).
- Far end 2×: [before](evidence/crops_2x/before_negative_X_far_end_2x.png) / [after](evidence/crops_2x/after_negative_X_far_end_2x.png).
- Near end 2×: [before](evidence/crops_2x/before_positive_X_near_end_2x.png) / [after](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m: [accepted #431](evidence/before/{NAME}_12m.png) / [round3](evidence/round3_reference/{NAME}_12m.png) / [final game](evidence/after/{NAME}_12m.png).
- 450 m: [game](evidence/after/{NAME}_450m.png).
- Switch: [LOD0 249.6 m](evidence/switch/{NAME}_249_6m.png) / [LOD1 270.4 m](evidence/switch/{NAME}_270_4m.png).
- Master/game: [12 m master](evidence/master/{NAME}_12m.png) / [game](evidence/after/{NAME}_12m.png); [150 m master](evidence/master/{NAME}_150m.png) / [game](evidence/after/{NAME}_150m.png); [450 m master](evidence/master/{NAME}_450m.png) / [game](evidence/after/{NAME}_450m.png).

[Capture hashes and cameras](evidence/index.json) · [crop provenance](evidence/crops_2x/index.json). Crops duplicate original pixels 2× with nearest-neighbour sampling. Matching 60° FOV and sun `(-42,135,0)`; 150 m arm to `(0,0,600)`, pitch −55°, yaw 145°. The inherited 12 m camera uses −35°. Switch distances use one common LOD0 bounds centre. Nominal waterline remains −600 cm; accepted preview top world +11 cm against deck +600 cm gives −589 cm.

## Limits and handoff

Source and fresh FBX imports pass geometry, UV0 and finite tangent checks. Inherited body geometry and hulls are exact. New LOD1 bar charts match LOD0. Inherited coplanar LOD1 body surfaces retain ambiguous ray-based UV comparisons; the parent's 180–230 m normal/AO fade precedes the 260 m switch. The switch captures assess the intended use. Master keeps its inherited zero bake strength: both show the common tint, while fine painted normal/AO is assessed on the game mesh.

Modelling lessons were rechecked before reporting. These are editor captures with manually swapped LOD actors. Gameplay, automatic LOD switching, navigation/state swaps and runtime performance remain Claude's integration checks. Verification uses same-editor reload and post-reload smoke; no restart was permitted. Editor stays running. No production assets, root village FBXs, `village.json`, setup scripts, commits or pushes changed.

Use [Claude implementation prompt](CLAUDE_IMPLEMENTATION_PROMPT.md). The final copy is complete under the user's explicit round4 instruction; separate producer approval of these round4 images is not inferred.
'''
    (OUT/'REPORT.md').write_text(report,encoding='utf8')
    print('REPORT READY',len(images),'captures;',delivery['unchanged_count'],'unchanged final files',flush=True)

if __name__=='__main__':main()
