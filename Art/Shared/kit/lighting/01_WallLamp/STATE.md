# 01_WallLamp — current state: Round 4 final check

**Producer appearance approval — 2026-10-23:** "Yes they are all approved", given per piece on the Round 4b comparison sheets (reference, day, night, LOD0/LOD1). This approves the current model and finish; production integration and runtime validation are Claude's (#392).


2026-10-23, issue #392. Independent Blender/export check complete; producer approval given 2026-10-23 (see above).

Preserved the reference construction. Narrowed diffuse edge wear, reduced oversized surface pits, restored centre-weighted ribbed emission, and rebuilt both LOD texture/normal bindings.

**624 / 208 LOD0 / LOD1 triangles**, budgets 1200 / 400, headroom 576 / 192. Housing then Lens; seven packed runtime maps; one final DirectX normal per slot. Both LODs have correctly authored UV0/FinishBake charts and normal bakes. Complete runner rebuild repeated with matching digests; both FBXs reimported successfully.

No remaining blocking Blender model/export defect identified. Producer appearance approval remains pending.

Round 4b: replaced grain-gated dashed edge wear with continuous irregular rubbing and sparse wider chips. Geometry, dimensions and UV layouts are unchanged.

[Audit](evidence/astra_check.md) · [Handoff](CLAUDE_HANDOFF.md) · [Day](review/ref_match_day.png) · [Night](review/ref_match_night.png) · [Review index](review/README.md) · [Verification](evidence/verification.json).

Claude owns Unreal/material/runtime integration after art acceptance. No editor work or producer approval is claimed. Recovery: archive/round03/.
