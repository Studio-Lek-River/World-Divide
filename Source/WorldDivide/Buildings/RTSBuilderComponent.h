#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSBuilderComponent.generated.h"

class ARTSBuilding;
class ARTSPlayerState;
class ARTSUnit;
class URTSBuilderData;
class URTSBuildingData;
struct FRTSBuildingFootprint;
struct FRTSCommand;

/** A building a builder has been ordered to place but has not placed yet: the current order's or a Shift-queued one. */
USTRUCT()
struct WORLDDIVIDE_API FRTSPlannedSite
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<URTSBuildingData> Data = nullptr;

	/** Snapped footprint centre. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	uint8 QuarterTurns = 0;
};

UENUM()
enum class ERTSBuildState : uint8
{
	Idle,
	ToSite,
	Constructing,
};

/**
 * Server-side Build order of a builder unit: walk to a spot just outside the site's footprint, place the building there
 * once standing next to it (with the same faction and spot checks as any placement) and drive its construction for as long
 * as the builder stays. A new building is paid when it is ordered (ChargeOrder), also when queued; the order carries the
 * payment until StartBuild takes it over, and the placed site keeps it as its paid cost. An order that ends before its
 * building is placed is refunded in full. Own units standing on the spot are first sent aside; anything else there makes
 * the placement fail. A site whose builder leaves or dies stays where it is, paused, until a builder is sent to it again.
 * Every unit has one; it stays inert unless the unit's data is URTSBuilderData.
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
	 * Server only. Pays for a Build order's new building: takes its cost from the owner's player and puts it on the command.
	 * False when the builder cannot build it, the player lacks one of its prerequisites, it overlaps a site an own builder
	 * plans (RTSBuildPlacement::OverlapsPlannedSite) or the player cannot afford it; nothing is charged then. An order naming
	 * an own construction site costs nothing and needs no prerequisites.
	 */
	bool ChargeOrder(FRTSCommand& Command);

	/** Server only. Gives back what the command still holds for a building it never placed. */
	void RefundOrder(FRTSCommand& Command);

	/**
	 * Server only. Starts a Build order: a new site of the command's EntityData at its TargetLocation, paid through
	 * ChargeOrder, or, when TargetActor is set, the own construction site it names. The payment moves from the command to
	 * the builder. False when the builder cannot take the order.
	 */
	bool StartBuild(FRTSCommand& Command);

	/**
	 * Server only. Leaves the site as it is; nothing more gets built until a builder returns. A building paid for but not
	 * placed yet is refunded.
	 */
	void StopBuild();

	/**
	 * Server only. Stops the order so it can start again from Command later: a placed site is named in it to be resumed, a
	 * building not placed yet keeps its payment there instead of being refunded.
	 */
	void SuspendBuild(FRTSCommand& Command);

	/** Server only. Advances the order; returns false once it has ended. */
	bool UpdateBuild(float DeltaSeconds);

	ERTSBuildState GetState() const { return State; }

	/** The building being constructed, or null before it is placed. */
	ARTSBuilding* GetSite() const { return Site.Get(); }

	/** Buildings this builder was ordered to place and has not placed yet, in order. Replicated so ghosts can avoid them. */
	const TArray<FRTSPlannedSite>& GetPlannedSites() const { return PlannedSites; }

	/** Server only. Rebuilds the planned sites from the owner's order queue; the owner calls it whenever the queue changes. */
	void UpdatePlannedSites(const TArray<FRTSCommand>& Queue);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Places the planned building next to the builder, already paid. False when the placement is refused. */
	bool PlaceSite();

	/** The player of the owner's team, who pays for its buildings. */
	ARTSPlayerState* FindPlayer() const;

	/**
	 * Sends own units standing on the planned footprint to the nearest reachable spot outside it. True while waiting for them
	 * to leave; false when the spot is free, blocked by anything else, or the wait has timed out.
	 */
	bool ClearSite(float DeltaSeconds);

	/**
	 * Pathfinds to a reachable spot just outside Footprint, unless a move is running or a retry is on hold. False when no
	 * such spot exists.
	 */
	bool ApproachSite(const FRTSBuildingFootprint& Footprint);

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

	uint8 PlannedQuarterTurns = 0;

	/** What was paid for the planned building: the site's paid cost once placed, refunded if it never is. */
	int32 PlannedCost = 0;

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

	UPROPERTY(Replicated)
	TArray<FRTSPlannedSite> PlannedSites;
};
