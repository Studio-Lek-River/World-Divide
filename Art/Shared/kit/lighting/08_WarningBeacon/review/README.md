# 08 WarningBeacon — Round 4b review

Producer reference is always LEFT; current Round 4b is RIGHT:

- [Day](reference_left_round04b_right_day.png), [night](reference_left_round04b_right_night.png), [LowPower 30%](reference_left_round04b_right_lowpower.png).
- [Physical attachment angles](construction_angles_day.png): top left front (+X), top right three-quarter, bottom left side (+Y), bottom right rear (−X). Separate construction_*_day.png files are larger per-view evidence. The real holder reaches the collar; the rear bracket intersects the reflector. The rear view correctly hides the bulb behind the actual mirror.
- pbr_day_off/on.png and pbr_night_off/on.png retain lit/unlit checks. lowpower_night.png is the native close review.
- astra_12m/150m/260m/450m_LOD0_left_LOD1_right.png and astra_distance_contact.png compare the two LODs. LOD1 is meant for the 260 m building transition, not close use; its lower circumferential sampling is visible when enlarged at 12 m.
- Native distance_* and switch_260m_* frames: 1920×1080, horizontal FOV 60°, no compositor bloom. Stable 150/260 m frames use a 128×128 centre render region at native resolution, 512 fixed samples, no denoising/adaptive sampling. Enlarged sheets do not establish native pixel size.

Cycles / AgX. Close review includes modest Fog Glow (threshold 1.5, strength .45, size .4) and two vertical reflection cards to show the glass surface. Day-on LampLevel=.05, night=1, LowPower=.3, off=0. Emission/core shading and true internal geometry are identical across all these views; no image of the interior is projected onto the dome. The reference is red-orange; delivery remains amber as required by the brief.

Round 4 comparisons in this folder are historical and retain their original images; archive/round04/ holds the complete old delivery. Current source is ../source/WarningBeacon.blend; ../source/WarningBeacon_Preview.blend adds review lighting/context only. See ../evidence/astra_check.md and ../CLAUDE_HANDOFF.md. Producer acceptance and Unreal transparency/rotation checks remain pending.
