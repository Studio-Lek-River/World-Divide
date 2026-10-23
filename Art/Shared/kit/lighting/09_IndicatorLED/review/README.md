# 09_IndicatorLED — Round 4 review

Current review set; producer approval pending. All images are Blender Cycles, not Unreal.

The box chamfer, plug, front screws and ribbed underside match the reference construction. Night review lighting now reveals worn steel; maps remain unchanged and neutral. The elevated RTS camera sees the top of the housing and occludes the downward-facing diffuser; 150/450 m elevated views therefore do not show a bright indicator. The reference-angle blue/red/green views demonstrate the actual emitting face.

- [Reference LEFT / Round 4 RIGHT — day](reference_left_round04_right_day.png), [night](reference_left_round04_right_night.png). 07 uses the 04 family-style reference; 09 night uses its blue emission-tint view.
- [Reference camera day](ref_match_day.png), [night](ref_match_night.png); pbr_day_on/off and pbr_night_on/off show lit/unlit context.
- [12 m](astra_12m_LOD0_only.png), [150 m](astra_150m_LOD0_only.png), [260 m](astra_260m_LOD0_only.png), [450 m](astra_450m_LOD0_only.png).
- [Blue](tint_blue.png), [red](tint_red.png), [green](tint_green.png): emission tint only; achromatic BaseColor and LightProfile. Class S has no LOD1.

Native distance frames are 1920 × 1080 at 60° horizontal FOV, 128 Cycles samples without denoising. The 12 m astra sheet enlarges a 256 × 256 centre crop 2×; far sheets enlarge a 48 × 48 crop 8×. Stable 150/260 m pairs use 512 fixed samples, no adaptive sampling or denoising, with only the centre 128 × 128 region rendered at native pixel scale. Enlargements are diagnostic and are not the on-screen fixture size. No compositor bloom is added.

The underside lens is physically hidden from the elevated RTS test camera. No artificial upper emitting surface was added. The product-angle tint views, not the top view, establish optical appearance.

See [independent check](../evidence/astra_check.md), [technical measurements](../evidence/astra_technical.json), [distance measurements](../evidence/astra_distance_comparison.json), and [handoff](../CLAUDE_HANDOFF.md). Round 3 and its generator snapshots are preserved in archive/round03/.
