# 06_RoofFloodLight — current state: Round 4 final check

**Producer appearance approval — 2026-10-23:** "Yes they are all approved", given per piece on the Round 4b comparison sheets (reference, day, night, LOD0/LOD1). This approves the current model and finish; production integration and runtime validation are Claude's (#392).

**In game — 2026-10-24:** close-up (12 m) and RTS (150/450 m) shots, day and night, on `DA_TestKitLighting`: [../ingame/](../ingame/README.md) (#392).


2026-10-23, issue #392. Independent Blender/export check complete; producer approval given 2026-10-23 (see above).

Replaced sparse triangular fin spikes with sixteen 3 mm thick rectangular fins on EACH head in BOTH LODs. Removed buried duplicate LED planes; the clear-coated covers carry the three fields, with physical front separators retained. Simplified concealed LOD1 rail faces, bolt backs and cable sections to stay at 800 triangles. Preserved emitting-face winding and restored the producer-matched neutral-white display tint for the specified 4000 K floodlight.

**2288 / 800 LOD0 / LOD1 triangles**, budgets 2500 / 800, headroom 212 / 0. Housing then Lens; seven packed runtime maps; one final DirectX normal per slot. Both LODs have correctly authored UV0/FinishBake charts and normal bakes. Complete runner rebuild repeated with matching digests; both FBXs reimported successfully.

OPEN: dimensional authority for 06. The appearance table’s approximate 80 × 40 × 75 cm envelope conflicts with the detailed head/base dimensions. This delivery retains the detailed brief and measures 82.4 × 27.9 × 70.9 cm (width/depth/height). A producer clarification was requested; no answer or approval is assumed. Do not stretch the export silently.

Round 4b: replaced grain-gated dashed edge wear with continuous irregular rubbing and sparse wider chips. Geometry, dimensions and UV layouts are unchanged. Restored the reference-matched neutral-white 4000 K appearance with linear emission tint [0.89,0.94,1]; Lens BaseColor and achromatic LightProfile remain unchanged.

[Audit](evidence/astra_check.md) · [Handoff](CLAUDE_HANDOFF.md) · [Day](review/ref_match_day.png) · [Night](review/ref_match_night.png) · [Review index](review/README.md) · [Verification](evidence/verification.json).

Claude owns Unreal/material/runtime integration after art acceptance. No editor work or producer approval is claimed. Recovery: archive/round03/.
