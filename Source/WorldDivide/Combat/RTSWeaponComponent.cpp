#include "Combat/RTSWeaponComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "Units/RTSUnit.h"

URTSWeaponComponent::URTSWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// For Multicast_PlayShot only; nothing of the weapon replicates.
	SetIsReplicatedByDefault(true);
}

void URTSWeaponComponent::SetWeaponData(URTSWeaponData* InWeaponData)
{
	WeaponData = InWeaponData;
}

float URTSWeaponComponent::GetRange() const
{
	return WeaponData ? WeaponData->Range : 0.f;
}

float URTSWeaponComponent::GetDamage() const
{
	return WeaponData ? WeaponData->Damage * DamageMultiplier : 0.f;
}

void URTSWeaponComponent::SetOrderedTarget(AActor* InTarget)
{
	if (InTarget)
	{
		Target = InTarget;
		bTargetOrdered = true;
	}
	else if (bTargetOrdered)
	{
		Target = nullptr;
		bTargetOrdered = false;
	}
}

void URTSWeaponComponent::SetAutoAcquire(bool bEnable)
{
	bAutoAcquire = bEnable;
	if (!bAutoAcquire && !bTargetOrdered)
	{
		Target = nullptr;
	}
}

bool URTSWeaponComponent::CanTarget(const AActor* Candidate) const
{
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Candidate);
	return Targetable && IsValid(Candidate) && Candidate != GetOwner()
		&& Targetable->GetTeamId() != TeamId
		&& !Targetable->GetHealthComponent()->IsDead();
}

bool URTSWeaponComponent::IsInRange(const AActor* Candidate) const
{
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Candidate);
	if (!WeaponData || !Targetable)
	{
		return false;
	}
	const FVector Origin = GetOwner()->GetActorLocation();
	return FVector::Dist2D(Origin, Targetable->GetClosestFootprintPoint(Origin)) <= WeaponData->Range;
}

template <typename T>
T* URTSWeaponComponent::FindNearestInRange() const
{
	// A full scan is fine at prototype unit counts; a spatial query replaces it once armies grow.
	T* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	const FVector Origin = GetOwner()->GetActorLocation();
	for (TActorIterator<T> It(GetWorld()); It; ++It)
	{
		T* Candidate = *It;
		if (!CanTarget(Candidate) || !IsInRange(Candidate))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(Origin, Candidate->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			Nearest = Candidate;
			NearestDistSq = DistSq;
		}
	}
	return Nearest;
}

AActor* URTSWeaponComponent::FindNearestEnemyInRange() const
{
	// Units shoot back and buildings don't run away, so units come first.
	if (ARTSUnit* Unit = FindNearestInRange<ARTSUnit>())
	{
		return Unit;
	}
	return FindNearestInRange<ARTSBuilding>();
}

void URTSWeaponComponent::UpdateWeapon(float DeltaSeconds)
{
	Cooldown = FMath::Max(Cooldown - DeltaSeconds, 0.f);
	if (!WeaponData)
	{
		return;
	}

	AActor* Current = Target.Get();
	if (bTargetOrdered)
	{
		// The unit drops the order once the target is gone and closes in while it is out of range.
		if (!CanTarget(Current) || !IsInRange(Current))
		{
			return;
		}
	}
	else
	{
		if (!bAutoAcquire)
		{
			return;
		}
		if (!CanTarget(Current) || !IsInRange(Current))
		{
			Current = FindNearestEnemyInRange();
		}
		else if (!Current->IsA<ARTSUnit>())
		{
			// A building is only a fallback: switch as soon as an enemy unit comes into range.
			if (ARTSUnit* Unit = FindNearestInRange<ARTSUnit>())
			{
				Current = Unit;
			}
		}
		Target = Current;
		if (!Current)
		{
			return;
		}
	}

	// Tolerance so a reload that is a whole number of frames does not slip one frame to float error.
	if (Cooldown > KINDA_SMALL_NUMBER)
	{
		return;
	}
	Cooldown = WeaponData->ReloadSeconds;
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Current);
	const FVector Origin = GetOwner()->GetActorLocation();
	const float FlightTime = WeaponData->GetFlightTime(FVector::Dist2D(Origin, Targetable->GetClosestFootprintPoint(Origin)));
	// Shown before any damage, which may destroy the target.
	Multicast_PlayShot(WeaponData, Current, Current->GetActorLocation(), FlightTime);
	if (FlightTime > 0.f)
	{
		GetWorld()->GetSubsystem<URTSShotSubsystem>()->AddShell(*Current, GetDamage(), GetOwner(), FlightTime);
	}
	else
	{
		Targetable->GetHealthComponent()->ApplyDamage(GetDamage(), GetOwner());
	}
}

void URTSWeaponComponent::Multicast_PlayShot_Implementation(URTSWeaponData* Weapon, AActor* ShotTarget, FVector_NetQuantize TargetLocation,
	float FlightTime)
{
	if (!Weapon || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ARTSShotEffect::Spawn(*GetOwner(), *Weapon, ShotTarget, TargetLocation, FlightTime);
	if (ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner()))
	{
		Unit->PlayFireKick(Weapon->EffectScale);
	}
}

void URTSWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Units and buildings hold fire once the match is over.
	if (GetOwner()->HasAuthority() && !ARTSGameState::IsMatchOver(*GetWorld()))
	{
		UpdateWeapon(DeltaTime);
	}
}
