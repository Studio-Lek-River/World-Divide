#include "Units/RTSAvoidanceSubsystem.h"

#include "AIController.h"
#include "Algo/BinarySearch.h"
#include "Navigation/PathFollowingComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSGameState.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"

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

	/** A push that starts off the navmesh is kept when its end lies this close above or below it. */
	const FVector OffMeshExtent(25.f, 25.f, 200.f);

	FVector2D RightOf(const FVector2D& Direction)
	{
		return FVector2D(-Direction.Y, Direction.X);
	}
}

void URTSAvoidanceSubsystem::Separate()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSAvoidanceSubsystem::Separate);

	const double StartSeconds = FPlatformTime::Seconds();
	UWorld* World = GetWorld();

	Agents.Reset();
	float MaxRadius = 0.f;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (!Unit->GetUnitData() || Unit->GetAircraftComponent()->CanFly() || Unit->GetHealthComponent()->IsDead())
		{
			continue;
		}
		const AAIController* AI = Unit->GetController<AAIController>();
		FAgent& Agent = Agents.AddDefaulted_GetRef();
		Agent.Unit = Unit;
		Agent.Location = FVector2D(Unit->GetActorLocation());
		Agent.Velocity = FVector2D(Unit->GetVelocity());
		Agent.Radius = Unit->GetSelectionRadius();
		Agent.bWalking = AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
		Agent.bIdle = !Agent.bWalking && Unit->GetQueuedCommandCount() == 0;
		Agent.Yield = Agent.bWalking || Agent.bIdle ? 1.f : BusyYield;
		MaxRadius = FMath::Max(MaxRadius, Agent.Radius);
	}
	if (Agents.Num() < 2)
	{
		LastSeparateSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	// Cells as wide as the largest pair can reach, so every overlap lies in a unit's own cell or the eight around it. Sorted
	// by row, then column, the three cells of a neighbouring row form one run of the array: no per-frame allocations.
	const float CellSize = 2.f * MaxRadius;
	auto CellOf = [CellSize](const FVector2D& Location)
	{
		return FIntPoint(FMath::FloorToInt32(Location.X / CellSize), FMath::FloorToInt32(Location.Y / CellSize));
	};
	auto KeyOf = [](const FIntPoint& Cell)
	{
		return (static_cast<int64>(Cell.Y) << 32) + (static_cast<int64>(Cell.X) + MAX_int32);
	};
	for (FAgent& Agent : Agents)
	{
		Agent.CellKey = KeyOf(CellOf(Agent.Location));
	}
	Agents.StableSort([](const FAgent& A, const FAgent& B) { return A.CellKey < B.CellKey; });

	Arrived.Reset();
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		FAgent& A = Agents[Index];
		const FIntPoint Cell = CellOf(A.Location);
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const int64 LastKey = KeyOf(Cell + FIntPoint(1, DY));
			// Each pair once: only partners after this unit, which also skips rows above it.
			const int32 First = FMath::Max(Index + 1, Algo::LowerBoundBy(Agents, KeyOf(Cell + FIntPoint(-1, DY)), &FAgent::CellKey));
			for (int32 OtherIndex = First; OtherIndex < Agents.Num() && Agents[OtherIndex].CellKey <= LastKey; ++OtherIndex)
			{
				FAgent& B = Agents[OtherIndex];
				const FVector2D Delta = B.Location - A.Location;
				const float MinDistance = A.Radius + B.Radius;
				const float DistanceSquared = Delta.SizeSquared();
				if (DistanceSquared >= FMath::Square(MinDistance))
				{
					continue;
				}

				const float Distance = FMath::Sqrt(DistanceSquared);
				const FVector2D Normal = Distance > UE_KINDA_SMALL_NUMBER
					? Delta / Distance
					: FVector2D(FMath::Cos(Index * CoincidentAngleStep), FMath::Sin(Index * CoincidentAngleStep));
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
				auto SharesGoal = [MinDistance](const FAgent& Walker, const FAgent& Standing)
				{
					const TOptional<FVector> WalkerGoal = Walker.Unit->GetMoveGoal();
					const TOptional<FVector> StandingGoal = Standing.Unit->GetMoveGoal();
					return WalkerGoal && StandingGoal && FVector::Dist2D(*WalkerGoal, *StandingGoal) < MinDistance;
				};
				if (A.bWalking && B.bIdle && SharesGoal(A, B))
				{
					Arrived.AddUnique(A.Unit);
				}
				else if (B.bWalking && A.bIdle && SharesGoal(B, A))
				{
					Arrived.AddUnique(B.Unit);
				}
			}
		}
	}

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* NavData = NavSystem ? NavSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	for (const FAgent& Agent : Agents)
	{
		if (Agent.Push.IsNearlyZero())
		{
			continue;
		}
		// Many neighbours pushing the same way would throw a unit clear across; the crowd settles over a few frames instead.
		const FVector2D Push = Agent.Push.GetClampedToMaxSize(Agent.Radius);
		const FVector From = Agent.Unit->GetNavAgentLocation();
		FVector To = From + FVector(Push, 0.f);
		if (NavData)
		{
			FVector Hit;
			if (NavData->Raycast(From, To, Hit, nullptr))
			{
				// A unit just off the navmesh hits at its own feet; it may still be pushed back onto it.
				FNavLocation Projected;
				const bool bOffMesh = Hit.Equals(From, 1.f);
				To = bOffMesh && NavSystem->ProjectPointToNavigation(To, Projected, OffMeshExtent, NavData) ? To : Hit;
			}
		}
		const FVector Step(To.X - From.X, To.Y - From.Y, 0.f);
		if (!Step.IsNearlyZero())
		{
			Agent.Unit->SetActorLocation(Agent.Unit->GetActorLocation() + Step);
		}
	}

	// After the pushes: ending a move starts the next order, which may set off new moves.
	for (ARTSUnit* Unit : Arrived)
	{
		Unit->ArriveInCrowd();
	}

	LastSeparateSeconds = FPlatformTime::Seconds() - StartSeconds;
}

void URTSAvoidanceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (World->GetNetMode() != NM_Client && CVarAvoidance.GetValueOnGameThread() && !ARTSGameState::IsMatchOver(*World))
	{
		Separate();
	}
}

TStatId URTSAvoidanceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSAvoidanceSubsystem, STATGROUP_Tickables);
}

bool URTSAvoidanceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
