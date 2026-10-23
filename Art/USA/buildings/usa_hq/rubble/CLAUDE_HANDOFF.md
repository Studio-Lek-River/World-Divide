# Issue 403 - Round 4 intake

READY FOR PRODUCER REVIEW after Round 4 final check; not approved for production. Follow BRIEF.md.

Import `SM_HQ_Rubble.fbx` as one static mesh, import `SM_HQ_Rubble_LOD1.fbx` into its LOD1. SM_HQ_Rubble: 41,862 triangles, SM_HQ_Rubble_LOD1: 14,234 triangles.
No skeleton, collision mesh or moving pieces. Disable automatic collision and Nanite; apply no scale or fit stretch. Origin is the intact HQ ground centre, +X front, Z up, scale 1. FBX contains centimetre geometry (UnitScaleFactor 1). Blender reimport uses object scale 0.01 to display those buffers in metres; Unreal import scale is 1. Blender garage is +Y, matching the source shell; Unreal garage is -Y after the same X-forward/Z-up export.

Slots in exact order: **Debris, Scorch, Embers**, all used. Bind each to an opaque one-sided PBR material from `material_recipe.json`.
BaseColor: sRGB on. DirectX Normal: sRGB off, Normalmap compression, no green flip. Normal strength: Debris 0.85, Scorch/Embers 0.4. ORM: sRGB off, Masks compression; R AO / G roughness / B metallic. Emissive mask: linear R, Masks compression. Keep mips and follow the established project building texture residency policy. Debris 2048, Scorch 1024, Embers 512. UV0 is a unique planar atlas per slot, no overlaps or tiling. UV1 SurfaceDetail holds tiled procedural Debris normal and DetailColor (0.5 m repeat), not a FinishBake or lightmap. Sample Debris Normal with TexCoord index 1, strength 0.85; DetailColor is linear/sRGB off, UV1, BaseColor = UV0 BaseColor * lerp(1, UV1 DetailColor, 0.65). All other maps use UV0. No finish-normal bake.

**Embers:** constant linear `(1, 0.105, 0.008) * 7 * Emissive.R`. Do not multiply by Operational, LampOn, night, team colour or any lighting channel. No real lights or fire supplied. Source graph converts DX green for Blender only; Unreal uses the delivered map directly.

LOD1 retains every named piece and the continuous fill shells; fracture face relief and slab corner profiles are simplified, and small tubes use triangular sections. Continuous mound shells and broken wall profiles remain exact. See the rendered switch pair for the practical silhouette comparison. Whole ruin switches at 26000 cm. See exact-camera pair in review/. Independent FBX evidence is Blender reimport, not Unreal import.

Review limitations: closest view targets garage rather than fitting 56 m into 12 m; intact comparison is shell only with neutral material override; cameras reproduce presentation orientations from supplied images, not recovered editor transforms. No producer approval, engine import, light-state integration, runtime LOD/collision or sink testing claimed. Claude owns all Unreal/code work; the producer owns visual acceptance.

## File receipt

| File | SHA256 |
|---|---|
| `source/HQ_Rubble.blend` | `e8ab11804de47a896c6d0170e4e0a4b85c2fd138b77cdf56afb30b9f274c70e5` |
| `SM_HQ_Rubble.fbx` | `d0e0f7c3fe7425e23d99197b77fbe285e812bd7eeaedd0ddb809ec5e8c3faf64` |
| `SM_HQ_Rubble_LOD1.fbx` | `95b52865f98a0ac261e5ce473068b2be5747528fce8bb6a8f87ca5182b33a7a1` |
| `material_recipe.json` | `7e01348fff4aace4c83ac9bfd197b77adb35c1d9aea66a63ea0d152368c318d8` |
| `textures/T_HQ_Rubble_Debris_BaseColor.png` | `b991153e91fa4591d644ca0d69e70fc1d01c7a26a6025d95ac6373d1a2a0036f` |
| `textures/T_HQ_Rubble_Debris_DetailColor.png` | `411d8de4bd1a73a6bcc578ec5ffb16d7a42ad37a29b1223ba2e2c75ab1784da6` |
| `textures/T_HQ_Rubble_Debris_Normal.png` | `94d611e6229d708be9605746b625f5cbb4b96af74ea3033d63f5209b3e36c8dc` |
| `textures/T_HQ_Rubble_Debris_ORM.png` | `53b52e91ccd4bce73208ac46029d29de595c17a622b4672eb3e1963a0cda49f4` |
| `textures/T_HQ_Rubble_Embers_BaseColor.png` | `79ebe472d4023688da940caf3c0262b0aa84bcdbd9dfbd78c6fdb4ca9147e2e8` |
| `textures/T_HQ_Rubble_Embers_Emissive.png` | `ee93fd5cd863e7ff94051ab2e65cfd79a52c3e608271820e35f48fb9c4460253` |
| `textures/T_HQ_Rubble_Embers_Normal.png` | `3733100e5fedea9e3363a9001f20f34ab120f70f605a63a6d92b9342ca1bf7b1` |
| `textures/T_HQ_Rubble_Embers_ORM.png` | `a95c3fc39611d90ddeabbbfac3c333f64b88978898ebbfd647414a8aedd05090` |
| `textures/T_HQ_Rubble_Scorch_BaseColor.png` | `b0a81f5971918094707f738743352018fda634fc3530b2820b27cb79eae77ef3` |
| `textures/T_HQ_Rubble_Scorch_Normal.png` | `7c9dfc2f1eb35014fdd355e20e8d3f6a134b819fa0af68e05d58a936754c0efa` |
| `textures/T_HQ_Rubble_Scorch_ORM.png` | `21c66f5ffa049a6e3d8a5edaa27b482d1ee24e5592c41e4b84c5f05c5efe6374` |

## Continuation

Use the repo-owned world-divide-building-production skill. Read rubble/STATE.md, BRIEF.md, ROUND_NOTES.md and review/README.md. Preserve the HQ source. Review the Round 4 renders and evidence/round4_independent_check.json; await producer acceptance before integration. Stay inside the rubble brief's write scope.

Final Round 4 audit: two clean runs reproduce both LOD geometry hashes and all maps/recipe bytes. All 15 file receipts and 14 review images pass evidence/final_audit.json; the separate fresh-process audit is evidence/round4_independent_check.json. See ROUND_NOTES.md for the final verdict and scope. No producer approval claimed.
