#include "Navigation/RTSHullClearance.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSBarrelClearance.h"
#include "Units/RTSUnitShape.h"

float RTSHullClearance::GetErosion(const ARecastNavMesh& NavMesh)
{
	const float Cell = NavMesh.GetCellSize(ENavigationDataResolution::Default);
	return Cell > 0.f ? FMath::CeilToFloat(NavMesh.AgentRadius / Cell) * Cell : NavMesh.AgentRadius;
}

namespace
{
	/** The stadium is sampled as discs at most this far apart along its segment, and the path walked back in such steps. */
	constexpr float SampleStep = 25.f;

	/** Each box corner the stadium rounds off is sampled at this many points along each of its two edges. */
	constexpr int32 CornerEdgeSamples = 4;

	/** A path end is moved back to within this much of the furthest spot where the hull clears the obstacle ahead. */
	constexpr float EndPrecision = 5.f;

	/** A corner is moved away from its walls at most this many times. */
	constexpr int32 CornerIterations = 4;

	/** The headings a hull turns through at a corner are checked this many degrees apart. */
	constexpr float CornerTurnStep = 15.f;

	/** How far above and below a point the navmesh under it is looked for. */
	constexpr float ProjectHeight = 250.f;

	/** A path into an obstacle ends where the hull's box, grown by this much, overlaps nothing solid. */
	constexpr float ContactGap = 10.f;

	/** A hull's box looks for solids from this high above its ground: a kerb its navmesh lets it climb is none. */
	constexpr float SolidStepHeight = 50.f;

	const FName SolidQueryTag(TEXT("RTSHullClearance"));

	/** One point of the hull: a disc of Radius, or a point when Radius is zero, in the footprint's frame. */
	struct FSample
	{
		FVector2D Local;
		float Radius;
	};

	/** The navmesh's walls near one spot, and which side of them the navmesh lies on. */
	struct FWalls
	{
		TArray<FNavigationWallEdge> Edges;
		/** +1 when the navmesh lies left of each edge, going from its start to its end; -1 when right. */
		float InsideSign = 1.f;
	};

	/** Set the first time a check finds out which side of its walls the navmesh lies on; Detour winds every polygon alike. */
	float GKnownInsideSign = 0.f;

	/** The point of segment Start-End closest to Point, and where along it, 0 to 1. */
	FVector2D ClosestOnSegment(const FVector2D& Point, const FVector2D& Start, const FVector2D& End, float& OutAlpha)
	{
		const FVector2D Edge = End - Start;
		const float LengthSquared = Edge.SizeSquared();
		OutAlpha = LengthSquared > UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - Start, Edge) / LengthSquared, 0.f, 1.f) : 0.f;
		return Start + Edge * OutAlpha;
	}

	using RTSHullClearance::GetErosion;

	float GetMargin(const ARecastNavMesh& NavMesh)
	{
		return 0.5f * NavMesh.GetCellSize(ENavigationDataResolution::Default);
	}

	void GetSamples(const FRTSFootprint& Footprint, TArray<FSample, TInlineAllocator<64>>& OutSamples)
	{
		const float HalfSegment = Footprint.HalfSegment;
		const float Radius = Footprint.Radius;
		const int32 Steps = FMath::CeilToInt32(HalfSegment / SampleStep);
		for (int32 Step = -Steps; Step <= Steps; ++Step)
		{
			OutSamples.Add({ FVector2D(Steps > 0 ? HalfSegment * Step / Steps : 0.f, 0.f), Radius });
		}
		// The corners of the box beyond the stadium's round ends: along the end edge out to the corner, and back along the side.
		for (const float X : { -1.f, 1.f })
		{
			for (const float Y : { -1.f, 1.f })
			{
				for (int32 Index = 1; Index <= CornerEdgeSamples; ++Index)
				{
					const float Along = static_cast<float>(Index) / CornerEdgeSamples;
					OutSamples.Add({ FVector2D(X * (HalfSegment + Radius), Y * Radius * Along), 0.f });
					if (Index < CornerEdgeSamples)
					{
						OutSamples.Add({ FVector2D(X * (HalfSegment + Radius * Along), Y * Radius), 0.f });
					}
				}
			}
		}
	}

	/**
	 * The signed distance from Point to the navmesh's edge, positive on the navmesh, and the ground direction into it. Walls
	 * meeting at a corner of the edge decide the side together.
	 */
	float GetSignedDistance(const FWalls& Walls, const FVector2D& Point, FVector2D& OutInward)
	{
		float Best = FLT_MAX;
		for (const FNavigationWallEdge& Edge : Walls.Edges)
		{
			float Alpha;
			Best = FMath::Min(Best, FVector2D::DistSquared(Point, ClosestOnSegment(Point, FVector2D(Edge.Start), FVector2D(Edge.End), Alpha)));
		}
		const float Distance = FMath::Sqrt(Best);

		// Every wall at that distance: one through its middle decides alone; walls meeting at a vertex vote with their normals.
		FVector2D NormalSum = FVector2D::ZeroVector;
		FVector2D Closest = Point;
		float Side = 0.f;
		bool bInterior = false;
		for (const FNavigationWallEdge& Edge : Walls.Edges)
		{
			const FVector2D Start(Edge.Start);
			const FVector2D End(Edge.End);
			float Alpha;
			const FVector2D OnEdge = ClosestOnSegment(Point, Start, End, Alpha);
			if (FVector2D::Distance(Point, OnEdge) > Distance + 0.5f)
			{
				continue;
			}
			const FVector2D Direction = (End - Start).GetSafeNormal();
			// Pointing into the navmesh.
			const FVector2D Normal = FVector2D(-Direction.Y, Direction.X) * Walls.InsideSign;
			Closest = OnEdge;
			if (Alpha > 0.f && Alpha < 1.f)
			{
				Side = FVector2D::DotProduct(Point - OnEdge, Normal);
				OutInward = Normal;
				bInterior = true;
				break;
			}
			NormalSum += Normal;
		}
		if (!bInterior)
		{
			const FVector2D Normal = NormalSum.GetSafeNormal();
			Side = FVector2D::DotProduct(Point - Closest, Normal);
			OutInward = Normal;
		}
		return Side >= 0.f ? Distance : -Distance;
	}

	/**
	 * Finds out which side of the walls the navmesh lies on from Point, on the navmesh when bOnMesh and off it otherwise, as
	 * long as it lies clear of the walls; otherwise keeps the side found before.
	 */
	void FindInsideSign(FWalls& Walls, const FVector2D& Point, bool bOnMesh)
	{
		Walls.InsideSign = 1.f;
		FVector2D Inward;
		const float Side = GetSignedDistance(Walls, Point, Inward);
		if (FMath::Abs(Side) > 1.f)
		{
			GKnownInsideSign = (Side > 0.f) == bOnMesh ? 1.f : -1.f;
		}
		Walls.InsideSign = GKnownInsideSign != 0.f ? GKnownInsideSign : 1.f;
	}

	float GetHalfDiagonal(const FRTSFootprint& Footprint)
	{
		return FMath::Sqrt(FMath::Square(Footprint.HalfSegment + Footprint.Radius) + FMath::Square(Footprint.Radius));
	}

	FVector2D Perpendicular(const FVector2D& Direction)
	{
		return FVector2D(-Direction.Y, Direction.X);
	}
}

namespace RTSHullClearance
{
	float GetClearance(const ARecastNavMesh& NavMesh, const FRTSFootprint& Footprint, FVector2D* OutAway, float ExtraReach)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSHullClearance::GetClearance);

		const float Erosion = GetErosion(NavMesh);
		const float Margin = GetMargin(NavMesh);
		const float HalfDiagonal = GetHalfDiagonal(Footprint);
		// Every wall that may lie nearest to a sample: a sample off the navmesh is at most the erosion from the wall that matters.
		const float Reach = HalfDiagonal + Erosion + ExtraReach;

		FNavLocation Center;
		if (!NavMesh.ProjectPoint(Footprint.Center, Center, FVector(HalfDiagonal + Erosion, HalfDiagonal + Erosion, ProjectHeight)))
		{
			return FLT_MAX;
		}
		// The search starts where the centre meets the navmesh, so it reaches further by the centre's distance to it.
		const float Offset = FVector::Dist2D(Center.Location, Footprint.Center);
		// What no wall beyond the search can take away, whatever is inside it.
		const float Bound = Reach - HalfDiagonal + Erosion - Margin;

		FWalls Walls;
		if (!NavMesh.FindEdges(Center.NodeRef, Center.Location, Reach + Offset, NavMesh.GetDefaultQueryFilter(), Walls.Edges)
			|| Walls.Edges.IsEmpty())
		{
			return Bound;
		}
		// The centre itself where it lies off the navmesh; its spot on the navmesh would sit right on a wall.
		FindInsideSign(Walls, FVector2D(Footprint.Center), Offset <= 1.f);

		TArray<FSample, TInlineAllocator<64>> Samples;
		GetSamples(Footprint, Samples);
		const FVector2D Origin(Footprint.Center);
		const FVector2D Axis(Footprint.Axis);
		const FVector2D Side = Perpendicular(Axis);
		float Clearance = Bound;
		for (const FSample& Sample : Samples)
		{
			const FVector2D Point = Origin + Axis * Sample.Local.X + Side * Sample.Local.Y;
			FVector2D Inward;
			const float ToObstacle = GetSignedDistance(Walls, Point, Inward) + Erosion;
			const float SampleClearance = ToObstacle - Sample.Radius - Margin;
			if (SampleClearance < Clearance)
			{
				Clearance = SampleClearance;
				if (OutAway)
				{
					*OutAway = Inward;
				}
			}
		}
		return Clearance;
	}

	float GetNavUncertainty(const ARecastNavMesh& NavMesh)
	{
		return NavMesh.GetCellSize(ENavigationDataResolution::Default) + GetMargin(NavMesh);
	}

	bool IsSolidClear(const UWorld& World, const FRTSFootprint& Footprint, float GroundZ, float Height, float Skin, const FVector& GroundNormal)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSHullClearance::IsSolidClear);

		if (Height <= SolidStepHeight)
		{
			return true;
		}
		const FHullBox Box = MakeHullBox(Footprint, GroundZ, GroundNormal, SolidStepHeight, Height, Skin);
		return !World.OverlapAnyTestByObjectType(Box.Center, Box.Rotation, RTSBarrelClearance::GetObstacleObjects(),
			FCollisionShape::MakeBox(Box.HalfExtent), FCollisionQueryParams(SolidQueryTag));
	}

	FHullBox MakeHullBox(const FRTSFootprint& Footprint, float GroundZ, const FVector& GroundNormal, float StepHeight, float Height, float Skin)
	{
		const FVector Up = GroundNormal.Z > UE_KINDA_SMALL_NUMBER ? GroundNormal.GetSafeNormal() : FVector::UpVector;
		FHullBox Box;
		Box.Rotation = FRotationMatrix::MakeFromZX(Up, FVector(Footprint.Axis.X, Footprint.Axis.Y, 0.f)).ToQuat();
		const float Floor = FMath::Min(StepHeight, Height);
		Box.HalfExtent = FVector(Footprint.HalfSegment + Footprint.Radius + Skin, Footprint.Radius + Skin, 0.5f * (Height - Floor));
		// Up from the ground under the centre, along the normal: the box's floor runs parallel to the slope.
		Box.Center = FVector(Footprint.Center.X, Footprint.Center.Y, GroundZ) + Box.Rotation.GetUpVector() * (0.5f * (Floor + Height));
		return Box;
	}

	FRTSFootprint MakeFootprint(const FRTSUnitShape& Shape, const FVector& Location, const FVector2D& Heading)
	{
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X));
		return RTSUnitShape::MakeFootprint(Shape, FTransform(FRotator(0.f, Yaw, 0.f), Location));
	}

	void AdjustPath(const ARecastNavMesh& NavMesh, const FRTSUnitShape& Shape, TArray<FNavPathPoint>& Points, const FVector* Beyond)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSHullClearance::AdjustPath);

		if (Points.Num() < 2)
		{
			return;
		}
		auto HeadingOf = [&Points](int32 Segment)
		{
			return FVector2D(Points[Segment + 1].Location - Points[Segment].Location).GetSafeNormal();
		};
		auto ClearanceAt = [&NavMesh, &Shape](const FVector& Location, const FVector2D& Heading, FVector2D* OutAway = nullptr)
		{
			return Heading.IsNearlyZero() ? FLT_MAX : GetClearance(NavMesh, MakeFootprint(Shape, Location, Heading), OutAway);
		};

		// The end: walk back along the path until the hull, facing the way it drives there, clears what it ran into.
		const int32 LastSegment = Points.Num() - 2;
		if (ClearanceAt(Points.Last().Location, HeadingOf(LastSegment)) < 0.f)
		{
			const FRTSFootprint Footprint = MakeFootprint(Shape, Points.Last().Location, FVector2D(1.f, 0.f));
			// Never further back than the whole hull plus the navmesh's erosion: past that the hull cannot reach the obstacle.
			float Budget = GetHalfDiagonal(Footprint) + GetErosion(NavMesh) + SampleStep;
			for (int32 Segment = LastSegment; Segment >= 0 && Budget > 0.f; --Segment)
			{
				const FVector Start = Points[Segment].Location;
				const FVector End = Points[Segment + 1].Location;
				const FVector2D Heading = HeadingOf(Segment);
				const float Length = FVector::Dist2D(Start, End);
				// From the segment's end toward its start: Blocked is still in the obstacle, Clear is the first spot out of it.
				float Blocked = Length;
				float Found = -1.f;
				for (float Back = SampleStep; Back < Length + SampleStep && Budget > 0.f; Back += SampleStep, Budget -= SampleStep)
				{
					const float Along = FMath::Max(Length - Back, 0.f);
					if (ClearanceAt(FMath::Lerp(Start, End, Length > 0.f ? Along / Length : 0.f), Heading) >= 0.f)
					{
						Found = Along;
						break;
					}
					Blocked = Along;
				}
				if (Found < 0.f)
				{
					continue;
				}
				while (Blocked - Found > EndPrecision)
				{
					const float Middle = 0.5f * (Found + Blocked);
					(ClearanceAt(FMath::Lerp(Start, End, Middle / Length), Heading) >= 0.f ? Found : Blocked) = Middle;
				}
				Points.SetNum(Segment + 2);
				Points.Last().Location = FMath::Lerp(Start, End, Length > 0.f ? Found / Length : 0.f);
				break;
			}
		}
		else if (Beyond)
		{
			// The path ends where the navmesh does, short of a goal in an obstacle: a hull shorter than the navmesh's erosion
			// drives on toward it until its front meets the obstacle, so it does not stop short by the difference.
			const FVector End = Points.Last().Location;
			const FVector2D Toward = FVector2D(*Beyond - End);
			const float Distance = FMath::Min(static_cast<float>(Toward.Size()), GetErosion(NavMesh));
			const FVector2D Heading = Toward.GetSafeNormal();
			auto At = [&End, &Heading](float Along) { return End + FVector(Heading * Along, 0.f); };
			float Clear = 0.f;
			float Blocked = Distance;
			if (Distance > EndPrecision && ClearanceAt(At(Distance), Heading) >= 0.f)
			{
				Clear = Distance;
			}
			else
			{
				while (Blocked - Clear > EndPrecision)
				{
					const float Middle = 0.5f * (Clear + Blocked);
					(ClearanceAt(At(Middle), Heading) >= 0.f ? Clear : Blocked) = Middle;
				}
			}
			if (Clear > EndPrecision)
			{
				FNavPathPoint Added = Points.Last();
				Added.Location = At(Clear);
				Points.Add(Added);
			}
		}

		// The last cell before an obstacle: the navmesh may end up to GetNavUncertainty short of its true face, so the hull
		// drives on along its last stretch while its box, a small gap larger, stays clear of anything solid.
		const UWorld* World = NavMesh.GetWorld();
		if (Beyond && World)
		{
			const FVector End = Points.Last().Location;
			const FVector2D Heading = HeadingOf(Points.Num() - 2);
			const float Distance = FMath::Min(FVector2D::DotProduct(FVector2D(*Beyond - End), Heading), GetNavUncertainty(NavMesh));
			const float Height = Shape.Hull.GetSize().Z;
			auto At = [&End, &Heading](float Along) { return End + FVector(Heading * Along, 0.f); };
			auto SolidClearAt = [&](float Along)
			{
				return IsSolidClear(*World, MakeFootprint(Shape, At(Along), Heading), End.Z, Height, ContactGap);
			};
			if (!Heading.IsNearlyZero() && Distance > EndPrecision && SolidClearAt(0.f))
			{
				float Clear = 0.f;
				float Blocked = Distance;
				if (SolidClearAt(Distance))
				{
					Clear = Distance;
				}
				else
				{
					while (Blocked - Clear > EndPrecision)
					{
						const float Middle = 0.5f * (Clear + Blocked);
						(SolidClearAt(Middle) ? Clear : Blocked) = Middle;
					}
				}
				Points.Last().Location = At(Clear);
			}
		}

		// The corners: away from the walls beside them, until the hull clears them on the way in, the way out and in between.
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			for (int32 Iteration = 0; Iteration < CornerIterations; ++Iteration)
			{
				const FVector Location = Points[Index].Location;
				const FVector2D In = FVector2D(Location - Points[Index - 1].Location).GetSafeNormal();
				const FVector2D Out = FVector2D(Points[Index + 1].Location - Location).GetSafeNormal();
				// The hull turns from In to Out about here, the short way round: every heading on the way, in steps.
				const float InYaw = FMath::Atan2(In.Y, In.X);
				const float Turn = FMath::FindDeltaAngleRadians(InYaw, FMath::Atan2(Out.Y, Out.X));
				const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Turn) / FMath::DegreesToRadians(CornerTurnStep)));
				float Worst = FLT_MAX;
				FVector2D Away = FVector2D::ZeroVector;
				for (int32 Step = 0; Step <= Steps; ++Step)
				{
					const float Yaw = InYaw + Turn * Step / Steps;
					const FVector2D Heading(FMath::Cos(Yaw), FMath::Sin(Yaw));
					FVector2D HeadingAway;
					const float Clearance = ClearanceAt(Location, Heading, &HeadingAway);
					if (Clearance < Worst)
					{
						Worst = Clearance;
						Away = HeadingAway;
					}
				}
				if (Worst >= 0.f || Away.IsNearlyZero())
				{
					break;
				}
				FNavLocation Moved;
				const FVector Target = Location + FVector(Away * (1.f - Worst), 0.f);
				if (!NavMesh.ProjectPoint(Target, Moved, FVector(SampleStep, SampleStep, ProjectHeight)))
				{
					break;
				}
				Points[Index].Location = Moved.Location;
				Points[Index].NodeRef = Moved.NodeRef;
			}
		}
	}
}

bool FRTSHullClearanceCache::IsClear(const FRTSFootprint& Footprint, double Now) const
{
	if (Slack < 0.f || Now > ExpireTime)
	{
		return false;
	}
	// A turn swings the hull's furthest point by its half diagonal times the angle, a move shifts every point by its length.
	const float Turn = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(Axis, FVector2D(Footprint.Axis)), -1.f, 1.f));
	const float Moved = FVector2D::Distance(Center, FVector2D(Footprint.Center)) + Turn * GetHalfDiagonal(Footprint);
	return Moved < Slack;
}

namespace
{
	FRTSSolidStats GSolidStats;

	/** Adds the cycles of its scope to Seconds. */
	struct FSolidTimer
	{
		double& Seconds;
		const uint64 Start = FPlatformTime::Cycles64();
		explicit FSolidTimer(double& InSeconds) : Seconds(InSeconds) {}
		~FSolidTimer() { Seconds += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Start); }
	};

	/** The broad overlap's results, kept between calls so a look round a hull allocates nothing once warm. */
	TArray<FOverlapResult> GSolidOverlaps;

	/** How deep a box of Shape at Box's pose reaches into Component; zero when it does not. */
	float GetDepth(UPrimitiveComponent& Component, const RTSHullClearance::FHullBox& Box, const FCollisionShape& Shape)
	{
		FMTDResult Result;
		return Component.ComputePenetration(Result, Shape, Box.Center, Box.Rotation) ? Result.Distance : 0.f;
	}
}

bool FRTSSolidNeighbourhood::IsCurrent(const FRTSFootprint& Footprint, double Now) const
{
	// The looked-at box is level and as wide as the hull's half diagonal at any heading, and as tall as Margin allows a slope.
	return Margin >= 0.f && Now <= ExpireTime && FVector2D::Distance(FVector2D(Center), FVector2D(Footprint.Center)) <= Margin
		&& FMath::Abs(Center.Z - Footprint.Center.Z) <= Margin;
}

void FRTSSolidNeighbourhood::Update(const UWorld& World, const FRTSFootprint& Footprint, float GroundZ, float Height, float InMargin,
	double Now, double Lifetime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRTSSolidNeighbourhood::Update);
	FSolidTimer Timer(GSolidStats.BroadSeconds);
	++GSolidStats.Updates;

	Center = Footprint.Center;
	Margin = InMargin;
	ExpireTime = Now + Lifetime;
	Solids.Reset();
	// Any heading within Margin of the centre, on a slope of up to 45 degrees from there: the hull's half diagonal plus the
	// margin each way across, and that much more up and down than the hull is tall.
	const float Across = GetHalfDiagonal(Footprint) + Margin;
	const FVector HalfExtent(Across, Across, 0.5f * Height + Across);
	const FVector BoxCenter(Footprint.Center.X, Footprint.Center.Y, GroundZ + 0.5f * Height);
	GSolidOverlaps.Reset();
	World.OverlapMultiByObjectType(GSolidOverlaps, BoxCenter, FQuat::Identity, RTSBarrelClearance::GetObstacleObjects(),
		FCollisionShape::MakeBox(HalfExtent), FCollisionQueryParams(SolidQueryTag));
	for (const FOverlapResult& Overlap : GSolidOverlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		// What navigation knows is the navmesh's to keep the hull out of; what does not stop a pawn is not solid.
		if (Component && !Component->IsNavigationRelevant() && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
		{
			Solids.AddUnique(Component);
		}
	}
	GSolidOverlaps.Reset();
}

bool FRTSSolidNeighbourhood::IsClear(const RTSHullClearance::FHullBox& Box, const RTSHullClearance::FHullBox& Current, float Tolerance) const
{
	if (Solids.IsEmpty())
	{
		return true;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(FRTSSolidNeighbourhood::IsClear);
	FSolidTimer Timer(GSolidStats.NarrowSeconds);
	++GSolidStats.Checks;

	const FCollisionShape Shape = FCollisionShape::MakeBox((Box.HalfExtent - FVector(Tolerance)).ComponentMax(FVector(1.f)));
	const FCollisionShape CurrentShape = FCollisionShape::MakeBox((Current.HalfExtent - FVector(Tolerance)).ComponentMax(FVector(1.f)));
	for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Solids)
	{
		UPrimitiveComponent* Component = Weak.Get();
		if (!Component || !Component->IsCollisionEnabled())
		{
			continue;
		}
		++GSolidStats.ComponentQueries;
		if (!Component->OverlapComponent(Box.Center, Box.Rotation, Shape))
		{
			continue;
		}
		// In it where it stands too: out, or along, but no deeper.
		GSolidStats.ComponentQueries += 3;
		if (!Component->OverlapComponent(Current.Center, Current.Rotation, CurrentShape)
			|| GetDepth(*Component, Box, Shape) > GetDepth(*Component, Current, CurrentShape) + KINDA_SMALL_NUMBER)
		{
			return false;
		}
	}
	return true;
}

void FRTSSolidStats::AddReuse()
{
	++GSolidStats.Reuses;
}

FRTSSolidStats FRTSSolidStats::Take()
{
	const FRTSSolidStats Stats = GSolidStats;
	GSolidStats = FRTSSolidStats();
	return Stats;
}
