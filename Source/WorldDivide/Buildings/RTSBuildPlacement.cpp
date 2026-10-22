#include "Buildings/RTSBuildPlacement.h"

#include "Buildings/RTSBuilderComponent.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

namespace
{
	/** How far a footprint point may lie from the navmesh. Covers the agent-radius margin around existing buildings. */
	constexpr float NavTolerance = 75.f;
	constexpr float NavHeightTolerance = 250.f;

	/** How far a unit's spawn spot at the exit may lie from its navmesh. */
	constexpr float ExitTolerance = 25.f;

	/** The overlap box starts this far above the ground so the ground itself never counts. */
	constexpr float GroundClearance = 20.f;
	constexpr float OverlapHeight = 200.f;
}

namespace RTSBuildPlacement
{
	FVector SnapToGrid(const FVector& Location, const FIntPoint& Footprint)
	{
		auto SnapAxis = [](double Value, int32 Cells)
		{
			return Cells % 2 == 0
				? FMath::RoundToDouble(Value / GridSize) * GridSize
				: FMath::FloorToDouble(Value / GridSize) * GridSize + GridSize * 0.5;
		};
		return FVector(SnapAxis(Location.X, Footprint.X), SnapAxis(Location.Y, Footprint.Y), Location.Z);
	}

	FVector GetFootprintExtent(const FIntPoint& Footprint)
	{
		return FVector(Footprint.X * GridSize * 0.5f, Footprint.Y * GridSize * 0.5f, 0.f);
	}

	FIntPoint GetRotatedFootprint(const FIntPoint& Footprint, int32 QuarterTurns)
	{
		return QuarterTurns % 2 == 0 ? Footprint : FIntPoint(Footprint.Y, Footprint.X);
	}

	float GetYaw(int32 QuarterTurns)
	{
		return (((QuarterTurns % 4) + 4) % 4) * 90.f;
	}

	int32 GetQuarterTurns(float Yaw)
	{
		return ((FMath::RoundToInt(Yaw / 90.f) % 4) + 4) % 4;
	}

	const URTSUnitData* GetWidestExitUnit(const URTSBuildingData& Data)
	{
		const URTSUnitData* Widest = nullptr;
		float WidestAgent = 0.f;
		float WidestHull = 0.f;
		auto Consider = [&](const URTSUnitData* Unit)
		{
			// Aircraft take off from their pads.
			if (!Unit || Unit->IsA<URTSAircraftData>())
			{
				return;
			}
			const FRTSUnitShape Shape = RTSUnitShape::FromData(*Unit);
			const float Agent = RTSNavAgents::SelectAgent(Shape).AgentRadius;
			const float Hull = Shape.GetHalfWidth();
			if (!Widest || Agent > WidestAgent || (Agent == WidestAgent && Hull > WidestHull))
			{
				Widest = Unit;
				WidestAgent = Agent;
				WidestHull = Hull;
			}
		};
		Consider(Data.SpawnOnComplete);
		for (const URTSUnitData* Unit : Data.Units)
		{
			Consider(Unit);
		}
		return Widest;
	}

	float GetExitDistance(const URTSBuildingData& Data, const URTSUnitData& Unit)
	{
		const FRTSUnitShape Shape = RTSUnitShape::FromData(Unit);
		return GetFootprintExtent(Data.Footprint).X + FMath::Max(Shape.GetHalfLength(), Shape.GetHalfWidth()) + ExitClearance;
	}

	float GetExitLaneLength(const URTSBuildingData& Data)
	{
		const URTSUnitData* Unit = GetWidestExitUnit(Data);
		return Unit ? GetExitDistance(Data, *Unit) - GetFootprintExtent(Data.Footprint).X + 2.f * RTSUnitShape::FromData(*Unit).GetHalfLength()
			: 0.f;
	}

	float GetExitLaneOffset(const URTSBuildingData& Data)
	{
		const FVector* Exit = Data.FindSocket(URTSBuildingData::GarageExitSocket);
		return Exit ? Exit->Y * Data.MeshScale.Y : 0.f;
	}

	bool CanUnitsLeave(const UWorld& World, const URTSBuildingData& Data, const FVector& Location, int32 QuarterTurns)
	{
		const URTSUnitData* Unit = GetWidestExitUnit(Data);
		if (!Unit)
		{
			return true;
		}
		const FRTSUnitShape Shape = RTSUnitShape::FromData(*Unit);
		const FNavDataConfig& Agent = RTSNavAgents::SelectAgent(Shape);
		const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(World, Agent);
		if (Agent.AgentRadius <= RTSNavAgents::GetNarrowestAgent().AgentRadius || !NavMesh)
		{
			return true;
		}

		// Where SpawnUnitAtExit puts it, or the garage's drive-out hands it over, facing out on the exit lane: it must stand on
		// its navmesh, clear of everything around it...
		const FRotator Rotation(0.f, GetYaw(QuarterTurns), 0.f);
		const FVector Forward = Rotation.Vector();
		const FVector Lane = Rotation.RotateVector(FVector(0.f, GetExitLaneOffset(Data), 0.f));
		FNavLocation Exit;
		if (!NavMesh->ProjectPoint(Location + Forward * GetExitDistance(Data, *Unit) + Lane, Exit, FVector(ExitTolerance, ExitTolerance, NavHeightTolerance))
			|| RTSHullClearance::GetClearance(*NavMesh, RTSHullClearance::MakeFootprint(Shape, Exit.Location, FVector2D(Forward))) < 0.f)
		{
			return false;
		}
		// ...and drive its own length straight out.
		FVector Hit;
		return !NavMesh->Raycast(Exit.Location, Exit.Location + Forward * (2.f * Shape.GetHalfLength()), Hit, nullptr);
	}

	bool CanPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation, FVector& OutLocation,
		TArray<AActor*>* OutBlockers, int32 QuarterTurns)
	{
		if (OutBlockers)
		{
			OutBlockers->Reset();
		}
		const FIntPoint Footprint = GetRotatedFootprint(Data.Footprint, QuarterTurns);
		OutLocation = SnapToGrid(RequestedLocation, Footprint);

		// Buildings stand on the narrowest agent's navmesh, which reaches closest to obstacles and other buildings.
		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		const ANavigationData* NavData = RTSNavAgents::GetNavMesh(World, RTSNavAgents::GetNarrowestAgent());
		if (!NavSystem || !NavData)
		{
			return false;
		}

		// The requested height is not trusted: take it from the navmesh, then from the ground just below it.
		const FVector QueryExtent(NavTolerance, NavTolerance, NavHeightTolerance);
		FNavLocation NavPoint;
		if (!NavSystem->ProjectPointToNavigation(OutLocation, NavPoint, QueryExtent, NavData))
		{
			return false;
		}
		OutLocation.Z = NavPoint.Location.Z;

		FHitResult GroundHit;
		const FVector TraceStart = OutLocation + FVector(0.f, 0.f, GroundClearance);
		if (World.LineTraceSingleByObjectType(GroundHit, TraceStart, TraceStart - FVector(0.f, 0.f, NavHeightTolerance), ECC_WorldStatic))
		{
			OutLocation.Z = GroundHit.Location.Z;
		}

		const FVector Extent = GetFootprintExtent(Footprint);
		for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(1.f, 1.f) })
		{
			const FVector Point = OutLocation + FVector(Corner.X * Extent.X, Corner.Y * Extent.Y, 0.f);
			if (!NavSystem->ProjectPointToNavigation(Point, NavPoint, QueryExtent, NavData))
			{
				return false;
			}
		}
		if (!CanUnitsLeave(World, Data, OutLocation, QuarterTurns))
		{
			return false;
		}

		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		// Shrunk by a centimetre so buildings may stand flush against each other.
		const FVector BoxExtent(Extent.X - 1.f, Extent.Y - 1.f, OverlapHeight * 0.5f);
		const FVector BoxCenter = OutLocation + FVector(0.f, 0.f, GroundClearance + BoxExtent.Z);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSBuildPlacement));
		if (!OutBlockers)
		{
			return !World.OverlapAnyTestByObjectType(BoxCenter, FQuat::Identity, Objects, FCollisionShape::MakeBox(BoxExtent), Params);
		}

		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByObjectType(Overlaps, BoxCenter, FQuat::Identity, Objects, FCollisionShape::MakeBox(BoxExtent), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (AActor* Actor = Overlap.GetActor())
			{
				OutBlockers->AddUnique(Actor);
			}
		}
		return Overlaps.IsEmpty();
	}

	bool CanBuilderPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation, FVector& OutLocation,
		int32 TeamId, int32 QuarterTurns)
	{
		TArray<AActor*> Blockers;
		if (CanPlace(World, Data, RequestedLocation, OutLocation, &Blockers, QuarterTurns))
		{
			return true;
		}
		// No blockers means the navmesh ruled the spot out.
		if (Blockers.IsEmpty())
		{
			return false;
		}
		for (const AActor* Blocker : Blockers)
		{
			const ARTSUnit* Unit = Cast<ARTSUnit>(Blocker);
			if (!Unit || Unit->GetTeamId() != TeamId)
			{
				return false;
			}
		}
		return true;
	}

	bool OverlapsPlannedSite(const UWorld& World, int32 TeamId, const URTSBuildingData& Data, const FVector& RequestedLocation,
		int32 QuarterTurns, const ARTSUnit* IgnoreBuilder)
	{
		const FIntPoint Footprint = GetRotatedFootprint(Data.Footprint, QuarterTurns);
		const FVector Location = SnapToGrid(RequestedLocation, Footprint);
		const FVector Extent = GetFootprintExtent(Footprint);
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			if (*It == IgnoreBuilder || It->GetTeamId() != TeamId || It->IsActorBeingDestroyed())
			{
				continue;
			}
			for (const FRTSPlannedSite& Site : It->GetBuilderComponent()->GetPlannedSites())
			{
				if (!Site.Data)
				{
					continue;
				}
				const FVector SiteExtent = GetFootprintExtent(GetRotatedFootprint(Site.Data->Footprint, Site.QuarterTurns));
				// A centimetre of slack, as in CanPlace, so buildings may stand flush.
				if (FMath::Abs(Location.X - Site.Location.X) < Extent.X + SiteExtent.X - 1.f
					&& FMath::Abs(Location.Y - Site.Location.Y) < Extent.Y + SiteExtent.Y - 1.f)
				{
					return true;
				}
			}
		}
		return false;
	}

	void GetMeshPlacement(const URTSEntityData& Data, float& OutBaseOffset, float& OutHeight)
	{
		OutBaseOffset = 0.f;
		OutHeight = 0.f;
		if (Data.Mesh)
		{
			const FBoxSphereBounds Bounds = Data.Mesh->GetBounds();
			OutBaseOffset = (Bounds.BoxExtent.Z - Bounds.Origin.Z) * Data.MeshScale.Z;
			OutHeight = Bounds.BoxExtent.Z * 2.f * Data.MeshScale.Z;
		}
	}
}
