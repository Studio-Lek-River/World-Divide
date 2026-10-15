#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Buildings/RTSBuildPlacement.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Tests/RTSTestFlags.h"
#include "Units/RTSUnitShape.h"

/**
 * Distances the tests take from the game's data and the generated maps instead of writing them down, so a change of scale
 * or a retuned unit moves the tests' spots with it (WORLD_SCALE_STANDARD.md, #136): the size of a map, its obstacles, a
 * hull, a footprint, a weapon's range, how long a drive takes.
 */
namespace RTSTests
{
	/** Half the width of the square ground of a generated map: its NavMeshBoundsVolume covers exactly the ground. */
	inline float GetMapHalfSize(const UWorld& World)
	{
		float HalfSize = 0.f;
		for (TActorIterator<ANavMeshBoundsVolume> It(&World); It; ++It)
		{
			FVector Origin;
			FVector Extent;
			It->GetActorBounds(false, Origin, Extent);
			HalfSize = FMath::Max(HalfSize, static_cast<float>(FMath::Max(Extent.X, Extent.Y)));
		}
		return HalfSize;
	}

	/** One of a map's walls, blocks or other labelled static meshes, or null. */
	inline AStaticMeshActor* FindLabelledActor(const UWorld& World, const TCHAR* Label)
	{
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The ground a labelled actor covers, seen from above; an empty box when the map has no such actor. */
	inline FBox2D GetLabelledBox(const UWorld& World, const TCHAR* Label)
	{
		const AStaticMeshActor* Actor = FindLabelledActor(World, Label);
		if (!Actor)
		{
			return FBox2D(ForceInit);
		}
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(false, Origin, Extent);
		return FBox2D(FVector2D(Origin - Extent), FVector2D(Origin + Extent));
	}

	/** A unit's hull, length along X and width along Y, from its data alone. */
	inline FVector2D GetHullSize(const URTSUnitData& Data)
	{
		const FRTSUnitShape Shape = RTSUnitShape::FromData(Data);
		return FVector2D(2.f * Shape.GetHalfLength(), 2.f * Shape.GetHalfWidth());
	}

	/** Half a building's footprint, in cm. */
	inline FVector2D GetFootprintExtent(const URTSBuildingData& Data)
	{
		return FVector2D(RTSBuildPlacement::GetFootprintExtent(Data.Footprint));
	}

	/** The range of a unit's or building's weapon, or zero without one. */
	inline float GetRange(const URTSEntityData& Data)
	{
		const URTSWeaponData* Weapon = Data.Weapon;
		return Weapon ? Weapon->Range : 0.f;
	}

	/**
	 * Real seconds to wait for a unit at Speed (cm/s) to cover Distance: twice the drive, for detours, turns and starts, plus
	 * Slack game seconds, at the test run's time dilation, and never less than Minimum.
	 */
	inline double GetTravelTimeout(float Distance, float Speed, double Slack = 10.0, double Minimum = 30.0)
	{
		const double GameSeconds = 2.0 * Distance / FMath::Max(Speed, 1.f) + Slack;
		return FMath::Max(Minimum, GameSeconds / FMath::Max(GetTestTimeDilation(), 1.f));
	}
}

#endif
