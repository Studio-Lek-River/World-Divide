#include "Units/RTSAvoidanceSubsystem.h"

#include "AIController.h"
#include "Algo/BinarySearch.h"
#include "Navigation/PathFollowingComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSGameState.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

namespace
{
	TAutoConsoleVariable<bool> CVarAvoidance(
		TEXT("rts.Avoidance"), true,
		TEXT("Pushes overlapping ground units apart on the server."));

	/** A unit at work in place gives way this much against 1 for a walking or idle one, so others flow around it. */
	constexpr float BusyYield = 0.1f;

	/** Two walking units running into each other also step this share of the overlap to their own right, so they pass. */
	constexpr float SidestepFraction = 0.5f;

	/** Golden angle in radians: coincident units part in directions that differ for every pair. */
	constexpr float CoincidentAngleStep = 2.39996f;

	/**
	 * Grid cells are this wide whatever the units' sizes; a footprint wider than a cell sits in every cell its circle
	 * covers, so one large unit no longer makes the cells, and every unit's neighbour count, large for everyone.
	 */
	constexpr float CellSize = 600.f;

	/** A unit is pushed apart at most at this share of its own top speed, so even a deep overlap parts smoothly. */
	constexpr float SeparationSpeedFraction = 0.5f;

	/** A push that starts off the navmesh is kept when its end lies this close above or below it. */
	const FVector OffMeshExtent(25.f, 25.f, 200.f);

	/**
	 * A hull check looks this much further for walls than the hull reaches, so a unit found clear is not checked again
	 * until it has driven or turned about this far, or this many seconds passed, in which a building may have gone up.
	 */
	constexpr float HullCheckExtraReach = 200.f;
	constexpr double HullCheckSeconds = 1.0;

	/** A hull found inside an obstacle backs out by up to this many frames of its top speed at once, in up to this many passes. */
	constexpr float HullBackOutFrames = 4.f;
	constexpr int32 HullBackOutPasses = 3;

	/** How far a hull inside an obstacle is nudged each way to find which way out is quickest. */
	constexpr float HullGradientStep = 5.f;

	FIntPoint CellOf(const FVector2D& Location)
	{
		return FIntPoint(FMath::FloorToInt32(Location.X / CellSize), FMath::FloorToInt32(Location.Y / CellSize));
	}

	int64 KeyOf(const FIntPoint& Cell)
	{
		return (static_cast<int64>(Cell.Y) << 32) + (static_cast<int64>(Cell.X) + MAX_int32);
	}

	FVector2D RightOf(const FVector2D& Direction)
	{
		return FVector2D(-Direction.Y, Direction.X);
	}
}

bool URTSAvoidanceSubsystem::BuildIndex(float DeltaSeconds)
{
	Agents.Reset();
	Entries.Reset();
	MaxBoundRadius = 0.f;
	IndexFrame = GFrameCounter;
	int32 Avoiding = 0;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (Unit->GetHealthComponent()->IsDead() || Unit->IsActorBeingDestroyed())
		{
			continue;
		}
		const FRTSFootprint Footprint = Unit->GetFootprint();
		const AAIController* AI = Unit->GetController<AAIController>();
		FAgent& Agent = Agents.AddDefaulted_GetRef();
		Agent.Unit = Unit;
		Agent.Location = FVector2D(Footprint.Center);
		Agent.Axis = FVector2D(Footprint.Axis);
		Agent.Velocity = FVector2D(Unit->GetVelocity());
		Agent.HalfSegment = Footprint.HalfSegment;
		Agent.Radius = Footprint.Radius;
		Agent.BoundRadius = Footprint.HalfSegment + Footprint.Radius;
		Agent.bAvoids = Unit->GetUnitData() && !Unit->GetAircraftComponent()->CanFly();
		Agent.MaxStep = Agent.bAvoids ? Unit->GetUnitData()->MoveSpeed * SeparationSpeedFraction * DeltaSeconds : 0.f;
		Agent.bWalking = AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
		Agent.bIdle = !Agent.bWalking && Unit->GetQueuedCommandCount() == 0;
		Agent.Yield = Agent.bWalking || Agent.bIdle ? 1.f : BusyYield;
		Agent.MinCell = CellOf(Agent.Location - FVector2D(Agent.BoundRadius));
		Agent.MaxCell = CellOf(Agent.Location + FVector2D(Agent.BoundRadius));
		MaxBoundRadius = FMath::Max(MaxBoundRadius, Agent.BoundRadius);
		Avoiding += Agent.bAvoids ? 1 : 0;

		const int32 AgentIndex = Agents.Num() - 1;
		for (int32 Y = Agent.MinCell.Y; Y <= Agent.MaxCell.Y; ++Y)
		{
			for (int32 X = Agent.MinCell.X; X <= Agent.MaxCell.X; ++X)
			{
				Entries.Add({ KeyOf(FIntPoint(X, Y)), AgentIndex });
			}
		}
	}
	// Sorted by row, then column, then agent: each cell is one run, and a row of cells one run too. No per-frame allocations
	// once the arrays have grown.
	Entries.Sort([](const FEntry& A, const FEntry& B) { return A.CellKey != B.CellKey ? A.CellKey < B.CellKey : A.Agent < B.Agent; });
	return Avoiding >= 2;
}

void URTSAvoidanceSubsystem::Separate(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSAvoidanceSubsystem::Separate);

	const double StartSeconds = FPlatformTime::Seconds();
	// With fewer than two units to push apart, or rts.Avoidance 0, only the hulls are kept out of obstacles.
	const int32 RunCount = BuildIndex(DeltaSeconds) && CVarAvoidance.GetValueOnGameThread() ? Entries.Num() : 0;

	Arrived.Reset();
	for (int32 RunStart = 0; RunStart < RunCount;)
	{
		const int64 CellKey = Entries[RunStart].CellKey;
		int32 RunEnd = RunStart + 1;
		while (RunEnd < Entries.Num() && Entries[RunEnd].CellKey == CellKey)
		{
			++RunEnd;
		}
		for (int32 First = RunStart; First < RunEnd; ++First)
		{
			FAgent& A = Agents[Entries[First].Agent];
			if (!A.bAvoids)
			{
				continue;
			}
			for (int32 Second = First + 1; Second < RunEnd; ++Second)
			{
				FAgent& B = Agents[Entries[Second].Agent];
				if (!B.bAvoids)
				{
					continue;
				}
				// A pair sharing several cells meets only in the first of them: the corner where their cell ranges start.
				const FIntPoint SharedCell(FMath::Max(A.MinCell.X, B.MinCell.X), FMath::Max(A.MinCell.Y, B.MinCell.Y));
				if (KeyOf(SharedCell) != CellKey)
				{
					continue;
				}
				const float Bound = A.BoundRadius + B.BoundRadius;
				if (FVector2D::DistSquared(A.Location, B.Location) >= FMath::Square(Bound))
				{
					continue;
				}

				// The stadiums overlap where their segments come closer than their radii.
				FVector2D OnA;
				FVector2D OnB;
				RTSUnitShape::GetClosestSegmentPoints(A.Location - A.Axis * A.HalfSegment, A.Location + A.Axis * A.HalfSegment,
					B.Location - B.Axis * B.HalfSegment, B.Location + B.Axis * B.HalfSegment, OnA, OnB);
				const FVector2D Delta = OnB - OnA;
				const float MinDistance = A.Radius + B.Radius;
				const float DistanceSquared = Delta.SizeSquared();
				if (DistanceSquared >= FMath::Square(MinDistance))
				{
					continue;
				}

				const float Distance = FMath::Sqrt(DistanceSquared);
				const int32 PairIndex = Entries[First].Agent;
				const FVector2D Normal = Distance > UE_KINDA_SMALL_NUMBER
					? Delta / Distance
					: FVector2D(FMath::Cos(PairIndex * CoincidentAngleStep), FMath::Sin(PairIndex * CoincidentAngleStep));
				const float Overlap = MinDistance - Distance;
				const float ShareA = A.Yield / (A.Yield + B.Yield);
				const float ShareB = 1.f - ShareA;
				A.Push -= Normal * Overlap * ShareA;
				B.Push += Normal * Overlap * ShareB;

				// Head on: pushing straight back would only hold both in place.
				if (A.bWalking && B.bWalking && FVector2D::DotProduct(A.Velocity, Normal) > 0.f
					&& FVector2D::DotProduct(B.Velocity, Normal) < 0.f)
				{
					A.Push += RightOf(A.Velocity.GetSafeNormal()) * Overlap * ShareA * SidestepFraction;
					B.Push += RightOf(B.Velocity.GetSafeNormal()) * Overlap * ShareB * SidestepFraction;
				}

				// Touching a unit that already stands on the same goal: this one has arrived as well.
				auto SharesGoal = [Bound](const FAgent& Walker, const FAgent& Standing)
				{
					const TOptional<FVector> WalkerGoal = Walker.Unit->GetMoveGoal();
					const TOptional<FVector> StandingGoal = Standing.Unit->GetMoveGoal();
					return WalkerGoal && StandingGoal && FVector::Dist2D(*WalkerGoal, *StandingGoal) < Bound;
				};
				if (A.bWalking && B.bIdle && SharesGoal(A, B))
				{
					Arrived.AddUnique(A.Unit.Get());
				}
				else if (B.bWalking && A.bIdle && SharesGoal(B, A))
				{
					Arrived.AddUnique(B.Unit.Get());
				}
			}
		}
		RunStart = RunEnd;
	}

	ApplyPushes(DeltaSeconds);

	// After the pushes: ending a move starts the next order, which may set off new moves.
	for (ARTSUnit* Unit : Arrived)
	{
		Unit->ArriveInCrowd();
	}

	LastSeparateSeconds = FPlatformTime::Seconds() - StartSeconds;
}

void URTSAvoidanceSubsystem::ApplyPushes(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (const FAgent& Agent : Agents)
	{
		ARTSUnit* Unit = Agent.Unit.Get();
		if (!Agent.bAvoids || !Unit)
		{
			continue;
		}
		const FVector From = Unit->GetNavAgentLocation();
		// The unit's own navmesh, so a push never takes it where it could not drive.
		const ANavigationData* NavData = NavSystem->GetNavDataForProps(Unit->GetNavAgentPropertiesRef(), From);
		FVector2D Step = FVector2D::ZeroVector;
		if (!Agent.Push.IsNearlyZero())
		{
			// Many neighbours pushing the same way, or a deep overlap, part over several frames instead of in one jump.
			FVector To = From + FVector(Agent.Push.GetClampedToMaxSize(Agent.MaxStep), 0.f);
			FVector Hit;
			if (NavData && NavData->Raycast(From, To, Hit, nullptr))
			{
				// A unit just off the navmesh hits at its own feet; it may still be pushed back onto it.
				FNavLocation Projected;
				const bool bOffMesh = Hit.Equals(From, 1.f);
				To = bOffMesh && NavSystem->ProjectPointToNavigation(To, Projected, OffMeshExtent, NavData) ? To : Hit;
			}
			Step = FVector2D(To - From);
		}

		// The hull where it ends up, path following's move and the push together, kept out of obstacles: the centre alone
		// stays on the navmesh, the rest of the hull is checked here (RTSHullClearance), only once it may have closed the gap.
		FRTSFootprint Footprint = Unit->GetFootprint();
		Footprint.Center += FVector(Step, 0.f);
		FRTSHullClearanceCache& HullCache = Unit->GetHullClearanceCache();
		const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavData);
		// A hull backs out by up to a few frames' driving at once: more only a building dropped on it would ask for.
		float BackOut = HullBackOutFrames * Unit->GetUnitData()->MoveSpeed * DeltaSeconds;
		for (int32 Pass = 0; NavMesh && Pass < HullBackOutPasses && !HullCache.IsClear(Footprint, Now); ++Pass)
		{
			FVector2D Away = FVector2D::ZeroVector;
			const float Clearance = RTSHullClearance::GetClearance(*NavMesh, Footprint, &Away, HullCheckExtraReach);
			if (Clearance >= 0.f)
			{
				HullCache.Center = FVector2D(Footprint.Center);
				HullCache.Axis = FVector2D(Footprint.Axis);
				HullCache.Slack = Clearance;
				HullCache.ExpireTime = Now + HullCheckSeconds;
				break;
			}
			HullCache.Slack = -1.f;
			// Out along the way the clearance grows fastest: beside a wall's end the deepest point alone may point straight
			// back along the way the unit drives, and hold it there.
			auto ClearanceAt = [&](const FVector2D& Offset)
			{
				FRTSFootprint Moved = Footprint;
				Moved.Center += FVector(Offset, 0.f);
				return RTSHullClearance::GetClearance(*NavMesh, Moved);
			};
			const FVector2D Gradient(ClearanceAt(FVector2D(HullGradientStep, 0.f)) - ClearanceAt(FVector2D(-HullGradientStep, 0.f)),
				ClearanceAt(FVector2D(0.f, HullGradientStep)) - ClearanceAt(FVector2D(0.f, -HullGradientStep)));
			if (!Gradient.IsNearlyZero())
			{
				Away = Gradient.GetSafeNormal();
			}			// The part of the push that drives the hull further in goes, and the hull backs out along the way out of its
			// deepest point; a hull wedged at a corner may take another pass.
			const float Into = FVector2D::DotProduct(Step, Away);
			if (Into < 0.f)
			{
				Step -= Away * Into;
			}
			const float Out = FMath::Min(-Clearance, BackOut);
			Step += Away * Out;
			// Nor does it keep driving in: the next frame's move starts without the speed toward the obstacle.
			if (UPawnMovementComponent* Movement = Unit->GetMovementComponent())
			{
				const float Toward = FVector2D::DotProduct(FVector2D(Movement->Velocity), Away);
				if (Toward < 0.f)
				{
					Movement->Velocity -= FVector(Away * Toward, 0.f);
				}
			}
			BackOut -= Out;
			Footprint.Center = Unit->GetFootprint().Center + FVector(Step, 0.f);
		}

		if (!Step.IsNearlyZero())
		{
			Unit->SetActorLocation(Unit->GetActorLocation() + FVector(Step, 0.f));
		}
	}
}

void URTSAvoidanceSubsystem::QueryRadius(const FVector& Center, float Radius, TArray<ARTSUnit*>& OutUnits) const
{
	OutUnits.Reset();
	// The grid is built once a frame, after the actors tick; without one from this or the last frame, every unit is a candidate.
	if (IndexFrame == MAX_uint64 || GFrameCounter > IndexFrame + 1)
	{
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{
			OutUnits.Add(*It);
		}
		return;
	}

	const FVector2D Location(Center);
	const FIntPoint MinCell = CellOf(Location - FVector2D(Radius + MaxBoundRadius));
	const FIntPoint MaxCell = CellOf(Location + FVector2D(Radius + MaxBoundRadius));
	for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
	{
		const int64 LastKey = KeyOf(FIntPoint(MaxCell.X, Y));
		for (int32 Index = Algo::LowerBoundBy(Entries, KeyOf(FIntPoint(MinCell.X, Y)), &FEntry::CellKey);
			Index < Entries.Num() && Entries[Index].CellKey <= LastKey; ++Index)
		{
			const FAgent& Agent = Agents[Entries[Index].Agent];
			// A unit in several of the cells counts once: in the first cell both ranges share.
			const FIntPoint SharedCell(FMath::Max(MinCell.X, Agent.MinCell.X), FMath::Max(MinCell.Y, Agent.MinCell.Y));
			if (KeyOf(SharedCell) != Entries[Index].CellKey
				|| FVector2D::DistSquared(Location, Agent.Location) > FMath::Square(Radius + Agent.BoundRadius))
			{
				continue;
			}
			if (ARTSUnit* Unit = Agent.Unit.Get())
			{
				OutUnits.Add(Unit);
			}
		}
	}
}

void URTSAvoidanceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (World->GetNetMode() == NM_Client || ARTSGameState::IsMatchOver(*World))
	{
		return;
	}
	// With rts.Avoidance 0 the pass still keeps hulls out of obstacles, and weapons still find their targets through its grid.
	Separate(DeltaTime);
}

TStatId URTSAvoidanceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSAvoidanceSubsystem, STATGROUP_Tickables);
}

bool URTSAvoidanceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
