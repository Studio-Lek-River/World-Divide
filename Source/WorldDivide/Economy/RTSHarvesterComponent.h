#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSHarvesterComponent.generated.h"

class ARTSBuilding;
class ARTSSupplySource;
class URTSHarvesterData;

UENUM()
enum class ERTSHarvestState : uint8
{
	Idle,
	ToSource,
	Gathering,
	ToDepot,
};

/**
 * Server-side supply run of a harvester unit: drive to a source, load for GatherSeconds, drive to the nearest own depot
 * and pay the load to the team's player as credits, then go again. When the source runs dry the load is delivered first,
 * then the harvester moves on to the nearest other source within its search radius, or the run ends.
 * Every unit has one; it stays inert unless the unit's data is URTSHarvesterData. The unit's Harvest order drives it.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSHarvesterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSHarvesterComponent();

	/** Server only. Null means the owner cannot harvest. */
	void SetHarvesterData(URTSHarvesterData* InHarvesterData) { HarvesterData = InHarvesterData; }

	bool CanHarvest() const { return HarvesterData != nullptr; }

	/** Server only. Starts the run at Source, or at the nearest source in reach when null. False when there is nothing to do. */
	bool StartHarvest(ARTSSupplySource* InSource);

	/** Server only. Ends the run where it stands; a load on board stays there for the next run. */
	void StopHarvest();

	/** Server only. Advances the run; returns false once it has ended. */
	bool UpdateHarvest(float DeltaSeconds);

	ERTSHarvestState GetState() const { return State; }
	ARTSSupplySource* GetSource() const { return Source.Get(); }
	int32 GetCarried() const { return Carried; }

	/** The constructed depot of TeamId whose footprint edge is nearest From, or null. */
	static ARTSBuilding* FindNearestDepot(const UWorld& World, int32 TeamId, const FVector& From);

private:
	/** Heads for a depot with the load, or for another source when nothing is on board. False when the run is over. */
	bool ContinueAfterSourceEmpty();

	/** Picks the nearest source with supplies in reach of the owner. False when there is none. */
	bool PickNextSource();

	void Deposit();
	void StopOwnerMovement() const;

	/** Pathfinds to just outside the footprint edge at EdgePoint, unless a move is running or a retry is on hold. */
	void ApproachDock(const FVector& EdgePoint);

	/** The owner stands within reach of EdgePoint. */
	bool IsDocked(const FVector& EdgePoint) const;

	int32 GetTeamId() const;

	UPROPERTY(Transient)
	TObjectPtr<URTSHarvesterData> HarvesterData;

	TWeakObjectPtr<ARTSSupplySource> Source;
	TWeakObjectPtr<ARTSBuilding> Depot;
	ERTSHarvestState State = ERTSHarvestState::Idle;
	int32 Carried = 0;
	float GatherTimeLeft = 0.f;

	/** Seconds before a failed approach or a search for a depot is tried again. */
	float RetryCooldown = 0.f;
};
