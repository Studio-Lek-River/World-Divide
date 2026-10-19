#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSSuperweaponComponent.generated.h"

class ARTSBuilding;
class URTSBuildingData;

namespace RTSSuperweapon
{
	/**
	 * Server only. Deals Data's strike at Center to every unit on the ground and every building, of any team, within its
	 * radius, through RTSCombat::ApplyAreaDamage.
	 */
	WORLDDIVIDE_API void ApplyStrike(UWorld& World, const FVector& Center, const URTSBuildingData& Data, AActor* Instigator);
}

/**
 * The charge and launch of a superweapon. Once its building is constructed it charges over SuperweaponChargeTime, holding
 * while the owner is low on power; charged, it is ready to launch at a point, where the strike lands SuperweaponDelay later
 * and the charge starts over. Opponents are warned when it is built and when it is ready, the owner when it is ready.
 * Every building has one; it stays inert unless its data has a charge time. The server runs it; the charge and a pending
 * strike replicate to every player.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSSuperweaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSSuperweaponComponent();

	/** The building's data makes it a superweapon. */
	bool IsSuperweapon() const;

	/** The owner is a constructed, standing superweapon. */
	bool IsOperational() const;

	/** Operational, charged and with no strike on its way. */
	bool CanLaunch() const;

	/** Server only. Fills the charge for its first countdown; called once the building is constructed. */
	void StartCharging();

	/**
	 * Server only. Counts the charge down, unless the owner is low on power, warning the players once it is ready, and lands a
	 * pending strike once its delay is over.
	 */
	void UpdateCharge(float DeltaSeconds);

	/** Server only. Sends a strike to Location and starts the charge over; false, changing nothing, unless CanLaunch. */
	bool Launch(const FVector& Location);

	/** Seconds of charging left; 0 once ready. */
	float GetChargeRemaining() const { return ChargeRemaining; }

	/** The charge holds because the owner is low on power. */
	bool IsChargePaused() const { return bChargePaused; }

	/** A strike is on its way to GetStrikeLocation, landing in GetStrikeRemaining seconds. */
	bool IsStrikePending() const { return bStrikePending; }
	FVector GetStrikeLocation() const { return StrikeLocation; }
	float GetStrikeRemaining() const { return StrikeRemaining; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	ARTSBuilding* GetBuilding() const;
	const URTSBuildingData* GetData() const;

	UPROPERTY(Replicated)
	float ChargeRemaining = 0.f;

	UPROPERTY(Replicated)
	bool bChargePaused = false;

	UPROPERTY(Replicated)
	bool bStrikePending = false;

	UPROPERTY(Replicated)
	FVector StrikeLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float StrikeRemaining = 0.f;
};
