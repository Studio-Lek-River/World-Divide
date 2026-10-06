#include "Combat/RTSWeaponComponent.h"

#include "Combat/RTSHealthComponent.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "Units/RTSUnit.h"

URTSWeaponComponent::URTSWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URTSWeaponComponent::SetWeaponData(URTSWeaponData* InWeaponData)
{
	WeaponData = InWeaponData;
}

float URTSWeaponComponent::GetRange() const
{
	return WeaponData ? WeaponData->Range : 0.f;
}

void URTSWeaponComponent::SetOrderedTarget(ARTSUnit* InTarget)
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

bool URTSWeaponComponent::CanTarget(const ARTSUnit* Candidate) const
{
	return IsValid(Candidate) && Candidate != GetOwner()
		&& Candidate->GetTeamId() != TeamId
		&& !Candidate->GetHealthComponent()->IsDead();
}

bool URTSWeaponComponent::IsInRange(const ARTSUnit* Candidate) const
{
	if (!WeaponData || !Candidate)
	{
		return false;
	}
	const float Distance = FVector::Dist2D(GetOwner()->GetActorLocation(), Candidate->GetActorLocation());
	return Distance - Candidate->GetSelectionRadius() <= WeaponData->Range;
}

ARTSUnit* URTSWeaponComponent::FindNearestEnemyInRange() const
{
	// A full scan is fine at prototype unit counts; a spatial query replaces it once armies grow.
	ARTSUnit* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	const FVector Origin = GetOwner()->GetActorLocation();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Candidate = *It;
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

void URTSWeaponComponent::UpdateWeapon(float DeltaSeconds)
{
	Cooldown = FMath::Max(Cooldown - DeltaSeconds, 0.f);
	if (!WeaponData)
	{
		return;
	}

	ARTSUnit* Current = Target.Get();
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
			Target = Current;
		}
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
	Current->GetHealthComponent()->ApplyDamage(WeaponData->Damage, GetOwner());
}

void URTSWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner()->HasAuthority())
	{
		UpdateWeapon(DeltaTime);
	}
}
