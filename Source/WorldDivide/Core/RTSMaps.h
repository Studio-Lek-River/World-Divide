#pragma once

#include "CoreMinimal.h"

class APlayerStart;
class UWorld;

/** What a level says about itself as a map: its start spots and the area the lobby's preview and the minimap show (#289). */
namespace RTSMaps
{
	/** The start spot of a PlayerStart tagged "Team<N>", or INDEX_NONE for any other tag. */
	WORLDDIVIDE_API int32 ParseTeamTag(FName Tag);

	/** "Team<SpotIndex>", the tag the game mode looks for. */
	WORLDDIVIDE_API FName MakeTeamTag(int32 SpotIndex);

	/**
	 * The Team<N> PlayerStarts (ARTSStartSpot or plain tagged ones) of a level by N, the first of each N. Reads the persistent level, so a
	 * level that is only loaded, not played, works too. Each spot index used more than once, and each index below the highest that no
	 * PlayerStart uses, goes to OutProblems: Team0..TeamN-1 must all exist once.
	 */
	WORLDDIVIDE_API TMap<int32, const APlayerStart*> FindStartSpots(const UWorld& World, TArray<FString>* OutProblems = nullptr);

	/**
	 * The world XY square a level's preview covers: the bounds of its NavMeshBoundsVolumes (the area the camera pans over),
	 * else of its Landscape, squared around their centre. Reads the persistent level, as FindStartSpots. Invalid when the level
	 * has neither.
	 */
	WORLDDIVIDE_API FBox2D FindMapArea(const UWorld& World);
}
