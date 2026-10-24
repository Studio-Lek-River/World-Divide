#pragma once

#include "CoreMinimal.h"

/**
 * The custom primitive data the game's materials read (#150, #151): one value per primitive component, sent to the
 * renderer as an update of the primitive's data, without a render state rebuild. Components that share a mesh and a
 * material instance then still draw together, each with its own values, where a dynamic material instance per component
 * would keep every one of them in a draw of its own.
 *
 * The indices are floats; a vector parameter reads four from its index. Tools/Editor/vehicle_art.py, placeholder_art.py
 * and setup_testmap.py build the materials with the same numbers.
 */
namespace RTSPrimitiveData
{
	/**
	 * M_Vehicle with TrackAnimation on (#216): the phase through one link at a start time, the links per second it counts on
	 * by from there, that start time in world seconds, and the links per second squared the rate changes by; the material
	 * works out the phase, and last frame's, from the scene's time (ARTSUnit::UpdateRunningGear).
	 */
	constexpr int32 TrackPhase = 0;
	constexpr int32 TrackRate = 1;
	constexpr int32 TrackStart = 2;
	constexpr int32 TrackAcceleration = 3;

	/**
	 * M_ShotFx: a shot's glow, RGBA, emissive strength included (ARTSShotEffect; #227), from 0 to 3. M_ShotFx shares no
	 * mesh with the materials above, so it reuses their indices.
	 */
	constexpr int32 ShotGlow = 0;

	/** M_Vehicle and M_PlaceholderTeam: the team colour, RGBA (ARTSUnit::GetTeamColor), from 4 to 7. */
	constexpr int32 TeamColor = 4;

	/**
	 * M_VehicleLamp: 1 while the lamps are on in the dark, 0 by day (URTSVehicleLightSubsystem; #223), at 8. Set on every part
	 * of a vehicle with lights, as the team colour is.
	 */
	constexpr int32 LampOn = 8;

	/**
	 * M_Bld_Operational's lighting groups (#195): 1 while the group glows, 0 switched off (ARTSBuilding::SetLightGroupOn,
	 * for the state controller, #196), 1 when unset; each instance belongs to one group or none. Night is 1 in the dark,
	 * when the glow rises to the instance's NightGain (URTSBuildingLightSubsystem).
	 */
	constexpr int32 LightWindows = 9;
	constexpr int32 LightFixtures = 10;
	constexpr int32 LightAccents = 11;
	constexpr int32 LightNight = 12;
}

/**
 * M_Kit_Lens's per-instance custom data (#387, #388): the floats each kit lens instance carries in its instanced component
 * (SetNumCustomDataFloats(Floats) before the instances are added; SetCustomData per instance, then one
 * MarkRenderInstancesDirty). The lens also reads the building's custom primitive data above: Operational (LampOn),
 * the Fixtures group (LightFixtures) and the dark (LightNight). Tools/Editor/building_art.py builds it with the same numbers.
 */
namespace RTSKitLensData
{
	/** How bright the lens glows, 0 off to 1 full. */
	constexpr int32 Level = 0;
	/** 1 when the building's Fixtures group switches the lens, 0 when it ignores the group. */
	constexpr int32 Group = 1;
	/** A fault's flicker phase, 0 for no fault. */
	constexpr int32 Fault = 2;
	/** A flashing lens's phase, 0 for a steady one. */
	constexpr int32 Flash = 3;
	/** The glow's colour, RGB, from 4 to 6. */
	constexpr int32 Color = 4;
	/**
	 * A turning beacon's switch times in game seconds (#459, RTSKit::GetSpin): when it last came on and last went off, off
	 * while SpinOff >= SpinOn. Its SWEEP turns and fades with them; 0 and 0 for every other lens.
	 */
	constexpr int32 SpinOn = 7;
	constexpr int32 SpinOff = 8;
	constexpr int32 Floats = 9;
}

/**
 * M_Kit_Glow's per-instance custom data (#440): each kit beam and glare halo instance, lit with its part's look
 * (ARTSBuilding::SetKitLooks). Tools/Editor/building_art.py builds it with the same numbers.
 */
namespace RTSKitGlowData
{
	/** How bright it glows, 0 off to 1 full: its part's look. */
	constexpr int32 Level = 0;
	/** Its part's fault and flash phases, as RTSKitLensData's, so it goes out with its lens. */
	constexpr int32 Fault = 1;
	constexpr int32 Flash = 2;
	/** Its colour at full, strength included, RGB, from 3 to 5. */
	constexpr int32 Color = 3;
	/** A turning beacon's (#459): its turns a second, 0 for every other glow, and its part's switch times (RTSKitLensData). */
	constexpr int32 Turn = 6;
	constexpr int32 SpinOn = 7;
	constexpr int32 SpinOff = 8;
	constexpr int32 Floats = 9;
}
