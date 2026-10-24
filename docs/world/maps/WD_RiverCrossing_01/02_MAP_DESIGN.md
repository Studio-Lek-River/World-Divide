# WD_RiverCrossing_01: map design

A 1v1 skirmish map set in the modern European countryside. The two players' halves are point-symmetric (180° about the map centre) in **gameplay**, but each side is **dressed** differently so the map reads as a real place.

All numbers here are **provisional**. They assume the producer's decision of 2026-10-15 to move the game to **real scale**, which lands first as its own gameplay issue (phase 0 in [04_IMPLEMENTATION_ROADMAP.md](04_IMPLEMENTATION_ROADMAP.md)). They are checked again once that issue fixes the final unit values.

**Source of truth for scale.** [`WORLD_SCALE_STANDARD.md`](../../../tech/scale/WORLD_SCALE_STANDARD.md) (approved 2026-10-15) is authoritative for unit and building sizes, weapon ranges, speeds, the nav agents and the camera. Where this document and the standard differ, the standard wins. The planning envelope below was reconciled to it on 2026-10-15 (recovery decision D-E); the layout, symmetry, base-building space, village and river crossings are unchanged.

## Scale basis

### Current gameplay values

Generator verified, `Tools/Editor/setup_testmap.py`:

| Entity | Size today | Speed today | Range today |
|---|---|---|---|
| Abrams (largest vehicle) | 2.9 × 1.6 m, radius 1.0 m | 5 m/s | 12 m |
| Tank | 2.6 × 1.5 m | 6 m/s | 12 m |
| Infantry | 1.0 m, radius 0.3 m | 3.5 m/s | 7 m |
| Headquarters | 4 × 4 cells = 4 × 4 m | | |
| Airfield (largest building) | 4 × 8 m | | |
| Build grid | 1 m (`RTSBuildPlacement::GridSize = 100.f`) | | |
| Camera | arm 1.75–70 m, pitch -55° → -8° | | |
| Largest map today | `L_SixPeaks`, 240 m across | | |

### Planning envelope for real scale

These values come from the world scale standard (§3, §5, §6). The rescale issue (#136) sets each unit's final value within them.

| Entity | Planning size | Planning speed | Planning range |
|---|---|---|---|
| Main battle tank (M1A2 class) | 9.8 m overall, 7.9 × 3.7 m hull | 10–12 m/s (RTS time) | 60–75 m |
| IFV / light tank (M20 Guardian, 7.1 × 3.54 m hull) | 7–8 m overall | 12–14 m/s | 50–65 m |
| Wheeled support (truck, builder, flak) | 6.5–10 × 2.5–3.0 m | 13–15 m/s | AA 70–90 m against air |
| Infantry | 1.8 m tall, footprint radius 0.4 m | 4–5 m/s | rifle 30–40 m |
| Defence tower | 6 × 6 m | | 1.25 × the standard tank's range |
| Headquarters | 28 × 28 m (compressed) | | |
| Vehicle factory | 24 × 24 m (compressed), exit lane for a 9.8 × 3.7 m tank | | |
| Airfield | 24 × 48 m (compressed) | | |
| Nav agents | vehicle radius 1.9 m, height 3.5 m; infantry 0.4 m, 1.8 m | | |
| Camera | FOV 60°, default arm about 150 m (about 170 m across), max arm about 300 m | | |

With these values an MBT crosses the 2 km map in about 3 minutes (standard §5). That is a Generals-like pacing, so **2.0 × 2.0 km of playable area is kept** as the target. Weapon ranges are short against the map size (60–75 m against 2 km), so terrain cover and the bridges' approaches matter at close range; sightlines are checked in the greybox against the final #136 ranges.

## Overall layout

Coordinates are in metres, with the origin at the map centre, +X east and +Y north.

- **Playable area:** 2,000 × 2,000 m, from -1000 to +1000. The `NavMeshBoundsVolume` covers exactly this, so the RTS camera's pan limits follow it automatically (`ARTSCameraPawn::BeginPlay`).
- **Border ring:** about 260 m of non-playable, dressed terrain on every side (landscape about 2.52 km). Rising wooded hills hide the map edge.

```
 N  +1000 ┌──────────────────────────────────────────────┐
          │ ░░░░ forest      ~~~~river~~~~      field    │
          │ ░░           ~~~~~     │                     │
          │   field    ~~~      [B-N] north bridge (0,300)│
          │         ~~~  E-farm    ║      ▲ E hill        │
          │  W-fwd ~~        village east   (350,650)     │
          │  supply ~   ╔═══ VILLAGE ═══╗    ┌────────┐  │
          │ (-400,500)~ ║ west │  east  ║    │ BASE E │  │
          │          ~  ╚══ (0,0) ══════╝    │(720,220)│  │
          │ ┌────────┐ ~~ village west       └────────┘  │
          │ │ BASE W │   ~~   E-fwd supply     ░░ forest  │
          │ │(-720,  │      ~~ (400,-500)                 │
          │ │ -220)  │  [B-S] south bridge (0,-300)       │
          │ └────────┘  ▲ W hill    ~~~                   │
          │  ░░ forest  (-350,-650)    ~~~~~       ░░░░   │
 S  -1000 └──────────────────────────────────────────────┘
          -1000 W                                 E +1000
```

The sketch is schematic. The river runs north to south through the centre in an S-curve that maps onto itself under the 180° rotation. The final river spline is authored by hand in the greybox phase.

## Bases

| | West base (Team0) | East base (Team1) |
|---|---|---|
| Centre (`PlayerStart` tagged `Team<N>`) | (-720, -220) | (720, 220) |
| Buildable pad | about 350 × 350 m, slope ≤ 5° | same |
| Home bridge | South bridge, about 725 m by road | North bridge, about 725 m |
| Far bridge | North bridge, about 890 m | South bridge, about 890 m |
| Safe supply | Behind the base, near the map edge | Mirror position |
| Dressing | Farmstead edge, orchard, hedgerows | Light industrial yard, sawmill |

**Rules for the bases**
- **Flat pad.** Keep the pad near-flat. `URTSBuildPlacement::CanPlace` has **no slope check**; it only needs navmesh under the centre and the four corners and an empty overlap box. A flat pad keeps buildings level and their exits clear.
- **No decorative collision on the pad.** Any WorldStatic or WorldDynamic mesh there blocks the overlap test. Grass with no collision is allowed.
- **Room to grow.** A 350 m pad holds well over 25 buildings of the standard's compressed sizes (16–28 m) with 15 m lanes, plus airfields of 24 × 48 m; the pad size is kept, so bases have spare room. The AI's base search radius (`BuildSearchDistance`, 40 m today, `AI/RTSAIPlayer.cpp`) and the 45 m `BaseRadius` of `WorldDivide.Smoke.AIBuildsBase` (`Tests/RTSAISmokeTest.cpp`) **must scale** with the rescale issue.
- **Exits.** Each base needs at least three exits: the main road to the home bridge, a farm track to the forward supply, and an open-field exit toward the far bridge. No exit should be a single choke.

## River

- **Width:** 40–70 m of open water. It is narrowest at the bridges (about 45 m) and widest at the outer bends.
- **Not crossable** by ground units anywhere except the two bridges. Two blocks make sure of this (see [03_TECHNICAL_PLAN.md](03_TECHNICAL_PLAN.md)):
  - banks steeper than the nav agent's max slope (44° today);
  - a `NavModifierVolume` with `NavArea_Null` named `Water_*` along the river. The prefix keeps the existing map-test convention.
- **Depth:** the visual depth is 2–4 m below the field level. Bank tops sit 1–2 m above the water.
- **Reeds and willows** may line the banks but must stay off the bridge corridors.

## Bridges

There are two road bridges, at roughly (0, +300) and (0, -300), a point-symmetric pair.

| Property | Target |
|---|---|
| Carriageway | ≥ 12 m, which is two lanes and more than 3× the widest vehicle (3.7 m); built: 13 m |
| Overall deck | about 14 m including parapets; built: 14 m with 0.5 m parapets |
| Length | about 70–90 m including abutments; built: three 30 m spans, 90 m between the abutments |
| Ramps | ≤ 8° (well under the 44° nav slope); built: 3 m up over 30 m, 5.7° |
| Clearance | 5–6 m above the water (visual only; there are no boats in this map); built: the deck's top 6 m over the water, the girders' feet about 4.3 m |
| Approach | 60 m of straight, obstacle-free road on both ends for columns forming up |
| Collision | Deck on **WorldStatic** so `RTSGround::Trace` and the building/HQ ground traces find it |

The usable lane is the carriageway minus twice the vehicle nav agent radius (1.9 m), so 12 − 3.8 = 8.2 m, which meets the 6 m minimum; the built 13 m carriageway leaves 9.2 m.

The bridges can be destroyed later (DESIGN_FOUNDATION §6, persistent ruins). That is out of scope for 01, but the bridge mesh should be built as separate spans to allow it.

## Central village

- **Location:** it straddles the river between the two bridges, about 150 × 250 m on each bank, on a slight rise of 3–5 m. The rise reads as a landmark from both bases.
- **Size:** 15–25 buildings in total, with **equal cover value per bank**. The two banks look different (the west bank has a church square, the east bank a mill and workshops) but hold similar counts and footprints.
- **Garrison:** DESIGN_FOUNDATION §8 confirms garrisonable civilian buildings. Choose 3–4 buildings per bank as future garrison buildings with the same capacity on both banks.
- **Streets:** at least 10 m between building faces on the main street and at least 7 m on side lanes. That leaves room for a tank to turn (pathing turns are visual, `FaceTowardYaw`, but believability needs it).
- **Objective:** one capturable supply stockpile per bank (DESIGN_FOUNDATION §7), at about (-90, 40) and (90, -40).

### Village layout (#157)

`rivercrossing_greybox.py` (`VILLAGE`) lays out both banks from the in-house set of #408 (`ARTSCivilBuilding` with `DA_Civil_<Type>`), 10 buildings per bank, each with its door toward the bank's main street, the ring road at x = ∓157 m. Both banks use the same street, the same lots and the same distances to the river and the stockpiles. The west bank has a church square: the church faces an empty lot across the street. The east bank has a barn and two workshops, one with a chain-link yard.

| | West bank | East bank |
|---|---|---|
| Buildings | 3 × village house, 2 × semi-detached, corner house, church, 2 × farmhouse, workshop | 3 × village house, semi-detached, 2 × corner house, barn, 2 × workshop, farmhouse |
| Footprint | 1,781 m² | 1,678 m² (6% less) |
| Garrison (actor tag `Garrison`, D5) | church 10, semi-detached 6, corner house 4 = **20** | barn 8, workshop 6, semi-detached 6 = **20** |

- **Streets.** No building comes within 9 m of the main street's centre line (the ring road and its falloff), so its faces stand at least 18 m apart. Side lanes are at least 7 m, and every building stands at least 15 m from the river. The generator refuses a layout that breaks any of these rules or the equal capacity.
- **Collision.** Each building's mesh is its collision, and a null nav volume over its footprint keeps the navmesh off its roof.
- **Props (#409).** These are dressing only, with no collision and no navmesh:
  - Street lamps every 30 m along the main street's river side.
  - A power line on its far side, a pole every 40 m, with two `SM_PowerLine_Cable` spline-mesh cables from each pole's cross-arm.
  - Place-name, 50, give-way, priority-road and warning signs.
  - Post-and-rail garden fences behind the houses.
  - A field-wire pasture by the farm.
  - The east bank's chain-link yard.
  
  The Poly Haven meshes and textures are copied into the committed `/Game/World/Props` under their own masters, so the level never points into the uncooked `/Game/PolyHaven`.
- **Checks.** `WorldDivide.Maps.RiverCrossing.Layout` checks:
  - Equal counts and garrison capacity on both banks.
  - No building in either 10 m main street.
  - A tank drives each main street end to end (565 m along a 562 m street).
  - The base routes stay within 3%. The enemy village stockpile is 0.7% apart, since the banks' buildings differ.
- **Camera.** `ue.ps1 -Task camerashots -Map /Game/Maps/WD_RiverCrossing_01` adds `village_west` and `village_east`.

## Road network

| Road | Type | Purpose |
|---|---|---|
| Base → home bridge → village | Asphalt, 7–8 m | Main axis |
| Base → forward supply | Dirt/farm track, 5 m | Early economy |
| Forward supply → far bridge | Farm track | Flank route |
| Village ring road on each bank | Asphalt, 6 m | Links both bridges inside the village |
| Border | Roads end in the border ring (a dressing road leading off the map) | Believability |

- Curves have a radius of at least 25 m.
- Junctions are T- or Y-shaped, with no tight hairpins.
- Roads are visual only: units move equally well on fields. "Optional roads" is DESIGN_FOUNDATION §6; there is no road speed bonus.

## Terrain, fields and forests

- **Open fields:** most of the area between the bases and the river. These are the armour engagement zones, with 400–600 m of uninterrupted sightline. The patchwork of crop, meadow and fallow fields is shown by landscape layers only, with no collision.
- **High ground:**
  - One hill per side, at (-350, -650) and (350, 650), 15–25 m high.
  - Each overlooks the side's own home-bridge approach.
  - Slopes stay ≤ 20° on the faces vehicles are meant to drive.
- **Forest clusters:**
  - Large woods in the corners behind each base and in the border ring.
  - Smaller copses of 30–80 m across on the hills and beside the river.
  - Every copse has drivable gaps. Trees there have no collision, or collision only on the trunks, kept out of the navmesh, to be decided in [03_TECHNICAL_PLAN.md](03_TECHNICAL_PLAN.md).
- **Hedgerows:**
  - Low (1.5–2 m), visual only.
  - They break up the fields but never form a closed wall.
- **Rolling terrain:**
  - 2–8 m of undulation across the fields.
  - The flats are the base pads and the bridge approaches.

## Balance checklist

Measured in the greybox phase with the navmesh and recorded in phase 2 of the roadmap.

- [x] The route from each base to its home bridge, its far bridge and the village centre is within ±3% in length on both sides.
- [x] Each side has the same number of supply sources, at the same distances (safe, forward, village).
- [x] Each side has one hill of the same height, the same distance from its home bridge.
- [ ] The cover around both bridges is equal: the same copse sizes and the same distances. *(No copses yet; they come with the vegetation in phase 6.)*
- [x] Each village bank has the same number of buildings and the same garrison capacity. *(10 buildings a bank, 20 garrison capacity in 3 tagged buildings each, #157.)*
- [x] There is no path from either base to the other that avoids both bridges.
- [x] Building placement succeeds at no fewer than N test positions per base pad (N = 16).
- [x] Starting units spawn and path to both bridges on both sides.

### Greybox results (#154, 2026-10-20)

`Tools/Editor/rivercrossing_greybox.py` builds the greybox from the numbers in this document, but only when the level doesn't exist yet. From then on the level is authored by hand. Every height and every actor is point-symmetric by construction: what stands at (x, y) for the west side stands at (−x, −y) for the east. So the supply sources, the hills (20 m, at (∓350, ∓650)) and the village banks (10 blockout buildings each) match exactly. Each side has 3 supply sources:

- **Safe:** 15,000, 90 m behind the start spot, within the AI's depot reach.
- **Forward:** 8,000, at (∓400, ±500).
- **Village:** 10,000, at (∓90, ±40).

`WorldDivide.Maps.RiverCrossing.Layout` measures the rest on the navmesh, using the vehicle agent. Each route starts at the base exit, past the headquarters:

| Route | West (team 0) | East (team 1) | Difference |
|---|---|---|---|
| Home bridge | 702 m | 702 m | 0.0% |
| Far bridge | 873 m | 874 m | 0.0% |
| Own village stockpile | 633 m | 633 m | 0.0% |
| Enemy village stockpile | 1,001 m | 1,002 m | 0.1% |
| Enemy base | 1,575 m | 1,576 m | 0.1% |

- No path sample lies over a `Water_*` volume except on a bridge deck, and the route from base to base crosses a bridge on both sides.
- Both bridge decks carry vehicle navmesh.
- A vehicle factory fits at 16 of 16 grid sites on each pad.
- `WorldDivide.Smoke.Maps` plays the map with both start spots taken.
- `WorldDivide.Maps.RiverCrossing.AIvsAI` plays two AIs to the end of a match: team 1 wins after 514 game seconds.
  - The skirmish AI doesn't yet expand to the forward and village sources, so a side lives off its safe source. That is why the safe source holds 15,000.
  - Team 1's harvester stalls between its source and the depot the AI built 8 m beside it, and never delivers. The test therefore judges only that the match ends with a winner (follow-up issues).

**Navmesh build:** 18–20 s for both agents (`ue.ps1 -Task rivercrossing`, logged by `BuildNavigation`). Each agent gets 3,072 tiles at about 2 MB, plus a 2.7 MB tile cache.

## Camera and readability

- **Camera reach.** At the standard's maximum zoom (FOV 60°, arm about 300 m, pitch -55°) the view covers about 350 m across. That shows a bridge and its near approach, and never both bases at once. The final values are the producer's choice in #137.
- **Tree height:**
  - 25 m trees hide about 17 m of ground behind them at -55°.
  - Within 40 m of roads, bridges and village streets, keep trees ≤ 18 m.
  - Use the tall hero trees (`SM_HP_Tree`, about 28 m) only in the border ring and the corner woods.
- **Units on terrain.** Units must stand out against the ground: avoid high-contrast, busy ground textures at RTS distance, and keep the grass ≤ 0.6 m high.
- **Fog.** No height fog thick enough to grey out the far side of the view. Cinematic fog is excluded by the brief.
- **Lighting.** Clear afternoon sun at about 40–50° elevation, so long shadows show the terrain's shape without hiding units.
- **Fog of war and minimap.** Neither system exists yet (fog-of-war comments in `Units/RTSUnit.cpp`; the minimap is a reserved HUD slot only). The map doesn't depend on them, but it keeps strong landmarks (the river, the village and the two hills) that will read well on a future minimap.
