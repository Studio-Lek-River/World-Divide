#include "Units/RTSApproach.h"

#include "Buildings/RTSBuildPlacement.h"
#include "GameFramework/Pawn.h"
#include "Navigation/RTSPathSearch.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

namespace
{
	/** How far a candidate may be nudged onto the navmesh: the agent's default query extent. */
	const FVector ProjectExtent(50.f, 50.f, 250.f);
}

namespace RTSApproach
{
	bool FindReachableSpot(const APawn& Agent, const FRTSBuildingFootprint& Footprint, float StandOff,
		float MaxDistance, FVector& OutSpot)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Agent.GetWorld());
		const FVector From = Agent.GetNavAgentLocation();
		const ANavigationData* NavData = NavSystem ? NavSystem->GetNavDataForProps(Agent.GetNavAgentPropertiesRef(), From) : nullptr;
		if (!NavData)
		{
			return false;
		}

		// The spot facing the agent, then a ring around the footprint: one per grid cell along each side, one off each corner,
		// laid out in the footprint's own frame.
		const FVector2D Extent = Footprint.Extent;
		const FVector Facing = Footprint.GetClosestPoint(From);
		TArray<FVector, TInlineAllocator<32>> Candidates;
		// A builder may stand inside the footprint of a building it has yet to place; then no side faces it.
		if (FVector::DistSquared2D(Facing, From) > KINDA_SMALL_NUMBER)
		{
			Candidates.Add(Facing + (From - Facing).GetSafeNormal2D() * StandOff);
		}
		for (const FVector2D Normal : { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) })
		{
			const FVector2D Along(FMath::Abs(Normal.Y), FMath::Abs(Normal.X));
			const float Depth = FMath::Abs(Normal.X) * Extent.X + FMath::Abs(Normal.Y) * Extent.Y;
			const float HalfLength = FVector2D::DotProduct(Along, Extent);
			for (float Offset = -HalfLength; Offset <= HalfLength + KINDA_SMALL_NUMBER; Offset += RTSBuildPlacement::GridSize)
			{
				Candidates.Add(Footprint.ToWorld(Normal * (Depth + StandOff) + Along * Offset));
			}
		}
		for (const FVector2D Corner : { FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(-1.f, -1.f) })
		{
			Candidates.Add(Footprint.ToWorld(Corner * Extent + Corner.GetSafeNormal() * StandOff));
		}
		// From outside the ring the facing spot is the nearest of them all, so it stays first.
		Candidates.Sort([&From](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, From) < FVector::DistSquared2D(B, From); });

		for (const FVector& Candidate : Candidates)
		{
			// Inside a wall, a building or off the map there is no navmesh to stand on.
			FNavLocation NavPoint;
			if (!NavSystem->ProjectPointToNavigation(Candidate, NavPoint, ProjectExtent, NavData)
				|| FVector::Dist2D(NavPoint.Location, Footprint.GetClosestPoint(NavPoint.Location)) > MaxDistance)
			{
				continue;
			}
			// A partial path would end short, on the near side of whatever is in the way; one that only ran out of search
			// nodes is searched again with more.
			FPathFindingQuery Query(&Agent, *NavData, From, NavPoint.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = RTSPathSearch::FindFullPath(*NavSystem, Query);
			if (Result.IsSuccessful() && !Result.IsPartial())
			{
				OutSpot = NavPoint.Location;
				return true;
			}
		}
		return false;
	}
}
