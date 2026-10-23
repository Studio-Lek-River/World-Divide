# 04 StripLight — current art state

## Issue #437 Round 1 — source rebake, 2026-10-23

Seven maps now follow the shared finish sampling contract: BaseColor/ORM and Lens LightProfile on untiled UV0; DirectX Finish_Normal on UV1 (FinishBake), tangent basis on UV0. Lens UV0 alone is uniformly scaled about (0.5, 0.5), to 0.003906–0.996094. Geometry, normals, transforms, slots, UV1 and housing UV0 match the archived approved source after save/reload. Still 300 triangles / 176 vertices / Housing then Lens. Source: [StripLight.blend](source/StripLight.blend).

Housing maps are 2048². Lens BaseColor/ORM/16-bit linear LightProfile are 1024²; Lens Finish_Normal currently retains the approved 2048² bake. All seven names, sizes, hashes and verification numbers are in [material_recipe.json](material_recipe.json) and [rebake_verification.json](evidence/rebake_verification.json). Four matched renders show no visible regression (mean absolute differences 0.0077–0.0206 / 255). Review: [rebake gallery](review/rebake/). See [handoff](CLAUDE_HANDOFF.md) for integration values and unresolved checks.

The producer's 2026-10-22 approval (“Yes is prima zo”) covers the archived shape and finish. This round does not claim new producer approval or Unreal acceptance. Claude owns export and integration; root FBXs were not written.

## Historical — final finish / material passes

The complete previous state and handoff, approved source, preview, tiled textures and recipe are retained in [archive/material_pass02](archive/material_pass02/). Earlier material pass evidence remains in its existing locations. Historical pending-approval statements there are superseded by the producer's approval above; historical FBX and UV0 claims describe those passes only.
