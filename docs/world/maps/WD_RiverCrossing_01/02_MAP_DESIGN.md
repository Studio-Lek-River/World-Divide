# WD_RiverCrossing_01: map design

A 1v1 skirmish map set in the modern European countryside. The two players' halves are point-symmetric (180° about the map centre) in **gameplay**, but each side is **dressed** differently so the map reads as a real place.

All numbers here are **provisional**. They assume the producer's decision of 2026-10-15 to move the game to **real scale**, which lands first as its own gameplay issue (phase 0 in [04_IMPLEMENTATION_ROADMAP.md](04_IMPLEMENTATION_ROADMAP.md)). They are checked again once that issue fixes the final unit values.

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

These are target values the rescale issue should confirm or replace.

| Entity | Planning size | Planning speed | Planning range |
|---|---|---|---|
| Main battle tank (Abrams; mesh is already about 8.0 × 3.7 m) | 8 × 3.7 m | 12 m/s on roads and fields (compressed for RTS pacing) | 180–220 m |
| Wheeled support (truck, builder, flak) | 6–7 × 2.5 m | 13–15 m/s | |
| Infantry | 0.8 m, nav radius 0.4 m | 4 m/s | 100–150 m |
| Headquarters | about 30 × 30 m | | |
| Vehicle factory | about 30 × 40 m | | |
| Airfield | about 50 × 110 m | | |
| Camera | arm about 40–300 m | | |

With these values a 2 km map gives a base-to-base road trip of about 2 minutes for tanks. That is a typical Generals-like pacing, so **2.0 × 2.0 km of playable area is kept** as the target.

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
- **Room to grow.** A 350 m pad holds about 20–25 buildings of 30–40 m with 15 m lanes, plus one airfield. The AI's base search radius (`BuildSearchDistance`, 40 m today, `AI/RTSAIPlayer.cpp`) and the 45 m `BaseRadius` of `WorldDivide.Smoke.AIBuildsBase` (`Tests/RTSAISmokeTest.cpp`) **must scale** with the rescale issue.
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
| Carriageway | ≥ 12 m, which is two lanes and more than 3× the widest vehicle (3.7 m) |
| Overall deck | about 14 m including parapets |
| Length | about 70–90 m including abutments |
| Ramps | ≤ 8° (well under the 44° nav slope) |
| Clearance | 5–6 m above the water (visual only; there are no boats in this map) |
| Approach | 60 m of straight, obstacle-free road on both ends for columns forming up |
| Collision | Deck on **WorldStatic** so `RTSGround::Trace` and the building/HQ ground traces find it |

The final width is checked against the vehicle nav agent radius chosen in the rescale issue. The usable lane is the carriageway minus twice the agent radius, and it must stay at least 6 m.

The bridges can be destroyed later (DESIGN_FOUNDATION §6, persistent ruins). That is out of scope for 01, but the bridge mesh should be built as separate spans to allow it.

## Central village

- **Location:** it straddles the river between the two bridges, about 150 × 250 m on each bank, on a slight rise of 3–5 m. The rise reads as a landmark from both bases.
- **Size:** 15–25 buildings in total, with **equal cover value per bank**. The two banks look different (the west bank has a church square, the east bank a mill and workshops) but hold similar counts and footprints.
- **Garrison:** DESIGN_FOUNDATION §8 confirms garrisonable civilian buildings. Choose 3–4 buildings per bank as future garrison buildings with the same capacity on both banks.
- **Streets:** at least 10 m between building faces on the main street and at least 7 m on side lanes. That leaves room for a tank to turn (pathing turns are visual, `FaceTowardYaw`, but believability needs it).
- **Objective:** one capturable supply stockpile per bank (DESIGN_FOUNDATION §7), at about (-90, 40) and (90, -40).

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

- [ ] The route from each base to its home bridge, its far bridge and the village centre is within ±3% in length on both sides.
- [ ] Each side has the same number of supply sources, at the same distances (safe, forward, village).
- [ ] Each side has one hill of the same height, the same distance from its home bridge.
- [ ] The cover around both bridges is equal: the same copse sizes and the same distances.
- [ ] Each village bank has the same number of buildings and the same garrison capacity.
- [ ] There is no path from either base to the other that avoids both bridges.
- [ ] Building placement succeeds at no fewer than N test positions per base pad (N set in the validation test).
- [ ] Starting units spawn and path to both bridges on both sides.

## Camera and readability

- **Camera reach.** At the planned maximum zoom (arm about 300 m, pitch -55°) the view covers about 500–600 m. That shows a bridge and both of its approaches, but never both bases at once.
- **Tree height:**
  - 25 m trees hide about 17 m of ground behind them at -55°.
  - Within 40 m of roads, bridges and village streets, keep trees ≤ 18 m.
  - Use the tall hero trees (`SM_HP_Tree`, about 28 m) only in the border ring and the corner woods.
- **Units on terrain.** Units must stand out against the ground: avoid high-contrast, busy ground textures at RTS distance, and keep the grass ≤ 0.6 m high.
- **Fog.** No height fog thick enough to grey out the far side of the view. Cinematic fog is excluded by the brief.
- **Lighting.** Clear afternoon sun at about 40–50° elevation, so long shadows show the terrain's shape without hiding units.
- **Fog of war and minimap.** Neither system exists yet (fog-of-war comments in `Units/RTSUnit.cpp`; the minimap is a reserved HUD slot only). The map doesn't depend on them, but it keeps strong landmarks (the river, the village and the two hills) that will read well on a future minimap.
