# Construction kits — production roadmap

Date: 2026-10-22. Status: planning proposal following the producer's request to expand the roadmap. This is a concrete work order sequence, not approval of unreviewed building designs, dimensions or kit pieces. No modelling or production integration has been performed for this roadmap.

## Scope and authority

Expand from shared fixtures into reusable architectural source modules, equipment and surface families. Prove a small working kit on two real buildings before growing the catalogue.

Read with [the existing kit specification](../../../docs/art/BUILDING_KIT.md), [model budgets](../../../docs/art/MODEL_PERFORMANCE.md), [design foundation](../../../docs/design/DESIGN_FOUNDATION.md) and the repo-owned building-production skill. Those requirements continue to apply. This roadmap lives in the assigned kit Art folder; it does not replace the existing kit specification or building state records.

Preserve the approved StripLight and accepted HQ architecture. HQ remains the existing lighting-kit / Gate A pilot. Barracks reuse remains outside Gate A, as already decided. The milestones below are a separate construction-kit workstream and add no prerequisite to HQ sign-off. Do not renumber existing M7.00 issues or create GitHub issues from this plan without a separate request.

## The two test buildings

| Building | What it proves | Distinctive design to retain |
|---|---|---|
| USA Barracks | Compact shell, human-scale openings, corners, windows, roof edge, entry light, small roof equipment and clear infantry exit | Personnel entrance and training / accommodation massing; readable infantry-production identity |
| USA Vehicle Factory | Reuse across a larger footprint, repeated wall bays, taller openings, long roof spans, vehicle-door mechanism, industrial equipment and vehicle clearance | Large production hall and vehicle exit; readable vehicle-production identity |

These are proposed art directions, not existing approved briefs. Establish reference, envelope, footprint, spawn/exit routes and attached-ground boundary for each before detailed modelling. Do not infer factory vehicle clearance from the HQ garage: derive it from the largest intended supported vehicle and Claude's gameplay requirements. Do not invent a walkable interior requirement; finish only the interior visible through working openings unless the brief calls for more.

Use the Barracks first for quality development, but block out BOTH buildings before freezing module dimensions. Use the Vehicle Factory second to expose kit weaknesses. A second building that needs remodelled copies of every shared part has not proved reuse.

## Kit families and order

| Order | Family | First useful pieces / work | Expansion trigger |
|---|---|---|---|
| 1 | Construction contract and surfaces | Units/axes/pivots, grid, connection dimensions, naming, material slots; concrete, coated metal, roof and glass surface families | Required before repeatable architecture |
| 2 | USA architectural core | Plain wall bay, window bay, personnel-door bay, outside corner, inside corner, column, plinth, roof field, roof edge, roof corner | Both test buildings |
| 3 | Access and large spans | Personnel door leaf/frame, factory bay jamb/header, sectional or sliding vehicle door chosen from reference, roof beam and long-span roof source assembly | Factory blockout and route test |
| 4 | Lighting and identity | Approved StripLight; next entrance light, wall lamp and warning beacon; flag/sign/ownership mounting provision | Fixtures actually needed by the pair; remaining approved lighting list follows HQ priorities |
| 5 | Utilities and roof access | Small HVAC, large HVAC, static vent, fan with separate rotor, electrical cabinet, railing straight/corner/end, ladder, roof hatch | Equipment scheduled for the pair |
| 6 | Communications | Whip antenna, short mast sections/base/top, small dish with support; then rotating radar base/head | First building whose approved function requires each piece |
| 7 | Perimeter construction | Defensive wall straight/corner/end, gate, tower connection and slope transitions | Dedicated defense-wall milestone with gameplay rules and route tests |
| 8 | Further faction / specialist families | China and Insurgent architecture, identity and specialist equipment; hangar, airfield and tech-building extensions | Reference-led briefs for those buildings |

The first architectural core is about ten module TYPES, not ten finished buildings or every imaginable size. Add closure/filler lengths only where the two blockouts demonstrate a need. Produce door-frame and moving-leaf interfaces together to avoid incompatible openings.

Masts and radar belong in the roadmap, but are not prerequisites for the Barracks and Factory. Do not attach a radar merely to exercise it. The accepted HQ masts remain HQ-specific under the existing kit specification; extraction or replacement needs its own assigned scope and evidence.

Defensive map walls are a separate kit from building wall panels. The design foundation calls for connected, destructible walls and friendly-opening gates. Their junctions, slopes, damage, navigation and placement need a dedicated Claude integration task; decorative building panels cannot stand in for that system.

## Construction contract to establish in the first milestone

- Unreal units are centimetres; sources use real dimensions and unit export scale. Adopt local +Z up and a documented outward/forward axis, compatible with existing fixture mount frames (+X out, +Z up).
- Start with the existing 1 m structural grid and a proposed 10 cm authoring subgrid for reveals and mounting offsets. Test candidate 2 m and 4 m wall bay widths against both blockouts; approve actual bay widths, wall thickness, floor heights and door clearances from the references before freezing them. These are proposed values, not accepted envelopes.
- Architecture pivots sit on a documented grid corner at base level; equipment pivots sit on their mount frame; moving parts pivot on their real hinge/shaft. Record dimensions and connection frames in a module register.
- Specify shared wall/roof thicknesses and how ends, inside/outside corners, plinths and roof edges meet. Include end caps. Connection rules must prevent gaps, overlaps, z-fighting and visible light leaks.
- Keep a stable semantic material-slot map. Share surface families and trim detail; reserve player-colour regions. Follow the kit's 512 px/m target where applicable and document exceptions. Keep the approved fixture finish intact while Claude resolves its existing shared-atlas adoption.
- Keep each texture set within 2K unless the producer grants an exception. Plan architecture surface/trim resources separately from the existing lighting Trim/Lens atlases; do not assume every architectural surface fits a lamp atlas. Inventory total resident texture cost and unique resources across both buildings.
- Keep a distinct high-detail master, efficient game geometry and bake sources. Fine details below the project threshold belong in normal/AO maps. Sources remain modular and editable even when final static architecture is consolidated.
- Record source revision, dimensions, slots, UV/texture dependencies, per-LOD counts, connection frames and approved evidence for each reusable piece. One canonical source per shared module; avoid building-specific copied versions of neutral fixtures.

## Delivery milestones

### CK-0 — Two blockouts and interface sheet

Deliver reference boards, both neutral-material blockouts, route envelopes and a dimensioned module/interface sheet. Place the buildings side by side in an isolated Unreal candidate preview. Agree their distinctive silhouettes before detail work. Inventory existing approved assets for reuse, without altering the HQ.

Exit: producer approves the two envelopes and representative module dimensions; Claude's required infantry/vehicle routes and moving-part envelopes are recorded. Undefined gameplay dimensions are explicit open items, not guessed final values.

### CK-1 — One complete Barracks quality section

Build a corner plus two connected wall bays, one personnel entrance, roof edge and one approved-type physical fixture. Include the adjoining ground/threshold within the agreed boundary. Finish its materials and game geometry, then show neutral daylight, night and close/RTS views in Unreal.

Exit: the section demonstrates the proposed quality standard, joins cleanly and has producer appearance approval. Record reimported geometry/section counts and measured material scale before extending it.

### CK-2 — Complete Barracks and freeze core v1

Extend the approved section into the whole Barracks. Add only required identity, roof utilities and access pieces. Complete moving parts if the brief needs them; provide both building LODs and the source assembly recipe/register.

Exit: approved Barracks appearance, complete art delivery within the SMALL building budget, documented shared-module versions and pending Claude integration. Do not call the building gameplay-ready from candidate evidence.

### CK-3 — Vehicle Factory reuse proof

Begin with canonical CK-2 wall/corner/plinth/roof-edge sources, fixture assets and material families. Add the large bay opening, chosen door mechanism, longer-span roof assembly and industrial utilities. Change shared interfaces only after checking both buildings; preserve approved Barracks appearance. Add purpose-built factory features where its silhouette requires them.

Exit: approved Factory appearance, complete art delivery within the LARGE budget and a reuse report. Show at least five named architectural module types and two equipment types used by both buildings from the same canonical source, with exact dimensions for shared interfaces and no geometry edits to make those shared pieces fit. Source reuse is sufficient for consolidated architecture; it need not remain a shared runtime mesh. Equipment reuse should retain canonical mesh identity. Material variants and ordinary placement transforms are allowed; non-uniform stretching is not a substitute for compatible modules. Explain every new factory-only part and every shared-interface revision.

The reuse count is a proposed practical acceptance target, not a reason to compromise an approved silhouette. If the blockouts cannot support it, revise the proposed kit family at CK-0 before detailed production.

### CK-4 — Claude integration and scale validation

Deliver accepted portable assets, assembly transforms, socket/mount frames, material bindings, motion pivots/travel, both LODs, resource manifest, review evidence and source-generation instructions. Clearly distinguish modular architecture from mounted equipment.

Claude implements deterministic setup, generated catalogue/data assets, production assembly, collision/navigation, selection, build presentation, unit exits, owner colours, low-power/damage/destruction state wiring and runtime measurement. Architectural source modules need an agreed assembly representation; do not force them into the current fixture-oriented catalogue schema or hand-edit generated data assets.

Exit: repeat setup is idempotent; actual gameplay validates routes, doors, LOD synchronisation, fixture states and a representative mixed Barracks/Factory scene under the project's benchmark protocol. Agree scene counts, hardware/settings and baseline with Claude before measurement. A quiet one-building editor preview is not performance acceptance. New integration work remains with Claude.

### CK-5 — Grow from proven demand

Expand utilities and communications for the next approved building brief, then defensive construction and further faction architecture. Reuse neutral equipment where appropriate; permit different faction architecture and signature mechanisms. Each new family gets a representative section and a second meaningful consumer before being called a proven construction kit.

## Authoring versus game delivery

Architecture is a reusable SOURCE construction system. Consolidate static building-shell output into one or a few meshes as required by MODEL_PERFORMANCE.md, preserving the editable modular source. Keep moving doors/radars/fans separate with named pivots. Mounted kit equipment follows the existing kit's instanced-parts route; do not flatten it into the shell and lose shared geometry, state data or mounts. Do not leave each architectural panel as its own production actor. Any change to these delivery choices belongs in Claude's reviewed integration design.

All kit geometry counts against each consuming building, once per placed copy. Material names shared across many modules do not by themselves prove low section/component cost: report the final assembly's actual sections and components, including equipment.

| Whole building | Authoring target including equipment | Hard LOD0 cap | Hard LOD1 cap | Material limit |
|---|---|---|---|---|
| Barracks (small) | <=20,000 LOD0 / <=4,800 LOD1, leaving 20% headroom | 25,000 | 6,000 | <=8 under project rules |
| Vehicle Factory (large) | <=120,000 LOD0 / <=32,000 LOD1, leaving 20% headroom | 150,000 | 40,000 | <=8 under project rules |

The targets are planning reserves, not new approved caps. Moving parts remain <=10,000 LOD0 triangles each and count inside the building cap. Individual kit pieces retain their category budgets. No Nanite. Track cumulative assembly cost before approving a new module.

## Review evidence required for each test building

- Reference-aligned views, neutral daylight and night fixture views; label Blender, Unreal candidate and actual gameplay captures accurately.
- Approved master versus game mesh at 12 m, 150 m and 450 m; paired switch captures at 249.6 m and 270.4 m around the 260 m transition. No missing parts or visible silhouette change; all pieces switch with the building.
- Reimported FBX triangle/section counts, stable LOD material-slot order, texture dimensions, shared-resource inventory and final component/section inventory.
- Corner/roof/door connections from both sides, including rotated placements; an interface test assembly showing compatible end caps and no seam defects.
- Full door travel, reversals and route clearance; animated equipment pivot/motion bounds where used. Candidate pose evidence and actual runtime control remain separate claims.
- Canonical-source reuse register for both buildings, with unique building features clearly identified.

All Unreal authoring and previews stay under /Game/Art/Candidates/. Keep each building's brief, state and handoff in its assigned Art folder when its work begins. Only Claude touches production setup/content. No commits, pushes or PRs.

## Next single task

CK-0: prepare the USA Barracks and USA Vehicle Factory reference-led blockouts and module/interface sheet. Start no additional speculative equipment family before this establishes the architectural dimensions. Existing HQ lighting work may continue independently, preserving the approved StripLight.
