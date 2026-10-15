#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSAvoidanceSubsystem.generated.h"

class ARTSUnit;

/**
 * Server-side local avoidance between ground units. Units steer by the navmesh and their own path only; once they have
 * moved each frame, this pushes every pair whose footprints overlap apart along the line between them, never off their
 * navmesh, and never faster than half the unit's own speed. Footprints are stadiums (ARTSUnit::GetFootprint), so tanks
 * side by side only touch where their hulls do.
 * A walking unit shoves idle ones aside but gives way to units at work in place, such as a harvester at its source or a
 * unit firing from where it stands. A unit walking into units that stand on its own goal stops where it touches them, so a
 * crowd sent to one point settles around it instead of jostling for it. Aircraft are left out. rts.Avoidance 0 turns it off.
 * The same pass indexes every living unit, aircraft too, in a grid of fixed cells for QueryRadius.
 */
UCLASS()
class WORLDDIVIDE_API URTSAvoidanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Server only. Separates the units once, as DeltaSeconds of separation. Public so tests can step it. */
	void Separate(float DeltaSeconds);

	/**
	 * Server only. Every living unit whose footprint may reach within Radius of Center, aircraft included; callers test
	 * the exact distance. Uses the grid of this frame's or the last frame's pass and scans every unit in a world that has
	 * none, such as one that never ticks.
	 */
	void QueryRadius(const FVector& Center, float Radius, TArray<ARTSUnit*>& OutUnits) const;

	/** Real seconds the last Separate took, for the frame-time test. */
	double GetLastSeparateSeconds() const { return LastSeparateSeconds; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FAgent
	{
		TWeakObjectPtr<ARTSUnit> Unit;
		/** The footprint's centre and the unit vector along its segment. */
		FVector2D Location = FVector2D::ZeroVector;
		FVector2D Axis = FVector2D(1.f, 0.f);
		FVector2D Velocity = FVector2D::ZeroVector;
		/** Push accumulated this frame. */
		FVector2D Push = FVector2D::ZeroVector;
		float HalfSegment = 0.f;
		float Radius = 0.f;
		/** The circle around the whole footprint. */
		float BoundRadius = 0.f;
		/** Furthest the unit is pushed this frame. */
		float MaxStep = 0.f;
		/** How readily the unit gives way: its share of an overlap is its yield over the pair's. */
		float Yield = 1.f;
		/** The cells the bounding circle covers. */
		FIntPoint MinCell = FIntPoint::ZeroValue;
		FIntPoint MaxCell = FIntPoint::ZeroValue;
		/** Ground units that are pushed; aircraft are only indexed. */
		bool bAvoids = false;
		bool bWalking = false;
		/** Standing with no orders at all. */
		bool bIdle = false;
	};

	/** One cell an agent's bounding circle covers. */
	struct FEntry
	{
		/** The grid cell, row in the high half. */
		int64 CellKey = 0;
		int32 Agent = 0;
	};

	/** Gathers the living units into Agents and Entries; true when a pass has anything to push. */
	bool BuildIndex(float DeltaSeconds);

	TArray<FAgent> Agents;
	TArray<FEntry> Entries;
	TArray<ARTSUnit*> Arrived;
	float MaxBoundRadius = 0.f;
	/** GFrameCounter of the last BuildIndex. */
	uint64 IndexFrame = MAX_uint64;
	double LastSeparateSeconds = 0.0;
};
