#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSAvoidanceSubsystem.generated.h"

class ARTSUnit;

/**
 * Server-side local avoidance between ground units. Units steer by the navmesh and their own path only; once they have
 * moved each frame, this pushes every pair whose circles overlap apart along the line between them, never off the navmesh.
 * A walking unit shoves idle ones aside but gives way to units at work in place, such as a harvester at its source or a
 * unit firing from where it stands. A unit walking into units that stand on its own goal stops where it touches them, so a
 * crowd sent to one point settles around it instead of jostling for it. Aircraft are left out. rts.Avoidance 0 turns it off.
 */
UCLASS()
class WORLDDIVIDE_API URTSAvoidanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Server only. Separates the units once. Public so tests can step it. */
	void Separate();

	/** Real seconds the last Separate took, for the frame-time test. */
	double GetLastSeparateSeconds() const { return LastSeparateSeconds; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FAgent
	{
		ARTSUnit* Unit = nullptr;
		FVector2D Location = FVector2D::ZeroVector;
		FVector2D Velocity = FVector2D::ZeroVector;
		/** The grid cell, row in the high half. */
		int64 CellKey = 0;
		/** Push accumulated this frame. */
		FVector2D Push = FVector2D::ZeroVector;
		float Radius = 0.f;
		/** How readily the unit gives way: its share of an overlap is its yield over the pair's. */
		float Yield = 1.f;
		bool bWalking = false;
		/** Standing with no orders at all. */
		bool bIdle = false;
	};

	TArray<FAgent> Agents;
	TArray<ARTSUnit*> Arrived;
	double LastSeparateSeconds = 0.0;
};
