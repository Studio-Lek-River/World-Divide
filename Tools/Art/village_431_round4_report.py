"""Assemble the review-ready final handoff only after native QA and evidence."""
import hashlib,html,json,shutil,sys
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round4_common as c
OUT=c.OUT;FINAL=c.ROOT/'Art/Shared/village/final';BASE=OUT.parent
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,x):p.write_text(json.dumps(x,indent=2),encoding='utf8')

def main():
    from PIL import Image
    source=read(OUT/'evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');geo=read(OUT/'evidence/geometry_qa.json')
    assert native['persisted_level']==native['level']
    protected=read(OUT/'evidence/protected_before.json');assert all(sha(c.ROOT/f)==h for f,h in protected.items())
    original=read(c.ROOT/'Art/Shared/village/village.json');patch={'meshes':{},'buildings':{}};openings={};rows=[]
    for key in c.TYPES:
        meta=read(OUT/'evidence'/(key+'_openings.json'));contract=meta['contract_garrison'];building=json.loads(json.dumps(original['buildings'][key]));checks=source['apertures'][c.names(key)[0]]
        for check in checks:
            building['firing_positions'][check['index']]['location'][2]=round(check['proposed_eye_m']*100,3)
        if key=='Barn':building['capacity']=8
        patch['buildings'][key]=building
        changes=json.loads(json.dumps(meta['changes']))
        for change in changes:
            if 'firing_eye_old_m' in change:change['firing_eye_old_m']=original['buildings'][key]['firing_positions'][change['index']]['location'][2]/100
        openings[key]={'retained_openings':meta['openings'],'named_firing_positions':building['firing_positions'],'changes_from_greybox':changes,'aperture_checks':checks,'capacity':building['capacity'],'entry':building['entry']}
        openings[key]['firing_position_patch']=[{'name':new['name'],'old_location_cm':old['location'],'new_location_cm':new['location']} for old,new in zip(original['buildings'][key]['firing_positions'],building['firing_positions']) if old['location']!=new['location']]
        if key=='Church':
            openings[key]['belfry_note']='The three original BelfryNX/PY/NY helpers are retained unchanged at 2150 cm. They are outside the 276 nave/house aperture-ray checks. Tower apertures remain at 20-23 m; check gameplay state-dependent belfry firing during integration.'
    openings['visual_only']={'Church':'Additional west tower entrance and belfry louvres; contracted +X garrison entry retained. Arched nave heads keep original sill, width, head and eye clearance.','Workshop':'Additional +Y personnel door is closed decorative geometry, not a new firing/entry helper.','HouseB':'Paired long-side doors remain visual entries; contracted +X service/garrison entry retained.'}
    patch['buildings']['BridgeSpan']=original['buildings']['BridgeSpan']
    for n in c.NAMES:
        g=native['meshes'][n];low=native['meshes'][n+'_LOD1'];q=source['meshes'][n];lq=source['meshes'][n+'_LOD1']
        assert g['unreal_triangles']==q['triangles']==q['reimport']['triangles']
        assert low['unreal_triangles']==lq['triangles']==lq['reimport']['triangles']
        budget=25000 if c.type_for(n)=='Church' else 12000;assert g['budget_triangles']<=budget and low['budget_triangles']<=(6000 if budget==25000 else 3000)
        b=g['bounds_cm'];patch['meshes'][n]={'fbx':n+'.fbx','slots':g['slots'],'triangles':g['budget_triangles'],'collision_boxes':g['collision']['convex_hulls'],'bounds':[[b['min'][a] for a in 'xyz'],[b['max'][a] for a in 'xyz']]}
        rows.append(f"| {n.replace('SM_Village_','').replace('SM_Bridge_Span_','Bridge ')} | {g['budget_triangles']:,} | {low['budget_triangles']:,} | {len(g['slots'])} | {g['collision']['convex_hulls']} |")
    write(FINAL/'village_patch.json',patch);write(FINAL/'openings_handoff.json',openings)
    write(FINAL/'collision.json',{n:native['meshes'][n]['collision'] for n in c.NAMES})
    receipt={'files':{},'textures':{},'scripts':{},'protected_prior_rounds_and_greybox_match':True,'protected_file_count':len(protected)}
    for folder in ['source','fbx','textures']:
        for f in sorted((FINAL/folder).glob('*')):
            if f.is_file():receipt['files'][f.relative_to(FINAL).as_posix()]={'bytes':f.stat().st_size,'sha256':sha(f)}
    for f in (FINAL/'textures').glob('*.png'):
        size=Image.open(f).size;assert max(size)<=2048
        if f.stem.startswith(('SM_','Round')):provenance={'license':'in-house bake/tint/data','source':'Round4 source and bake generator'}
        else:
            surface,kind=f.stem.rsplit('_',1);src=c.ROOT/'Saved/Surfaces'/surface/(kind+'.png');assert sha(src)==sha(f)
            provenance={'license':'CC0','provider':'ambientCG','url':'https://ambientcg.com/view?id='+surface,'source':str(src.relative_to(c.ROOT)),'unchanged_source_sha256':sha(src)}
        receipt['textures'][f.name]={'dimensions':size,**provenance}
    for f in (c.ROOT/'Tools/Art').glob('village_431_round4*.py'):receipt['scripts'][str(f.relative_to(c.ROOT))]=sha(f)
    receipt['generator_helper_hashes']={str(f.relative_to(c.ROOT)):sha(f) for f in [c.ROOT/'Tools/Art'/n for n in ['village_431_housea.py','village_431_round2.py','village_blender.py','bridge_blender.py','village_431_preview.py','kit/strip_light_preview.py']] if f.exists()}
    write(FINAL/'hashes.json',receipt)
    manifest={'status':'Round4 final set delivered for producer approval; approval pending','native_level':native['level'],'native_meshes':native['meshes'],'materials':native['materials'],'texture_import_settings':native['textures'],'portable_textures':receipt['textures'],'dependency_closure':native['dependencies'],'source_registry_tags':native['source_tags'],'source_qa':source,'styles':c.CONFIG,'wood_tints':c.WOOD_TINT,'roof_scans':{k:'RoofingTiles006' if k in ['HouseA','HouseC','Farmhouse','Church'] else 'RoofingTiles013A' for k in c.TYPES},'source_authority':'source/<Type>_baked.blend; review masters never used as game meshes','origins':'Ground-centred, +X ridge/front, Z up; church nave centre; bridge deck-top centre. FBX metres converted to Unreal cm by explicit export scale; FBX_SCALE_NONE. Native preview actor scale 1.','lod':'Separate authored LOD1, one native LOD each. Claude attaches at 260m using whole-building bounds centre. No automatic native switch tested.','collision_note':'UCX embedded in each LOD0 FBX; destroyed bridge exactly two pier stub convex hulls, CTF_UseSimpleAsComplex','scope':'Native Unreal editor evidence only; not production gameplay or performance proof.'}
    write(FINAL/'manifest.json',manifest)
    for f in ['source_qa.json','geometry_qa.json','bake_receipt.json','unreal_verified.json','source_dependencies.json','material_graph.json','scorch_refinement.json']:
        shutil.copy2(OUT/'evidence'/f,FINAL/'evidence'/f)
    gallery=['<!doctype html><meta charset="utf-8"><title>#431 final village set</title><style>body{background:#202221;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}figure{margin:0}img{width:100%}figcaption{padding:7px}h2{margin-top:2em}</style><h1>#431 — Round 4 final village set</h1><p>Producer approval pending. Native Unreal editor captures beside Abrams; 60° horizontal FOV, 16:9. Manual LOD comparison, not gameplay. <a href="../issue431/ROUND_4.md">Report</a> · <a href="CLAUDE_IMPLEMENTATION_PROMPT.md">Claude handoff</a></p>']
    evidence={}
    def pic(phase,name,label):
        file=OUT/'evidence'/phase/(name+'.png');data=read(file.with_suffix('.json'));assert file.exists() and data['cameraFOV']==60
        dest=FINAL/'evidence'/phase;dest.mkdir(exist_ok=True);shutil.copy2(file,dest/file.name);shutil.copy2(file.with_suffix('.json'),dest/(name+'.json'))
        rel='evidence/'+phase+'/'+file.name;evidence[rel]={'sha256':sha(file),'camera':data}
        return '<figure><a href="'+rel+'"><img loading="lazy" src="'+rel+'" alt="'+html.escape(label)+'"></a><figcaption>'+html.escape(label)+'</figcaption></figure>'
    gallery.append('<h2>Whole set — 150 m</h2>'+pic('final','whole_set_150m','Four red-brown roofs, three lifted anthracite roofs'))
    for key in c.TYPES+['Bridge']:
        gallery.append('<h2>'+key+'</h2><div class="grid">')
        for n in c.names(key):
            for view in ['150m','threequarter']:gallery.append(pic('final',n+'_'+view,n+' '+view))
        gallery.append('</div><details><summary>Every state: 12 / 150 / 450 m master against game, and 260 m switch</summary>')
        for n in c.names(key):
            for view in ['12m','150m','450m']:
                game=read(OUT/'evidence/final'/(n+'_'+view+'.json'));master=read(OUT/'evidence/master'/(n+'_'+view+'.json'))
                for field in ['cameraLocation','cameraRotation','cameraFOV','target_cm','sun_rotation_deg']:assert game[field]==master[field],(n,view,field)
                gallery.append('<h3>'+n+' '+view+'</h3><div class="grid">'+pic('master',n+'_'+view,'Review master')+pic('final',n+'_'+view,'Game LOD0')+'</div>')
            gallery.append('<div class="grid">')
            for tag,label in [('249_6m','LOD0 at 249.6 m'),('270_4m','Separate LOD1 actor at 270.4 m')]:gallery.append(pic('switch',n+'_'+tag,label))
            gallery.append('</div>')
        gallery.append('</details>')
    gallery.append('<h2>Post-reload smoke</h2><div class="grid">')
    for n in ['SM_Village_HouseA_Rubble','SM_Village_Barn','SM_Village_Workshop','SM_Village_Church_Damaged','SM_Bridge_Span_Destroyed']:gallery.append(pic('persisted_smoke',n+'_threequarter',n+' after same-editor reload'))
    gallery.append('</div><h2>Church — opposite base direction</h2>'+pic('final','Church_opposite_base','Church from opposite base, 150 m')+'<p>Church damage views use the same neutral afternoon light intensity with azimuth reversed to light the breach-facing elevation. All master/game pairs use identical camera and light. Exact 12 m views target exterior details and intentionally crop large buildings. The preview river and pier blocks are not exported.</p>')
    (FINAL/'REVIEW.html').write_text(''.join(gallery),encoding='utf8');write(FINAL/'evidence/index.json',evidence)
    (OUT/'REVIEW.html').write_text('<!doctype html><meta http-equiv="refresh" content="0;url=../../final/REVIEW.html"><a href="../../final/REVIEW.html">Final review gallery</a>',encoding='utf8')
    report='''# #431 — Round 4 final set

**Delivered for producer approval of the set. Approval pending; stop here.** Final realism/consolidation round requested on GPT-6-Astra. Round2 HouseA remains the approved design/quality bar; Round3 was reviewed and carried into this round.

[Final gallery](../final/REVIEW.html) · [Claude implementation prompt](../final/CLAUDE_IMPLEMENTATION_PROMPT.md) · [manifest](../final/manifest.json) · [hashes/provenance](../final/hashes.json) · [village.json patch](../final/village_patch.json).

## What changed

- Four red-brown terracotta roofs (HouseA, HouseC, Farmhouse, Church), three lifted anthracite roofs. Actual CC0 roof scans, real-scale UVs, rounded ridge caps on the six new types, restrained eave moss/grime; approved HouseA geometry retained.
- Church: tower entrance, clock faces and belfry louvres on four sides, stone plinth/bands/corner quoins, tall arched nave windows with real frames, irregular torn tower rim and fallen masonry. Broader asymmetric roof breach, exposed framing and retained wall breach. Damaged church is captured on its breach-facing side in light.
- Barn: timber paired doors and loft hatch, braces/hardware/hoist beam, vertical timber grain and board-and-batten relief, retained small stable windows and plinth.
- Workshop: profiled metal lower cladding, two roller doors with guide rails/housings, personnel door, blank signboard, dark-backed glazed skylight strips, drainage.
- Houses: existing chimneys retained on all four houses; painted door/wood colours vary, small window boxes on HouseB/C and a compact HouseB bin cabinet. No extra dish. All buildings retain gutters/downpipes. Plinths receive restrained grime.
- Farmhouse/church roof tears are wider and asymmetric, with broken battens/tiles and scorch. Full wall thickness and backing retained. Existing breach debris retained.
- Destroyed bridge halves drop 36° from their pier ends. At the +6 m preview deck height the broken deck tips dip below the river surface; rebar and water debris remain. Two stub collision hulls only.

## Portable and Unreal authority

`Art/Shared/village/final/`: 23 game FBXs, 23 separate LOD1 FBXs, 23 review-master FBXs, eight baked Blender sources, portable PNGs, native material recipe/overrides/settings, collision and opening data. All game exports preserve their names and origins. Footprints/main massing retained; attached door steps, trim, bin cabinet and hoist project locally as documented geometry.

Unreal: `/Game/Art/Candidates/Village/Final/Meshes/`, `Masters/`, `Materials/`, `Textures/Scans/`, `Textures/Atlas/`, `Textures/Tint02/`, `Textures/AtlasBridge02/`, and `M_SetSurface`. Existing `/Game/Art/Candidates/Village/L_VillagePreview` remains the only preview World. Historical actors/packages are retained and parked. Early `/Final/First/` imports and root `/Final/Textures/` scans are historical; the final dependency closure does not use them. Bridge final meshes and masters are in `Meshes02/` and `Masters02/`; their revised atlas is in `Textures/AtlasBridge02/`. Farmhouse/Church damaged tint uses `Textures/Tint02/`. The manifest provides the exact per-mesh mapping. Every final mesh and texture was imported from the final portable folder; asset-registry source tags are in the manifest.

## Verification and evidence

First native captures preceded each type's refinement. Final gallery covers **every state** at 12 m, 150 m, 450 m and three-quarter; all 23 master/game comparisons at 12/150/450 m; all 23 manual 249.6/270.4 m pairs measured from the same LOD0 bounds centre; whole-set overview; five post-reload smoke views. 60° FOV throughout. These are editor views, not gameplay-camera or automatic-LOD evidence.

All 69 source/export meshes pass geometry/UV/tangent checks; all 46 game source/FBX/Unreal counts agree. 276 aperture rays pass. The Workshop damaged review master is 5,412 source / 5,411 Blender FBX re-import / 5,412 Unreal triangles; the higher count is retained. Game budgets are judged on the higher count. All 69 imported meshes have Nanite off and one native LOD; the authored LOD1 remains a separate mesh. All final instance parents/texture/scalar/vector bindings persisted through a same-editor reload. Native dependency closure is recorded and contains no earlier-round dependency. All eight saved Blender sources resolve images inside the delivery.

| Mesh/state | LOD0 triangles | LOD1 triangles | Slots | UCX hulls |
|---|---:|---:|---:|---:|
'''+ '\n'.join(rows)+'''

## Lessons and finish-workflow check

Real projecting facade details remain geometry; fine chamfers are baked from review masters. No size-based visible-part deletion or global decimation. Openings have returns/backing; rubble walls retain 30 cm thickness. Large flat surfaces bypass geometric AO. Game triangle altitude is above the approximately 1 cm threshold; independent FBX tangents checked. Physical brick/plank/tile scale checked against the close views. Mixed debris uses the building's own materials. Explicit `FBX_SCALE_NONE` export and native centimetre bounds checks prevent 1/100 scaling. Textures are CC0/in-house PNGs at most 2K, with mipmaps retained. Glass uses the current shared parent directly; the private surface graph is needed for scan/atlas inputs and is documented. No native-dialog operation, native LOD attachment, production-level edit, commit or push.

Before changes, saved Village candidate packages were archived with hashes in `round4/archive/pre_round4_candidates.zip` and `receipt.json`. A first validated portable geometry checkpoint is also retained. Final closure was copied/imported and explicitly saved; serialized parents, slot bindings and registry dependencies were checked. No historical package was deleted: a fresh editor process was forbidden, so same-editor reload verification is the stated limit and recovery dependencies remain. Final smoke images follow reload. Editor left running.

## Integration and remaining acceptance

Producer visual approval is pending. The handoff lists the exact opening/eye changes: Barn loft 5.95 m, Workshop high windows 3.3 m, Church nave 4.4 m; Barn capacity 8 per brief. Original entry helpers remain. Decorative new doors are not new gameplay helpers. `village_patch.json` contains ready-to-merge building/mesh entries; greybox originals were not edited.

Preserve scan/atlas UV conventions, object-normal decode, Church brick UV scale, normal/AO fade 180–230 m, and whole-building LOD1 at 260 m. Final graph/overrides and all material bindings are serialized. The LOD1 near-detail atlas is for far use only; the documented fade is required.

Still untested: #432 idempotent setup/regeneration, actual production state swaps and firing, collision/navigation/selection/placement/rotations, damage and bridge gameplay, actual camera/culling/shadows/LOD switching, owner/power applicability, and representative village runtime measurements at 240 units. No production readiness or performance claim is made from these editor views.
'''
    (BASE/'ROUND_4.md').write_text(report,encoding='utf8')
    state='''# #431 village set — current state

- **Gate:** Round4 delivered for **producer approval of the set; approval pending**. Stop here. [ROUND_4.md](ROUND_4.md), [final gallery](../final/REVIEW.html), [Claude handoff](../final/CLAUDE_IMPLEMENTATION_PROMPT.md).
- **Authority:** producer decisions → `docs/art/village/VILLAGE_BRIEF.md` → greybox contract. `BRIEF.md` defines the four rounds. Ground-centred origins; church nave centre; bridge deck-top centre. Greybox files remain untouched for #432.
- **Portable authority:** `Art/Shared/village/final/`: eight `source/<Type>_baked.blend`, 23 LOD0 +23 LOD1 FBXs, 23 source review masters, ≤2K PNGs, `manifest.json`, `hashes.json`, `village_patch.json`, `openings_handoff.json`, `collision.json`, material recipe and evidence.
- **Native authority:** `/Game/Art/Candidates/Village/Final/Meshes/`, `Masters/`, `Materials/`, `Textures/Scans/`, `Textures/Atlas/`, `Textures/Tint02/`, `Textures/AtlasBridge02/`, `M_SetSurface`; existing `/Game/Art/Candidates/Village/L_VillagePreview`. Bridge meshes/review masters use `Meshes02/` / `Masters02/` for the final bent-rebar revision. Earlier candidates/first imports retained, parked and excluded from final dependency closure. Editor left running.

## Approvals

| Stage | State |
|---|---|
| Round1 HouseA architecture | producer approved 2026-10-23: "Approve + my fixes" |
| Round2 HouseA quality bar | producer approved 2026-10-23: "Approve + carry notes"; "The producer approved HouseA (Round 2) as the quality bar for the set." |
| Round3 set | Claude reviewed 2026-10-24; critique carried into Round4 |
| Round4 realism/consolidation/handoff | delivered; producer approval pending |

Latest authorization: "Round 4 of #431, the final round, now on GPT-6-Astra: realism, consolidation and the Claude handoff." No inferred approval of the final visuals.

## Verified delivery and limits

All 46 game source/FBX/native counts agree and fit budgets; 69 source/review exports pass UV/tangent QA; 276 opening rays pass. Every state has final 12/150/450 m and three-quarter editor evidence, matched master/game sets and manual switch pairs. Native parents/material overrides persisted on reload; final closure contains no earlier candidate dependency. Final registry source filenames point to the final portable files. Previous round files and greybox hashes match.

No editor restart was permitted. Historical packages were archived and retained, not deleted. Same-editor reload and post-reload smoke checks are the verification limit. No native dialogs, commits, pushes or production edits. Recovery: `round4/archive/receipt.json` and `pre_round4_candidates.zip`; restore packages only with producer-coordinated editor shutdown. Never close the running editor as cleanup.

Integration: #432 replaces greybox from final files, merges `village_patch.json`, binds the serialized material recipe, attaches LOD1 at 260 m, applies destroyed-bridge two-stub `CTF_UseSimpleAsComplex`, and tests setup/gameplay/performance. Barn brief capacity 8, loft eyes 5.95 m; Workshop 3.3 m; Church 4.4 m. Detail normal/AO fade 180–230 m required for far LOD. Production state swaps, firing, collision/navigation/placement, culling and runtime performance remain untested.

## Next single milestone

Producer reviews the final gallery and approves the set or names bounded corrections. After acceptance Claude integrates #432 using `final/CLAUDE_IMPLEMENTATION_PROMPT.md`. Do not start integration or remove historical assets before that decision.

Safe reproduction: capture controls are in `Tools/Art/village_431_round4_delivery.py`; authoritative paths and values are the final manifest and saved sources. Full `round4.py` and bake runs destructively rebuild round4 outputs; do not replay on an accepted delivery merely to render. Final copy/import stages are explicit, and import skips existing packages; changed geometry needs a deliberate new revision rather than silently reusing an old import.

Continuation: Use `$world-divide-building-production` for #431. Read this STATE and ROUND_4.md. Preserve approved HouseA and the final saved set. Record the producer's decision; apply only requested corrections or hand accepted files to Claude for #432. Keep the editor running, never trigger native dialogs, do not commit or push.
'''
    (BASE/'STATE.md').write_text(state,encoding='utf8')
    print('Final report, gallery, manifest and state assembled',len(evidence),'images',flush=True)

if __name__=='__main__':main()
