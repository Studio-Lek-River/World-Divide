# USA HQ Candidate 13 — art delivery and Unreal integration

Producer direction: preserve Candidate 12's approved campus; warm concrete and charcoal roofs; selective office glazing; restrained functional equipment; player-colour accents; a utilitarian courtyard with a large waving American flag. Target: normal RTS camera, not a ground-level walkthrough.

## Delivery and ownership

The delivery is `Art/USA/buildings/usa_hq/candidate13/`. Its source is `source/USA_HQ_Candidate13.blend`; `fbx/` contains LOD0–3 exports with a combined static root, separate garage door, radar, dish and two flag cloth meshes, plus preserved helpers. `textures/` contains portable PNG maps. `manifest.json`, `provenance.json`, and `audit/` record the measured result. `renders/` and `animation/` are Blender evidence, not Unreal captures.

Codex writes only this HQ art delivery, new HQ13 scripts and this handoff under the producer's explicit exception. Claude owns all Unreal Content, Source, Data, import automation, collision and runtime integration. No commits or pushes are authorized by this delivery.

**Preservation exception:** the initial build imported the old `usa_hq_blender.py` module, whose unguarded module-level entry point reran Candidate 12. This regenerated its `.blend`, `.fbx`, and five individual renders before the import was removed. Seven files are no longer byte-identical. The unchanged pre-existing Candidate 12 manifest still matches its geometry/transforms; all Candidate 11 and older files and the Candidate 12 texture sources remain hash-identical. Exact paths and original hashes are in `preservation_incident.json`; this is not a claim of complete byte preservation. No earlier generator source was edited.

## Geometry, scale and interfaces

- Retain **38 m in local X × 56 m in local Y**, with +X front/garage exit, +Y west, Z up; maximum height 18 m. Blender mirrors Y. Import at correct metre-to-centimetre scale, MeshScale (1,1,1), without fitting stretches.
- Preserve garage opening 12.6 × 5.6 m, the 7 m swept corridor, and existing spawn/exit/radar/dish helpers. Current source bounds and helper locations are recorded in the manifest. This asset does not implement production gameplay.
- `Socket_Flag_Courtyard` moves to Z=17 m above ground, including the plinth. Cloth top is 16.85 m, size 7.6 × 4 m. The smaller roof flag retains its existing location and 3.04 × 1.6 m size.
- Cloth has its own mesh/material and uses UV0: U=0 at the fixed hoist, U=1 at the fly, V=1 at top and V=0 at bottom. Exported cloth is the unanimated sagged rest mesh. Do not bake a displayed wind pose into the runtime rest mesh.
- Superseded (2026-10-21): the budgets and LODs below are replaced by [`docs/art/MODEL_PERFORMANCE.md`](../MODEL_PERFORMANCE.md), LOD0 and LOD1 only.
- Root budgets: LOD0 ≤60k triangles, LOD1 ≤25k, LOD2 ≤8k, LOD3 ≤2k; all animated parts ≤10k at LOD0. Eight root materials. Flag grids reduce at each LOD. Read actual counts from `manifest.json` and re-import checks from `audit/audit.json`.
- LOD1/2 reduce welded geometry. LOD3 is a purpose-built closed campus shell using the approved perimeters/elevations, flat window panes, simplified rooftop equipment and antenna silhouettes. It does not use the rejected destructive whole-mesh reduction. Material slot order is identical across LODs. The manifest distinguishes pre-export counters from independently re-imported FBX counts.
- Keep garage door, radar and dish origins from their named pivots. Build simple collision separately (existing ≤16-box policy); no flag collision and no per-poly building collision. Keep the open bay and scripted vehicle exit behavior.
- LOD FBXs each contain a root plus parts/helpers. Import roots as static-mesh LODs and the parts into their respective meshes; do not import each entire FBX as one combined mesh. Root LOD switching distances remain the project integration setting; compare adjacent levels at actual gameplay distances before accepting transitions.

## Materials and texture import

All major maps are native 4096²: Concrete, Roof, Cladding and Paving. Props and Flag are 2048²; standalone team and emissive masks are 1024². Major surfaces use metric repetition rather than one 4K image stretched over the entire campus. UV0 is texture mapping, not a unique lightmap unwrap; generate a separate lightmap UV channel if the lighting pipeline requires one.

| Material | Maps / physical mapping | Reconstruction |
|---|---|---|
| Concrete | Concrete set; 5.42 m repeat | Scan colour saturation 0.35, value ×1.22; normal strength 0.45; AO supplied separately. Building-local base dust blends 9% at ground to 0% at 1.1 m. |
| Roof | Roof set; 4 m tile | Charcoal membrane, 1 m rolls and shallow seams; normal strength 0.65. |
| Cladding | Cladding set; 4 m tile | Coated metal panels; coating is dielectric, with metallic map zero. |
| Paving | Paving set; 8 m tile | 2 m concrete pours; restrained seams and variation. |
| Props | Props atlas | Four padded quadrants: coating, bare metal, dark grille and solar cells. Preserve supplied UV0; ORM selects the appropriate metalness/roughness. |
| Glass | No unique texture | Opaque reflective dark glazing for RTS, linear base colour (0.035, 0.06, 0.075), roughness 0.19, metallic 0, default dielectric specular. Recesses provide depth; no modeled office interior. |
| Team | Cladding maps plus TeamMask | Base colour = player linear RGB × (0.78 + 0.22 × cladding base-colour luminance). Preserve cladding roughness/normals. Dedicated material slot confines tint. |
| Emissive | EmissiveMask | Neutral-white/warm fixture colour, restrained emission; never player tinted. No extra dynamic lights are included. |
| Flag (separate parts) | Flag set | Actual US flag colours, two-sided opaque fabric, roughness from ORM; never player tinted. Wind described below. |

BaseColor is sRGB; normals, ORM and masks are linear. ORM channels: R ambient occlusion, G roughness, B metallic. Use Normalmap compression for DirectX normals; do not invert green in Unreal. Blender inverts green to display those same source files. Do not multiply AO into base colour. Shader-node wiring is not carried reliably by FBX; reconstruct it explicitly.

Retain normal mip generation and texture streaming. The 4096 source dimensions are not a requirement to force every highest mip resident at RTS distance. Preserve the native maps and let the project's texture streaming policy select runtime mips.

The Props atlas deliberately packs metal and nonmetal regions in one material. Flags do not carry a player-colour mask. Reuse the project's existing custom primitive-data team-colour interface rather than introducing a second ownership parameter. Feed the same colour to any separate component that receives a tintable material.

Only the concrete is a downloaded scan (the Candidate 12 Poly Haven CC0 source, with original provenance retained). Roofing, cladding, paving, props and the flag are original deterministic native-resolution authored textures. They are not described as photographed scans or AI-upscaled imagery. No paid generation was used.

## Flag wind implementation

Use material World Position Offset, without runtime cloth simulation. The supplied Blender source uses six shape-key basis functions driven by time to preview the same mathematical surface; these are authoring evidence, not a request to run morph animation in Unreal.

Let `u=UV0.x`, `v=1-UV0.y`, `t=Time` in seconds, and lengths in metres:

```text
p = 6*u + 2*v - 2.4*t
q = 13*u - 5*v - 5.1*t
A = 0.36 * (0.8 + 0.2*sin(0.71*t))
offset_x = u * (A*sin(p) + 0.09*sin(q))
offset_y = 0
offset_z = 0.12*u*sin(p)
```

These offsets are in the asset's local Unreal frame (+X across the cloth, -Y along its length, +Z up). Multiply by 100 for Unreal centimetres, transform as a vector from local to world, then connect to WPO. Use global Time so both flags share a breeze, with distinct mesh sizes. The exported rest surface already includes longitudinal extent `0.96*length*u` and gravity sag `-0.42*u*u`; **do not add the sag twice**. A=0.216…0.36 m, transverse ripple ≤0.45 m, vertical ripple ≤0.12 m. U=0 remains exactly pinned.

For correct moving lighting, reconstruct the deformed surface normal rather than keeping the rest normal. In local Unreal coordinates:

```text
dx_du = A*sin(p) + 0.09*sin(q) + u*(6*A*cos(p) + 1.17*cos(q))
dx_dv = u*(2*A*cos(p) - 0.45*cos(q))
dz_du = -0.84*u + 0.12*sin(p) + 0.72*u*cos(p)
dz_dv = -height + 0.24*u*cos(p)
Tu = (dx_du, -0.96*length, dz_du)
Tv = (dx_dv, 0, dz_dv)
N_local = normalize(cross(Tu,Tv))
```

Transform the normal correctly with the actor, use a world-space normal material configuration, and handle TwoSidedSign for the reverse face. Expose `length` and `height` per flag material instance. Allow **50 cm maximum WPO displacement** and bounds padding in the horizontal movement direction; verify actual component bounds/culling in UE5.8. At every LOD the same UV functions and time must be used, avoiding phase jumps.

## Review and acceptance

Compare the matched Candidate 12/13 RTS views using 150 m spring-arm length, vertical FOV 60°, pitch 55°, and identical lighting. Older Candidate 12 review images used horizontal FOV and are not the camera-scale reference. Inspect the full building at 1080p and 3840×2160, four elevations, façade/roof/courtyard close-ups, neutral overcast, six player colours and LOD contact sheets. Close-ups are diagnostic; acceptance remains normal RTS distance.

The Blender review animation is at least 20 seconds. The audit samples all fixed hoists, height bounds and cloth-to-architecture proximity through the full interval. It also checks source image paths/dimensions, footprint, helper preservation, actual garage rays and independently re-imported LOD FBXs.

Unreal acceptance remains pending Claude: verify material parity, six ownership colours, flag motion and shadows, no culling flicker, LOD transitions, open/closed garage behavior, and the existing gameplay clearance tests. Record actual Unreal screenshots and motion before calling this engine-validated. No global rendering settings or lighting budgets should be increased for this asset.

## Reproduction

Run from the repository root using the installed Blender 5.1 and a Python environment with NumPy/Pillow. The scripts write only the Candidate 13 delivery directory. `usa_hq13_build.py` does **not** import the legacy executable `usa_hq_blender.py`.

```text
python Tools/Art/usa_hq13_textures.py
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_build.py
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_review.py -- full
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_review.py -- baseline
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_review.py -- teams
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_review.py -- lods
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_review.py -- animation
blender -b --python-exit-code 1 --python Tools/Art/usa_hq13_audit.py
python Tools/Art/usa_hq13_package.py
```

The two render helpers `m20_guardian_render.py` and `m20_guardian_bake.py` are existing read-only project dependencies. Do not substitute an import of the legacy USA HQ generator.
