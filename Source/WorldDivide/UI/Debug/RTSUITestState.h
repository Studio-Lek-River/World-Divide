#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class ARTSBuilding;
class ARTSPlayerController;

/**
 * The fixed match state the HUD screenshots show (rts.UI.Screenshot), so every panel has something in it: 12,500 credits,
 * low power, six units selected with a builder first (its build menu fills the command grid), a barracks with three
 * units queued and one attack warning. Everything stands in a ring around Center and the camera looks at it.
 */
namespace RTSUITestState
{
	constexpr int32 Credits = 12500;
	constexpr int32 SelectedUnits = 6;
	constexpr int32 QueuedItems = 3;

	/** The empty strip north of both bases on L_TestMap. */
	extern const FVector Center;

	/** The tag on every actor Apply spawned; a second Apply removes them first. */
	extern const FName SpawnedTag;

	/**
	 * Server only. Spawns the state for Controller's team from its faction's data and centres the camera on it. The caller
	 * pauses the game once the camera has settled, so the queue and the warning stay as they are. False, with OutError,
	 * when the faction lacks a builder, a power plant, a power-drawing building or a building that produces units.
	 */
	bool Apply(ARTSPlayerController& Controller, FString& OutError);

	/** The barracks Apply queued the units at, or null before Apply. */
	ARTSBuilding* FindQueueBuilding(const UWorld& World);
}

#endif
