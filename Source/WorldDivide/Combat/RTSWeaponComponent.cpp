#include "Combat/RTSWeaponComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSAreaDamage.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"

URTSWeaponComponent::URTSWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// For Multicast_PlayShot and the ammo count.
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

void URTSWeaponComponent::SetOrderedTarget(AActor* InTarget, bool bForce)
{
	if (InTarget)
	{
		Target = InTarget;
		OrderedLocation.Reset();
		bTargetOrdered = true;
		bTargetForced = bForce;
	}
	else
	{
		ClearOrder();
	}
}

void URTSWeaponComponent::SetOrderedLocation(const FVector& Location)
{
	Target = nullptr;
	OrderedLocation = Location;
	bTargetOrdered = true;
	bTargetForced = true;
}

void URTSWeaponComponent::ClearOrder()
{
	if (bTargetOrdered)
	{
		Target = nullptr;
		OrderedLocation.Reset();
		bTargetOrdered = false;
		bTargetForced = false;
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
	return CanForceTarget(Candidate) && Cast<IRTSTargetable>(Candidate)->GetTeamId() != TeamId;
}

bool URTSWeaponComponent::CanForceTarget(const AActor* Candidate) const
{
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Candidate);
	return Targetable && IsValid(Candidate) && Candidate != GetOwner()
		&& !Targetable->GetHealthComponent()->IsDead()
		&& (!WeaponData || WeaponData->CanHit(Targetable->IsAirborne()));
}

bool URTSWeaponComponent::CanTargetGround() const
{
	return WeaponData && WeaponData->bCanTargetGround;
}

bool URTSWeaponComponent::CanForceFireAt(const AActor* InTarget, const FVector& Location) const
{
	return IsArmed() && (InTarget ? CanForceTarget(InTarget) : CanTargetGround());
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

bool URTSWeaponComponent::IsInRange(const FVector& Location) const
{
	return WeaponData && FVector::Dist2D(GetOwner()->GetActorLocation(), Location) <= WeaponData->Range;
}

TOptional<FVector> URTSWeaponComponent::GetAimPoint() const
{
	if (OrderedLocation)
	{
		return OrderedLocation;
	}
	if (const AActor* Current = Target.Get())
	{
		return Current->GetActorLocation();
	}
	return {};
}

namespace
{
	/** A weapon without a target searches this often; it keeps checking the one it has every frame. */
	constexpr float SearchInterval = 0.2f;

	/**
	 * Units are indexed by where they stood when the grid was built, up to a frame ago: candidates come from this much
	 * further out, and the exact range check decides.
	 */
	constexpr float SearchSlack = 500.f;

	/** Cycles spent searching for a target since the last TakeSearchSeconds, game thread only. */
	uint64 WeaponSearchCycles = 0;

	struct FWeaponSearchTimer
	{
		const uint64 Start = FPlatformTime::Cycles64();
		~FWeaponSearchTimer() { WeaponSearchCycles += FPlatformTime::Cycles64() - Start; }
	};
}

double URTSWeaponComponent::TakeSearchSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(WeaponSearchCycles);
	WeaponSearchCycles = 0;
	return Seconds;
}

template <typename T>
void URTSWeaponComponent::ConsiderCandidate(T* Candidate, const FVector& Origin, T*& Nearest, float& NearestDistSq) const
{
	if (!CanTarget(Candidate) || !IsInRange(Candidate))
	{
		return;
	}
	const float DistSq = FVector::DistSquared2D(Origin, Candidate->GetActorLocation());
	if (DistSq < NearestDistSq)
	{
		Nearest = Candidate;
		NearestDistSq = DistSq;
	}
}

ARTSUnit* URTSWeaponComponent::FindNearestUnitInRange() const
{
	const FWeaponSearchTimer Timer;
	ARTSUnit* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	const FVector Origin = GetOwner()->GetActorLocation();
	if (const URTSAvoidanceSubsystem* Avoidance = GetWorld()->GetSubsystem<URTSAvoidanceSubsystem>())
	{
		// Game thread only, and reused so a search allocates nothing once it has grown.
		static TArray<ARTSUnit*> Candidates;
		Avoidance->QueryRadius(Origin, WeaponData->Range + SearchSlack, Candidates);
		for (ARTSUnit* Candidate : Candidates)
		{
			ConsiderCandidate(Candidate, Origin, Nearest, NearestDistSq);
		}
		return Nearest;
	}
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ConsiderCandidate(*It, Origin, Nearest, NearestDistSq);
	}
	return Nearest;
}

ARTSBuilding* URTSWeaponComponent::FindNearestBuildingInRange() const
{
	const FWeaponSearchTimer Timer;
	ARTSBuilding* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	const FVector Origin = GetOwner()->GetActorLocation();
	if (const ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>())
	{
		GameState->ForEachEnemyBuilding(TeamId, [&](ARTSBuilding& Candidate)
		{
			ARTSBuilding* Building = &Candidate;
			ConsiderCandidate(Building, Origin, Nearest, NearestDistSq);
		});
		return Nearest;
	}
	// A world without the game state, as in tests: every building.
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		ConsiderCandidate(*It, Origin, Nearest, NearestDistSq);
	}
	return Nearest;
}

AActor* URTSWeaponComponent::FindNearestEnemyInRange() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSWeaponComponent::FindNearestEnemyInRange);

	// Units shoot back and buildings don't run away, so units come first.
	if (ARTSUnit* Unit = FindNearestUnitInRange())
	{
		return Unit;
	}
	return FindNearestBuildingInRange();
}

void URTSWeaponComponent::UpdateWeapon(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSWeaponComponent::UpdateWeapon);

	Cooldown = FMath::Max(Cooldown - DeltaSeconds, 0.f);
	SearchCooldown = FMath::Max(SearchCooldown - DeltaSeconds, 0.f);
	SearchClock += DeltaSeconds;
	if (!WeaponData)
	{
		return;
	}

	AActor* Current = Target.Get();
	if (OrderedLocation)
	{
		// The unit closes in while the location is out of range.
		if (!CanTargetGround() || !IsInRange(*OrderedLocation))
		{
			return;
		}
	}
	else if (bTargetOrdered)
	{
		// The unit drops the order once the target is gone and closes in while it is out of range. A forced target that is
		// gone ends the order here, so a building, which has no order queue, engages on its own again.
		if (bTargetForced && !CanForceTarget(Current))
		{
			ClearOrder();
			return;
		}
		if (!(bTargetForced || CanTarget(Current)) || !IsInRange(Current))
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
			// A target just lost is replaced at once; with none, the weapon looks again on its next search.
			if (Current || Target.IsStale(true) || SearchCooldown <= KINDA_SMALL_NUMBER)
			{
				Current = FindNearestEnemyInRange();
				ScheduleSearch();
			}
			else
			{
				Current = nullptr;
			}
		}
		else if (!Current->IsA<ARTSUnit>() && SearchCooldown <= KINDA_SMALL_NUMBER)
		{
			// A building is only a fallback: switch once an enemy unit comes into range.
			TRACE_CPUPROFILER_EVENT_SCOPE(URTSWeaponComponent::FindNearestUnitInRange);
			if (ARTSUnit* Unit = FindNearestUnitInRange())
			{
				Current = Unit;
			}
			ScheduleSearch();
		}
		Target = Current;
		if (!Current)
		{
			return;
		}
	}

	// A turret comes round first; the reload keeps counting meanwhile.
	const FVector AimPoint = OrderedLocation ? *OrderedLocation : Current->GetActorLocation();
	if (const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner()); Unit && !Unit->IsAimedAt(AimPoint))
	{
		return;
	}
	// Tolerance so a reload that is a whole number of frames does not slip one frame to float error.
	if (Cooldown > KINDA_SMALL_NUMBER || Ammo == 0)
	{
		return;
	}
	Cooldown = WeaponData->ReloadSeconds;
	++ShotsFired;
	if (Ammo > 0)
	{
		--Ammo;
	}
	if (OrderedLocation)
	{
		FireAtLocation(*OrderedLocation);
		return;
	}
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

void URTSWeaponComponent::FireAtLocation(const FVector& Location)
{
	const float FlightTime = WeaponData->GetFlightTime(FVector::Dist2D(GetOwner()->GetActorLocation(), Location));
	Multicast_PlayShot(WeaponData, nullptr, Location, FlightTime);
	if (FlightTime > 0.f)
	{
		GetWorld()->GetSubsystem<URTSShotSubsystem>()->AddGroundShell(Location, GetDamage(), WeaponData->SplashRadius, GetOwner(), FlightTime);
	}
	else
	{
		RTSCombat::ApplyAreaDamage(*GetWorld(), Location, GetDamage(), WeaponData->SplashRadius, URTSWeaponData::SplashEdgeFraction, GetOwner());
	}
}

void URTSWeaponComponent::ScheduleSearch()
{
	// Each weapon searches in its own slot of the interval, so weapons that appear together, or lose a target together,
	// still search on different frames.
	const double Phase = FMath::Frac(GetUniqueID() * UE_GOLDEN_RATIO);
	SearchCooldown = SearchInterval * (1.f - static_cast<float>(FMath::Frac(SearchClock / SearchInterval + Phase)));
}

void URTSWeaponComponent::Multicast_PlayShot_Implementation(URTSWeaponData* Weapon, AActor* ShotTarget, FVector_NetQuantize TargetLocation,
	float FlightTime)
{
	if (!Weapon || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AActor& Shooter = *GetOwner();
	const FVector AimPoint = ShotTarget ? ARTSShotEffect::GetAimPoint(*ShotTarget, Shooter.GetActorLocation()) : FVector(TargetLocation);
	const FTransform Muzzle = GetMuzzleTransform(Shooter, AimPoint);
	GetWorld()->GetSubsystem<URTSShotSubsystem>()->PlayShot(Shooter, *Weapon, ShotTarget, TargetLocation, FlightTime, Muzzle);
	if (ARTSUnit* Unit = Cast<ARTSUnit>(&Shooter))
	{
		// The one event per shot the vehicle's presentation listens to (#217), before the kick moves the barrel; a unit
		// without listeners builds nothing.
		if (Unit->OnShot().IsBound())
		{
			FRTSShotEvent Event;
			Event.Weapon = Weapon;
			Event.Muzzle = Muzzle;
			const FVector Toward = AimPoint - Muzzle.GetLocation();
			Event.Direction = Toward.IsNearlyZero() ? Muzzle.GetUnitAxis(EAxis::X) : Toward.GetSafeNormal();
			Unit->OnShot().Broadcast(Event);
		}
		Unit->PlayFireKick(Weapon->EffectScale);
		Unit->PlayGunRecoil();
	}
}

FTransform URTSWeaponComponent::GetMuzzleTransform(const AActor& Shooter, const FVector& Toward)
{
	if (const ARTSUnit* Unit = Cast<ARTSUnit>(&Shooter); Unit && Unit->HasTurret())
	{
		return Unit->GetMuzzleTransform();
	}
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Shooter.GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const FVector Direction = (Toward - BoundsOrigin).GetSafeNormal2D();
	// The bounds are world aligned, so their corner reach overshoots a turned hull; most of it lands near the front.
	const float Reach = FVector(BoundsExtent.X, BoundsExtent.Y, 0.f).Size() * 0.7f;
	const FVector Location = BoundsOrigin + Direction * Reach + FVector(0.f, 0.f, BoundsExtent.Z * 0.3f);
	const FVector Facing = Toward - Location;
	return FTransform(Facing.IsNearlyZero() ? FRotator::ZeroRotator : Facing.Rotation(), Location);
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

void URTSWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URTSWeaponComponent, Ammo);
}
