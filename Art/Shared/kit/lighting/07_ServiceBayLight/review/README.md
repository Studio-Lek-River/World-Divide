# 07_ServiceBayLight — Round 4 review

Current review set; producer approval pending. All images are Blender Cycles, not Unreal.

Spec-led panel: recessed depth, real enamel returns and trim/latches read clearly. The 04 image establishes the metal/diffuser family, not this panel silhouette. At 12 m the frame remains readable; at 150 m the lit rectangle persists and at 450 m it reduces to a small pale signal.

- [Reference LEFT / Round 4 RIGHT — day](reference_left_round04_right_day.png), [night](reference_left_round04_right_night.png). 07 uses the 04 family-style reference; 09 night uses its blue emission-tint view.
- [Reference camera day](ref_match_day.png), [night](ref_match_night.png); pbr_day_on/off and pbr_night_on/off show lit/unlit context.
- [12 m](astra_12m_LOD0_left_LOD1_right.png), [150 m](astra_150m_LOD0_left_LOD1_right.png), [260 m](astra_260m_LOD0_left_LOD1_right.png), [450 m](astra_450m_LOD0_left_LOD1_right.png).
- [Enlarged LOD pair](rts_LOD0_left_LOD1_right.png). Both LODs are forced at the same distance for diagnosis; normal LOD0 use extends to the 260 m building-controlled switch.

Native distance frames are 1920 × 1080 at 60° horizontal FOV, 128 Cycles samples without denoising. The 12 m astra sheet enlarges a 256 × 256 centre crop 2×; far sheets enlarge a 48 × 48 crop 8×. Stable 150/260 m pairs use 512 fixed samples, no adaptive sampling or denoising, with only the centre 128 × 128 region rendered at native pixel scale. Enlargements are diagnostic and are not the on-screen fixture size. No compositor bloom is added.

No producer image for the panel itself; appearance remains spec-led. Blender camera evidence is not Unreal/gameplay acceptance.

See [independent check](../evidence/astra_check.md), [technical measurements](../evidence/astra_technical.json), [distance measurements](../evidence/astra_distance_comparison.json), and [handoff](../CLAUDE_HANDOFF.md). Round 3 and its generator snapshots are preserved in archive/round03/.
