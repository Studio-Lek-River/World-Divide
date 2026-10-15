# M20 Guardian — detail integration pass (v3)

Built from code by `Tools/Art/m20_guardian_blender.py` (Blender 5.1) with `m20_guardian_materials.py`,
`m20_guardian_bake.py` and `m20_guardian_render.py`; rebuild with `blender -b --python Tools/Art/m20_guardian_blender.py`.
v2 (the quality pass) is kept in `_v2/` and v1 in `_v1/`: .blend, FBX, renders, JSON, reports and the scripts as they were.
Proportions, silhouette, colours, pivots and sockets are unchanged (`m20_guardian.json` identical to v2, checked by diff).
No LODs and no collision yet.

## Renders for this pass

`renders/m20_hero34` (front ¾), `m20_rear34`, `m20_close_smoke`, `m20_close_commander`, `m20_close_mantlet`; also
`m20_v2_v3_compare`, `m20_close_turret`, `m20_rts_default`, `m20_rts_mid`, `m20_pivot_test` and the orthographic views.

## What was improved

- **Smoke dischargers (rebuilt).** The two olive plinths with loose tubes on the roof corners are gone. Each side now has
  an armoured housing on the roof edge just behind the cheek module. Its outboard face sits flush with the flank slabs
  and caps them. Its front face is set back so the tubes look forward, outward (~22°) and up (~30°). The eight tubes sit
  in a shallow bay under a brow, between side cheeks, and only their muzzles stand out. The housing also has a welded
  mounting pad, a gusset to the shell top, a bolted top access cover, a junction box and an armoured firing-cable
  conduit into the roof. The laser warning receivers moved onto the housings.
- **Turret roof hierarchy.** An armoured brow on the roof front edge covers the mantlet top. There is a bolted rear roof
  access panel and a weld seam across the roof. The antennas and lifting eyes stand on welded mounting pads, and a
  weld ring runs round the cupola base. Welds, bolts and seams go into the normal map only.
- **Gunner's sight.** It now has a bolted base flange and a housing with chamfered rear corners and sides that lean in.
  The windows have an armoured door frame, the open shutters have hinge pins, and there is a side access plate and a
  rear cable gland.
- **Commander's sight and cupola.** The sight moved 8 cm forward and 6 cm inboard, so it no longer crowds the RWS ammo
  box. It stands on a stepped pedestal: bolted flange, armoured column, a dark traverse ring and a cable entry. The head
  has chamfered rear corners, side armour plates, gussets under the sun hood and a parked wiper arm. The vision-block
  hoods now rest on their glass, with cheeks down to the cupola wall, which fixes the v2 float.
- **RWS.** It has a slip-ring band and a bolt circle on the base ring, a chamfered body and a bolted rear cover. The
  sensor head has armoured shutter doors on hinge pins and a cable loom into the cradle. The ammo box has a lid hinge
  and a latch.
- **Mantlet and gun base.** The mantlet has a bolted add-on top plate and cheek plates. A rubber gun-shield boot and a
  clamp ring join the cover plate to the barrel collar. The coax port has an armoured octagonal surround, and the
  auxiliary sight a hood with cheeks. The lifting eye is on a lug.
- **Hull front and roof fixtures.** The lamp clusters are bolted to the nose on base plates and have a brow, and the
  guard bars stand on welded feet. The tow eyes were floating about 13 cm in front of the lower glacis. They are now
  clevises: lug plates welded down the slope, a pin with washers and a shackle ring. The three driver's periscope hoods
  are one welded hood strip, and the hull lifting eyes stand on pads.
- **Rear and stowage.** The bustle rack's side rails used to pass through the bins. They now run clear of them, on posts
  held by arms welded to the bustle wall, and gussets carry the rack floor. The containers have ratchet straps and
  handles, and the bins have hinge knuckles. The tool boxes have tie-down brackets, and the jerry cans sit in a tray
  with an end stop. The tail lamps are on base plates, and the rear plate has clevis tow eyes, a pintle base plate with
  a pin, and cable clamps.
- **Running gear.** The road wheels have a steel rim flange between disc and tyre, and the track guide horns are
  slightly taller and broader. Wheel count, link pitch and sockets are unchanged.
- **Ready for materials.** Every new plate, pad and housing is its own bevelled piece with clean panel edges. Seams,
  welds and fasteners are in the high-poly twin only, so the bake gives clean edge masks for the wear, dirt and
  roughness passes.

## Most changed

| Component | v2 tris | v3 tris |
|---|---:|---:|
| Composite armour (incl. smoke dischargers) | 6,176 | 7,504 |
| Sights and roof | 4,800 | 6,724 |
| Mantlet | 844 | 2,304 |
| Glacis and nose | 3,972 | 5,092 |
| Rear plate | 2,964 | 3,908 |
| RWS | 2,004 | 2,540 |
| Road wheels (12) | 16,320 | 17,472 |
| **Total** | **80,820** | **89,396** |

The budget (60–90k) still holds. To stay inside it, small hidden brackets, clamps and pads have no bevels. Per-part
counts are in `report_stats.md`, and `--verify` passes on every FBX.

## Status

- **Detail integration pass: complete.** Every visible fixture now has a mounting method: a pad, flange, lug, bracket,
  weld or hinge. No box is left sitting loose on a surface.
- **Ready for the material/weathering pass: yes.** The geometry, UV layout and bake pipeline are unchanged and the
  panel separations are clean. The textures were re-baked for the new geometry, but the weathering look is still v2's
  and is the next step.

### Still open (not part of this pass)

1. Weathering strength: dust, oil streaks and mud, as listed in v2.
2. UV efficiency: a trim sheet or hand seams for the large armour faces.
3. Gun length is still 1.1 m past the hull (the pivot and socket are kept).
4. Tracks are rigid, with no sag.
5. A bake cage would tidy the junk normals on faces hidden behind cover plates.
6. LODs and collision.

## UE5 notes

These are unchanged from v2 (below). Texture memory is still 123 MB at 4K; import at 2K for gameplay. Draw calls are
unchanged.

---

# Previous pass (v2)


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
