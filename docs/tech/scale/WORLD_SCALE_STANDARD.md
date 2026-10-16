# World Divide: world scale standard

Status: **approved** by the producer (2026-10-15, recovery decision D-F in [the recovery audit](../../production/SHARED_FOUNDATION_RECOVERY.md)). This is the authoritative source for world scale: 1 Unreal unit = 1 cm, and any change to the world scale needs the producer's explicit approval. Companion documents in this folder:
[placeholder audit](PLACEHOLDER_SCALE_AUDIT.md), [collision and selection](VEHICLE_COLLISION_AND_SELECTION.md),
[movement performance](RTS_MOVEMENT_PERFORMANCE.md), [implementation plan](SCALE_IMPLEMENTATION_PLAN.md).

This is the "rescale issue" that [WD_RiverCrossing_01 technical plan](../../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md)
§9, §12 and §14 depend on.

## 1. Units of measure

| Rule | Value |
|---|---|
| Unreal units | 1 uu = 1 cm (engine standard). All data (`MoveSpeed`, `Range`, footprints, offsets) is in cm or cm/s. |
| Axes | Z up, +X forward. Every vehicle mesh faces +X with its pivot centred on X/Y. |
| Ground contact | The lowest running gear (tracks, wheels, feet) is the ground contact, as `GetHullBox` already computes. |
| Building grid | 1 m (`RTSBuildPlacement::GridSize = 100`). Footprints are whole metres. |
| Imported art | Exported at real size in metres and converted by the importer; the import checks one reference length, as `make_m20_guardian` checks the 710.2 cm hull. |

## 2. Size categories

The producer chose three rules (2026-10-15):

| Category | Rule | Why |
|---|---|---|
| Infantry and ground vehicles | **Real size.** No visual exaggeration. | The units fight each other, so their relative size has to read true. The M20 Guardian is the reference. |
| Buildings | **RTS-compressed to about 0.6–0.7× real**, the Command & Conquer: Generals convention. | Real factories and airfields are hundreds of metres across. Compressed buildings keep bases readable at RTS zoom, while vehicles still fit their doors and exits. |
| Aircraft | **RTS-compressed to about 0.6×.** | A real fighter (about 16 m long, 11 m span) dwarfs a tank on screen and needs an airfield far larger than the building rule allows. |

Gameplay distances are **not** scaled by one global factor. Weapon range, sight, speed and the other gameplay values are set per unit in its data (producer decision). §5 gives starting bands.

## 3. Reference dimensions

The overall length includes the gun pointing forward. "Hull" is the body without the gun.

| Class | Overall L | Hull L | W | H | Reference |
|---|---:|---:|---:|---:|---|
| **M20 Guardian** (fixed) | ~7.8–8.0 m | 7.10 m | 3.54 m | 3.36 m | Imported mesh. Hull checked at 710.2 cm on import. |
| Main battle tank | 9.8 m | 7.9 m | 3.7 m | 2.4 m | M1A2 class |
| Light tank or IFV | 7.0–8.0 m | 6.5–7.0 m | 3.2–3.5 m | 2.9–3.4 m | M20 Guardian, M2 Bradley class |
| APC (wheeled) | 7.0 m | 7.0 m | 2.9 m | 2.7 m | Stryker class |
| Utility or air-defence truck | 7.0 m | 7.0 m | 2.5 m | 3.0 m | FMTV class |
| Heavy supply truck (harvester) | 10.0 m | 10.0 m | 2.5 m | 3.0 m | HEMTT class |
| Engineering vehicle (builder) | 6.5 m | 6.5 m | 3.0 m | 3.2 m | Armoured dozer class |
| Infantry | — | — | 0.5 m (footprint radius 0.4 m) | 1.8 m | Adult soldier with gear |
| Jet (compressed 0.6×) | 10 m | — | 7 m span | 3 m | Fighter |

**M20 note.** The brief gives about 7.8 m overall. The imported mesh measures about 8.0 m: the Blender report gives 8.03 m, and the muzzle offset puts the muzzle about 4.5 m ahead of the hull centre. This is within tolerance. The geometry is **not** changed.

`report_stats.md` gives a 6.68 m hull, which comes from an older export. The 710.2 cm import check is authoritative.

### Buildings (compressed)

| Building | Footprint (m) | Height (m) | Note |
|---|---|---:|---|
| Headquarters | 28 × 28 | 18–25 | Largest base building |
| Vehicle factory | 24 × 24 | 12–14 | The exit lane must clear a 9.8 × 3.7 m tank with 2 m of margin each side |
| Power plant | 20 × 20 | 12–16 | |
| Barracks | 16 × 16 | 6–8 | |
| Supply depot | 16 × 16 | 8–10 | The dock side must take a 10 m truck |
| Superweapon | 24 × 24 | 30–35 | |
| Defence tower | 6 × 6 | 10–12 | |
| Airfield | 24 × 48 | 1–2 (slab) | Four pads of about 12 × 24 m, each holding one 10 m jet |
| Supply source | 12 × 12 | 2–4 | |

## 4. Rules

1. **Vehicles fit their buildings.** A building that produces or docks a unit has an exit or dock lane at least the largest such unit's width plus 2 × 2 m.
2. **No visual scaling for gameplay.** Make a unit easier to see with HUD markers (brackets, health bars, team colour), not by enlarging the mesh. `MeshScale` stays uniform. Non-uniform Z stretching (`fit_scale(..., height)`) is not used for units.
3. **Gameplay spacing is separate from mesh size.** Formation gaps, exit clearance and avoidance margins are their own values (see [collision and selection](VEHICLE_COLLISION_AND_SELECTION.md)). They are never baked into the mesh or the selection size.
4. **Navigation clearance follows vehicle width.** The vehicle nav agent has a radius of 190 cm and a height of 350 cm; the infantry agent 40 cm and 180 cm (D-D, implemented in #135). Every gap meant for vehicles is at least the vehicle nav agent diameter (3.8 m) plus about 2 m, so at least 6 m. A road or bridge carriageway is at least 8 m. The 12 m bridge in RiverCrossing satisfies this.
5. **New art is delivered at real size.** It is checked on import against one reference dimension, which fails setup if the size is wrong. Buildings and aircraft are modelled at their compressed size.

## 5. Starting bands for per-unit gameplay values

These are not rules. They are starting points for tuning so that the first real-scale data is consistent. Each unit's data owns its final value.

| Value | Infantry | IFV / light tank | MBT | Trucks | Jet |
|---|---|---|---|---|---|
| `MoveSpeed` (cm/s) | 400–500 | 1200–1400 | 1000–1200 | 1300–1500 | 3500–4500 |
| `TurnRate` (°/s) | 540 | 90–120 | 60–90 | 60–90 (with a turn radius, see movement doc) | 60–90 |
| Main weapon `Range` (cm) | Rifle 3000–4000; grenade 2500–3500 | 5000–6500 | 6000–7500 | AA 7000–9000 (against air) | 4500–5500 |

- Defences outrange the standard tank by 1.25×, which keeps the existing `TOWER_RANGE_OVER_TANK`.
- Speeds are in "RTS time": they look plausible at real size (infantry jogs, tanks drive) and are about 2× real road speeds compressed into a skirmish. A 2 km map takes about 3 minutes to cross for an MBT.
- Shell speeds go up in the same proportion as ranges, so flight times stay at today's 0.2–1 s.
- There is no sight or fog-of-war system yet. Units acquire targets at their weapon `Range` (`URTSWeaponComponent::IsInRange`). Sight becomes a per-unit field when fog of war arrives.

## 6. Camera and maps (summary)

The full argument is in [the implementation plan](SCALE_IMPLEMENTATION_PLAN.md#step-5-camera-retune).

| | Before real scale | Standard |
|---|---|---|
| Camera FOV | 90° (engine default) | 60° horizontal at 16:9, the vertical kept on wider screens |
| Default arm | 30 m (about 60 m of ground across the screen) | about 150 m (about 170 m across) |
| Max arm | 70 m | 450 m (about 520 m across: a base and its approaches) |
| Pitch | -55° | -55° (unchanged) |
| Test map (L_TestMap) | 200 × 200 m | 600 × 600 m |
| Skirmish maps | 160–240 m across | 500–800 m across for small maps; full maps follow RiverCrossing (2 km playable) |
