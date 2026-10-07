#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSProductionComponent.generated.h"

class ARTSBuilding;
class ARTSPlayerState;
class URTSEntityData;
class URTSUnitData;

namespace RTSProduction
{
	/**
	 * The first of Data's prerequisites TeamId does not own, or null when it owns them all. A building counts once it is
	 * constructed and standing, a unit while it lives. The client's menu and the server's check share it.
	 */
	WORLDDIVIDE_API const URTSEntityData* FindMissingPrerequisite(const UWorld& World, int32 TeamId, const URTSEntityData& Data);
}

/**
 * The build queue of a production building, such as a barracks. Queueing charges the unit's cost, cancelling refunds it.
 * The front entry builds over its BuildTime, slower while the owner is low on power; the finished unit comes out just
 * outside the footprint and walks to the rally point. Every building has one; it stays inert unless the building is
 * constructed and its data lists units. The server runs it; the queue, progress and rally point replicate for the HUD.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSProductionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSProductionComponent();

	static constexpr int32 MaxQueue = 5;

	/** The owner is a constructed, standing building whose data lists units. */
	bool CanProduce() const;

	/** CanProduce, and Data is on the building's list. */
	bool CanProduce(const URTSUnitData* Data) const;

	/**
	 * Server only. Appends Data and charges its cost to Player. Refused, changing nothing, unless the building is Player's and
	 * can produce Data, Player owns its prerequisites and can afford it, and the queue is not full.
	 */
	bool Enqueue(ARTSPlayerState& Player, URTSUnitData* Data);

	/**
	 * Server only. Removes the last queued entry of Data and refunds its cost to Player, so progress on the front entry is
	 * kept while a later one of the same kind is left to cancel. False when Player does not own the building or Data is not queued.
	 */
	bool CancelLast(ARTSPlayerState& Player, const URTSUnitData* Data);

	/** Server only. Where finished units walk to. */
	void SetRallyPoint(const FVector& Location);

	/** Server only. Advances the front entry; once complete, spawns it and sends it to the rally point. */
	void UpdateProduction(float DeltaSeconds);

	const TArray<TObjectPtr<URTSUnitData>>& GetQueue() const { return Queue; }

	/** 0 to 1, of the front entry. */
	float GetProgress() const { return Progress; }

	FVector GetRallyPoint() const { return RallyPoint; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	ARTSBuilding* GetBuilding() const;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<URTSUnitData>> Queue;

	UPROPERTY(Replicated)
	float Progress = 0.f;

	UPROPERTY(Replicated)
	FVector RallyPoint = FVector::ZeroVector;
};
