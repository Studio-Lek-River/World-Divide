#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSBuilderComponent.generated.h"

class ARTSBuilding;
class ARTSUnit;
class URTSBuilderData;
class URTSBuildingData;
struct FRTSCommand;

UENUM()
enum class ERTSBuildState : uint8
{
	Idle,
	ToSite,
	Constructing,
};

/**
 * Server-side Build order of a builder unit: walk to a spot just outside the site's footprint, place the building there
 * once standing next to it (charged then, with the same faction, spot and credit checks as any purchase) and drive its
 * construction for as long as the builder stays. Own units standing on the spot are first sent aside; anything else there
 * makes the purchase fail. A site whose builder leaves or dies stays where it is, paused, until a
 * builder is sent to it again. Every unit has one; it stays inert unless the unit's data is URTSBuilderData.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSBuilderComponent();

	/** Server only. Null means the owner cannot build. */
	void SetBuilderData(URTSBuilderData* InBuilderData) { BuilderData = InBuilderData; }

	bool CanBuild() const { return BuilderData != nullptr; }

	/** Data is on the builder's list. The faction is checked when the building is placed. */
	bool CanBuild(const URTSBuildingData* Data) const;

	/**
	 * Server only. Starts a Build order: a new site of the command's EntityData at its TargetLocation, or, when TargetActor
	 * is set, the own construction site it names. False when the builder cannot take the order.
	 */
	bool StartBuild(const FRTSCommand& Command);

	/** Server only. Leaves the site as it is; nothing more gets built until a builder returns. */
	void StopBuild();

	/** Server only. Advances the order; returns false once it has ended. */
	bool UpdateBuild(float DeltaSeconds);

	ERTSBuildState GetState() const { return State; }

	/** The building being constructed, or null before it is placed. */
	ARTSBuilding* GetSite() const { return Site.Get(); }

private:
	/** Places the planned building next to the builder. False when the purchase is refused. */
	bool PlaceSite();

	/**
	 * Sends own units standing on the planned footprint to the nearest reachable spot outside it. True while waiting for them
	 * to leave; false when the spot is free, blocked by anything else, or the wait has timed out.
	 */
	bool ClearSite(float DeltaSeconds);

	/**
	 * Pathfinds to a reachable spot just outside the footprint at Center, unless a move is running or a retry is on hold.
	 * False when no such spot exists.
	 */
	bool ApproachSite(const FVector& Center, const FIntPoint& Footprint);

	/** Ends the order, logging why. Always false. */
	bool EndBuild(const TCHAR* Reason);

	/** The owner stands next to the footprint edge at EdgePoint, without overlapping the footprint. */
	bool IsDocked(const FVector& EdgePoint) const;

	void StopOwnerMovement() const;

	UPROPERTY(Transient)
	TObjectPtr<URTSBuilderData> BuilderData;

	/** What to place and where, until the site exists. */
	UPROPERTY(Transient)
	TObjectPtr<URTSBuildingData> PlannedData;

	FVector PlannedLocation = FVector::ZeroVector;

	TWeakObjectPtr<ARTSBuilding> Site;

	/** The order names an existing site: once it is gone, the order is over. */
	bool bHasSite = false;

	ERTSBuildState State = ERTSBuildState::Idle;

	/** Seconds before a failed approach is tried again. */
	float RetryCooldown = 0.f;

	/** Seconds spent waiting for own units to leave the planned footprint. */
	float ClearingSeconds = 0.f;

	/** Own units already sent off the planned footprint. */
	TArray<TWeakObjectPtr<ARTSUnit>> ClearedUnits;
};
