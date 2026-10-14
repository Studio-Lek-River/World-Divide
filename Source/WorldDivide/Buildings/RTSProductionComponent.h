#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSProductionComponent.generated.h"

class ARTSBuilding;
class ARTSPlayerState;
class URTSBuildingData;
class URTSEntityData;
class URTSUnitData;
class URTSUpgradeData;

namespace RTSProduction
{
	/** The first of Data's prerequisites not in Owned, or null when Owned holds them all. */
	WORLDDIVIDE_API const URTSEntityData* FindMissingPrerequisite(const TSet<const URTSEntityData*>& Owned, const URTSEntityData& Data);

	/** Every one of Data's prerequisites is in Owned. */
	WORLDDIVIDE_API bool IsAvailable(const TSet<const URTSEntityData*>& Owned, const URTSEntityData& Data);

	/** What TeamId owns for prerequisites: its constructed, standing buildings, its living units and its researched upgrades. */
	WORLDDIVIDE_API TSet<const URTSEntityData*> GatherOwned(const UWorld& World, int32 TeamId);

	/**
	 * The first of Data's prerequisites TeamId does not own, or null when it owns them all. The client's menus and the
	 * server's checks share it; it only gates new orders, so what is already queued or paid for carries on.
	 */
	WORLDDIVIDE_API const URTSEntityData* FindMissingPrerequisite(const UWorld& World, int32 TeamId, const URTSEntityData& Data);

	/** Upgrade waits in the queue of one of TeamId's buildings, so it cannot be queued again anywhere. */
	WORLDDIVIDE_API bool IsUpgradeQueued(const UWorld& World, int32 TeamId, const URTSUpgradeData& Upgrade);

	/**
	 * TeamId owns Data's MaxCount of it, construction sites included, so it cannot order another. Never true for a building
	 * without a limit. The client's menus and the server's checks share it.
	 */
	WORLDDIVIDE_API bool IsAtBuildLimit(const UWorld& World, int32 TeamId, const URTSBuildingData& Data);
}

/**
 * The build queue of a production building, such as a barracks: units and upgrades in one queue. Queueing charges the entry's
 * cost, cancelling refunds it. The front entry builds over its BuildTime, slower while the owner is low on power; a finished
 * unit comes out just outside the footprint and walks to the rally point, a finished aircraft parks on a pad of the airfield,
 * a finished upgrade goes to the owner's player.
 * Every building has one; it stays inert unless the building is constructed and its data lists units or upgrades. The server
 * runs it; the queue, progress and rally point replicate for the HUD.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSProductionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSProductionComponent();

	static constexpr int32 MaxQueue = 10;

	/** The owner is a constructed, standing building whose data lists units or upgrades. */
	bool CanProduce() const;

	/** CanProduce, and Data is on the building's list of units or of upgrades. */
	bool CanProduce(const URTSEntityData* Data) const;

	/**
	 * Server only. Appends Data, a unit or an upgrade, and charges its cost to Player. Refused, changing nothing, unless the
	 * building is Player's and can produce Data, Player owns its prerequisites and can afford it, and the queue is not full;
	 * an upgrade also when Player has researched it or it is queued at any of Player's buildings, an aircraft also when the
	 * building has no free pad for it.
	 */
	bool Enqueue(ARTSPlayerState& Player, URTSEntityData* Data);

	/**
	 * Server only. Removes the last queued entry of Data and refunds its cost to Player, so progress on the front entry is
	 * kept while a later one of the same kind is left to cancel. False when Player does not own the building or Data is not queued.
	 */
	bool CancelLast(ARTSPlayerState& Player, const URTSEntityData* Data);

	/**
	 * Server only. Removes the queue entry at Index and refunds its cost to Player, restarting progress when it was the front
	 * one. False when Player does not own the building or the entry at Index is not Data (the queue moved on since).
	 */
	bool CancelAt(ARTSPlayerState& Player, int32 Index, const URTSEntityData* Data);

	/** Server only. Where finished units walk to. */
	void SetRallyPoint(const FVector& Location);

	/**
	 * Server only. Advances the front entry; once complete, spawns a unit and sends it to the rally point, or completes an
	 * upgrade for the owner's player.
	 */
	void UpdateProduction(float DeltaSeconds);

	const TArray<TObjectPtr<URTSEntityData>>& GetQueue() const { return Queue; }

	/** 0 to 1, of the front entry. */
	float GetProgress() const { return Progress; }

	FVector GetRallyPoint() const { return RallyPoint; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	ARTSBuilding* GetBuilding() const;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<URTSEntityData>> Queue;

	UPROPERTY(Replicated)
	float Progress = 0.f;

	UPROPERTY(Replicated)
	FVector RallyPoint = FVector::ZeroVector;
};
