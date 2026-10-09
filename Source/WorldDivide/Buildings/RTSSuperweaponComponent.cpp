#include "Buildings/RTSSuperweaponComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace RTSSuperweapon
{
	float ComputeFalloffDamage(float Damage, float Distance, float Radius, float EdgeFraction)
	{
		if (Distance > Radius)
		{
			return 0.f;
		}
		if (Radius <= 0.f)
		{
			return Damage;
		}
		const float Alpha = FMath::Clamp(Distance / Radius, 0.f, 1.f);
		return Damage * FMath::Lerp(1.f, FMath::Clamp(EdgeFraction, 0.f, 1.f), Alpha);
	}

	void ApplyStrike(UWorld& World, const FVector& Center, const URTSBuildingData& Data, AActor* Instigator)
	{
		// Gathered first: damage destroys what it kills, which must not happen while iterating.
		TArray<TPair<TWeakObjectPtr<AActor>, float>> Hits;
		auto Consider = [&](AActor& Actor, const IRTSTargetable& Target)
		{
			if (Actor.IsActorBeingDestroyed() || Target.GetHealthComponent()->IsDead())
			{
				return;
			}
			// A unit's footprint point lies on its selection radius; its centre is what stands in the blast.
			const FVector Point = Actor.IsA<ARTSUnit>() ? Actor.GetActorLocation() : Target.GetClosestFootprintPoint(Center);
			const float Distance = FVector::Dist2D(Point, Center);
			const float Damage = ComputeFalloffDamage(Data.SuperweaponDamage, Distance, Data.SuperweaponRadius, Data.SuperweaponEdgeDamage);
			if (Damage > 0.f)
			{
				Hits.Emplace(&Actor, Damage);
			}
		};
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			// A ground strike: aircraft in flight are out of it.
			if (!It->IsAirborne())
			{
				Consider(**It, **It);
			}
		}
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			Consider(**It, **It);
		}

		for (const TPair<TWeakObjectPtr<AActor>, float>& Hit : Hits)
		{
			const IRTSTargetable* Target = Cast<IRTSTargetable>(Hit.Key.Get());
			if (Target && !Target->GetHealthComponent()->IsDead())
			{
				Target->GetHealthComponent()->ApplyDamage(Hit.Value, Instigator);
			}
		}
	}
}

URTSSuperweaponComponent::URTSSuperweaponComponent()
{
	// The building's tick drives the charge once it is constructed.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

ARTSBuilding* URTSSuperweaponComponent::GetBuilding() const
{
	return Cast<ARTSBuilding>(GetOwner());
}

const URTSBuildingData* URTSSuperweaponComponent::GetData() const
{
	const ARTSBuilding* Building = GetBuilding();
	return Building ? Building->GetBuildingData() : nullptr;
}

bool URTSSuperweaponComponent::IsSuperweapon() const
{
	const URTSBuildingData* Data = GetData();
	return Data && Data->SuperweaponChargeTime > 0.f;
}

bool URTSSuperweaponComponent::IsOperational() const
{
	const ARTSBuilding* Building = GetBuilding();
	return IsSuperweapon() && Building->IsConstructed() && !Building->IsActorBeingDestroyed() && !Building->GetHealthComponent()->IsDead();
}

bool URTSSuperweaponComponent::CanLaunch() const
{
	return IsOperational() && ChargeRemaining <= 0.f && !bStrikePending;
}

void URTSSuperweaponComponent::StartCharging()
{
	check(GetOwner()->HasAuthority());
	if (IsSuperweapon())
	{
		ChargeRemaining = GetData()->SuperweaponChargeTime;
	}
}

void URTSSuperweaponComponent::UpdateCharge(float DeltaSeconds)
{
	check(GetOwner()->HasAuthority());
	if (!IsOperational())
	{
		return;
	}
	ARTSBuilding* Building = GetBuilding();

	if (bStrikePending)
	{
		StrikeRemaining = FMath::Max(StrikeRemaining - DeltaSeconds, 0.f);
		if (StrikeRemaining <= 0.f)
		{
			bStrikePending = false;
			UE_LOG(LogWorldDivide, Log, TEXT("Team %d's superweapon strikes at %s"), Building->GetTeamId(), *StrikeLocation.ToString());
			RTSSuperweapon::ApplyStrike(*GetWorld(), StrikeLocation, *GetData(), Building);
			// The strike may have destroyed the superweapon itself.
			if (!IsValid(Building) || !IsOperational())
			{
				return;
			}
		}
	}

	if (ChargeRemaining <= 0.f)
	{
		bChargePaused = false;
		return;
	}
	// Unlike production, which only slows, the charge holds outright on low power.
	bChargePaused = ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), Building->GetTeamId());
	if (bChargePaused)
	{
		return;
	}
	ChargeRemaining = FMath::Max(ChargeRemaining - DeltaSeconds, 0.f);
	if (ChargeRemaining <= 0.f)
	{
		ARTSPlayerController::NotifySuperweapon(*Building, ERTSSuperweaponAlert::Ready);
	}
}

bool URTSSuperweaponComponent::Launch(const FVector& Location)
{
	check(GetOwner()->HasAuthority());
	if (!CanLaunch())
	{
		return false;
	}
	const URTSBuildingData* Data = GetData();
	bStrikePending = true;
	StrikeLocation = Location;
	StrikeRemaining = Data->SuperweaponDelay;
	ChargeRemaining = Data->SuperweaponChargeTime;
	// Without a delay the strike lands at once.
	if (StrikeRemaining <= 0.f)
	{
		UpdateCharge(0.f);
	}
	return true;
}

void URTSSuperweaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URTSSuperweaponComponent, ChargeRemaining);
	DOREPLIFETIME(URTSSuperweaponComponent, bChargePaused);
	DOREPLIFETIME(URTSSuperweaponComponent, bStrikePending);
	DOREPLIFETIME(URTSSuperweaponComponent, StrikeLocation);
	DOREPLIFETIME(URTSSuperweaponComponent, StrikeRemaining);
}
