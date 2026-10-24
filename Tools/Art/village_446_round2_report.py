"""Offline #446 R2 evidence crops, hashes and report. Round2 writes only."""
import sys,json,hashlib,math,html
from pathlib import Path
from PIL import Image
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round2';NAME='SM_Bridge_Span_Destroyed'
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def crops(phase='after'):
    source=OUT/'evidence'/phase/(NAME+'_150m.png');img=Image.open(source);w,h=img.size
    # Same boxes for all versions. Coordinates are recorded as original pixels.
    # Nearest neighbour enlarges evidence pixels without inventing edge detail.
    boxes={'negative_X_far_end':(706,394,908,530),'positive_X_near_end':(628,458,836,602)}
    folder=OUT/'evidence/crops_2x';folder.mkdir(exist_ok=True)
    result={}
    for end,base in boxes.items():
        box=tuple(round(v*(w/1537 if i%2==0 else h/924)) for i,v in enumerate(base))
        crop=img.crop(box);dest=folder/(phase+'_'+end+'_2x.png');crop.resize((crop.width*2,crop.height*2),Image.Resampling.NEAREST).save(dest)
        data={'source':source.relative_to(OUT).as_posix(),'source_sha256':sha(source),'original_pixel_box':box,'scale':2,'resampling':'nearest-neighbour, no filtering or enhancement','output':dest.relative_to(OUT).as_posix(),'sha256':sha(dest)}
        dest.with_suffix('.json').write_text(json.dumps(data,indent=2));result[end]=data
    return result
def main():
    q=read(OUT/'evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');audit=read(OUT/'evidence/independent_audit.json');build=read(OUT/'evidence/build.json')
    protected=read(OUT/'evidence/protected_before.json');assert all(sha(ROOT/f)==v for f,v in protected.items())
    rows=[];paths=[]
    for suffix,label in [('', 'LOD0'),('_LOD1','LOD1'),('_MASTER','Master')]:
        n=NAME+suffix;s=q[n];u=native['meshes'][n];assert s['triangles']==s['fbx']['triangles']==u['triangles']
        assert s['added_triangles']==580
        rows.append(f"| {label} | {s['baseline_triangles']:,} | {s['triangles']:,} | {s['fbx']['triangles']:,} | {u['triangles']:,} | +580 |")
        for tag in json.loads(u['source_tags']['AssetImportData']):
            package=ROOT/'Content'/u['path'].removeprefix('/Game/');file=(package.parent/tag['RelativeFilename']).resolve()
            assert file.is_relative_to(OUT) and hashlib.md5(file.read_bytes()).hexdigest()==tag['FileMD5']
        paths.append(('source/' if suffix=='_MASTER' else 'fbx/')+n+'.fbx')
    paths += ['source/Bridge_baked.blend']+['textures/'+NAME+'_'+s+'.png' for s in ['Tint','BakeNormal','BakeAO']]
    images={}
    for phase,views in [('before',['12m','150m','450m']),('before_master',['12m','150m','450m']),('before_switch',['249_6m','270_4m']),('round1_before',['12m','150m']),('after',['12m','150m','450m']),('master',['12m','150m','450m']),('switch',['249_6m','270_4m'])]:
        for tag in views:
            p=OUT/'evidence'/phase/(NAME+'_'+tag+'.png');data=read(p.with_suffix('.json'));assert data['cameraFOV']==60
            if 'switch' in phase:
                distance=24960 if tag=='249_6m' else 27040;actual=math.dist([data['cameraLocation'][a] for a in 'xyz'],data['target_cm']);assert abs(actual-distance)<.1
                data.update(manual_separate_LOD_actor=True,bounds_centre_world_cm=data['target_cm'],camera_to_bounds_centre_cm=actual,distance_reference='Common LOD0 whole-building bounds centre');p.with_suffix('.json').write_text(json.dumps(data,indent=2))
            images[p.relative_to(OUT).as_posix()]={'sha256':sha(p),'dimensions':Image.open(p).size,'camera':data}
    for view in ['12m','150m','450m']:
        states=['before','after','master']+(['round1_before'] if view!='450m' else [])
        cameras=[read(OUT/'evidence'/phase/(NAME+'_'+view+'.json')) for phase in states]
        for field in ['cameraLocation','cameraRotation','cameraFOV','target_cm','sun_rotation_deg']:assert all(c[field]==cameras[0][field] for c in cameras)
    crop_index={phase:crops(phase) for phase in ['before','round1_before','after']}
    (OUT/'evidence/crops_2x/index.json').write_text(json.dumps(crop_index,indent=2))
    hashes={'files':{},'scripts':{},'protected_files_unchanged':len(protected)}
    for folder in ['fbx','source','textures']:
        for p in sorted((OUT/folder).iterdir()):
            if p.suffix in ['.fbx','.blend','.png']:hashes['files'][p.relative_to(OUT).as_posix()]={'bytes':p.stat().st_size,'sha256':sha(p)}
    for p in (ROOT/'Tools/Art').glob('village_446_round2*.py'):hashes['scripts'][p.relative_to(ROOT).as_posix()]=sha(p)
    (OUT/'hashes.json').write_text(json.dumps(hashes,indent=2));(OUT/'evidence/index.json').write_text(json.dumps(images,indent=2))
    manifest={'status':'Round2 delivered for review; producer approval pending','mesh':NAME,'native_root':'/Game/Art/Candidates/Village/Rebar446/R2/Review01','native':native,'source_qa':q,'source_audit':audit,'captures':images,'crops_2x':crop_index,'textures':{p.name:Image.open(p).size for p in (OUT/'textures').glob('*.png')},'lod':'Separate LOD1, manual 249.6/270.4m comparison; same new reinforcement in both LODs','waterline':'Contract -600cm; accepted preview retained: water cube top world +11cm, bridge at +600cm, effective -589cm. No water/ground staging change.'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    def pic(phase,tag,label):
        rel=f'evidence/{phase}/{NAME}_{tag}.png';return f'<figure><a href="{rel}"><img src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    def cp(phase,end,label):
        rel=crop_index[phase][end]['output'];return f'<figure><a href="{rel}"><img class="crop" src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    gallery=['<!doctype html><meta charset="utf-8"><title>#446 R2 rebar review</title><style>body{background:#202322;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}.triple{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}figure{margin:0}img{width:100%}.crop{width:auto;max-width:100%;image-rendering:pixelated}figcaption{padding:8px}h2{margin-top:2em}</style><h1>#446 Round 2 — reinforcement that projects over water</h1><p>Unreal editor evidence; 60° horizontal FOV. <a href="REPORT.md">Report</a> · <a href="manifest.json">Manifest</a>. Producer review pending. Click full images for native pixels.</p><h2>150 m — accepted before / R2 after</h2><div class="pair">',pic('before','150m','Accepted #431'),pic('after','150m','R2: dark irregular bristles over water'),'</div><h2>150 m — reviewed round1 / R2</h2><div class="pair">',pic('round1_before','150m','Round1 reviewed candidate'),pic('after','150m','Round2'),'</div>']
    for end in ['negative_X_far_end','positive_X_near_end']:
        gallery += [f'<h2>2× pixel crops — {end}</h2><div class="triple">',cp('before',end,'Accepted #431'),cp('round1_before',end,'Round1'),cp('after',end,'Round2'),'</div>']
    for view in ['12m','150m','450m']:
        gallery += [f'<h2>Master against game — {view}</h2><div class="pair">',pic('master',view,'Master'),pic('after',view,'LOD0'),'</div>']
    gallery += ['<h2>Manual switch pair</h2><div class="pair">',pic('switch','249_6m','LOD0 249.6 m'),pic('switch','270_4m','Separate LOD1 270.4 m'),'</div><p>Editor views, not automatic production LOD switching or gameplay performance proof.</p>']
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    files='\n'.join(f"| [{p}]({p}) | `{hashes['files'][p]['sha256']}` |" for p in paths)
    bars=build['reinforcement'];widths=[b['width_m'] for b in bars];lengths=[b['length_m'] for b in bars]
    report=f'''# #446 — round 2 of 4

**Yes: the rebar reads at the fixed 150 m view.** Both slab ends have a distinct dark, irregular fringe extending over the water, with individual long lines and crossed pieces visible. The after image is visibly different from the accepted before and the reviewed round1 candidate. [Full-size gallery and 2× end crops](REVIEW.html). Producer/Claude visual approval remains pending.

## What changed

Rebuilt from the accepted #431 source; none of round1's 42 staple-like prism bars were carried forward. Kept the inherited submerged stubs and every other accepted vertex/face. Added 47 irregular pieces: 32 longitudinal bars across the two water-entry edges, 12 side-face bars and three short transverse pieces. Forty-three have three segments; four hooked-back bars have four. Actual ribbon widths {min(widths)*100:.1f}–{max(widths)*100:.1f} cm; centreline lengths {min(lengths):.2f}–{max(lengths):.2f} m. Roots leave the slab near the waterline, splay over water and turn downward; side bars emerge from the exposed concrete side near the same line. Some tips submerge. Spacing, bends, splay and lengths are seeded and varied; the three transverse pieces are isolated tangles rather than repeating rungs.

Each segment is an open strip with explicit reverse-wound backfaces (four triangles per segment); the broad face points upward toward the RTS view. No pipe cross-sections or end caps. This avoids changing the accepted material parent's sidedness. Atlas steel stays dark, without the light tan edge; `Trim` SurfaceTint is still exactly `(0.10, 0.075, 0.055)`. Small dark root stains are confined to this mesh's atlas. At 12 m these are visibly exaggerated heavy reinforcement strips, with flat ribbon shading; they no longer form identical hoops or a ladder. Round3/4 can refine the close-up shape while preserving the now-readable 150 m silhouettes.

## Triangle counts

| Mesh | Accepted Blender | Round2 Blender | FBX reimport | Unreal | Added |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

Each game LOD has 20 triangles left under the +600 allowance. LOD1 uses the same new bars, widths and outline. Four source/export/native slots: `Brick`, `Roof`, `Trim`, `Rubble` (source datablock names retain their mesh prefix). Nanite off; one native LOD per imported asset; authored LOD1 is a separate mesh. Origin remains deck-top centre, +X along span, Z up. Two original UCX pier-stub convex hulls and `CTF_UseSimpleAsComplex` unchanged; native collision data exactly equals the accepted baseline. No approved concrete or break geometry changed.

## Files, paths and hashes

Portable authority is `round2/fbx/`, `round2/source/`, `round2/textures/`. Native authority is `/Game/Art/Candidates/Village/Rebar446/R2/Review01/`: game meshes in `Meshes/`, corrected review master in `Masters02/`, plus `Materials/` and `Textures/`. The first master import is retained but superseded after its atlas UV mapping was corrected. The only preview level is the existing `/Game/Art/Candidates/Village/L_VillagePreview`. Prior R1 and accepted assets are retained.

| Portable file | SHA-256 |
|---|---|
{files}

[All file/script hashes](hashes.json) · [Manifest, bounds, bindings and import settings](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [Source audit](evidence/independent_audit.json) · [Native verification](evidence/unreal_verified.json) · [Bar widths, lengths, paths and bake receipt](evidence/build.json).

Rebaked only `SM_Bridge_Span_Destroyed_Tint`, `_BakeNormal`, `_BakeAO`, all 2048², from the retained accepted chamfered master plus this reinforcement: Cycles selected-to-active object normals/AO, four samples, 3.5 cm cage, 8 cm max ray, two-pixel margin. Round4 object-normal Y conversion and inherited tint/weathering recipe retained; normal outlier repair retained. Coincident ribbon backfaces bypass self-AO darkening. New atlas texels use dark steel modulation and root stains. The original round4 rebuild/bake entrypoints were not run; only their pure helpers were reused with the output root set to round2.

Native instances reuse the accepted `/Final/M_SetSurface` and scan textures. Material parent/scalar/vector overrides match the accepted instances exactly; only this mesh's atlas texture references point into R2. Source dependencies resolve inside this round's textures; retained scan PNGs are unchanged copies. Mipmaps and streaming settings retained. The master UV1 was transferred onto the repacked game atlas, including exact charts for the new ribbons: the first master comparison caught unmapped ribbon UVs making them pale, which was corrected without changing any material values or geometry. `checkpoints/master_uv_before/` retains the previous source/export for recovery; use the main `source/` files and native `Masters02/` for review.

## Captures

Captured the accepted mesh at all fixed cameras before building, then the reviewed round1 candidate at 150/12 m. The primary before is the accepted #431 baseline named in STATE; the additional round1 views make the reviewed failure directly comparable. Before, round1, after and master share the exact camera pose, FOV and sun for their matching distances. [Capture index with camera JSON and hashes](evidence/index.json).

- 150 m before: [accepted](evidence/before/{NAME}_150m.png), [round1](evidence/round1_before/{NAME}_150m.png); [round2 after](evidence/after/{NAME}_150m.png).
- 2× far end: [accepted](evidence/crops_2x/before_negative_X_far_end_2x.png), [round1](evidence/crops_2x/round1_before_negative_X_far_end_2x.png), [round2](evidence/crops_2x/after_negative_X_far_end_2x.png).
- 2× near end: [accepted](evidence/crops_2x/before_positive_X_near_end_2x.png), [round1](evidence/crops_2x/round1_before_positive_X_near_end_2x.png), [round2](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m: [game](evidence/after/{NAME}_12m.png), [master](evidence/master/{NAME}_12m.png).
- 150 m: [game](evidence/after/{NAME}_150m.png), [master](evidence/master/{NAME}_150m.png).
- 450 m: [game](evidence/after/{NAME}_450m.png), [master](evidence/master/{NAME}_450m.png).
- Switch: [LOD0 at 249.6 m](evidence/switch/{NAME}_249_6m.png), [LOD1 at 270.4 m](evidence/switch/{NAME}_270_4m.png), both from the common LOD0 whole-building bounds centre. Separate actors, not an automatic native LOD test.

The 2× crops use identical original-pixel boxes with nearest-neighbour duplication: no edge enhancement or generated detail. [Crop coordinates, provenance and hashes](evidence/crops_2x/index.json). Captures are the editor viewport's native resolution, recorded per image. Fixed 150 m camera: target `(0,0,600)` cm, arm 15000 cm, pitch -55°, yaw145°, FOV60°, sun `(-42,135,0)`. Exact 12 m uses pitch -35° as the accepted camera. Accepted water staging is unchanged: nominal mesh-local waterline -600 cm; preview water cube top is world +11 cm / mesh-local -589 cm. New bars straddle that water-entry region and most of their free length shows over water.

## Verification and open items

Blender and independent FBX reimport: zero degenerate faces, zero degenerate UV0 triangles, valid finite tangents. Game minimum triangle altitude {audit['meshes'][NAME]['minimum_triangle_altitude_m']*100:.2f} cm. Exact inherited positions, topology, surface assignments and object transforms verified; source material-index compaction only. Both original collision hulls unchanged. The master retains its accepted fine bevel triangles as bake source. New LOD1 ribbon UV1 charts match exactly, including both facing directions. The inherited LOD1 has coplanar/overlapping construction surfaces and a different triangulation; its surface-ray atlas audit remains ambiguous on inherited faces (max {audit['LOD1_BakeUV_max_difference']:.3f} UV units). It is used beyond 260 m, after the unchanged parent's normal/AO fade at 180–230 m; forced near-distance LOD1 bake fidelity is not certified. Supplied switch images show the authored far comparison.

Native counts, centimetre bounds, four-slot bindings, source import MD5s, unchanged scalar/vector values, collision and same-editor save/reload persistence checked. Scoped native import log is in the verification receipt. All {len(protected)} protected accepted/greybox/round1 files still match their initial hashes. Rechecked `references/modelling-lessons.md` before reporting: projecting reinforcement stays geometry, game faces exceed the 1 cm floor, no scale metadata error, no unintended opening, unchanged parent/material values, local PNG dependency delivery and all mandatory captures are present.

Open: Claude/producer judgment of the 150 m read and close-up realism; flat ribbon shading and deliberate size exaggeration at 12 m; later realism polish. Actual gameplay camera, automatic production LOD attachment, state integration, collision/navigation and runtime performance remain Claude's integration work. Verification used the same editor process; no restart. No native dialogs, new levels, editor close, commit, push, production asset or setup edit. The direct connected MCP tools required unavailable approval; the authorised local MCP driver and installed background Blender were used successfully.

Next round: use this report, `source/Bridge_baked.blend`, and R2/Review01. Preserve the readable silhouette while improving root transitions and close-up strip shading within the same allowance. Do not rerun old round4 rebuilds or overwrite accepted final delivery.
'''
    (OUT/'REPORT.md').write_text(report,encoding='utf8')
    print('REPORT READY',len(images),'native captures, six 2x end crops;',len(hashes['files']),'portable files',flush=True)
if __name__=='__main__':
    if '--probe-crops' in sys.argv:crops('probe')
    else:main()
