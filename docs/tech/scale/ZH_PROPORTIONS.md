# Zero Hour proportions in World Divide metres

Status: **reference, not a standard.** The [world scale standard](WORLD_SCALE_STANDARD.md) stays authoritative. Producer decision (2026-10-22): **map sizes follow ZH** (at least ZH's size for as many players, not much bigger); building sizes stay as the standard has them. The building proposals in §6 are kept for reference only.

The standard says buildings follow "the Command & Conquer: Generals convention", but nobody had measured that convention. This document measures it from the game's own data. [`Tools/Art/zh_scale_report.py`](../../../Tools/Art/zh_scale_report.py) reads the data straight from the user's own Zero Hour install and prints every table below. Nothing is extracted or committed, only numbers.

```
python -I Tools/Art/zh_scale_report.py ["<Zero Hour install dir>"]
```

## 1. Method

| Source | What is read |
|---|---|
| `INIZH.big` → `Data/INI/Object/*.ini` | Collision geometry of each object: `Geometry` (BOX/CYLINDER), `GeometryMajorRadius`, `GeometryMinorRadius`, `GeometryHeight`. The game also uses this box for build placement, so it is the footprint. A BOX is 2·major long (X) by 2·minor wide. |
| `Locomotor.ini`, `Weapon.ini` | `Speed` of the object's `SET_NORMAL` locomotor, and `AttackRange` of its first PRIMARY weapon. |
| `GameData.ini` | Camera height and pitch. |
| `MapsZH.big` → `Maps/MapCache.ini` | For the 64 official multiplayer maps: playable extent, start positions, supply positions. |

ZH is not internally to scale. Its units are oversized next to its buildings, and its infantry is oversized again next to its tanks. A single "units per metre" factor therefore does not exist, and the conversion needs an anchor:

| Anchor | Rule | Factor | Effect |
|---|---|---|---|
| **A: MBT** | The average ZH MBT geometry length (Crusader, Battlemaster, Scorpion: 28 u) = our MBT, 9.8 m | **0.35 m/u** | Infantry would be 4.3 m tall |
| B: infantry | ZH infantry height (12.3 u) = 1.8 m | 0.146 m/u | Our MBT would be 4.1 m long |

Our vehicles are real size and fixed by the standard, and vehicles are what must fit buildings and maps. So **anchor A is the one that preserves the ratios we care about**: tank to building, tank to map, tank to range. Every metre value below uses anchor A. Anchor B is listed only to show how far ZH's infantry is exaggerated (about 2.4×).

Caveats:
- ZH collision boxes are fat. A Crusader is 30 × 20 u, which is 10.5 × 7.0 m under anchor A against our 9.8 × 3.7 m. Lengths compare well; widths of units do not.
- Some building boxes include yard space (the Patriot and Stinger compounds, the airfield's hangar and apron).

## 2. Buildings

Anchor A, in metres. "×MBT" is the building length divided by the MBT length, and does not depend on the anchor.

| Our building | ZH equivalents | ZH L × W × H (m) | ×MBT | Standard | In game now | ZH / standard (length) |
|---|---|---|---:|---|---|---:|
| Headquarters | Command Center (USA, China, GLA) | 42–45.5 × 44–49 × 13–19 | 4.3–4.6 | 28 × 28 × 18–25 | 38 × 56 | 1.5–1.6 |
| Vehicle factory | War Factory (USA, China), Arms Dealer | 28–40 × 42–43 × 9–14 | 2.9–4.1 | 24 × 24 × 12–14 | 24 × 24 | 1.2–1.7 |
| Power plant | Power Plant (USA), Nuclear Reactor (China) | 15–19 × 21–24 × 14–16 | 1.6–1.9 | 20 × 20 × 12–16 | 20 × 20 | ≈ 1.0 |
| Barracks | Barracks (USA, China, GLA) | 25–38.5 × 31–36 × 7–10.5 | 2.6–3.9 | 16 × 16 × 6–8 | 16 × 16 | 1.6–2.4 |
| Supply depot | Supply Center (USA, China), Supply Stash | 25–36 × 24.5–33 × 8–10.5 | 2.6–3.7 | 16 × 16 × 8–10 | 16 × 16 | 1.6–2.3 |
| Superweapon | Particle Cannon, Nuke Silo, Scud Storm | 31.5–50 × 27–47 × 9–22 | 3.2–5.1 | 24 × 24 × 30–35 | 24 × 24 | 1.3–2.1 |
| Defence tower | Gattling Cannon, Patriot | 5.6–8.4 square × 5–7 | 0.6–0.9 | 6 × 6 × 10–12 | 6 × 6 | ≈ 1.0–1.4 |
| Airfield | Airfield (USA, China) | 58–78 × 52–53 × 9 | 5.9–8.0 | 24 × 48 × 1–2 | 24 × 48 | 1.2–1.6 (long side) |
| Supply source | Supply Dock, Supply Warehouse | 30–35 × 36–43 × 5–13 | 3.1–3.6 | 12 × 12 × 2–4 | 12 × 12 | 2.5–3 |

Findings:
- **Production and economy buildings are 1.5–2.4× longer in ZH** than in our standard. In area that is roughly 2.5–5×. Only the power plant and the tower already match.
- **The in-game HQ (38 × 56) is already close to ZH** (42–45 square). The 28 × 28 in the standard is the outlier.
- **ZH buildings are low.** HQs are 13–19 m and factories 9–14 m. Our height bands are similar or taller. Only the superweapon (30–35 m against ZH's 9–22 m) is clearly taller, which is a deliberate landmark choice.

## 3. Units, speeds and ranges

Our unit classes against their ZH equivalents. Speed and range are given in MBT lengths, so they do not depend on the anchor.

| Class | ZH speed (MBT-len/s) | ZH as m/s (A) | Our band (m/s) | ZH range (MBT-len) | ZH as m (A) | Our band (m) |
|---|---:|---:|---|---:|---:|---|
| MBT | 0.9–1.4 | 8.8–14 | 10–12 | 5.4 | 52 | 60–75 |
| Light vehicle / IFV | 1.4–3.2 | 14–31 | 12–14 | 5.4–6.2 | 52–61 | 50–65 |
| AA vehicle | 1.1–1.4 | 10.5–14 | 13–15 | 5.4–7.1 | 52–70 | 70–90 |
| Infantry | 0.7–0.9 | 7–8.8 | 4–5 | 3.6 | 35 | 30–40 |
| Jet | 5.7–6.3 | 56–61 | 35–45 | 11.4 | 112 | 45–55 |

Findings:
- MBT speed, infantry range and IFV range already match ZH.
- Our MBT outranges ZH's by about 15–45%. ZH tanks shoot at 5.4 tank lengths, ours at 6–7.6.
- ZH infantry moves about 1.6× faster than ours, but that comes from ZH's oversized infantry, not from a ratio worth copying.
- ZH jets have twice our range. Our 45–55 m jet range is short by any measure.

## 4. Maps

The playable extent of the 64 official multiplayer maps, in anchor-A metres. "Nearest start" is the shortest distance between two start positions.

| Players | Maps | Longest side: range | Longest side: median | Nearest start: median |
|---:|---:|---|---:|---:|
| 2 | 23 | 665–1313 m | **963 m** | 771 m |
| 3 | 2 | 1190–1225 m | 1208 m | 685 m |
| 4 | 25 | 945–1558 m | **1225 m** | 555 m |
| 6 | 7 | 1138–1663 m | 1246 m | 406 m |
| 8 | 6 | 1470–1925 m | **1601 m** | 477 m |

In MBT lengths, a typical 1v1 map is about 100 across, and a 4-player map about 125.

| Our map | Across | ZH equivalent |
|---|---:|---|
| `L_TestMap`, `L_Skirmish`, `L_SkirmishDuel` | 600 m | Smaller than every ZH 2-player map. Scorched Earth, the smallest, is 665 m. |
| `L_RiverFord` / `L_QuarryCross` / `L_SixPeaks` | 1000 / 1250 / 1250 m (were 500 / 660 / 720 m) | The ZH median for 2, 4 and 6 players |
| WD_RiverCrossing_01 | 2000 m playable | Larger than any ZH map. The largest 8-player maps are 1.6–1.9 km. |

Our buildings are about half ZH's length. A ZH base placed on a 600 m map would therefore crowd it about as much as one of our bases does now. If the buildings grow (§6), the maps have to grow with them.

## 5. Camera

| | ZH (`GameData.ini`) | ZH in m (A) | World Divide |
|---|---|---:|---|
| Default height | 232 u (8.3 MBT-len) | 81 m | Arm 150 m at 55° pitch, which is about 123 m high |
| Minimum height | 120 u | 42 m | — |
| Maximum height | 310 u | 109 m | Arm 450 m, about 369 m high |
| Pitch | 37.5 | — | 55° |

ZH's FOV is not in its INI files, so this table compares height only, not ground coverage. Our default camera sits about 1.5× higher than ZH's in tank lengths, and our maximum zoom goes much further. With larger buildings, the current default camera would frame a base about the way ZH does.

## 6. Proposals for the producer

These are options, not decisions. Each one changes `WORLD_SCALE_STANDARD.md` §3/§6, the footprints in `setup_testmap.py` and the map sizes, so each needs the producer's approval in an issue.

**Option 1: adopt ZH ratios (anchor A, rounded, square where ours are square).**

| Building | Now (standard) | Proposed footprint (m) | Height |
|---|---|---|---|
| Headquarters | 28 × 28 | 44 × 44 (or keep the current 38 × 56) | 18–20 |
| Vehicle factory | 24 × 24 | 36 × 40 | 12–14 |
| Power plant | 20 × 20 | 20 × 20 (unchanged) | 14–16 |
| Barracks | 16 × 16 | 28 × 32 | 7–9 |
| Supply depot | 16 × 16 | 30 × 30 | 9–10 |
| Superweapon | 24 × 24 | 38 × 38 | 30–35 (keep the landmark) |
| Defence tower | 6 × 6 | 7 × 7 | 10–12 |
| Airfield | 24 × 48 | 52 × 60, four pads | 1–2 slab plus a hangar |
| Supply source | 12 × 12 | 24 × 24 (between ours and ZH's 30–40) | 3–6 |

Maps under option 1:
- 1v1 skirmish maps: 900–1000 m (the ZH 2-player median is 963 m), with starts about 750 m apart.
- 2v2: about 1200 m.
- 4v4: 1500–1700 m.
- RiverCrossing at 2 km is then an 8-player-class map, or it gets a smaller playable area for 1v1.

**Option 2: halfway.** Use the geometric mean of today's standard and ZH, about 1.3× the current lengths (HQ 36 × 36, factory 30 × 32, barracks 22 × 24, depot 22 × 22), with 1v1 maps at 750–850 m. Bases grow, but the art already made needs less rework.

**Either option, independent of building size:**
- Fix the HQ mismatch: the game's 38 × 56 is not the standard's 28 × 28.
- Make 1v1 skirmish maps at least 700 m across. Even today's buildings are on the ZH-small end.

## 7. Verification

- The Crusader comes out at 30 × 20 × 10 u, which matches `AmericaVehicle.ini` (major 15, minor 10, height 10).
- The American Command Center comes out at 120 × 140 × 49 u, which matches `FactionBuilding.ini` (60 / 70 / 49).
- Alpine Assault comes out at 2400 × 2600 u, which matches `extentMax` in `MapCache.ini`.
- The script parses 2103 objects and 64 maps, and reports no missing equivalents.
