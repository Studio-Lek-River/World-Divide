# Village final material recipe

Authority: `manifest.json` contains the actual serialized instance overrides, mesh-slot bindings and texture settings. `evidence/material_graph.json` records the native graph nodes, their properties, connections and outputs. Use those values rather than intermediate generator defaults.

## Parents and slots

The six stable slots are `Brick`, `Roof`, `Trim`, `Glass`, `Wood`, `Rubble`; only surviving slots are present on a mesh. `Brick` is the facade surface and may be brick, render or timber. Bridge aliases: `Brick` = concrete, `Roof` = asphalt, `Trim` = rebar, `Rubble` = broken concrete. Do not assume the old bridge two-slot layout.

Glass uses the current `/Game/Art/Shared/Materials/Building/M_Bld_Glass` directly. The other slots use `/Game/Art/Candidates/Village/Final/M_SetSurface`. A private extended graph is necessary because the shared flat surface parent does not expose the scanned maps and unique bake atlas. Shared parents were not edited. Claude can reproduce this graph in an appropriate production parent; do not ship a dependency on candidate folders.

## Coordinates and texture import

- UV0: real-scale planar surface projection, continuous across construction pieces. Brick repeat 1 m. Church `Brick` scanned UV multiplier `(0.625, 1)` gives 1.6 m horizontal / 1 m vertical repeat for Bricks097, approximately 10 cm headers, 20 cm stretchers and 6–6.5 cm courses. The multiplier affects the three scan samples only.
- Roof UV0 repeat 2 m with slope compensation. RoofingTiles006 has eight rows per repeat, approximately 25 cm courses. Use RoofingTiles006 for HouseA, HouseC, Farmhouse, Church; RoofingTiles013A for HouseB, Barn, Workshop. Exact final tints are in the manifest. HouseA geometry is preserved from its approved design.
- Barn wall/wood UV0 rotates the scan grain vertically, 2 m repeat, approximately 20 cm boards. Physical battens provide silhouette/relief. Other wood scans use the authored UV0.
- UV1 (`BakeUV`): unique per-state game-mesh atlas. Normal/AO are selected-to-active Cycles bakes from the physical 20 mm review-master chamfers, followed by outlier repair/padding. Atlas tint carries small macro variation, eave grime/moss, plinth dirt, damage scorch and rubble dust. Large flat floors/paving bypass geometric AO.
- All delivered PNGs are at most 2048×2048. Scanned BaseColor: sRGB on, `TC_Default`. Scanned tangent Normal: sRGB off, `TC_Normalmap`. Packed ORM: sRGB off, `TC_Masks` (R=AO, G=roughness, B=metallic).
- **BakeNormal is an object-space RGB data map, not a tangent normal texture:** sRGB off, `TC_Masks`; do not use normal-map compression. BakeAO and Tint: sRGB off, `TC_Masks`.
- Preserve mipmaps. Candidate texture streaming is retained. Production's MODEL_PERFORMANCE policy imports building textures non-streamed/always resident; Claude applies that production policy and measures total set residency. No 4K textures or git-ignored pack dependencies.
- `Round3_White` / `Round4_White` are tiny in-house historical data fallbacks included for reproducibility; they are not required by the final mesh material closure.

## Graph equations

Let `S = UseScan`, `T = SurfaceTint`, `B = BaseColor(UV0 * ScanUVScale)`, `A = AtlasTint(UV1)`.

`BaseColor = lerp(1, B, S) * T * A`.

The geometric bake was encoded in Blender object coordinates with Y flipped for Unreal. Decode `N_object = BakeNormal(UV1).rgb * 2 - 1`. Transform **Local → Tangent** with the material Transform node. This explicitly avoids treating the atlas as though it used UV0's tangent basis.

`fade = 1 - saturate((distance(CameraPositionWS, ObjectPositionWS) - 18000) / 5000)`.

`weight = BakeStrength * fade`.

`N = normalize(lerp((0,0,1), TransformLocalToTangent(N_object), weight) + (ScanNormal - (0,0,1)) * S)`.

`AO = lerp(1, ORM.r, S) * lerp(1, BakeAO.r, weight)`.

`Roughness = lerp(FlatRoughness, ORM.g, S)`. Current parent uses constant non-metallic output; zinc/painted metal readability comes from colour/roughness, not a metallic workflow. Keep the serialized graph for parity unless a later approved material pass changes it.

Game BakeStrength is 0.35 except `Rubble` (concrete/stone) 0.08. Review masters use 0. Native master instances otherwise retain the game surface and atlas tint. Church stone tint is lifted to `(1.3, 1.28, 1.20)`; do not accidentally restore the older dark concrete value.

LOD1 merges coplanar geometry, so its transferred UV1 can cross unique near-detail normal/AO charts. **The 180–230 m fade is required**, and affects only geometric normal/AO. Scanned detail, colour, ORM and atlas tint remain. LOD1 is a far mesh; it is not valid as a close-up substitute without the intended distance policy. Attach at 260 m, measured to the whole-building bounds centre, with a single switch for the whole mesh.

## Reproduction

`Tools/Art/village_431_round4_delivery.py` shows explicit native import/binding from this final folder. Its saved manifest/graph is the authority. Import stages skip existing packages; do not rerun them expecting changed FBX geometry to replace an old import. A changed mesh needs deliberate native reimport/revision without opening any native dialog. Do not use the prohibited native LOD UI.

All geometry is static, merged per state. No moving door/hoist/skylight controller is implied. Door shapes, glass and hardware are visual construction, not gameplay mechanisms.

Final revision paths: bridge game/review meshes use `/Game/Art/Candidates/Village/Final/Meshes02/` and `Masters02/`, with `Textures/AtlasBridge02/`. Farmhouse/Church damaged tint uses `Textures/Tint02/`; other atlases use `Textures/Atlas/`. The exact manifest mapping is authoritative. Earlier bridge imports and tint maps are retained history and are excluded from the final mesh dependency closure.

Destroyed bridge exposed rebar uses Trim SurfaceTint (0.10, 0.075, 0.055) on both game and review-master instances; retain this dark steel/rust value.
