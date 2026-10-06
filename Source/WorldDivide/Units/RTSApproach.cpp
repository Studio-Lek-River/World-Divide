#include "Units/RTSApproach.h"

#include "Buildings/RTSBuildPlacement.h"
#include "GameFramework/Pawn.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

namespace
{
	/** How far a candidate may be nudged onto the navmesh: the agent's default query extent. */
	const FVector ProjectExtent(50.f, 50.f, 250.f);

	FVector ClosestFootprintPoint(const FVector& Center, const FVector& Extent, const FVector& From)
	{
		return FVector(
			FMath::Clamp(From.X, Center.X - Extent.X, Center.X + Extent.X),
			FMath::Clamp(From.Y, Center.Y - Extent.Y, Center.Y + Extent.Y),
			Center.Z);
	}
}

namespace RTSApproach
{
	bool FindReachableSpot(const APawn& Agent, const FVector& Center, const FIntPoint& Footprint, float StandOff,
		float MaxDistance, FVector& OutSpot)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Agent.GetWorld());
		const FVector From = Agent.GetNavAgentLocation();
		const ANavigationData* NavData = NavSystem ? NavSystem->GetNavDataForProps(Agent.GetNavAgentPropertiesRef(), From) : nullptr;
		if (!NavData)
		{
			return false;
		}

		// The spot facing the agent, then a ring around the footprint: one per grid cell along each side, one off each corner.
		const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Footprint);
		const FVector Facing = ClosestFootprintPoint(Center, Extent, From);
		TArray<FVector, TInlineAllocator<32>> Candidates;
		// A builder may stand inside the footprint of a building it has yet to place; then no side faces it.
		if (FVector::DistSquared2D(Facing, From) > KINDA_SMALL_NUMBER)
		{
			Candidates.Add(Facing + (From - Facing).GetSafeNormal2D() * StandOff);
		}
		for (const FVector Normal : { FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f) })
		{
			const FVector Along(FMath::Abs(Normal.Y), FMath::Abs(Normal.X), 0.f);
			const float Depth = FMath::Abs(Normal.X) * Extent.X + FMath::Abs(Normal.Y) * Extent.Y;
			const float HalfLength = FVector::DotProduct(Along, Extent);
			for (float Offset = -HalfLength; Offset <= HalfLength + KINDA_SMALL_NUMBER; Offset += RTSBuildPlacement::GridSize)
			{
				Candidates.Add(Center + Normal * (Depth + StandOff) + Along * Offset);
			}
		}
		for (const FVector Corner : { FVector(1.f, 1.f, 0.f), FVector(1.f, -1.f, 0.f), FVector(-1.f, 1.f, 0.f), FVector(-1.f, -1.f, 0.f) })
		{
			Candidates.Add(Center + Corner * Extent + Corner.GetSafeNormal2D() * StandOff);
		}
		// From outside the ring the facing spot is the nearest of them all, so it stays first.
		Candidates.Sort([&From](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, From) < FVector::DistSquared2D(B, From); });

		for (const FVector& Candidate : Candidates)
		{
			// Inside a wall, a building or off the map there is no navmesh to stand on.
			FNavLocation NavPoint;
			if (!NavSystem->ProjectPointToNavigation(Candidate, NavPoint, ProjectExtent, NavData)
				|| FVector::Dist2D(NavPoint.Location, ClosestFootprintPoint(Center, Extent, NavPoint.Location)) > MaxDistance)
			{
				continue;
			}
			// A partial path would end short, on the near side of whatever is in the way.
			FPathFindingQuery Query(&Agent, *NavData, From, NavPoint.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = NavSystem->FindPathSync(Query);
			if (Result.IsSuccessful() && !Result.IsPartial())
			{
				OutSpot = NavPoint.Location;
				return true;
			}
		}
		return false;
	}
}
