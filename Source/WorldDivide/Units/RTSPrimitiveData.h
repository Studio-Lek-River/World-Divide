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
	/** M_Vehicle with TrackAnimation on: the phase through one link, and last frame's, unwrapped (ARTSUnit::UpdateRunningGear). */
	constexpr int32 TrackPhase = 0;
	constexpr int32 TrackPreviousPhase = 1;

	/** 2 and 3 are free. */

	/**
	 * M_ShotFx: a shot's glow, RGBA, emissive strength included (ARTSShotEffect; #227), from 0 to 3. M_ShotFx shares no
	 * mesh with the materials above, so it reuses their indices.
	 */
	constexpr int32 ShotGlow = 0;

	/** M_Vehicle and M_PlaceholderTeam: the team colour, RGBA (ARTSUnit::GetTeamColor), from 4 to 7. */
	constexpr int32 TeamColor = 4;
}
