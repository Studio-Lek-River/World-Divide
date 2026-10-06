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
 * then the harvester moves on to the nearest other source within its search radius, or the run ends. A depot without a
 * reachable spot around it is skipped for the next nearest; the run ends, load on board, once none is reachable. A source the
 * harvester picked itself is skipped the same way; a source named in the Harvest order that cannot be reached ends the run.
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

	/** The constructed depot of TeamId, not in Exclude, whose footprint edge is nearest From, or null. */
	static ARTSBuilding* FindNearestDepot(const UWorld& World, int32 TeamId, const FVector& From,
		TConstArrayView<TWeakObjectPtr<ARTSBuilding>> Exclude = {});

private:
	/** Heads for a depot with the load, or for another source when nothing is on board. False when the run is over. */
	bool ContinueAfterSourceEmpty();

	/** Picks the nearest source with supplies in reach of the owner. False when there is none. */
	bool PickNextSource();

	void Deposit();
	void StopOwnerMovement() const;

	/**
	 * Pathfinds to a reachable spot just outside the footprint at Center, unless a move is running or a retry is on hold.
	 * False when no such spot exists.
	 */
	bool ApproachDock(const FVector& Center, const FIntPoint& Footprint);

	/** Ends the run because Dock cannot be reached; any load stays on board. Always false. */
	bool EndUnreachable(const AActor& Dock);

	/** Leaves the unreachable self-picked source for good and carries on as if it ran dry. False when the run is over. */
	bool SkipUnreachableSource();

	/** Leaves the unreachable depot for this delivery; the next tick looks for the next nearest one. Always true. */
	bool SkipUnreachableDepot();

	/** The owner stands within reach of EdgePoint. */
	bool IsDocked(const FVector& EdgePoint) const;

	int32 GetTeamId() const;

	UPROPERTY(Transient)
	TObjectPtr<URTSHarvesterData> HarvesterData;

	TWeakObjectPtr<ARTSSupplySource> Source;
	TWeakObjectPtr<ARTSBuilding> Depot;

	/** Sources found unreachable this run, and depots found unreachable on this delivery. */
	TArray<TWeakObjectPtr<ARTSSupplySource>> SkippedSources;
	TArray<TWeakObjectPtr<ARTSBuilding>> SkippedDepots;

	/** The source was named in the Harvest order rather than picked by the harvester. */
	bool bSourceOrdered = false;

	ERTSHarvestState State = ERTSHarvestState::Idle;
	int32 Carried = 0;
	float GatherTimeLeft = 0.f;

	/** Seconds before a failed approach or a search for a depot is tried again. */
	float RetryCooldown = 0.f;
};
