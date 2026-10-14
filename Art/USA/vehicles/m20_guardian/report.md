# M20 Guardian — quality pass (v2)

Built from code by `Tools/Art/m20_guardian_blender.py` (Blender 5.1) with its sibling modules
`m20_guardian_materials.py` (procedural and game materials), `m20_guardian_bake.py` (finishing, UVs, bakes) and
`m20_guardian_render.py` (presentation scene). Rebuild everything with
`blender -b --python Tools/Art/m20_guardian_blender.py` (about 4 minutes on the RTX 4090); `-- --preview` renders quick
views of the geometry only; `-- --no-bake` skips the textures; `-- --verify` checks the FBX files.

The first prototype (v1) is kept in `_v1/`: its .blend, FBX, renders, JSON, reports and the script as it was.

## Files

- `source/m20_guardian.blend`: game meshes (modifiers applied, UVs, game materials) and, in the hidden `M20_High`
  collection, each part's high-poly twin with live Bevel and Weighted Normal modifiers and the procedural materials.
- `fbx/SM_M20_*.fbx`: one mesh per moving part, each at its own pivot, metres, +X forward, Z up, triangulated, with
  tangents and a colour attribute for glass and lamps. Names unchanged from v1.
- `textures/T_M20_<Set>_{BC,N,ORM}.png`: BaseColor (team mask in alpha: 1 on the blue ID panels), Normal (DirectX,
  green down, ready for Unreal), ORM (occlusion, roughness, metallic).
- `m20_guardian.json`: pivots and sockets, identical to v1 (checked by diff).
- `renders/`: `m20_hero34`, `m20_rear34`, `m20_close_turret`, `m20_close_commander`, `m20_close_running_gear`,
  `m20_rts_default` (30 m arm, 55° pitch, 90° FOV as in `ARTSCameraPawn`), `m20_rts_mid` (15 m), the orthographic
  `m20_front/side/top`, `m20_front34` (the v1 camera), `m20_v1_v2_compare` and `m20_pivot_test` (turret 45° left, gun
  10° up, RWS 90° right).

## Phase 1: what v1 needed

Reconstructed as geometry (silhouette and shading at RTS distance): turret shell (one loft → ring skirt, shell and inset
roof plate), cheek and flank armour (flat slabs → stand-off composite modules with chamfers and bolt-on covers),
mantlet (prism → faceted octagonal mantlet with cover plate), the 105 mm gun (cylinders → one bored lathe profile with
tapered sleeve, clamp bands and fume extractor), glacis and nose, skirts (6 cm boxes → 10 cm modules with cover plates
and hangers), road wheels, sprocket, idler, track links, the missing suspension, the commander's station and RWS, smoke
dischargers, bustle stowage, engine deck and rear plate.

Moved to the normal map: several hundred bolts, weld beads, track wedge and pad bolts, wheel nuts, plate texture,
scratches and paint chips.

## Phase 2: geometry

- Turret: stepped shell, cheek modules on brackets with two cover plates (outer one blue), four flank slabs with raised
  centre plates hung from hooks, two banks of eight smoke dischargers, gunner's sight with sloped armoured face, two
  windows, opened shutters and a hood, commander's sight on a pedestal, cupola with hooded vision blocks, loader's
  hatch, lifting eyes, grab handles, LWR sensors, wind sensor, round-bar bustle rack with bins, containers, jerry cans
  and a strapped tarp.
- Gun: faceted mantlet with cover plate, coax port, auxiliary sight, lifting eye; muzzle reference sensor on a bracket.
- Hull: nose plate with lamp clusters in bent-bar guards, tow eyes, lower add-on plate, four glacis modules (outer two
  blue) with cover plates and a spine, driver's hatch and hooded periscopes, framed engine-deck grilles with angled
  louvres, access plate, tool boxes and jerry cans, rear plate with framed exhaust and side grilles, lamps in guards,
  pintle and tow cable, front fenders and mud flaps.
- Running gear: road arms with hull hubs, spindles, bump stops and three dampers per side, final drives, idler cranks and
  tensioners, rubber-tyred return rollers on brackets; dished twin road wheels with hub caps and lightening holes;
  toothed twin sprocket rings on a dished hub; spoked idlers; links with steel shoe, twin rubber pads, end connectors and
  centre guide horn.
- Edges: 2.5 cm two-segment bevels on armour (0.9 cm on small parts) with hardened, face-weighted normals; round parts
  carry their chamfers in their lathe profile. No subdivision anywhere.

## Phase 3: materials

Procedural source (bake input): weathered desert tan, olive-gray (running gear, RWS, sights, stowage), desaturated USA
blue; mottling, rain streaks, AO grime, dust that builds up towards the ground and on top faces, edge wear to bare
steel, primer chips, sparse scratches, rolled-plate bump; worn steel, gunmetal, rubber and track steel. Baked into four
texture sets; glass and lamps share one emissive material driven by the mesh's colour attribute.

## Phase 5: quality control

Triangles (game mesh): **80,820** (target 60–90k). Per part and per component: see `report_stats.md`.

| Part | Triangles |
|---|---:|
| Hull (incl. suspension) | 26,012 |
| Turret | 15,996 |
| Gun | 3,040 |
| RWS | 2,004 |
| Tracks (2) | 12,240 |
| Road wheels (12 × 1,360) | 16,320 |
| Sprockets (2) | 2,416 |
| Idlers (2) | 2,792 |

Materials: 5 (`M20_Hull`, `M20_Turret`, `M20_Running`, `M20_Track`, `M20_Emissive`), down from 9 slots in v1.
Textures: Hull 4K, Turret 4K (turret, gun, RWS), Running 2K, Track 1K (one link, tiled).

### Still to improve

1. **Weathering strength.** The baked look is cleaner than the sheet: more dust and grime on the lower hull and
   running gear, oil streaks from the deck, and mud on the tracks would sell it further.
2. **UV efficiency.** Smart-project islands at uniform density leave many tiny islands (bolts, tubes) and waste in
   the 4K sheets; hand-tuned seams or a trim sheet would roughly double the texel density on the big armour faces.
3. **Commander's station.** The vision-block hoods float slightly above their glass; the RWS sensor head and the
   commander's sight are close enough to crowd each other in some angles.
4. **Turret clutter.** The sheet's roof and bustle are busier (more boxes, cables, cases); worth one more pass.
5. **Gun length.** Still 1.1 m past the hull against about 1.4 m on the sheet (pivot and socket kept as in v1).
6. **Tracks.** Rigid meshes with a 1-link tiling texture: ready for a scrolling material, no sag or wheel travel.
7. **Bake check.** Hidden faces (behind cover plates) bake to junk normals; harmless, but a cage object would make
   the bake tidier.

### UE5 performance concerns

- **Texture memory: 123 MB** for the full set at 4K (BC3/BC5/BC1 with mips). Import the 4K sets with a 2K max in game
  (≈27 MB total) and keep 4K for cinematics.
- **Draw calls:** 9 mesh types, 22 components per tank, 1–2 material sections each → about 30 draws per tank before
  shadows. Use instanced meshes for the 12 road wheels, or merge the static running gear into the hull for LODs.
- **No LODs or collision yet.** Suggested LOD1 ≈ 20k (drop the suspension detail, bolts already gone, 12-sided wheels),
  LOD2 ≈ 5k, and a simple UCX box set.
- **Thin geometry:** antennas, rack bars and grab handles alias and overdraw at RTS distance; fade them out in LOD1.
- **Nanite:** fine for the hull and turret; the tracks, wheels and RWS move, so keep them as regular meshes.
- **Normals:** import with "Import Normals and Tangents" so the baked normal map matches; the maps are DirectX.
