# Vehicle track animation (#151)

The M20's tracks move their links with the vehicle's realized motion: each side by its own travel, forward, back, on an arc or opposite ways in a pivot, and not at all while the hull stands or is blocked. Locomotion drives the animation; the animation never moves anything (#138).

## 1. How it works

| Layer | Where | What it does |
|---|---|---|
| Mesh | `Tools/Art/m20_guardian_blender.py`, `m20_guardian_bake.py` | Writes four extra UV channels on `SM_M20_Track_L/R`: per vertex, where the same vertex sits in the neighbouring links. Geometry, normals and UV0 are unchanged. |
| Material | `M_Vehicle`, static switch `TrackAnimation` (`Tools/Editor/vehicle_art.py`) | World Position Offset moves every vertex part of the way to its place in the next link, by a phase it works out from the scene's time and a phase, link rate, start time and acceleration in custom primitive data (#216). The normal turns with the link. |
| Phase | `ARTSUnit::UpdateRunningGear`, run by `URTSRunningGearSubsystem` | Adds each side's realized travel since the last frame, divided by the link pitch, to that track's phase, and sends the material a new phase and rate only when its own has drifted from it. |

The tread repeats every link, so a track looks the same after one link of travel as before it. The phase therefore only counts through one link, `[0, 1)`, and wraps without a jump: at phase 1 every link stands exactly where the next one stood at phase 0.

## 2. The mesh: link-to-link channels

`build_track` places the same link (`track_link`) 85 times along the track's centre line, in order, at a pitch of 15.99 cm. Every link has the same 144 vertices in the same order, so vertex `k` of link `i + 1` is vertex `k` of link `i` carried one pitch along the loop.

`m20_guardian_bake.add_track_animation_uvs` writes, per vertex, in local cm in the track's X-Z plane (Y does not change along the loop):

| Channel | Name in Blender | Value |
|---|---|---|
| UV0 | `UVMap` | The texture atlas, unchanged. |
| UV1 | `TrackPrev` | Offset to the vertex's place in the previous link. |
| UV2 | `TrackNext` | Offset to its place in the next link. |
| UV3 | `TrackNext2` | Offset to its place in the link after that. |
| UV4.x | `TrackTurn` | The turn in radians from this link to the next, counter-clockwise from +X toward +Z. |

- **V is written flipped** (`1 − value`). Unreal's FBX import flips V, so the values arrive as they are meant. `WorldDivide.Art.M20Tracks` reads them back and checks them: on a straight run the previous and next offsets are opposite and one pitch long.
- **The bake asserts that each link is a rigid copy of the one before** (largest error 0.0000 cm), the condition for one link's place to stand for the next.
- **`--tracks-only`** opens the saved `.blend`, writes the channels, checks them and exports only the two track FBX. Textures, renders and every other part stay as they are. A full run of the script writes the same channels.

Re-import: `setup_testmap.py` re-imports the tracks when their FBX changes. Regeneration is idempotent: a second setup writes nothing.

## 3. The material

`build_track_animation` adds a `TrackAnimation` static switch to `M_Vehicle`, off by default. `MI_M20_Track` turns it on; the hull, turret and running-gear instances keep the plain material.

- **World Position Offset** is a Catmull-Rom curve through the vertex's four places: previous link, its own (0), next and next-but-one. It is 0 at phase 0 and reaches the next link's place at phase 1. The offset is local, so it goes through the local-to-world transform with the unit's scale and turn.
- **The phase** (#216) is `Phase + s × (Rate + ½ × Acceleration × s)`, with `s` the scene's time (`View.GameTime`, the world's seconds) since `Start`, through one link (`TRACK_PHASE_HLSL`). Custom primitive data 0 to 3 hold `TrackPhase`, `TrackRate` (links per second), `TrackStart` (world seconds) and `TrackAcceleration` (links per second squared).
- **The previous frame's offset** uses the same curve at `View.PrevFrameGameTime`, unwrapped against the phase now, through `PreviousFrameSwitch`. Motion vectors and temporal anti-aliasing therefore see the links move, and a track without rate shows none.
- **The normal map's normal turns** in the X-Z plane by phase × `TrackTurn`. Without that, a link rounding the sprocket would keep its old shading and snap 25° when the phase wraps.

Checked against an exact rigid shift of every link along the centre line (`bake.check_track_animation`, written to `report_stats.md`):

| | Largest, over 33 phases through one link |
|---|---|
| Vertex away from its rigid place | 0.567 cm |
| Gap between neighbouring links, against the rigid shift | 0.590 cm |
| Jump where the phase wraps | 0.0000 cm |
| Normal's turn against the link's | 1.73° |

A straight linear shift would have pulled the links up to about 1 cm inward on the sprocket and the idler, and their normals would have snapped. The curve keeps every vertex within 0.6 cm.

`MI_M20_Track_Static` is the same instance without the switch: rigid tracks.

## 4. The phase: `UpdateRunningGear`

- **Tracks are data.** `FRTSVehiclePart::TrackPitch` (cm, before `MeshScale`) makes a hull part a track. `Offset.Y` says its side: negative left. `RigidMaterial` is its material when it does not animate. `CreateParts` makes such a part an `FTrack` and registers the unit with `URTSRunningGearSubsystem`, as it does for wheels.
- **Travel.** For a tracked unit, the change of `FRTSRealizedMotion::LeftTravel` or `RightTravel` since the last call. Another unit with a track part uses the drive and turn of its pose, as its wheels do.
- **Phase.** `Phase = Frac(Phase − Frac(travel / pitch))`. Forward travel lowers it: the bottom of the loop runs backward under the hull while the hull drives forward. The previous phase, unwrapped, is `Phase + travel / pitch`.
- **Rate.** The track's rate is the frame's travel over the frame's world seconds, in links per second, and its acceleration the change of that rate since the frame before.
- **To the material (#216).** One `SetCustomPrimitiveDataVector4(0, (Phase, Rate, Start, Acceleration))` (`RTSPrimitiveData::TrackPhase`; the team colour sits beside it at 4 to 7 since #150) only when the material's phase is off:
  - rolling, when the phase the material works out has drifted more than `ARTSUnit::TrackDriftLimit` (0.02 of a link, about 3 mm on the M20) from the travel's, or its rate is off by more than `TrackRateLimit` (a quarter link a frame, before a drift could wrap past half a link and look small). It gets the phase as it is, the rate half a frame of acceleration past the last frame's, and the acceleration;
  - stopped or blocked, once, with the phase exactly and no rate or acceleration, so the links stand without smearing.

  The links can jump by the drift limit when a phase is sent, too little to see. A steady drive, a constant acceleration or braking send almost nothing: about 3 % of track frames while accelerating, none cruising (`WorldDivide.Units.RunningGear.TrackRate`). This is a primitive-data update in the scene's update queue: no render-state rebuild, no component move, no transform propagation. Nothing is written while the track stands.
- **No new tick.** The phase is advanced in the existing `URTSRunningGearSubsystem::PlaceAll` pass (#165), after every hull move.

**The wheels go by the same numbers.** A tracked unit's road wheels, sprocket and idler now roll by their side's realized travel, the same value the track's phase is advanced by. Each wheel uses its own rolling radius, `FRTSVehiclePart::RollRadius`, which `m20_guardian_blender.running_layout` writes to `m20_guardian.json`:

| Wheel | Rolling radius | Why | Before (half the mesh's height) |
|---|---|---|---|
| Road wheel | 34 cm | Its tyre rolls on the track's inner face. | 34 cm |
| Sprocket | 36 cm | The track's centre line wraps it. | 34.5 cm, about 4 % too fast |
| Idler | 32 cm | Likewise. | 27 cm, about 18 % too fast |

## 5. Multiplayer

- **Nothing new replicates.** Every machine animates from its own `FRTSRealizedMotion`. The server has it from its simulation, clients from the replicated speed and yaw rate (#138 §7).
- **Corrections, teleports and depenetration add no travel**, so they never spin a track.
- **A unit that becomes relevant again** is a new actor on the client. Its phase starts from zero, which nobody sees, since the unit was not drawn.
- **A dedicated server sends nothing to materials.** `FRTSTrackViews::bDraws` is false there.

## 6. Distance and LOD

| Setting | Default | Effect |
|---|---|---|
| `rts.Vehicle.TrackAnimation` | 1 | 0 draws the tracks with `RigidMaterial` and keeps no phase. Read when a unit makes its parts. |
| `rts.Vehicle.TrackAnimationDistance` | 6000 cm | The tracks' `WorldPositionOffsetDisableDistance`: past it, the renderer leaves the link shift out. 0 never does. Read when a unit makes its parts. |

- **The CPU keeps counting the phase** at every distance. It only sends the phase to a track within 1.25 × the distance + 10 m of a local player's camera and its view's cone (`FRTSTrackViews`, #212). A track that comes back within range has drifted, so it sends its phase on the first frame. The links therefore come back where they should be, not where they were left.
- **The engine's switch of the shift itself happens at its own distance.** At that moment the tread can shift by part of a link. At 60 m a link is about 2 px high at 1080p.
- **The RTS camera's default zoom is 150 m.** At that zoom no track animates. The close zooms and the inspection views do.
- **LODs (#149).** LOD0 and LOD1 (the links reduced to 2,400 triangles, with all five UV channels kept) animate with `MI_M20_Track`. LOD2 and LOD3 are the belts of `m20_guardian_blender.py --lods`, in their own slot with `MI_M20_Track_Static`, so they never run the shift. LOD1 starts at 35 m and LOD2 at 110 m. Past 60 m the shift is already off, so the switch to the belt at 110 m has no phase to lose; the one tread jump stays at the 60 m cut-off, as before.
- **Is that jump visible in play?** At the default zoom every tank is 128 to 204 m from the camera, so no track animates and nothing jumps. It only happens while zooming through about 60 m with a tank driving: half a link on average, about 2 px at 1080p, in the frame the zoom passes it. #149 leaves the distance at 6000 cm.
- **Bounds and shadows.** A link moves at most one pitch along the closed loop, so the loop's outline, and with it the bounds, stays where it is, within 0.6 cm. Shadow depth passes run the same World Position Offset, so the shadows move with the links.

## 7. Tests and captures

| Test | Covers |
|---|---|
| `WorldDivide.Units.RunningGear.Spin` | A tracked hull driven by its gear:<br>- forward, reverse, a gentle and a sharp turn, both pivots: each track's phase follows its side's realized travel within 1 % of a link, and its sprocket within 1°;<br>- straight travel is equal on both sides, a pivot equal and opposite;<br>- accelerating and braking change the step frame by frame;<br>- standing and blocked for 5 s, the tracks stand;<br>- a teleport and a solid it is put in move no track;<br>- the material gets the phase only when drawn, and resumes where the phase got to;<br>- `rts.Vehicle.TrackAnimation 0` keeps no phase and draws the rigid material. |
| `WorldDivide.Units.RunningGear.TrackRate` | #216: accelerating, cruising, braking, reversing and pivoting, the phase the material works out from its custom primitive data stays within the drift limit of the travel's in every frame. A steady drive and an acceleration send in at most one track frame of twenty, standing sends nothing, and a stopped track has its phase exactly without a rate. Forward counts down, reverse up, a pivot both ways. |
| `WorldDivide.Art.M20Tracks` | The track meshes carry UV0 and the four channels, read as the material reads them. The instances have the switch where they should. The parts carry the pitch, the rigid material and the wheels' rolling radii. |
| `WorldDivide.Net.Vehicle.ListenServer`, `.Dedicated` | Each client's tracks follow that client's own travel through a pivot, within 1 % of a link. A correction of the client's copy spins neither track. The dedicated session runs at 100 ms round trip and about 2 % loss. |

**Close-up captures.** `ue.ps1 -Task trackshots -Unit DA_TestM20Guardian` writes 12 frames of each case to `Saved\TrackShots\<unit>`, from a camera carried with the hull:
- forward, reverse, both turns, both pivots, accelerating, braking, stopped, blocked;
- the wraps round the sprocket and the idler;
- from 90 m, and back near.

The game is slowed while the frames are taken, so the links move by a fraction of a link between frames. The log gives, with each frame, the realized travel and each track's phase next to what its material has. `-Cvars rts.Vehicle.TrackAnimation=0` captures the same cases with rigid tracks, for comparison.

## 8. Performance

Measured in [PERFORMANCE_RESULTS.md, #151](PERFORMANCE_RESULTS.md#151-m20-track-animation): 240 moving M20s, rigid against animated, long method.

| At 240 moving M20s | Change |
|---|---|
| Running-gear CPU | +0.015 ms mean, no change at p95. |
| GPU | No measurable change. |
| Draw calls, triangles | None. |
| Locomotion (#165) | Unchanged. |
| Worst case: every track animated, regardless of distance | Running gear +0.015 ms mean and +0.06 ms p95 since #216 (+0.12 / +0.42 before, from a primitive-data update per track and frame). The GPU is unchanged. |

`ue.ps1 -Task bench -Unit DA_TestM20Guardian@RigidTracks` benches rigid tracks: the set runs with `rts.Vehicle.TrackAnimation 0`. `-Cvars` passes console variables to a diagnostic bench run.

## 9. Known limitations

- **The tread pattern can alias.** At 400 cm/s and 60 frames a second the links move 0.42 of a link a frame. Below 48 frames a second at top speed they move more than half a link, and the eye can read the tread as running backward, as with any repeating pattern. Real tracks do the same on film.
- **The sprocket's teeth do not match the links.** It has 11 teeth on a circle that holds about 14 links. The teeth no longer slide against the links, but their spacing does not match. Fixing it needs a new sprocket mesh, which is outside #151.
- **The mesh's own gaps stay.** Light shows between the shoes, where the pads and end connectors leave room, rigid or animated. The animation adds at most 0.6 cm to a gap.
- **No sag, no track tension, no suspension travel.** The loop is the fixed outline of the wheels as built.
- **Late joiners and units back in relevancy** start their phase at zero, unseen.
- **The scene's time is a float in the shader.** After hours of play its steps grow (2 ms after about 4.5 hours), and a track at top speed between two sends steps by up to a tenth of a link with it. Every time-driven material in the engine shares this.
