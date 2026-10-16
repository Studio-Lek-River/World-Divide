#include "Navigation/RTSMoveDestination.h"

#include "GameFramework/Pawn.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace
{
	/** Tall enough to reach the ground from the roof of the tallest building (the 32 m superweapon), where a click on it lands. */
	constexpr float SearchHalfHeight = 3500.f;

	/** Spots this much further from the target than the nearest still count as near: the far face of a wall, another side of a block. */
	constexpr float NearSlack = 300.f;

	/** Spots closer together than this are one candidate: neighbouring polygons along the same face. */
	constexpr float MergeDistance = 100.f;

	/** At most this many candidates get a path search. */
	constexpr int32 MaxCandidates = 8;

	struct FCandidate
	{
		FVector Location;
		float Distance;
	};

	/** The nearest point of every polygon within HalfSize of Target, and the smallest distance among them (MAX_flt for none). */
	float GatherPoints(const ARecastNavMesh& NavMesh, const FVector& Target, float HalfSize, TArray<FCandidate, TInlineAllocator<64>>& OutPoints)
	{
		OutPoints.Reset();
		TArray<FNavPoly> Polys;
		NavMesh.GetPolysInBox(FBox::BuildAABB(Target, FVector(HalfSize, HalfSize, SearchHalfHeight)), Polys);
		float Best = MAX_flt;
		for (const FNavPoly& Poly : Polys)
		{
			FVector Point;
			if (NavMesh.GetClosestPointOnPoly(Poly.Ref, Target, Point))
			{
				const float Distance = FVector::Dist2D(Point, Target);
				OutPoints.Add({ Point, Distance });
				Best = FMath::Min(Best, Distance);
			}
		}
		return Best;
	}
}

namespace RTSMoveDestination
{
	bool FindReachable(const APawn& Agent, const FVector& Target, FVector& OutDestination)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Agent.GetWorld());
		const FVector From = Agent.GetNavAgentLocation();
		const ARecastNavMesh* NavMesh = NavSystem
			? Cast<ARecastNavMesh>(NavSystem->GetNavDataForProps(Agent.GetNavAgentPropertiesRef(), From)) : nullptr;
		if (!NavMesh)
		{
			return false;
		}

		// Grow a box around the target until it holds navmesh, whatever the size of the obstacle or where the unit stands.
		// Nothing beyond the navmesh bounds can turn up, so the growth stops there.
		const FBox Bounds = NavMesh->GetBounds();
		const float MaxHalfSize = Bounds.IsValid
			? FMath::Max(FMath::Abs(Target.X - Bounds.GetCenter().X), FMath::Abs(Target.Y - Bounds.GetCenter().Y))
				+ FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y)
			: 0.f;
		TArray<FCandidate, TInlineAllocator<64>> Points;
		float Best = MAX_flt;
		for (float HalfSize = FMath::Max(NavMesh->GetDefaultQueryExtent().X, 50.f); Best == MAX_flt; HalfSize *= 2.f)
		{
			Best = GatherPoints(*NavMesh, Target, FMath::Min(HalfSize, MaxHalfSize), Points);
			if (HalfSize >= MaxHalfSize)
			{
				break;
			}
		}
		if (Best == MAX_flt)
		{
			return false;
		}
		// A box only promises what lies within its half size; one sized to the nearest spot and the slack misses nothing near
		// and leaves out the rest of a large box, which Recast also caps at 256 polygons.
		Best = GatherPoints(*NavMesh, Target, Best + NearSlack, Points);

		TArray<FCandidate, TInlineAllocator<MaxCandidates>> Candidates;
		Points.Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
		for (const FCandidate& Point : Points)
		{
			if (Point.Distance > Best + NearSlack || Candidates.Num() == MaxCandidates)
			{
				break;
			}
			if (!Candidates.ContainsByPredicate([&Point](const FCandidate& Kept)
				{ return FVector::DistSquared2D(Kept.Location, Point.Location) < FMath::Square(MergeDistance); }))
			{
				Candidates.Add(Point);
			}
		}

		// A partial path would end on the near side of whatever cuts the candidate off: another island, an enclosure.
		double Shortest = TNumericLimits<double>::Max();
		for (const FCandidate& Candidate : Candidates)
		{
			FPathFindingQuery Query(&Agent, *NavMesh, From, Candidate.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = NavSystem->FindPathSync(Query);
			if (Result.IsSuccessful() && !Result.IsPartial() && Result.Path->GetLength() < Shortest)
			{
				Shortest = Result.Path->GetLength();
				OutDestination = Candidate.Location;
			}
		}
		return Shortest < TNumericLimits<double>::Max();
	}
}
