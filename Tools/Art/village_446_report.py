"""Issue446 R1 offline review receipt. Writes only the round1 delivery."""
import json,hashlib,html,math
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue446/round1';NAME='SM_Bridge_Span_Destroyed'
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    source=read(OUT/'evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');audit=read(OUT/'evidence/independent_audit.json');build=read(OUT/'evidence/build.json')
    protected=read(OUT/'evidence/protected_before.json');assert all(sha(ROOT/p)==v for p,v in protected.items())
    for phase in ['switch','before_switch']:
        for tag,distance in [('249_6m',24960),('270_4m',27040)]:
            path=OUT/'evidence'/phase/(NAME+'_'+tag+'.json');data=read(path);centre=data['target_cm'];actual=math.dist([data['cameraLocation'][a] for a in 'xyz'],centre);assert abs(actual-distance)<.1
            data.update(manual_separate_LOD_actor=True,bounds_centre_world_cm=centre,camera_to_bounds_centre_cm=actual,distance_reference='Common LOD0 whole-building bounds centre');path.write_text(json.dumps(data,indent=2))
    images={}
    for phase,views in [('before',['12m','150m','450m']),('before_master',['12m','150m','450m']),('before_switch',['249_6m','270_4m']),('after',['12m','150m','450m']),('master',['12m','150m','450m']),('switch',['249_6m','270_4m'])]:
        for view in views:
            p=OUT/'evidence'/phase/(NAME+'_'+view+'.png');data=read(p.with_suffix('.json'));assert data['cameraFOV']==60
            images[p.relative_to(OUT).as_posix()]={'sha256':sha(p),'camera':data,'dimensions':Image.open(p).size}
    for view in ['12m','150m','450m']:
        before=read(OUT/'evidence/before'/(NAME+'_'+view+'.json'));after=read(OUT/'evidence/after'/(NAME+'_'+view+'.json'));master=read(OUT/'evidence/master'/(NAME+'_'+view+'.json'))
        for field in ['cameraLocation','cameraRotation','cameraFOV','target_cm','sun_rotation_deg']:assert before[field]==after[field]==master[field],(view,field)
    rows=[]
    for suffix,label in [('', 'LOD0'),('_LOD1','LOD1'),('_MASTER','Master')]:
        n=NAME+suffix;s=source[n];u=native['meshes'][n];assert s['triangles']==s['fbx']['triangles']==u['triangles']
        rows.append(f"| {label} | {s['baseline_triangles']:,} | {s['triangles']:,} | {s['fbx']['triangles']:,} | {u['triangles']:,} | +{s['added_triangles']} |")
        for tag in json.loads(u['source_tags']['AssetImportData']):
            package=ROOT/'Content'/u['path'].removeprefix('/Game/');file=(package.parent/tag['RelativeFilename']).resolve()
            assert file.is_relative_to(OUT) and hashlib.md5(file.read_bytes()).hexdigest()==tag['FileMD5']
    hashes={'files':{},'scripts':{},'protected_files_unchanged':len(protected)}
    for folder in ['fbx','source','textures']:
        for p in sorted((OUT/folder).iterdir()):
            if p.suffix not in ['.fbx','.blend','.png']:continue
            hashes['files'][p.relative_to(OUT).as_posix()]={'bytes':p.stat().st_size,'sha256':sha(p)}
    for p in sorted((ROOT/'Tools/Art').glob('village_446_*.py')):hashes['scripts'][p.relative_to(ROOT).as_posix()]=sha(p)
    (OUT/'hashes.json').write_text(json.dumps(hashes,indent=2))
    (OUT/'evidence/index.json').write_text(json.dumps(images,indent=2))
    manifest={'status':'Round1 delivered for review; no producer approval claimed','scope':NAME+' only; round1 portable authority','native_root':'/Game/Art/Candidates/Village/Rebar446/R1/Review03','native':native,'source':source,'audit':audit,'images':images,'textures':{p.name:Image.open(p).size for p in (OUT/'textures').glob('*.png')},'lod':'Separate authored LOD1, manual switch comparison. Attach at 260m in production via Claude.','waterline':'Contract nominal mesh-local -600cm. Accepted preview retained: bridge z600cm, water cube centre z10cm, scaleZ .02, top world z11cm = mesh-local -589cm. All new bars above both.'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    def pic(phase,view,label):
        rel=f'evidence/{phase}/{NAME}_{view}.png'
        return f'<figure><a href="{rel}"><img src="{rel}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    gallery=['<!doctype html><meta charset="utf-8"><title>#446 R1 rebar review</title><style>body{background:#202322;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{width:100%}figcaption{padding:8px}h2{margin-top:2em}</style><h1>#446 Round 1 — destroyed bridge reinforcement</h1><p>Unreal editor preview, 60° FOV. Click an image for native pixels. Producer review pending. <a href="REPORT.md">Report</a></p>']
    gallery+=['<h2>150 m before / after</h2><div class="pair">',pic('before','150m','Accepted #431 mesh'),pic('after','150m','Round1 Review03: 42 bent bars, +588 triangles'),'</div>']
    for view in ['12m','150m','450m']:
        gallery += [f'<h2>Master against game — {view}</h2><div class="pair">',pic('master',view,'Review master'),pic('after',view,'LOD0 game mesh'),'</div>']
    gallery += ['<h2>Manual LOD switch pair</h2><div class="pair">',pic('switch','249_6m','LOD0 at 249.6 m from shared LOD0 bounds centre'),pic('switch','270_4m','Separate LOD1 actor at 270.4 m from shared LOD0 bounds centre'),'</div><p>These are editor views, not gameplay or an automatic LOD test. Historical checkpoints remain in checkpoint01 and checkpoint02.</p>']
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    paths=['fbx/'+NAME+'.fbx','fbx/'+NAME+'_LOD1.fbx','source/'+NAME+'_MASTER.fbx','source/Bridge_baked.blend']+['textures/'+NAME+'_'+end+'.png' for end in ['Tint','BakeNormal','BakeAO']]
    files='\n'.join(f"| [{p}]({p}) | `{hashes['files'][p]['sha256']}` |" for p in paths)
    length=[sum(math.dist(a,b) for a,b in zip(p,p[1:])) for p in build['bar_paths_m']]
    report=f'''# #446 — round 1 of 4

Round1 delivered for producer/Claude review. [Review gallery](REVIEW.html). Work is confined to the destroyed span, its separate LOD1, retained master and own atlas. No visual approval or production readiness is claimed.

## Result

Added 42 bent, closed triangular-prism bars: a nine-bar peeled mat at each dropped end, plus six-bar fans on each of the four side faces. Nominal diameter 6 cm (triangular cross-section maximum width about 5.2 cm); authored exposed paths {min(length):.2f}–{max(length):.2f} m. Roots sit in the approved concrete above the waterline; free segments turn down toward the collapse. The inherited submerged bars remain. New warm rust edges are atlas texels on `Trim`; `SurfaceTint` remains exactly `(0.10, 0.075, 0.055)`. No inherited material scalar/vector values or parent changes.

The final placement puts the new mat across the visible slab surface rather than showing only short vertical tips above water. The 12 m view reads clearly as bent reinforcement. At 150 m it adds grouped fine bristles and their shadows; each bar is still below one pixel in width, so the strength of that read remains a producer review point for round 2. No further exaggeration beyond the brief's diameter/length range was used.

## Triangle counts

| Mesh | Accepted Blender | Round1 Blender | FBX reimport | Unreal | Increase |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

LOD0 and LOD1 each have 12 triangles of allowance left. Four source/exported/native slots in order: `Brick`, `Roof`, `Trim`, `Rubble` (source material datablocks retain their mesh-name prefixes). Unused inherited Glass/Wood slots and other-state source datablocks were removed. Nanite disabled; each native asset has one native LOD. Separate LOD1 keeps all new bar geometry. Deck-top-centre origin, transforms and every inherited vertex/face are unchanged; material-index compaction preserves the same surfaces. Exactly two original UCX hulls, native collision data byte-for-data equal to the accepted mesh, `CTF_UseSimpleAsComplex`.

## Authority, paths and hashes

Portable authority is this folder's `fbx/`, `source/`, `textures/`. Native review authority is `/Game/Art/Candidates/Village/Rebar446/R1/Review03/`: `Meshes/`, `Materials/`, `Textures/`. The preview remains `/Game/Art/Candidates/Village/L_VillagePreview`. Earlier exploratory R1 imports and `Refined/` are retained, parked and not the review authority. `checkpoint01/` and `checkpoint02/` preserve validated earlier passes; their native reimport metadata is historical.

| Portable file | SHA-256 |
|---|---|
{files}

[All file/script hashes](hashes.json) · [Manifest and exact native bindings](manifest.json) · [Native verification](evidence/unreal_verified.json) · [Independent source audit](evidence/independent_audit.json) · [Blender/FBX QA](evidence/source_qa.json) · [Bar paths and bake receipt](evidence/build.json).

Only this mesh's three 2048² atlas PNGs were rebaked, from the retained 2,760-triangle accepted master plus the same 588-triangle reinforcement: Cycles selected-to-active object normals and AO, four samples, 3.5 cm cage, 8 cm maximum ray, 2 px margin. Object-normal Y converted for Unreal as in round4; inherited tint/weathering recipe retained, only new rebar texels receive the rust edge. Source texture dependencies are local to `textures/`; inherited concrete/other scan PNGs are unchanged copies for Blender portability. Native instances reuse the accepted `/Final/M_SetSurface` and accepted scan textures, with their atlas references redirected to Review03. Mipmaps and inherited streaming policy retained. No original round4 rebuild or bake entrypoint was run; this round calls only its pure bake/raster/QA helpers.

## Captures

The fixed cameras were captured from the accepted assets before modelling; the original first captures are preserved in `checkpoint01/evidence/before*`. The final before/after pairs use identical camera pose, 60° FOV, sun `(-42,135,0)` and the accepted #431 preview staging. Contract waterline is mesh-local -600 cm; the accepted water cube has its top at world +11 cm (centre +10, thickness 2), or mesh-local -589 cm with the bridge at +600. An exact-zero staging attempt intersected the preview ground and was reverted before the final captures. All new bars are above both heights. Camera JSON and image hashes: [evidence index](evidence/index.json).

- 150 m before: [PNG](evidence/before/{NAME}_150m.png), [camera](evidence/before/{NAME}_150m.json); after: [PNG](evidence/after/{NAME}_150m.png), [camera](evidence/after/{NAME}_150m.json).
- 12 m: [game](evidence/after/{NAME}_12m.png), [master](evidence/master/{NAME}_12m.png).
- 150 m: [game](evidence/after/{NAME}_150m.png), [master](evidence/master/{NAME}_150m.png).
- 450 m: [game](evidence/after/{NAME}_450m.png), [master](evidence/master/{NAME}_450m.png).
- Switch: [LOD0 249.6 m](evidence/switch/{NAME}_249_6m.png), [LOD1 270.4 m](evidence/switch/{NAME}_270_4m.png), from a common LOD0 bounds centre. Manual separate actors, not an automatic LOD test.

## Verification and open items

Blender and independent FBX reimport pass degenerate geometry, UV0 and tangent checks. Game minimum triangle altitude is {audit['meshes'][NAME]['minimum_triangle_altitude_m']*100:.2f} cm. The retained master has inherited finer bevel triangles, used only as bake/review source. Source audit verifies exact inherited geometry/collision and all image paths. New rebar LOD1 UV1 sample error is below 0.001 atlas texel. Native source MD5s match these FBXs; native counts, bounds, slot bindings, unchanged material values, collision and save/reload persistence verified. All {len(protected)} protected accepted/greybox files still match their starting hashes.

The inherited LOD1 triangulation has overlapping/coplanar construction surfaces. A surface-ray UV audit reports ambiguity/differences on those inherited faces (maximum {audit['LOD1_BakeUV_max_difference']:.3f} UV units); new rebar maps match. LOD1 is for beyond 260 m, after the unchanged parent fades normal/AO at 180–230 m. Review the supplied switch images; forced near-distance LOD1 bake fidelity is not certified.

Open: producer judgment of 150 m readability; later rounds' realism polish; actual gameplay camera, automatic production LOD switching, collision/navigation/state integration and runtime performance remain Claude's integration work. These captures are Unreal editor evidence. Same-editor save/reload only; the editor was not restarted or closed. No native dialogs, commits, pushes, production assets, setup scripts or accepted `final/` files changed.

Continue round 2 from this report and `source/Bridge_baked.blend`, with `Review03` as the native candidate. Preserve the accepted surrounding geometry and the +600-per-game-mesh ceiling; review the full-size 150 m pair before spending more detail.
'''
    (OUT/'REPORT.md').write_text(report,encoding='utf8')
    print('REPORT READY',len(images),'captures',len(hashes['files']),'portable files',flush=True)
if __name__=='__main__':main()
