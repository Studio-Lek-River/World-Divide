"""Build the Round3 review index only after native verification and final captures."""
import hashlib,html,json,sys
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round3_common as c
OUT=c.OUT;BASE=OUT.parent

def read(path):return json.loads(path.read_text())
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    from PIL import Image
    source=read(OUT/'evidence/source_qa.json');native=read(OUT/'evidence/unreal_verified.json');geo=read(OUT/'evidence/geometry_qa.json')
    assert native['persisted_level']==native['level']
    rows=[]
    for key in c.TYPES+['Bridge']:
        for n in c.names(key):
            q=source['meshes'][n];low=source['meshes'][n+'_LOD1'];actual=native['meshes'][n];alow=native['meshes'][n+'_LOD1']
            assert q['triangles']==q['reimport']['triangles']==actual['unreal_triangles']
            assert low['triangles']==low['reimport']['triangles']==alow['unreal_triangles']
            rows.append(f"| {n.replace('SM_Village_','').replace('SM_Bridge_Span_','Bridge ')} | {actual['budget_triangles']:,} | {alow['budget_triangles']:,} | {len(actual['slots'])} | {actual['collision']['convex_hulls']} |")
    handoff={}
    for key in c.TYPES:
        original=read(OUT/'evidence'/(key+'_openings.json'));contract=original['contract_garrison'];checks=source['apertures'][c.names(key)[0]]
        entries=[]
        for i,item in enumerate(original['openings']):
            ray=checks[i];entry={'index':i,'contract_name':contract['firing_positions'][i]['name'],'opening':dict(zip(['face','along_m','sill_m','width_m','height_m','kind','floor'],item)),'contract_position_cm':contract['firing_positions'][i]['location'],'proposed_position_cm':list(contract['firing_positions'][i]['location']),'ray':ray}
            entry['proposed_position_cm'][2]=ray['proposed_eye_m']*100;entries.append(entry)
        handoff[key]={'capacity_contract':contract['capacity'],'capacity_brief':8 if key=='Barn' else contract['capacity'],'entry_cm':contract['entry'],'changes':original['changes'],'openings':entries,'new_visual_only_openings':'four belfry louvres; not garrison positions' if key=='Church' else None}
    (OUT/'openings_handoff.json').write_text(json.dumps(handoff,indent=2))
    receipt={'files':{},'textures':{},'scripts':{},'protected_round2_matches_approved_hashes':True}
    approved=read(BASE/'round2/evidence/delivery_receipt.json')['files']
    assert all(digest(BASE/'round2'/path)==data['sha256'] for path,data in approved.items())
    r1=read(BASE/'round1/evidence/delivery_receipt.json');assert digest(BASE/'round1/source/HouseA_round1.blend')==r1['source']
    assert all(digest(BASE/'round1/fbx'/(name+'.fbx'))==sha for name,sha in r1['fbx'].items())
    receipt['protected_round1_source_and_six_fbx_match_approved_hashes']=True
    for folder in ['source','fbx','textures']:
        for p in sorted((OUT/folder).glob('*')):
            if p.is_file():receipt['files'][str(p.relative_to(OUT))]={'bytes':p.stat().st_size,'sha256':digest(p)}
    for p in sorted((OUT/'textures').glob('*.png')):
        rec={'size':list(Image.open(p).size)};assert max(rec['size'])<=2048
        if p.stem.startswith('SM_') or p.stem=='Round3_White':rec['provenance']='in-house bake/tint/data'
        else:
            surface,kind=p.stem.rsplit('_',1);src=c.ROOT/'Saved/Surfaces'/surface/(kind+'.png');assert digest(src)==digest(p);rec.update(provenance='ambientCG CC0; unchanged copy',source=str(src.relative_to(c.ROOT)),source_sha256=digest(src))
        receipt['textures'][p.name]=rec
    for p in sorted((c.ROOT/'Tools/Art').glob('village_431_round3*.py')):receipt['scripts'][p.name]=digest(p)
    (OUT/'evidence/delivery_receipt.json').write_text(json.dumps(receipt,indent=2))
    manifest={'status':'Round3 delivered for Claude review; approval pending','native_level':native['level'],'native_meshes':native['meshes'],'source_qa':source,'styles':c.CONFIG,'materials':native['materials'],'texture_settings':native['textures'],'portable_textures':receipt['textures'],'atlases':'Textures/Atlas, except Barn uses Textures/AtlasBarn02','openings':'openings_handoff.json','lod_policy':'Separate authored meshes/actors; Claude attaches at 260m. One native LOD per imported candidate.','shader_requirement':'Game BakeStrength .35 except concrete/Rubble .08; master 0. Geometric atlas normal/AO fades 180–230m camera to bounds centre; scans and tint remain, as approved Round2.','protected_deliveries_verified':True,'scope':'Native editor previews. Production integration and runtime acceptance remain untested.'}
    manifest['church_scan_uv_scale']=[.625,1]
    manifest['bridge_slot_aliases']={'Brick':'Concrete','Roof':'Asphalt','Trim':'Reinforcement','Rubble':'Broken concrete'}
    manifest['switch_distance_reference']='LOD0 whole-building bounds centre shared by both actors; camera receipts include independently calculated distance'
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    gallery=['<!doctype html><meta charset="utf-8"><title>#431 Round3 review</title><style>body{background:#202221;color:#eee;font:16px system-ui;margin:24px}a{color:#bce0ff}p{max-width:1100px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}figure{margin:0}img{width:100%}figcaption{padding:7px 0}h2{margin-top:2em}</style><h1>#431 — Round 3 village set</h1><p>Delivered for Claude review; approval pending. Native Unreal editor views beside Abrams, not gameplay. 60° horizontal FOV, constrained 16:9. Exact 12 m views crop the asset. Click for raw native captures; camera JSON is beside each image. <a href="../ROUND_3.md">Report</a> · <a href="manifest.json">Manifest</a> · <a href="openings_handoff.json">Opening / firing handoff</a></p>']
    def pic(file,label):
        p=OUT/file;assert p.exists(),p
        camera=read(p.with_suffix('.json'));assert camera['cameraFOV']==60
        return f'<figure><a href="{file}"><img loading="lazy" src="{file}" alt="{html.escape(label)}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    gallery.append('<h2>All pristine types together — 150 m</h2>'+pic('evidence/final/whole_set_150m.png','HouseA, HouseB, HouseC, Farmhouse, Barn, Workshop, Church'))
    for key in c.TYPES+['Bridge']:
        gallery.append(f'<h2>{key}</h2><div class="grid">')
        for n in c.names(key):
            for camera in ['150m','threequarter']:gallery.append(pic('evidence/final/'+n+'_'+camera+'.png',n+' '+camera))
        gallery.append('</div><details><summary>12 m and switch pair</summary>'+pic('evidence/final/'+c.names(key)[0]+'_12m.png',key+' exact 12 m')+'<div class="grid">')
        for d in ['249_6m','270_4m']:gallery.append(pic('evidence/switch/'+c.names(key)[0]+'_'+d+'.png',('Game LOD0 249.6 m' if d=='249_6m' else 'Separate LOD1 actor 270.4 m')))
        gallery.append('</div></details>')
    gallery.append('<h2>Master / game comparisons — church, plus HouseB spot check</h2><p>Matched cameras and surfaces. Physical 20 mm master chamfers against the normal/AO bake. Game BakeStrength 0.35 (concrete slot 0.08), master 0. These comparisons do not implement gameplay or an automatic LOD switch.</p>')
    for n in c.names('Church')+[c.names('HouseB')[0]]:
        for camera in ['12m','150m','450m']:
            gallery.append(f'<h3>{n} {camera}</h3><div class="grid">')
            for phase,label in [('master','Review master'),('final','Game LOD0')]:gallery.append(pic('evidence/'+phase+'/'+n+'_'+camera+'.png',label))
            gallery.append('</div>')
    gallery.append('<h2>Church from the opposite base</h2>'+pic('evidence/final/Church_opposite_base.png','Opposite direction, 150 m'))
    gallery.append('<h2>Post-reload smoke checks</h2><div class="grid">')
    for n in ['SM_Village_HouseA_Rubble','SM_Village_Barn','SM_Village_Church_Damaged','SM_Bridge_Span_Destroyed']:gallery.append(pic('evidence/persisted_smoke/'+n+'_threequarter.png',n+' after reload'))
    gallery.append('</div><h2>Evidence scope</h2><p>LOD1 remains a separate imported actor. Claude attaches it at 260 m in setup. Both candidate meshes share materials; fine geometric atlas normal/AO fades 180–230 m, with scans and tint retained. Bridge is staged at +6 m over a simple preview river plane with preview pier blocks; none of that stage terrain/support geometry is exported. First captures remain in evidence/first; checks and warmup are diagnostics.</p>')
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    text='''# #431 — Round 3: the village set

**Delivered for Claude review. Review/approval pending; stop here.** Requested lane: GPT-6.1-Sol, round 3 of 3 Sol rounds. The producer approved Round2 HouseA as the set's quality bar. Round4 has not started.

Open the [final Unreal gallery](round3/REVIEW.html), [manifest](round3/manifest.json) and [opening/firing handoff](round3/openings_handoff.json).

## Delivered

HouseA's four carry fixes plus **HouseB, HouseC, Farmhouse, Barn, Workshop and Church in all three states**, and `SM_Bridge_Span_Damaged` / `SM_Bridge_Span_Destroyed`. There are 23 LOD0 meshes, 23 separate authored LOD1 meshes, 23 source/review masters and eight type-specific baked Blender sources. Round1, Round2 and the greybox deliveries remain unchanged; protected portable hashes were checked.

The six-slot construction and bake pipeline follow approved HouseA. Substantial reveals, frame profiles, sills, lintels, backing, roof thickness, drainage and damage framing remain geometry. Fine stable-edge 20 mm master chamfers are baked into normal/AO at UV1; UV0 retains real-scale scanned surfaces. Narrow profiles are excluded from beveling, and sub-5 mm master junction remnants are dissolved. Geometric normals are baked without adding the scan normal twice; ray outliers are repaired to the receiving normal. Large horizontal surfaces bypass AO. LOD1 uses hand-authored profiles, backing planes, merged coplanar wall strips and wedge debris; whole-face containment removes internal returns, with no global decimation.

### Architecture and variation

- **HouseA:** approved architecture and damage arrangement retained. Rubble brick is darker and dusted while keeping the standing wall's course scale. Fascia/drainage use zinc/PVC colour rather than brown timber; narrow sill/lintel profiles are slimmer and concrete is calmer. Roof macro variation falls to 3.5%, down from Round2's stronger blotches. The dish and number 18 remain on HouseA only.
- **HouseB:** 14 × 9 m, 6/10 m eaves/ridge, paired long-side doors and mirrored bays, warm render, anthracite roof and a central party-wall line. The contracted +X gable entry remains as a service entry in addition to the two long-side front doors.
- **HouseC:** 9.5 m square, 5.6/9 m, brown brick and red-brown roof. The +X ground-floor window at along=-1.5 m is widened into a small shop frontage, with enough masonry left beside the door.
- **Farmhouse:** 18 × 11 m, low 4.8 m eaves and 10.5 m ridge, warm render and a long red-brown roof. Upper long-wall openings and the shorter eave projection clear the inherited 4.5 m firing eye.
- **Barn:** 20 × 13 m, 5/11 m, dark scanned timber cladding, a 4 × 4.5 m entry and wooden hay doors above. Loft openings retain their 5.2 m sill and clear the main entry; their proposed firing eyes rise to 5.95 m. Dark timber uses Planks023A / Planks037A.
- **Workshop:** 16 × 12 m, 5.5/6.6 m shallow roof, two 3.5 × 4 m roller doors, high windows and cool render. The retained high windows require raised firing positions, listed below.
- **Church:** 22 × 11 m nave, 8/15 m roof, 6.5 m tower at -X, 26 m tower and 36 m spire. Older Bricks097, slate-coloured roof, buttresses, tower bands and recessed louvres distinguish the landmark. Damaged loses the spire/top to a nominal 20 m jagged tower; rubble keeps a nominal 8.67 m tower, with broken teeth slightly higher. Opposite-direction 150 m evidence is included.

All new damaged buildings have a stepped breach through the contracted -Y long wall, a broken roof outline on that slope, rafters/battens, partial glazing/frame loss, localized soot and mixed debris. Rubble has closed 30 cm stumps, varied heights below 3 m, fallen gable/roof fragments and mixed material heaps; the church tower is the explicit height exception.

### Bridge

Both states keep the intact span's deck-top-centre origin and 30 × 14 m frame. Damaged has an octagonal shallow carriageway crater through the asphalt/slab top, a nominal 6 m parapet gap and deck chunks. Destroyed retains two 11 m sections hinged at the pier ends at 24°, a roughly 10 m middle gap, irregular concrete break teeth, exposed reinforcement and partly submerged debris. Concrete031 / Concrete036 supply the concrete; asphalt is an in-house flat material. For the shared six-slot pipeline, bridge **Brick=concrete, Roof=asphalt, Trim=reinforcement, Rubble=broken concrete**; these named mappings replace the greybox's two-slot material assumption in integration.

Only two pier-end stub boxes retain collision on the destroyed state. Its native BodySetup uses **CTF_UseSimpleAsComplex** so complex traces also use the stub hulls; carry that setting into setup. The preview raises the span 6 m above a simple river plane and uses two separate support blocks; those supports, water and terrain are staging only and are absent from the FBXs. No intact-span dressing or production bridge/physics integration was attempted.

## Counts and slots

Game source, independent FBX reimport and Unreal counts agree for all 46 LOD0/LOD1 meshes. Counts below are the higher verified values. LOD0 ≤12k, LOD1 ≤3k, ≤6 slots; church also fits its ≤25k/6k allowance. Every candidate has Nanite disabled and one native LOD: the reduced mesh is deliberately separate.

| State | LOD0 | LOD1 | Native LOD0 slots | UCX hulls |
| --- | ---: | ---: | ---: | ---: |
'''+ '\n'.join(rows)+'''

Portable semantic slot order is **Brick, Roof, Trim, Glass, Wood, Rubble**. `Brick` is the facade slot even on render, timber and concrete variants; map by name using the manifest. Native import strips unused slots per state; LOD0 and LOD1 use matching surviving slots. The very cheap bridge LOD1 retains the same topology/count; no reduction is needed to fit its far budget. Masters remain review/source assets.

All 69 portable meshes pass geometry, UV and tangent checks. One source-only review master, Workshop_Damaged_MASTER, is **4,224 source / 4,223 FBX** triangles; the higher value is retained in the record. No game mesh loses triangles. [Source/FBX QA](round3/evidence/source_qa.json), [native verification](round3/evidence/unreal_verified.json), [bake and normal-repair receipt](round3/evidence/bake_receipt.json).

## Openings and firing integration

All original opening locations are retained or explicitly reshaped in [openings_handoff.json](round3/openings_handoff.json), including original firing-position names. **276 opening/eye ray tests** pass across pristine/damaged states. Glass/leaves/backing sit behind the wall plane; breaches replace the affected apertures. No gameplay garrison test is claimed.

- HouseB/HouseC/Farmhouse attic heads extend to 7.8 m where needed, preserving the inherited 7.5 m eye.
- Farmhouse long-side upper windows move from sill 3.9 m to 3.15 m and become 1.5 m high; the 4.5 m eye remains clear. The short eave projection prevents roof occlusion along that horizontal ray.
- HouseC shop window: sill 0.5 m, width 1.65 m, height 1.8 m; same along position. Its original 1.5 m eye remains inside.
- Barn loft eyes: **4.5 → 5.95 m**. Workshop high-window eyes: **1.5 → 3.3 m**. Church nave-window eyes: **1.5 → 4.4 m**. Claude must apply these proposed eye heights in integration; this delivery does not edit `village.json` or gameplay data.
- Barn's brief capacity is **8**; the greybox says 10 but provides only eight openings. The handoff records brief=8 / contract=10, following the brief's precedence. Other capacities match the contract. Church belfry louvres are visual openings, not added garrison positions.

## Materials, textures and exact authority

Final meshes: `/Game/Art/Candidates/Village/Round3/Final/`; review masters for the church and HouseB spot check: `/Round3/Masters/`. Instances: `/Round3/Materials/MI_<MeshName>_<Slot>[_MASTER]`. Parent `/Round3/M_SetSurface` is a candidate copy of the approved Round2 extension, with defaults repointed to Round3 textures. The shared surface cannot express the scan/atlas graph, so this copy is required; shared parents and previous candidates are untouched. Glass uses the current shared `M_Bld_Glass` directly.

97 delivered PNGs are ≤2048²: **27 unchanged CC0 scan maps** from Bricks085, Bricks097, RoofingTiles013A, Planks023A, Planks037A, Plaster001, Plaster006, Concrete031 and Concrete036; **69 in-house normal/AO/tint atlases**; one white data fallback. Copies match `Saved/Surfaces/<Name>/` by hash. All eight baked sources resolve their external images inside `round3/textures/` ([dependency audit](round3/evidence/source_dependencies.json)). No git-ignored pack is needed for the building materials. Mips and streaming remain enabled in the candidate. See [hash/provenance receipt](round3/evidence/delivery_receipt.json).

Native scans are under `/Round3/Textures/`; final atlases under `/Round3/Textures/Atlas/`, **except Barn, which uses `/Round3/Textures/AtlasBarn02/`** after its hay-door refinement. Old Barn atlas imports are diagnostic history. Church's older English-bond brick uses **ScanUVScale=(0.625,1)** on the facade slot, giving a 1.6 m horizontal / 1 m vertical repeat (roughly 10 cm headers, 20 cm stretchers and 6–6.5 cm courses). Other instances keep the identity default. This scales only UV0 scan sampling; bake UV1 is untouched. [Calibration receipt](round3/evidence/church_brick_scale.json). Normal atlases encode object-space vectors with UE's Y mirror and use linear mask compression; do not import them as tangent normal maps. Source Blender materials show colour intent; the saved native graph/overrides are the final shading authority.

The approved Round2 material policy carries forward: game BakeStrength **0.35** except concrete/Rubble **0.08**, master **0**. The concrete exception calms excess normal/AO contrast on thin sills/lintels while keeping the scan's grain. Geometric atlas normal/AO fades from **180 to 230 m camera-to-bounds-centre** to avoid merged LOD1 chart interpolation. Scanned colour/normal/ORM and tint remain. Keep that graph in Claude's setup. Candidates use **separate LOD1 actors**; Claude attaches the authored files at **260 m**. The 249.6/270.4 m captures are manual substitutions, not an automatic switch implementation. No native LOD attachment or native-dialog UI was used.

## Evidence and persistence

Per the Round3 STATE brief: all three building states have final **150 m and three-quarter** views beside Abrams, one pristine **12 m** view per type, and a **249.6/270.4 m pair** per type. The bridge uses its damaged state for the pair because no intact revision was requested. Church has matched master/game **12/150/450 m** views for all states; HouseB pristine is the additional spot check. All pristine buildings share a whole-set **150 m overview**. Camera receipts record poses, arm lengths and 60° horizontal FOV / 16:9 framing. These are native **editor views, not gameplay**. On larger buildings, the 12 m arm aims at an exterior facade detail rather than the footprint centre, which otherwise puts the camera on or inside a wall/roof; master and game use the same target. These detail views intentionally crop the asset.

First captures were taken per type before refinement and remain in `evidence/first/`. Bridge first views were restaged above water to make the collapsed geometry visible. Final views correct the church's initial cropped spire and show doors/shopfronts/breaches from useful directions. `checks/` and `warmup/` are diagnostics. The [gallery](round3/REVIEW.html) indexes final evidence only.

Final assets and the existing `/Game/Art/Candidates/Village/L_VillagePreview` were saved, the preview reloaded in the same running editor, and all 162 material parents/texture/scalar/vector overrides checked again. Native counts, centimetre bounds, slots, collision hull counts, texture settings and dependencies are recorded. Switch cameras measure 249.6/270.4 m from the LOD0 whole-building bounds centre, shared by both actors; each receipt includes the independently calculated distance. Post-reload smoke captures cover HouseA rubble, Barn, damaged Church and destroyed Bridge; the saved viewport ends on the pristine overview. The editor was neither closed nor restarted by this round. Log history contains one early folder-save error, corrected by saving explicit asset paths; final saves and binding checks succeeded.

## Lessons check, limits and next action

Checked against current modelling lessons: real-scale scans; substantial profiles retained; sealed backing/returns; baked fine edges; horizontal AO bypass; game triangles above the 1 cm width rule; source/FBX/UE counts reported; irregular damage and mixed debris; full-thickness stumps; FBX_SCALE_NONE and centimetre import checks; portable CC0/in-house maps; current shared parents used or explicitly extended; no caches directed into Art; captures before refinement; validated exports; no UI operation that opens a native dialog. LOD1 stays separate per the newest lesson and producer decision.

Claude's visual review is the next gate. Weathering strength, the simple planar roof treatment and remaining master/game shading differences are exposed in the evidence for review. The normal/AO distance fade remains an integration requirement. Production state swaps, LOD attachment, eye-height/capacity updates, collision/navigation/placement, gameplay camera and runtime performance are **not verified** here. Historical candidate imports are retained; consolidation and the final Claude implementation prompt belong to Round4. No commits, pushes, PRs or production edits.

## Sources / continuation

Authoritative geometry/masters/UVs: `round3/source/<Type>_baked.blend` (Bridge included). Recovery checkpoints: `<Type>_geometry.blend`. Individual game/LOD1 FBXs: `round3/fbx/`; master FBXs: `round3/source/*_MASTER.fbx`; textures: `round3/textures/`. Generators are guarded under `Tools/Art/village_431_round3*.py`. The full geometry generator is a destructive rebuild of its named Round3 outputs; do not replay it over reviewed edits merely to export. The bake and QA scripts operate on type-specific checkpoints. Native preview recipes use only the existing Village level. Replaying into occupied native paths requires a fresh revision path; do not invoke a native import dialog.

Use `$world-divide-building-production` for #431. Read `STATE.md` and this report. HouseA Round2 is approved; Round3 is delivered for Claude review, with the documented eye-height/capacity proposals and separate LOD1 policy. Apply the next reviewer/producer decision; do not start Round4 on inferred approval. Keep round1/round2 and the running editor. Do not commit or push.
'''
    (BASE/'ROUND_3.md').write_text(text,encoding='utf8')
    old=(BASE/'STATE.md').read_text(encoding='utf8');start=old.index('## Next round: Round 3 targets') if '## Next round: Round 3 targets' in old else old.index('## Round 3 targets')
    stop=old.index('## Next action / continuation starter',start)
    if '## Verified delivery / integration requirements' in old[start:stop]:stop=old.index('## Verified delivery / integration requirements',start)
    targets=old[start:stop].replace('## Next round: Round 3 targets (the rest of the set)','## Round 3 targets (delivered for Claude review)')
    state='''# #431 village set: current state

- **Current gate:** Round3 delivered for **Claude review; approval pending**. Stop here. [ROUND_3.md](ROUND_3.md), [final gallery](round3/REVIEW.html), [manifest](round3/manifest.json), [opening/firing handoff](round3/openings_handoff.json).
- **Scope:** seven village types × three states, plus two bridge damage states. Round3 includes HouseA carry fixes; Round1 and Round2 files/assets remain unchanged. Greybox contract and gameplay files remain untouched.
- **Authority:** producer decisions → `docs/art/village/VILLAGE_BRIEF.md` → greybox `Art/Shared/village/village.json`. [BRIEF.md](BRIEF.md) governs the rounds. Ground-centred origin (+X ridge/front, Z up); church origin is nave centre; bridge origin is deck-top centre.
- **Sources:** `round3/source/<Type>_baked.blend` for HouseA/B/C, Farmhouse, Barn, Workshop, Church and Bridge; geometry recovery checkpoints `<Type>_geometry.blend`; individual LOD0/LOD1 FBXs in `round3/fbx/`; master FBXs in `round3/source/`; 97 portable PNGs ≤2K in `round3/textures/`.
- **Unreal:** existing `/Game/Art/Candidates/Village/L_VillagePreview`; final meshes `/Game/Art/Candidates/Village/Round3/Final/`; church/HouseB review masters `/Round3/Masters/`; instances `/Round3/Materials/`; parent `/Round3/M_SetSurface`; scans `/Round3/Textures/`; atlases `/Round3/Textures/Atlas/` except **Barn uses AtlasBarn02/**. Preview saved/reloaded; editor left running on pristine overview.

## Stages and approvals

| Stage | State |
| --- | --- |
| Round1 HouseA architecture | approved (2026-10-23) |
| Round2 HouseA quality bar | approved (2026-10-23) |
| Round3 rest of set + HouseA carry fixes | delivered for Claude review; approval pending |
| Round4 Astra realism / consolidation / implementation handoff | planned; not started |

- Producer approved the CC0 approach, three Sol rounds and one Astra round: "Yeah go ahead".
- Round1: "Approve + my fixes"; HouseA architecture approved with Claude's critique.
- Round2: "Approve + carry notes"; latest instruction: "The producer approved HouseA (Round 2) as the quality bar for the set."
- Producer continuation decision: no native LOD attachment or native-dialog UI. Use separate LOD1 actors at 249.6/270.4 m; Claude setup attaches at 260 m.

'''+targets+'''## Verified delivery / integration requirements

- 23 LOD0 + 23 authored LOD1 FBXs; 23 source/review masters; all 69 pass geometry/UV/tangent QA. All 46 game source/FBX/Unreal counts agree and fit budgets. Workshop damaged review master has one fewer FBX triangle (4,224 source / 4,223 FBX); use the higher count. Native Nanite off; ≤6 surviving slots. `round3/evidence/source_qa.json` and `unreal_verified.json` are authoritative receipts.
- Evidence: all states 150 m + three-quarter beside Abrams, one 12 m per type, 249.6/270.4 m pairs (bridge uses damaged), whole-set pristine overview at 150 m, church all-state master/game 12/150/450 m and HouseB pristine spot check. Native editor views, not gameplay. First captures preceded refinement. Final/switch/master/post-reload images are indexed in the gallery.
- HouseA darker course-readable rubble, zinc/PVC drainage/fascia, slimmer calmer concrete and quieter roof variation. Dish stays on HouseA only. New set varies brick/render/timber and red-brown/anthracite roofs. Bridge has a geometric crater, parapet gap, dropped halves/rebar/water debris; destroyed collision retains two stub hulls only with CTF_UseSimpleAsComplex. Bridge slot aliases: Brick=concrete, Roof=asphalt, Trim=reinforcement, Rubble=break concrete.
- **Apply in integration:** Barn loft eyes 4.5→5.95 m; Workshop high windows 1.5→3.3 m; Church nave windows 1.5→4.4 m. Farm upper long-wall apertures shifted down and eave shortened to keep 4.5 m eye; HouseB/C/Farm attic heads extend to 7.8 m. HouseC shop window is wider/lower at the same along position. Exact names, changed dimensions and proposed coordinates: `round3/openings_handoff.json`. All 276 opening/eye rays pass.
- Barn brief capacity **8** overrides greybox **10**, which supplies only eight openings. Recorded as a proposal; no `village.json` or gameplay edits were made.
- Preserve shader policy: game BakeStrength **0.35** except concrete/Rubble **0.08**, master **0**; fine geometric normal/AO fades **180–230 m camera-to-bounds-centre**, while scans/tint remain. Church facade ScanUVScale=(0.625,1) calibrates the older bond's horizontal repeat; other slots/types default to (1,1). LOD1 is a separate far mesh; Claude attaches it at **260 m**. Native comparison is manual, not automatic switching.
- Native material bindings persisted through same-editor level reload; affected rendered smoke checks passed. Full hashes/provenance: `round3/evidence/delivery_receipt.json`. No restart/close, native dialogs, commits, pushes or production-level edits. Old imports retained; no consolidation/deletion authorized this round.
- Remaining acceptance: Claude visual review; then the producer's next decision. Round4 owns realism and consolidation. Production state swaps, LOD attachment, firing/capacity changes, collision/navigation/placement and runtime benchmarking remain unverified integration work.

## Next action / continuation starter

Review [ROUND_3.md](ROUND_3.md) and [the final gallery](round3/REVIEW.html). Record Claude's critique and the producer's decision before beginning Round4.

Use `$world-divide-building-production` for #431. Read this STATE and ROUND_3.md. Round1 architecture and Round2 HouseA quality bar are approved; Round3 is delivered for Claude review. Apply only the next authorized review fixes or round. Keep round1/round2 and the running editor; do not commit or push.
'''
    (BASE/'STATE.md').write_text(state,encoding='utf8');print('Wrote Round3 report, gallery, manifest, hashes, openings and state; review pending',flush=True)

if __name__=='__main__':main()
