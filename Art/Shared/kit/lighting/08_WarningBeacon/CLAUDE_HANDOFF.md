# 08_WarningBeacon — Round 4b handoff

Issue #393, 2026-10-23. Supersedes Round 4 for 08 only. Producer review of 08 remains pending. The producer approved 09, 12 and 13; those and 07 were not changed. No git or Unreal work.

## Delivery and construction

- Clean editable source: source/WarningBeacon.blend, exactly two LOD mesh objects and seven packed runtime images. Preview scene: source/WarningBeacon_Preview.blend, review context only.
- Runner FBXs: SM_Kit_WarningBeacon.fbx and SM_Kit_WarningBeacon_LOD1.fbx. Class L, 2312/2500 and 784/800 triangles; headroom 188/16.
- Overall 18 × 18 × 32 cm; original base plate, external stem, bolts and two-band construction retained. Collar top shoulder is now 16.4 cm wide, against 14 cm main dome / 14.2 cm moulded lower lip: 1.155× at the widest glass. The top seating shoulder grows from Round 4's 15.8 cm to 16.4 cm; the outer bands are adjusted to the same 16.4 cm target.
- The floating painted interior is removed. Housing contains a solid holder/socket from z=.178 to .232 m and a rear bracket from .178 to .238 m. Both intersect the collar top at .180. Lens contains a real bulb from .231 to .283 m and a concave 9.6 cm reflector facing +X. The bulb overlaps its socket by 1 mm; the bracket intersects the reflector's lower meridian. Both LODs retain every component. See evidence/construction_round04b.json and review/construction_angles_day.png.
- Dome: actual rounded shell with a slightly flared lower lip; vertical moulded ribs and scratches/scuffs in the finish normal and roughness. No photographic or projected interior remains. The mirror is an intentionally open, two-sided optical sheet; the dome is an intentionally open thin shell seated on the collar.

Blender metres, exported centimetres, Z up, +X forward; origin at underside centre of the base. Retain the Point specification: 80 cd, 2200 K amber, attenuation 8 m, anchor (0,0,.25) m. Blender review lights are illustrative, not a candela calibration.

## Material contract — changed from Round 4

Exactly Housing then Lens. Housing has 2048² BaseColor, ORM and Finish_Normal. Lens has 1024² BaseColor, ORM, Finish_Normal and 16-bit RGB LightProfile. BaseColor is sRGB; all others linear. ORM remains occlusion / roughness / metallic. Mirror metallic=1; glass and bulb metallic=0. No additional runtime maps.

Use ONE delivered DirectX Finish_Normal per slot, sampled on FinishBake / TexCoord 1 with UVMap / TexCoord 0 tangents. No normal stacking, no Unreal green flip, and do not replace UV1 with generated lightmap UVs. The Blender graph inverts green only for Blender display. Each LOD owns distinct, non-overlapping atlas charts. Preserve mipmaps; no Nanite or fixture shadows. The owning building controls the 260 m LOD decision.

LightProfile now has distinct channels: **R = radiance profile, G = thin-shell mask (1 dome, 0 bulb/mirror), B = reserved zero. Do not use RGB luminance as emission.**

Let p=R; hot=saturate((p-.55)/.35). Linear Tint=[1,.22,.001], HotCoreTint=[1,.92,.48]. Emission=p × LampLevel × 24 × lerp(Tint,HotCoreTint,hot). LampLevel off=0, night=1, LowPower=.3; review day-on=.05. R includes a low amber scatter field on the shell, reflected warm radiance on the curved mirror, and a compact bright filament field on the actual bulb surface. The core remains distinct at LowPower.

The source graph is the authoritative material reference. Its opaque optical BSDF uses ORM, the single finish normal, IOR 1.49, coat weight .3, coat roughness .035 and specular IOR level .5. For the dome only, multiply its diffuse BaseColor by .18. Mix that BSDF with tinted transparent transmission using T=G × .91 × (1-Fresnel(1.49)); transmission colour=pow(linear BaseColor,.45). The bulb and mirror have G=0 and remain opaque. This is a thin-wall transmission approximation, **not a solid refractive-volume simulation**. Parallax and occlusion come from real geometry, not sliding imagery. Claude must reproduce the thin-shell/opaque-interior distinction in the Lens material and check two-sided transparency/sorting in Unreal; the old opaque Lens shader is no longer correct.

## Sweep / rotation

The outer dome is still one continuous angular UV island: U starts at +X and increases toward +Y. Per-LOD U0/U_width/V0/V_height are measured in evidence/verification.json → lens_uv_contract. Other Lens islands now belong to real bulb/mirror geometry: **do not wrap the whole atlas or slide their maps around the dome.**

Keep the existing shared phase / starting rate of 1.5 Hz, but rotate the internal assembly rigidly about the local Z axis through (0,0,.180) using material vertex displacement. Lens: rotate G<.5 (bulb and mirror), leave G=1 shell fixed. Housing: rotate vertices with z>=.177 m and radial distance<.055 m (holder and bracket); leave the collar, external stem and base fixed. Use the same phase in both slots and LODs. Sample the Lens mask at mip 0 for vertex displacement. In Unreal centimetres, the Housing mask is z>=17.7 and radius<5.5; the rotation pivot is (0,0,18). The mask includes the complete bracket foot, so it cannot shear away from its base. The point light anchor is on-axis and stays fixed. This preserves material-driven rotation with two slots and no additional moving mesh objects. This integration is documented, not implemented or tested in Unreal.

## Evidence and reproduction

Round 4 was verified against its delivery hashes before editing, with no half-finished edits found, then copied to archive/round04/ including generator snapshots. Current reviews: reference_left_round04b_right_day/night/lowpower.png; pbr lit/unlit views; construction angle sheet; 12/150/260/450 m LOD sheets. Close views include restrained Fog Glow and two vertical review-only reflection cards. Native distance frames and the stable 512-sample comparison disable compositor bloom; do not confuse cropped/enlarged sheets with native resolution.

Use Blender 5.1 in background mode, --python-exit-code 1, bytecode disabled:

1. Tools/Art/kit/warning_beacon_review.py -- prepare
2. Art/Shared/kit/lighting/_work_393/run_pieces.py -- 08_WarningBeacon (twice; matching digests in evidence/rebuild_determinism.json)
3. warning_beacon_review.py -- verify, then -- render, then -- stable, then -- sheets (separate invocations)
4. warning_beacon_angles.py; warning_beacon_check.py -- check and -- sheets
5. warning_beacon_review.py -- audit after documentation is complete.

All new warning_beacon_* helpers are restricted to 08. Shared lighting_393_* and #392 helpers were not edited. Legacy optical master files under source/ are retained historical authoring material and are no longer sampled by the generator or runtime textures. See evidence/astra_check.md, verification.json, astra_technical.json and delivery.json for validation and limits. Producer approval and Unreal material/transparency/rotation evaluation remain outstanding.
