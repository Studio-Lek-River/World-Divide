"""Index verified Round2 outputs. Does not grant producer approval or edit Unreal."""
import hashlib,html,json,sys
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'Art/Shared/village/issue431';OUT=BASE/'round2'

def main():
    from PIL import Image
    qa=json.loads((OUT/'evidence/source_qa.json').read_text())
    native=json.loads((OUT/'evidence/unreal_final_verified.json').read_text())
    assert native['persisted_level']==native['level']
    rows=[]
    for state,suffix in [('Pristine',''),('Damaged','_Damaged'),('Rubble','_Rubble')]:
        name='SM_Village_HouseA'+suffix
        for end in ['', '_LOD1','_MASTER']:
            assert qa[name+end]['triangles']==qa[name+end]['reimport']['triangles']==native['meshes'][name+end]['unreal_triangles']
            assert not qa[name+end]['bad_tangents'] and not qa[name+end]['degenerate_uv_triangles']
        rows.append(f"| {state} | {qa[name]['triangles']:,} | {qa[name+'_LOD1']['triangles']:,} | {qa[name+'_MASTER']['triangles']:,} | {len(native['meshes'][name]['slots'])} |")
    receipt={'files':{},'textures':{},'scripts':{},'scope':'Round2 final portable delivery; native editor verification in unreal_final_verified.json; producer quality approval pending'}
    for folder in ['source','fbx','textures']:
        for p in sorted((OUT/folder).glob('*')):
            if p.is_file():receipt['files'][str(p.relative_to(OUT))]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
    for p in sorted((OUT/'textures').glob('*.png')):
        rec={'size':list(Image.open(p).size),'source':'in-house' if p.name.startswith('SM_') else 'ambientCG CC0; Saved/Surfaces'}
        if not p.name.startswith('SM_'):
            surface,kind=p.stem.rsplit('_',1);src=ROOT/'Saved/Surfaces'/surface/(kind+'.png')
            rec['source_sha256']=hashlib.sha256(src.read_bytes()).hexdigest()
            rec['matches_source']=rec['source_sha256']==receipt['files'][str(p.relative_to(OUT))]['sha256'];assert rec['matches_source']
        assert max(rec['size'])<=2048
        receipt['textures'][p.name]=rec
    for p in sorted((ROOT/'Tools/Art').glob('village_431_round2*.py')):
        receipt['scripts'][p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
    (OUT/'evidence/delivery_receipt.json').write_text(json.dumps(receipt,indent=2))
    manifest={'status':'Ready for producer quality-bar review; approval pending','meshes':native['meshes'],'source_qa':qa,'textures':receipt['textures'],'native_level':native['level'],'game_meshes':'/Game/Art/Candidates/Village/Round2/Final02','masters':'/Game/Art/Candidates/Village/Round2/Masters02/Final','material_parent':'/Game/Art/Candidates/Village/Round2/M_Round2_Surface','instances':'/Game/Art/Candidates/Village/Round2/Materials','atlas_textures':'/Game/Art/Candidates/Village/Round2/Textures/Atlas02','lod_policy':'Separate imported actors only. Claude attaches authored LOD1 at 260m in production. No native attachment attempted in this continuation.','detail_fade':json.loads((OUT/'evidence/detail_fade.json').read_text()),'game_bake_strength':.35,'master_bake_strength':0,'evidence':['evidence/final','evidence/master','evidence/switch','evidence/persisted_smoke'],'production_integration':'Not performed; candidate editor evidence only'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    gallery=['<!doctype html><meta charset="utf-8"><title>HouseA Round 2 review</title><style>body{background:#202221;color:#eee;font:16px system-ui;margin:24px}h2{margin-top:2em}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.four{grid-template-columns:repeat(4,minmax(0,1fr))}figure{margin:0}img{width:100%}a{color:#bce0ff}figcaption{padding:8px 0}p{max-width:1000px}@media(max-width:900px){.four{grid-template-columns:repeat(2,minmax(0,1fr))}}</style><h1>HouseA — Round 2 quality-bar review</h1><p>Approval pending. Native Unreal editor previews beside Abrams; not gameplay. 60° horizontal FOV, constrained 16:9. Exact 12 m views crop the house; three-quarter views show the whole asset. Click images for full native captures. Camera receipts are beside each PNG.</p><p><a href="../ROUND_2.md">Delivery report</a> · <a href="manifest.json">Manifest</a></p>']
    def picture(path,label):
        assert (OUT/path).exists(),path
        return f'<figure><a href="{path}"><img loading="lazy" src="{path}" alt="{html.escape(label)}"></a><figcaption>{html.escape(label)}</figcaption></figure>'
    for state in ['pristine','damaged','rubble']:
        gallery.append(f'<h2>{state.title()} — final game mesh</h2><div class="grid four">')
        for camera in ['12m','150m','450m','threequarter']:gallery.append(picture(f'evidence/final/{state}_{camera}.png',state+' '+camera))
        gallery.append('</div>')
    gallery.append('<h2>Matched master / game mesh</h2><p>Same camera, position, surfaces and lighting; physical master chamfers against the game normal/AO bake. Master uses BakeStrength 0; game uses 0.35. Close under-eave contrast differs slightly and remains visible for producer judgment.</p>')
    for state in ['pristine','damaged','rubble']:
        for camera in ['12m','150m','450m']:
            gallery.append(f'<h3>{state.title()} {camera}</h3><div class="grid">')
            for phase,label in [('master','Review master'),('final','Game LOD0')]:gallery.append(picture(f'evidence/{phase}/{state}_{camera}.png',label))
            gallery.append('</div>')
    gallery.append('<h2>260 m switch comparison — separate actors</h2><p>Left: game at 249.6 m. Right: authored LOD1 at 270.4 m. These manually staged captures do not implement an automatic LOD switch. Both use the same materials. Fine normal/AO atlas detail fades from 180–230 m; scans and tint remain.</p>')
    for state in ['pristine','damaged','rubble']:
        gallery.append(f'<h3>{state.title()}</h3><div class="grid">')
        for kind,d in [('game','249_6m'),('lod1','270_4m')]:gallery.append(picture(f'evidence/switch/{state}_{kind}_{d}.png',kind+' '+d.replace('_','.')))
        gallery.append('</div><details><summary>Same-distance controls</summary><div class="grid">')
        for d in ['249_6m','270_4m']:
            for kind in ['game','lod1']:gallery.append(picture(f'evidence/switch/{state}_{kind}_{d}.png',kind+' '+d.replace('_','.')))
        gallery.append('</div></details>')
    gallery.append('<h2>History</h2><p>First improved captures were taken before refinement and remain in <a href="evidence/first/">evidence/first/</a>. Warmups, checks and atlas_baseline are diagnostics, not acceptance evidence. Earlier floating-cap and material defects are superseded by the final captures above.</p>')
    (OUT/'REVIEW.html').write_text(''.join(gallery),encoding='utf8')
    text='''# #431 — Round 2: HouseA quality bar

**Ready for producer quality-bar review. Approval pending; stop here.**
Requested lane: GPT-6.1-Sol, round 2 of 3 Sol rounds. Round 1 architecture remains approved. Round 3 has not started.

Open the [final Unreal gallery](round2/REVIEW.html). All three states have final 12 m, 150 m, 450 m and three-quarter views beside an Abrams; matched master/game views at 12/150/450 m; and separate-actor LOD comparisons at 249.6/270.4 m, including same-distance controls. These are native **editor previews, not gameplay**. Camera receipts accompany every PNG. The 60° horizontal lens has constrained 16:9 content; the exact 12 m ground-target view crops the house, so use the three-quarter views for the whole asset.

## Final pass against Claude's review

- Roof lifted to red-brown with scanned tile relief, broad variation and a slight slope tint. It reads as a tiled surface at 150 m without the first version's near-black uniformity.
- Damaged chimney cap is supported by the chimney shaft in the final three-quarter capture. The first capture's floating cap is superseded.
- Rubble uses the standing building's brick course scale and tint, with dust variation. Courses show in the final three-quarter view. The heap mixes brick, roof tile, timber and plaster/concrete fragments; it is lighter than the first black heap.
- The approved breach debris arrangement is retained.

The approved 11 × 9 m massing, 5.8 m eaves / 9.6 m ridge, +X front gable, opening positions and damage placement remain. The two attic heads extend to 7.8 m as requested, clearing the existing 7.5 m firing eye. Round 1 files were kept unchanged.

## Round 2 construction

Brick tiling is halved to approximately 6.5 cm courses and tinted browner. Concrete lintels, slimmer sills/drips, stepped PVC frame/sash profiles and the dark 50 cm plinth remain geometry. The door has a canopy, number plate, lamp, dish, vent and meter box. RoofingTiles013A replaces the orange scan; roof edges retain thickness and fascia/gutter geometry.

The hole has uneven blast edges, varied rafters, broken battens and hanging tiles. Blown windows have missing glazing and partial frames; tint atlases carry localized soot and ground-contact dust. Rubble stumps retain closed 30 cm thickness, varied broken tops under 3 m, and a fallen gable fragment. Weathering is restrained; its strength remains part of the visual quality review.

The review masters add 20 mm chamfers on stable edges. Touching masonry joins are excluded to avoid false vertical seams. Geometric object-space normal/AO is baked high-to-low without baking the scan normal twice. UV0 holds real-scale scans; UV1 is the unique atlas. Large horizontal floors bypass AO. Invalid cross-member bake rays are repaired to the receiving normal, preserving valid chamfer detail; the cleanup receipt records affected texels.

## Verified budgets

| State | Game LOD0 | Authored LOD1 | Review master | Native game slots |
| --- | ---: | ---: | ---: | ---: |
'''+ '\n'.join(rows)+'''

All nine source, independently reimported FBX and Unreal counts agree. The higher-count rule is therefore satisfied. LOD0 is below 12k; LOD1 below 3k. Zero degenerate geometry/UV triangles and invalid tangents in portable QA. Minimum game triangle altitude exceeds 1.6 cm. All 23 aperture checks pass in each standing state. No Nanite. Native imports each have one LOD, intentionally; the authored LOD1 is a separate mesh. Each game LOD0 retains two convex collision hulls for review; production collision remains Claude's.

Portable slot order: **Brick, Roof, Trim, Glass, Wood, Rubble**. Native rubble strips unused Trim/Glass and has Brick/Roof/Wood/Rubble. LOD0 and LOD1 match per state; bind by slot name. Masters are source/review assets, not production geometry.

## Authoritative assembly

| Item | Path |
| --- | --- |
| Final source | `round2/source/HouseA_round2_baked.blend` |
| Geometry recovery checkpoint | `round2/source/HouseA_round2.blend` |
| Game and separate LOD1 FBXs | `round2/fbx/SM_Village_HouseA*.fbx` (individual files) |
| Review-master FBXs | `round2/source/*_MASTER.fbx` |
| Preview level | `/Game/Art/Candidates/Village/L_VillagePreview` |
| Final LOD0/LOD1 meshes | `/Game/Art/Candidates/Village/Round2/Final02/` |
| Final masters | `/Game/Art/Candidates/Village/Round2/Masters02/Final/` |
| Surface parent | `/Game/Art/Candidates/Village/Round2/M_Round2_Surface` |
| State/slot instances | `/Game/Art/Candidates/Village/Round2/Materials/MI_<State>_<Slot>[_MASTER]` |
| Scanned textures | `/Game/Art/Candidates/Village/Round2/Textures/` |
| Final normal/AO/tint atlases | `/Game/Art/Candidates/Village/Round2/Textures/Atlas02/` |

The saved preview has three inspection columns: game at X=0, master at X=24 m, LOD1 at X=48 m; state rows Y=-24/0/24 m, Z=0. Evidence temporarily moves the chosen variant to the same origin and parks the others offscreen, then restores inspection columns. Actor references/transforms are in `round2/evidence/preview_state.json`.

Earlier First, Final, Masters, Masters02 root and texture experiments are historical diagnostics. No cleanup was authorized or performed. Final02 uses the materials/atlases listed above, with no Round1 material dependency. Shared glass and the existing Abrams preview dependencies remain required.

## Material and LOD handoff requirements

The current shared `M_Bld_Surface` lacks scan/atlas inputs, so a candidate copy was extended for UV0 BaseColor/Normal/ORM and UV1 Tint/BakeNormal/BakeAO. Shared parents and Round1 were untouched. Glass uses the current shared `M_Bld_Glass` directly. Atlas normals are object-space data (linear mask compression), decoded and transformed local-to-tangent before scanned micro-normal addition; do not import them as tangent normal maps. Tint/AO are linear masks. Game BakeStrength is **0.35**, master **0**. Exact overrides and parents are in the native verification receipt.

**LOD1 remains a separately imported mesh and actor by Claude's decision.** No native LOD attachment and no `village_431_round2_lods.py` execution. That historical recipe now fails immediately to prevent accidental use. Claude's production setup attaches LOD1 at **260 m**; the captures demonstrate manual mesh substitution, not an automatic switch.

LOD1 merged faces cross some unique near-detail atlas charts. The final surface shader therefore fades **only geometric atlas normal/AO** from camera-to-bounds-centre 180 m to 230 m, before either switch view. Scanned normal/colour/ORM and atlas tint stay present. Carry this graph into setup: `BakeStrength * (1 - saturate((distanceCm - 18000) / 5000))` drives both bake blends. See [detail_fade.json](round2/evidence/detail_fade.json). LOD1 is intended for the far range; its atlas interpolation is unsuitable for close-range use without this shader. The same-distance controls expose the remaining geometric difference for review. Close under-eave contrast differs slightly between baked game and physical master; final matched images retain that difference for the producer's judgment.

## Textures and persistence checks

21 portable PNGs, all at most 2048²: twelve unchanged ambientCG CC0 maps from **Bricks085, RoofingTiles013A, Planks023A, Concrete036** (BaseColor, Normal, packed ORM), plus three in-house normal/AO/tint atlas sets. Source copies match `Saved/Surfaces/<Name>/` by SHA-256. Mips and streaming remain enabled in the candidate. No git-ignored surface pack is needed for the delivered building.

Read-only restart checks confirmed the assets/preview survived Claude's restart. The clean production level was left unchanged. Final assets and the existing Village preview were saved, then the same preview was reloaded in the running editor. Counts, centimetre bounds, slot bindings, parent/texture/scalar overrides, texture settings and saved dependencies were queried; post-reload native smoke captures confirm the rendered assembly. No editor restart or close was performed by this continuation, and no native file dialog was triggered.

- [Native verification](round2/evidence/unreal_final_verified.json), [source / independent FBX QA](round2/evidence/source_qa.json)
- [Final captures](round2/evidence/final/), [matched masters](round2/evidence/master/), [switch controls](round2/evidence/switch/), [post-reload smoke](round2/evidence/persisted_smoke/)
- [Manifest](round2/manifest.json), [file hashes and provenance](round2/evidence/delivery_receipt.json), [bake receipt](round2/evidence/bake_receipt.json), [normal cleanup](round2/evidence/normal_bake_cleanup.json)

The first improved capture set remains in `round2/evidence/first/`, taken before refinement. `checks/`, `warmup/` and `atlas_baseline/` are diagnostics, not final evidence. Log history includes missing-parent warnings for obsolete Round2 root instances after the restart, a corrected graph-pin connection error, and a rejected read of the unavailable `sourceModels` property. Final instances use the verified M_Round2_Surface parent; the final graph compiled and rendered. No claim of an empty historical log is made.

## Modelling-lessons check and gate

Checked against the current shared lessons: substantial trim/reveals kept as geometry; backing/returns retained; fine chamfers baked; floors bypass AO; game triangles wider than 1 cm; source/FBX/UE counts agree; real brick scale; irregular breaks and mixed debris; full-thickness stumps; FBX_SCALE_NONE and centimetre bounds; portable CC0/in-house textures; current shared parents extended only where necessary; caches outside Art; captures before refinement; validated exports; no native-dialog operations. Separate LOD comparison follows the newest lesson and explicit producer decision.

Visual quality-bar approval remains the producer's next action. Gameplay state swaps, production LOD attachment, navigation, placement, firing/garrison integration and runtime benchmarking were not performed. The material fade and remaining close-view shading difference are disclosed above. No Round3 work, commits or pushes.

## Reproduction / continuation

Do not replay the destructive `village_431_round2.py` rebuild over the approved architecture. The validated baked blend and FBXs are authoritative. Portable pipeline: `round2_bake.py`, `round2_qa.py`, `round2_bake_clean.py`, `round2_tint.py`. Native recipes: `round2_materials.py`, `round2_finish.py install_final`, then `round2_detail_fade.py`; new installs need an unused candidate destination because native import refuses overwrites. `finish.py` game/master/switch stages reproduce the fixed captures; verify saves/reloads and queries. Never run `round2_lods.py`. Report generation is read-only with respect to Unreal.

Continuation: use `$world-divide-building-production` for #431. Read `STATE.md` and this report. Round1 architecture is approved; Round2 is delivered for quality-bar review, with separate LOD1 and the stated shader requirement. Await the producer's approval or fixes before Round3. Preserve round1 and leave the editor running. Do not commit or push.
'''
    (BASE/'ROUND_2.md').write_text(text,encoding='utf8')
    state=(BASE/'STATE.md').read_text(encoding='utf8')
    # Keep the producer's original target list as the historical brief; replace stale progress.
    start='## Next round: Round 2 targets' if '## Next round: Round 2 targets' in state else '## Round 2 brief'
    end='## Verification and open items' if '## Verification and open items' in state else '## Delivered verification and integration notes'
    targets=state[state.index(start):state.index(end)]
    targets=targets.replace('## Next round: Round 2 targets (HouseA quality bar)','## Round 2 brief (delivered; quality approval pending)')
    targets=targets.replace('LOD1 attached at 260 m','LOD1 separate in the candidate; Claude attaches at 260 m in production (producer continuation decision)')
    new='''# #431 village set: current state

- **Task:** River Crossing village set (7 types × 3 states) and bridge damage states. [BRIEF.md](BRIEF.md); design authority `docs/art/village/VILLAGE_BRIEF.md`, then `village.json` greybox contract. Producer decisions take precedence.
- **Current gate:** Round2 HouseA is **ready for producer quality-bar approval; approval pending**. Stop here. [ROUND_2.md](ROUND_2.md), [final gallery](round2/REVIEW.html), [manifest](round2/manifest.json).
- **Latest source:** `round2/source/HouseA_round2_baked.blend`; geometry checkpoint `HouseA_round2.blend`; individual game/LOD1 FBXs `round2/fbx/`; review masters `round2/source/*_MASTER.fbx`; 21 portable PNGs `round2/textures/` (≤2K).
- **Unreal authority:** `/Game/Art/Candidates/Village/Round2/Final02/`, masters `/Round2/Masters02/Final/`, instances `/Round2/Materials/`, parent `/Round2/M_Round2_Surface`, final atlases `/Round2/Textures/Atlas02/`. Existing preview `/Game/Art/Candidates/Village/L_VillagePreview`, saved/reloaded; editor remains running.
- **Frame:** ground-centred origin, +X ridge/front gable, Z up, centimetres on export; standing walls extend 50 cm below ground. Approved 11 × 9 m massing and damage placement preserved; requested attic heads raised to 7.8 m.

## Stages and approval receipts

| Stage | State |
| --- | --- |
| Round1 HouseA architecture | **approved** (2026-10-23) |
| Round2 HouseA quality bar | **delivered for review; approval pending** |
| Round3 other six types and bridge | planned; not started |
| Round4 Astra realism / consolidation / Claude handoff | planned |

- 2026-10-23 producer: approach approved for #431 ("Yeah go ahead"): CC0 textures, 3 Sol rounds + 1 Astra round.
- 2026-10-23 producer: HouseA architecture approved, "Approve + my fixes". Later instruction: "The producer approved HouseA's architecture from Round 1 together with Claude's critique."
- Continuation decision: no native LOD attachment or native-dialog UI; separate LOD1 actor comparisons at 249.6/270.4 m. Claude's production setup attaches at 260 m.

'''+targets+'''## Delivered verification and integration notes

- Source / independent FBX / Unreal counts match: LOD0 **6,120 / 6,158 / 1,760**; LOD1 **2,376 / 2,988 / 1,568**; masters **7,092 / 7,172 / 3,812** (pristine/damaged/rubble). No Nanite; native slots 6/6/4; LOD0 two convex hulls each. All nine portable meshes pass tangents/UVs; 23 aperture checks pass in each standing state.
- Final game captures: `round2/evidence/final/` (12/150/450 m + three-quarter, all states). Matched masters: `evidence/master/`; separate-actor switch and same-distance controls: `evidence/switch/`; post-reload smoke: `evidence/persisted_smoke/`. Native editor views, not gameplay; 60° horizontal FOV / 16:9. Exact 12 m views crop the house.
- Roof lifted/red-brown with tile relief and macro variation; damaged chimney cap supported; rubble courses/scanned scale match standing brick with dust and mixed heap; breach debris retained. First captures preserved before refinement.
- Candidate surface parent extends the current shared surface for scans and atlas channels; glass directly uses shared M_Bld_Glass. Game BakeStrength **0.35**, master **0**. Fine geometric normal/AO atlas fades **180–230 m camera-to-bounds-centre** to avoid merged LOD1 chart artifacts; scans and tint stay. Carry this shader requirement into setup. LOD1 is far-only. Close under-eave contrast differs slightly from master and is exposed in the gallery for review.
- Native assets/materials saved and Village preview reloaded in the same running editor; bindings, dependencies, centimetre bounds and textures queried. Receipts: `evidence/unreal_final_verified.json`, `source_qa.json`, `delivery_receipt.json`, `detail_fade.json`. No runtime/gameplay acceptance claimed.
- Inspection columns: game X0, master X24 m, LOD1 X48 m; state rows Y=-24/0/24 m. Old imports retained as diagnostics; no cleanup. Final02 needs Round2 Materials/Textures and shared glass, not Round1 materials.
- Claude resolved the previous native-dialog blocker by restarting before this continuation. Recovery inventory confirmed saved assets. This continuation never closed/restarted the editor or triggered a native file dialog. `village_431_round2_lods.py` is guarded against execution. No commits, pushes or production-level edits. Round1 unchanged.

## Next action / continuation starter

Producer reviews [ROUND_2.md](ROUND_2.md) and [the gallery](round2/REVIEW.html), then approves the quality bar or requests fixes. Do not begin Round3 until approval. Production attachment at 260 m, collision/navigation/placement, garrison/firing and runtime performance remain Claude's integration work.

Use `$world-divide-building-production` for #431. Read this state and `ROUND_2.md`; preserve approved HouseA architecture and round1. Round2 is delivered for quality-bar approval with separate LOD1 and the documented atlas fade. Apply only the next producer decision. Leave the editor running; do not commit or push.
'''
    (BASE/'STATE.md').write_text(new,encoding='utf8')
    print('Wrote review-ready Round2 report, gallery, manifest, hashes and state; approval pending')

if __name__=='__main__':main()
