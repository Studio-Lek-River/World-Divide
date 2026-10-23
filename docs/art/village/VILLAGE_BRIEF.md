# River Crossing village set: brief for the building workflow

Issue #408. The producer decided on 2026-10-22 that River Crossing's village buildings and its road bridge are made
in-house (`docs/world/maps/WD_RiverCrossing_01/05_PRODUCER_ACTIONS.md` §2): they must take an infantry squad that fires
from inside (#252) and break apart, which no pack offers. This brief starts the art for them in the shared building
workflow (`.agents/skills/world-divide-building-production/SKILL.md`): Codex makes the candidates and previews, and Claude
integrates the accepted delivery.

## What already exists

The integration exists before the art. Each building is a `URTSCivilBuildingData` (`/Game/Data/Civil/DA_Civil_<Type>`),
placed on a map as an `ARTSCivilBuilding`, which swaps its state's mesh as it loses health. The greybox of
`Tools/Art/village_blender.py` (`Art/Shared/village/`) gives every type its real-scale massing, its three states and its
garrison data. `ue.ps1 -Task setup` imports it, and `ue.ps1 -Task packaudit` stands every type in all three states next to
an Abrams in `/Game/Developers/PackAudit/L_PackScale`. **The delivery replaces the greybox meshes one for one.** The names,
origins, footprints and openings below are the contract: an opening that moves moves its firing position, so the
integration updates `village.json` along with the mesh.

## Style

One small modern village of temperate Western Europe (the Netherlands, Belgium, the German Rhineland), as it looks in the
2020s. It is lived in, not a ruin and not a museum.

- Rendered or brick walls in warm off-whites, buff and red-brown brick. Plinths in dark brick or concrete.
- Red-brown and anthracite clay pantile roofs. Use the scanned roof tiles (`ue.ps1 -Task surfaces`, the ambientCG sets of
  `Tools/Editor/free_assets.json`) or the kit's own materials. The barn's cladding is dark stained timber planks from the
  same source.
- White PVC or timber windows with sills, modern front doors, a garage roller door, gutters and downpipes, a satellite
  dish on one house. No shutters, half-timbering or alpine balconies.
- Nothing from C&C Generals or any other game: original buildings only.

Judge the buildings at the RTS camera first (100–200 m, `MODEL_PERFORMANCE.md`), and at the 12 m close zoom second. The
roof is what the player sees most.

## The set

The building's frame for every type: the origin is on the ground in the middle of the footprint (for the church, the
middle of the nave), X runs forward along the ridge, Z is up, in cm. The main door is in the +X gable. Walls reach 50 cm
under the ground so a slope shows no gap. Sizes in metres:

| Type | Name | Footprint (X × Y) | Eaves / ridge | Garrison | Notes |
|---|---|---|---|---:|---|
| HouseA | Village house | 11 × 9 | 5.8 / 9.6 | 5 | Two storeys and an attic, a gable to the street |
| HouseB | Semi-detached houses | 14 × 9 | 6.0 / 10.0 | 6 | Two front doors on the long side, mirrored halves |
| HouseC | Corner house | 9.5 × 9.5 | 5.6 / 9.0 | 4 | Square plan, could take a small shop window |
| Farmhouse | Farmhouse | 18 × 11 | 4.8 / 10.5 | 8 | Low eaves and a long roof, the farm's living quarters |
| Barn | Barn | 20 × 13 | 5.0 / 11.0 | 8 | Timber cladding, a 4 × 4.5 m door, a hay door above |
| Workshop | Workshop / garage | 16 × 12 | 5.5 / 6.6 | 6 | Shallow roof, two 3.5 × 4 m roller doors, high windows |
| Church | Church (the landmark) | 22 × 11 nave, 6.5 m tower at −X | 8 / 15, tower 26, spire 36 | 10 | The church square's landmark on the west bank; it must read from both bases |

Twenty-odd buildings stand on the map (`02_MAP_DESIGN.md` "Central village"), so the per-building budget is below the
small building's budget (`MODEL_PERFORMANCE.md`): **LOD0 ≤ 12,000 triangles, LOD1 ≤ 3,000, ≤ 6 material slots**; the
church may use **≤ 25,000 / 6,000**. No Nanite, two LODs, LOD1 at 260 m, as every building.

## States

Every type comes in three states, three meshes with the same origin and footprint:

1. **Pristine** (`SM_Village_<Type>`).
2. **Damaged** (`SM_Village_<Type>_Damaged`), at half health: a shell hole through the roof with the rafters and the
   attic floor showing, a breach through one long wall, blown windows, scorch and debris at its foot. The church loses its
   spire and the top of its tower.
3. **Rubble** (`SM_Village_<Type>_Rubble`), at no health: wall stumps up to about 3 m, fallen roof slabs and a heap. It
   stays for the match and keeps its collision, so it reads as cover. The church tower stands to about a third of its
   height.

The damage must read at 150 m: a silhouette change (the hole, the breach, the lost spire), not only a darker texture.

## Garrison openings

A garrisoned squad (#252) fires from the windows and doors: `village.json` puts a firing position just inside every
window and door, at eye height on its floor and looking out, and the entry point 3 m outside the main door. The delivery
keeps every opening of the greybox **as an opening**, at about the same place, or names the openings it moves. A type
needs at least as many openings as its garrison. Glass can be dark at the RTS camera; the interior behind an opening is
a dark room box, not a modelled room.

## Bridge

The intact span is `SM_Bridge_Span` (`Tools/Art/bridge_blender.py`, #155): a 30 m concrete girder span, 14 m wide, two
lanes. The set adds its two damage states with the same origin (the middle of the deck's top):

- `SM_Bridge_Span_Damaged`: a shell crater in the carriageway, a 6 m gap in one parapet, chunks on the deck.
- `SM_Bridge_Span_Destroyed`: the middle gone, each half hanging from its pier, tilted down with its broken end in the
  river, rebar showing at the breaks, debris in the water. Only the stubs over the piers keep collision.

A dressing pass on the intact span (expansion joints, drainage, lamp posts, kerbs) is welcome within the same contract.

## Delivery

Stage it as the workflow does, one approval at a time:

1. Reference and architecture for HouseA in its three states, in Blender and in an isolated Unreal candidate preview
   beside an Abrams, at the RTS views (12 m, 150 m, 450 m) and a three-quarter close-up.
2. HouseA finished to the agreed standard; that sets the quality bar of the set.
3. The other six types and the bridge states.
4. Final consolidation and the Claude implementation prompt in `Art/Shared/village/` (the workflow's "Finish,
   consolidate and hand to Claude"): FBX per mesh with its LOD1 and UCX_ boxes, slots by name, texture settings, and the
   master-against-game-mesh comparison at 12 m, 150 m, 450 m and the 260 m LOD switch.

Claude's integration then replaces the greybox in `Art/Shared/village/`, updates `village.json` (states, openings,
health), rebuilds through `ue.ps1 -Task setup` and checks it with `WorldDivide.Data.CivilBuildings`. #157 places the
buildings on River Crossing and benches the village at 240 units.
