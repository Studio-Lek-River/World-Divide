# 08_WarningBeacon — Round 4b

**Producer appearance approval:** "Approve" (2026-10-23), on the Round 4b sheets (day, night, LowPower). Claude integrated it in #393 (catalogue, setup, `DA_TestKitLighting`); the in-game close-up, night and LowPower shots are in [../ingame/](../ingame/README.md) (#392, #393).

Issue #393, 2026-10-23. Ready for producer review; not approved yet.

Only 08 was revised. Round 4 matched its delivery receipt with no interrupted edits found and is preserved in archive/round04/. 07, 09, 12 and 13 remain untouched; producer approval of 09/12/13 is unchanged.

Replaced the projected floating interior with actual holder/socket, rear bracket, bulb and concave reflector geometry in both LODs. Dome now uses thin amber transmission over that assembly, moulded vertical ribs, scratches/scuffs, dark grazing edges, bright reflections and a real lower lip. Night/LowPower retain a near-white/yellow filament against the warm curved mirror and amber shell. Collar seating shoulder 16.4 cm, widest dome lip 14.2 cm: 1.155×. Original external stem, base and bolts retained.

Class L: 2312/2500 triangles LOD0; 784/800 LOD1. Housing then Lens; seven runtime textures; one baked DirectX finish normal per slot. Both scoped runner builds have matching digests. See evidence/astra_check.md and evidence/delivery.json for the sealed evidence.

Review [day](review/reference_left_round04b_right_day.png), [night](review/reference_left_round04b_right_night.png), [LowPower](review/reference_left_round04b_right_lowpower.png), and [attachment angles](review/construction_angles_day.png). Material and rigid internal sweep integration are specified in CLAUDE_HANDOFF.md. Blender-only validation; no git or Unreal work.
