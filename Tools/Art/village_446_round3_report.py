"""Round3 review receipt. Deterministic crop/QA; writes only issue446/round3."""
import sys,json,hashlib,math,html
from pathlib import Path
from PIL import Image
sys.dont_write_bytecode=True
import village_446_round2_report as crops_helper
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round3';NAME='SM_Bridge_Span_Destroyed'
crops_helper.OUT=OUT
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    q=read(OUT/'evidence/source_qa.json');old=read(OUT.parent/'round2/evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');audit=read(OUT/'evidence/independent_audit.json');build=read(OUT/'evidence/build.json')
    protected=read(OUT/'evidence/protected_before.json');assert all(sha(ROOT/f)==v for f,v in protected.items())
    rows=[];files=[]
    for suffix,label in [('', 'LOD0'),('_LOD1','LOD1'),('_MASTER','Master')]:
        n=NAME+suffix;s=q[n];u=native['meshes'][n];assert s['triangles']==s['fbx']['triangles']==u['triangles']
        rows.append(f"| {label} | {old[n]['triangles']:,} | {s['triangles']:,} | {s['fbx']['triangles']:,} | {u['triangles']:,} | {s['triangles']-old[n]['triangles']:+,} |")
        files.append(('source/' if suffix=='_MASTER' else 'fbx/')+n+'.fbx')
        for tag in json.loads(u['source_tags']['AssetImportData']):
            package=ROOT/'Content'/u['path'].removeprefix('/Game/');file=(package.parent/tag['RelativeFilename']).resolve()
            assert file.is_relative_to(OUT) and hashlib.md5(file.read_bytes()).hexdigest()==tag['FileMD5']
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
    hashes={'files':{},'scripts':{},'protected_files_unchanged':len(protected)}
    for folder in ['fbx','source','textures']:
        for p in sorted((OUT/folder).iterdir()):
            if p.suffix in ['.fbx','.blend','.png']:hashes['files'][p.relative_to(OUT).as_posix()]={'bytes':p.stat().st_size,'sha256':sha(p)}
    for p in (ROOT/'Tools/Art').glob('village_446_round3*.py'):hashes['scripts'][p.relative_to(ROOT).as_posix()]=sha(p)
    hashes['crop_helper']={'path':'Tools/Art/village_446_round2_report.py','sha256':sha(ROOT/'Tools/Art/village_446_round2_report.py')}
    (OUT/'hashes.json').write_text(json.dumps(hashes,indent=2));(OUT/'evidence/index.json').write_text(json.dumps(images,indent=2))
    manifest={'status':'Round3 ready for Claude/producer review; no new approval claimed','mesh':NAME,'native_root':'/Game/Art/Candidates/Village/Rebar446/R3/Review03','native':native,'source_qa':q,'source_audit':audit,'captures':images,'crops_2x':crops,'textures':{p.name:Image.open(p).size for p in (OUT/'textures').glob('*.png')},'before':'Reviewed Round2, including corrected Masters02 asset','lod':'Separate LOD1; manual 249.6/270.4m comparison; same new angle-profile bars','waterline':'Accepted staging retained: nominal -600cm; preview top world+11cm relative to bridge world+600cm = -589cm'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    def pic(phase,view,label):
        rel=f'evidence/{phase}/{NAME}_{view}.png';return f'<figure><a href="{rel}"><img src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    def crop(phase,end,label):
        rel=crops[phase][end]['output'];return f'<figure><a href="{rel}"><img class="crop" src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    gallery=['<!doctype html><meta charset="utf-8"><title>#446 Round3 rebar</title><style>body{background:#202322;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{width:100%}.crop{width:auto;max-width:100%;image-rendering:pixelated}figcaption{padding:8px}h2{margin-top:2em}</style><h1>#446 Round3 — close-view refinement</h1><p>Round2 150 m read accepted; this round preserves it. Native editor evidence, 60° FOV. <a href="REPORT.md">Report</a> · <a href="manifest.json">Manifest</a>. Click images for native pixels.</p>']
    for view,label in [('150m','150 m read held'),('12m','12 m against round2')]:
        gallery += [f'<h2>{label}</h2><div class="pair">',pic('before',view,'Round2 before'),pic('after',view,'Round3 after'),'</div>']
    for end in ['negative_X_far_end','positive_X_near_end']:
        gallery += [f'<h2>150 m, 2× pixel crop — {end}</h2><div class="pair">',crop('before',end,'Round2'),crop('after',end,'Round3'),'</div>']
    for view in ['12m','150m','450m']:
        gallery += [f'<h2>Master against game — {view}</h2><div class="pair">',pic('master',view,'Review master'),pic('after',view,'LOD0 game mesh'),'</div>']
    gallery += ['<h2>Manual switch pair</h2><div class="pair">',pic('switch','249_6m','LOD0 at 249.6 m'),pic('switch','270_4m','Separate LOD1 at 270.4 m'),'</div><p>Editor views; automatic production LOD switching and gameplay are not tested.</p>']
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    file_rows='\n'.join(f"| [{p}]({p}) | `{hashes['files'][p]['sha256']}` |" for p in files)
    bars=build['reinforcement'];actual=audit['actual_rebar_atlas_centroid_samples'];main_bars=[b for b in bars if b['kind']!='tangled transverse'];transverse=[b for b in bars if b['kind']=='tangled transverse'];finish=build['bake']['round3_finish']
    report=f'''# #446 — Round 3 of 4

**Yes: the approved 150 m read held.** Both slab ends retain the dark, irregular fringe over water in the full-size view and the matched 2× crops. Density and tip reach remain recognisably round2. At 12 m, the new bars have a ridge and two differently lit facets rather than a flat card, their overall colour is dark steel/rust, bends run mainly in vertical planes, and a ragged aggregate band now marks their roots. [Review gallery](REVIEW.html). Claude/producer judgment of this round remains pending.

## Approval retained and changes

The round2 review says **“150 m: pass”** and the producer requested **“the 150 m read now passes; keep it.”** This remains the visual constraint. The starting source was `round2/source/Bridge_baked.blend` and its saved centreline/width data. Only round2's added reinforcement was replaced; every inherited accepted body vertex, face, surface assignment, object transform and hull was retained.

All 47 pieces remain: 32 across the two water-entry lines, 12 on the four side faces, three short transverse pieces. Widths stay {min(b['width_m'] for b in bars)*100:.1f}–{max(b['width_m'] for b in bars)*100:.1f} cm. Main/side centreline lengths are {min(b['length_m'] for b in main_bars):.2f}–{max(b['length_m'] for b in main_bars):.2f} m; short transverse pieces are {min(b['length_m'] for b in transverse):.2f}–{max(b['length_m'] for b in transverse):.2f} m after removing horizontal zigzags. Roots are unchanged. Ordinary free endpoints are unchanged; four hooks keep their outward reach while their in-plane bends are flattened into their vertical planes. There are two hooked bars per end plus one transverse piece at one end and two at the other: three/four deliberately hooked or crossed pieces per end. No horizontal letter-shaped zigzag was authored.

Each segment now has an open, apex-up angle profile: two upper facets, four triangles. Ridge heights are about 4–5.4 cm over the 10–14 cm width. Both upper facets face upward, including the turned-back tips; no backfaces or tube caps are needed for the intended cameras. The lit facet/ridge provides a narrow highlight against a shaded face. The 580-triangle reinforcement cost is unchanged.

Rebar UV1 has a separate atlas shelf with continuous charts across bends and wide dark gutters. The shelf avoids pale concrete/background bleeding into the thin bars' mipmaps. Actual saved PNG texels sampled at all {actual['count']} rebar face centroids range from `{[round(x,4) for x in actual['min_rgb']]}` to `{[round(x,4) for x in actual['max_rgb']]}` in linear mask-data RGB; no tan edge was painted. `Trim` SurfaceTint remains exactly `(0.10,0.075,0.055)`. The atlas modulation is roughly one-third round2's, with restrained warm variation; normal lighting of the two facets supplies the highlight.

The root line is now a connected, ragged band nominally 35–55 cm along the dropped slab, varying around the retained root positions. Darker broken-concrete tint, irregular aggregate/lip normals and AO are painted into this mesh's three atlases. Angular scabs also surround the side-face roots. Rough surface response comes from the normal/AO detail; no material roughness scalar was changed. Fine periodic patterning seen in the first preview was replaced with irregular aggregate. No new concrete geometry or chunks were added; the remaining 20 triangles were left unused.

## Counts

| Mesh | Round2 | Round3 Blender | FBX reimport | Unreal | Delta from R2 |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

Game LODs are each +580 against accepted #431's 984 (20 below the +600 ceiling). Master is +580 against accepted 2,760. Four slots in the same order: `Brick`, `Roof`, `Trim`, `Rubble`; source datablocks retain their mesh-name prefixes. Nanite disabled, one native LOD in each asset; LOD1 remains a separate authored mesh with identical new bar geometry/charts. Exactly two original UCX stub hulls, native collision equal to accepted, `CTF_UseSimpleAsComplex`. Deck-top-centre origin, +X along span, Z up unchanged.

## Paths and hashes

Portable authority: this round's `fbx/`, `source/`, `textures/`. Native authority: `/Game/Art/Candidates/Village/Rebar446/R3/Review03/` with `Meshes/`, `Materials/`, `Textures/`. Existing preview only: `/Game/Art/Candidates/Village/L_VillagePreview`. Earlier R3 imports/checkpoints are retained and superseded; do not select them by folder number without this report. `checkpoint01/` and `checkpoint02/` preserve intermediate validated exports; main files below are the review delivery.

| File | SHA-256 |
|---|---|
{file_rows}

[Complete hashes](hashes.json) · [Manifest with bounds, native bindings and texture settings](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [Source audit](evidence/independent_audit.json) · [Native verification](evidence/unreal_verified.json) · [Paths, profile data, changes against R2 and bake receipt](evidence/build.json).

Only this mesh's Tint, BakeNormal and BakeAO were rebaked/painted, each 2048². Same selected-to-active Cycles object-normal/AO recipe: four samples, 3.5 cm cage, 8 cm ray, two-pixel bake margin, round4 object-normal Y conversion and outlier repair. Root-band detail is applied to those maps after the geometric bake. Rebar atlas gutters are expanded 16 steps; the body occupies the upper 70% of UV1. Game, LOD1 and master sample the matching atlas charts, including the new profile facets. No original round4 rebuild or bake entrypoint was run; only safe helpers were reused with round3 outputs.

Native instances reuse the accepted `/Final/M_SetSurface` and scan textures. Parent/scalar/vector values match the accepted templates; only this mesh's atlas references point into R3. Inherited mipmap/streaming policy retained. Blender dependencies resolve within this round's texture folder; unchanged scan PNGs are portable copies.

## Captures

Before means the reviewed round2 game mesh and its corrected master. Captured the fixed camera set before modelling. Before/after/master share matching poses, 60° FOV, target and sun. [Capture index, hashes and camera JSON](evidence/index.json).

- 150 m: [round2 before](evidence/before/{NAME}_150m.png), [round3 after](evidence/after/{NAME}_150m.png).
- 2× far-end crop: [before](evidence/crops_2x/before_negative_X_far_end_2x.png), [after](evidence/crops_2x/after_negative_X_far_end_2x.png).
- 2× near-end crop: [before](evidence/crops_2x/before_positive_X_near_end_2x.png), [after](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m against round2: [before](evidence/before/{NAME}_12m.png), [after](evidence/after/{NAME}_12m.png).
- Master against game: [12 m master](evidence/master/{NAME}_12m.png) / [game](evidence/after/{NAME}_12m.png); [150 m master](evidence/master/{NAME}_150m.png) / [game](evidence/after/{NAME}_150m.png); [450 m master](evidence/master/{NAME}_450m.png) / [game](evidence/after/{NAME}_450m.png).
- Switch: [LOD0 at 249.6 m](evidence/switch/{NAME}_249_6m.png), [LOD1 at 270.4 m](evidence/switch/{NAME}_270_4m.png), measured from one common LOD0 whole-building bounds centre.

The four crops use identical original-pixel boxes and 2× nearest-neighbour duplication, without enhancement or generated detail. [Crop coordinates and provenance](evidence/crops_2x/index.json). Full images use the editor viewport's native resolution, recorded in the index. 150 m: arm15000 cm to `(0,0,600)`, pitch -55°, yaw145°, FOV60°, sun `(-42,135,0)`. The inherited 12 m camera uses pitch -35°. Accepted water staging is unchanged: nominal waterline -600 cm; preview top world +11 cm relative to the deck at +600 cm gives -589 cm. The new reinforcement still crosses that water-entry region and projects over water. These are editor previews, not production gameplay-camera or automatic LOD evidence.

## Verification and open items

Blender and fresh FBX imports pass finite tangent, nondegenerate geometry and nondegenerate UV0 checks. Game minimum triangle altitude {audit['meshes'][NAME]['minimum_triangle_altitude_m']*100:.2f} cm; finer inherited master bevel triangles remain source-only. All new facet normals point upward (minimum normal Z {actual['minimum_facet_normal_z']:.3f}). New LOD1 charts exactly match game; master charts are transferred onto the same atlas. The inherited LOD1's overlapping/coplanar body surfaces still produce ambiguous surface-ray UV comparisons (max {audit['LOD1_BakeUV_max_difference']:.3f} UV units); this is documented from R2. LOD1 is used beyond 260 m, after the unchanged parent's 180–230 m normal/AO fade. Forced near-distance LOD1 bake fidelity is not certified; inspect the supplied switch pair.

Native counts, centimetre bounds, four-slot bindings, source MD5s, unchanged material values, collision and same-editor save/reload persistence checked. Scoped native import log is in the receipt. All {len(protected)} protected accepted/greybox/round1/round2 files retain their starting hashes. Modelling lessons were rechecked before reporting: projecting steel stays geometry; game faces exceed 1 cm; no approved opening/body was removed; explicit `FBX_SCALE_NONE` export; atlas/parent bindings persisted; no new level or native file dialog.

Open for round4: realism judgment of the deliberately oversized, two-facet steel and painted root break; underside views are outside the intended camera envelope. Master materials retain their inherited zero bake strength, so atlas-painted fine normal/AO finish is assessed on the game view while both show the common painted tint. Producer approval of round3 is not inferred. Production LOD attachment, gameplay/state integration, collision/navigation and performance remain Claude's work. No restart, editor close, commit, push, accepted `final/` write, production asset edit, village.json or setup edit.

Continue round4 from this report, `source/Bridge_baked.blend` and R3/Review03. Retain the approved 150 m read and examine the round2/round3 12 m pair before further realism changes. Use the repo building-production workflow; do not rerun old round4 rebuild scripts.
'''
    (OUT/'REPORT.md').write_text(report,encoding='utf8');print('REPORT READY',len(images),'captures, four 2x crops;',len(hashes['files']),'portable files',flush=True)
if __name__=='__main__':
    if '--probe-crops' in sys.argv:crops_helper.crops('probe')
    else:main()
