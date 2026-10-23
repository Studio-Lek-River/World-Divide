# 12_MastBeaconSingle — Round 4 review

Current review set; producer approval pending. All images are Blender Cycles, not Unreal.

Tip-mounted reference variant preserves the collar, lugs, flared base and short stem. The day view retains the LED carrier with darker red edges and stronger skirt rings. Night and 30% LowPower remain red with a compact warm core and smooth local halo. The 12 m mesh reads as a small fixture; at 150/450 m it becomes a red signal cluster. Mapped optics do not reproduce refractive depth.

- [Reference LEFT / Round 4 RIGHT — day](reference_left_round04_right_day.png), [night](reference_left_round04_right_night.png). 07 uses the 04 family-style reference; 09 night uses its blue emission-tint view.
- [Reference camera day](ref_match_day.png), [night](ref_match_night.png); pbr_day_on/off and pbr_night_on/off show lit/unlit context.
- [12 m](astra_12m_LOD0_left_LOD1_right.png), [150 m](astra_150m_LOD0_left_LOD1_right.png), [260 m](astra_260m_LOD0_left_LOD1_right.png), [450 m](astra_450m_LOD0_left_LOD1_right.png).
- [Enlarged LOD pair](rts_LOD0_left_LOD1_right.png). Both LODs are forced at the same distance for diagnosis; normal LOD0 use extends to the 260 m building-controlled switch.
- [LowPower, 30%](lowpower_night.png).

Native distance frames are 1920 × 1080 at 60° horizontal FOV, 128 Cycles samples without denoising. The 12 m astra sheet enlarges a 256 × 256 centre crop 2×; far sheets enlarge a 48 × 48 crop 8×. Stable 150/260 m pairs use 512 fixed samples, no adaptive sampling or denoising, with only the centre 128 × 128 region rendered at native pixel scale. Enlargements are diagnostic and are not the on-screen fixture size. No compositor bloom is added.

Opaque mapped glass has no true transmission/parallax. Measurable subpixel shading/brightness differences remain at the forced LOD switch; strict no-pop acceptance is still a producer/runtime review item.

See [independent check](../evidence/astra_check.md), [technical measurements](../evidence/astra_technical.json), [distance measurements](../evidence/astra_distance_comparison.json), and [handoff](../CLAUDE_HANDOFF.md). Round 3 and its generator snapshots are preserved in archive/round03/.
