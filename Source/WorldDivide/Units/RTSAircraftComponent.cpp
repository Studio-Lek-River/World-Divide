#include "Units/RTSAircraftComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSAircraftData.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Units/RTSGround.h"
#include "Units/RTSUnit.h"

namespace
{
	/** A pad-less aircraft looks for a free pad this often. */
	constexpr float PadSearchSeconds = 1.f;

	/** The bank eases in and out at this many times the maximum bank per second. */
	constexpr float BankRatePerMaxBank = 4.f;

	/** Beyond its turning circle, so the aircraft can always reach the pad in a straight glide from where it starts landing. */
	constexpr float LandingMargin = 100.f;

	/**
	 * In flight the aircraft looks this many seconds ahead, at this interval, for ground it has to start climbing for now to
	 * clear it at cruise altitude.
	 */
	constexpr float ClimbLookaheadSeconds = 5.f;
	constexpr float ClimbLookaheadInterval = 0.25f;
	/** It plans its climb at this share of its climb rate, so it reaches the height with time to spare. */
	constexpr float ClimbPlanningShare = 0.8f;

	/** The ground under an aircraft is looked for over this span above and below it: higher than any mountain, deeper than any valley. */
	constexpr float TerrainTraceSpan = 100000.f;
}

URTSAircraftComponent::URTSAircraftComponent()
{
	// The unit's tick drives it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URTSAircraftComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URTSAircraftComponent, bRearming);
}

ARTSUnit* URTSAircraftComponent::GetUnit() const
{
	return Cast<ARTSUnit>(GetOwner());
}

void URTSAircraftComponent::SetAircraftData(URTSAircraftData* InAircraftData)
{
	AircraftData = InAircraftData;
	if (!AircraftData)
	{
		return;
	}
	State = ERTSAircraftState::Parked;
	Mission = ERTSAircraftMission::None;
	LastGroundZ = GetOwner()->GetActorLocation().Z;
	SetAmmo(AircraftData->AmmoPerSortie);
	ApplyWeaponMode();
}

float URTSAircraftComponent::GetTurnRadius() const
{
	return AircraftData && AircraftData->TurnRate > 0.f ? AircraftData->MoveSpeed / FMath::DegreesToRadians(AircraftData->TurnRate) : 0.f;
}

int32 URTSAircraftComponent::GetAmmo() const
{
	return GetUnit()->GetWeaponComponent()->GetAmmo();
}

void URTSAircraftComponent::SetAmmo(int32 Ammo) const
{
	GetUnit()->GetWeaponComponent()->SetAmmo(Ammo);
}

FVector URTSAircraftComponent::GetPadParkLocation() const
{
	return Airfield->GetPadLocation(PadIndex) + FVector(0.f, 0.f, GetUnit()->GetGroundOffset());
}

float URTSAircraftComponent::GetGroundZ() const
{
	return Airfield.IsValid() ? Airfield->GetActorLocation().Z : LastGroundZ;
}

float URTSAircraftComponent::GetTerrainZ(const FVector& Location, float Yaw) const
{
	const UWorld& World = *GetWorld();
	const FVector Heading = FRotator(0.f, Yaw, 0.f).Vector();
	float Highest = -UE_BIG_NUMBER;
	for (float Seconds = 0.f; Seconds <= ClimbLookaheadSeconds; Seconds += ClimbLookaheadInterval)
	{
		float GroundZ = 0.f;
		if (RTSGround::Trace(World, Location + Heading * AircraftData->MoveSpeed * Seconds, TerrainTraceSpan, TerrainTraceSpan, GroundZ))
		{
			// Ground ahead counts for less by what the aircraft climbs before it gets there, counted from the sample before,
			// since the ground may rise anywhere between the two.
			const float ClimbSeconds = FMath::Max(Seconds - ClimbLookaheadInterval, 0.f);
			Highest = FMath::Max(Highest, GroundZ - AircraftData->ClimbRate * ClimbPlanningShare * ClimbSeconds);
		}
	}
	// Off the map there is no ground to clear; the airfield's, or the last, height then.
	return Highest > -UE_BIG_NUMBER ? Highest : GetGroundZ();
}

bool URTSAircraftComponent::ParkAt(ARTSBuilding& InAirfield)
{
	check(GetOwner()->HasAuthority());
	ARTSUnit* Unit = GetUnit();
	if (!CanFly() || !Unit)
	{
		return false;
	}
	const int32 Index = InAirfield.ClaimPad(*Unit);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Airfield.Get() != &InAirfield || PadIndex != Index)
	{
		ReleasePad();
	}
	Airfield = &InAirfield;
	PadIndex = Index;
	State = ERTSAircraftState::Parked;
	Mission = ERTSAircraftMission::None;
	Target = nullptr;
	Roll = 0.f;
	RearmProgress = 0.f;
	// Pads face the airfield's front, +X.
	Unit->SetActorLocationAndRotation(GetPadParkLocation(), FRotator::ZeroRotator);
	ApplyWeaponMode();
	return true;
}

void URTSAircraftComponent::ExecuteCommand(const FRTSCommand& Command)
{
	check(GetOwner()->HasAuthority());
	if (!CanFly())
	{
		return;
	}

	switch (Command.Type)
	{
	case ERTSCommandType::Attack:
	{
		// Clients pick the target, so the server checks it: armed, alive, not on our side and on the ground for our weapon.
		const URTSWeaponComponent* Weapon = GetUnit()->GetWeaponComponent();
		// An empty aircraft in the air has nothing to attack with; one on its pad rearms first.
		if (!Weapon->IsArmed() || !Weapon->CanTarget(Command.TargetActor) || (State != ERTSAircraftState::Parked && GetAmmo() == 0))
		{
			return;
		}
		Mission = ERTSAircraftMission::Attack;
		Target = Command.TargetActor;
		if (State == ERTSAircraftState::Landing)
		{
			State = ERTSAircraftState::Flying;
		}
		break;
	}
	case ERTSCommandType::Move:
	case ERTSCommandType::AttackMove:
		Mission = ERTSAircraftMission::Move;
		MovePoint = Command.TargetLocation;
		Target = nullptr;
		if (State == ERTSAircraftState::Landing)
		{
			State = ERTSAircraftState::Flying;
		}
		TakeOff();
		break;
	case ERTSCommandType::Stop:
		if (State == ERTSAircraftState::Parked && Airfield.IsValid())
		{
			// Already home: a pending attack is called off.
			Mission = ERTSAircraftMission::None;
			Target = nullptr;
		}
		else if (Mission != ERTSAircraftMission::Return)
		{
			ReturnToPad();
		}
		break;
	default:
		// Aircraft neither harvest nor build.
		return;
	}
	ApplyWeaponMode();
}

void URTSAircraftComponent::UpdateAircraft(float DeltaSeconds)
{
	check(GetOwner()->HasAuthority());
	if (!CanFly())
	{
		return;
	}

	switch (State)
	{
	case ERTSAircraftState::Parked:
		UpdateRearm(DeltaSeconds);
		if (Mission == ERTSAircraftMission::Attack)
		{
			if (!GetUnit()->GetWeaponComponent()->CanTarget(Target.Get()))
			{
				Mission = ERTSAircraftMission::None;
				Target = nullptr;
			}
			// Each sortie starts with a full load.
			else if (GetAmmo() >= AircraftData->AmmoPerSortie)
			{
				TakeOff();
			}
		}
		break;
	case ERTSAircraftState::Flying:
		UpdateFlight(DeltaSeconds);
		break;
	case ERTSAircraftState::Landing:
		UpdateLanding(DeltaSeconds);
		break;
	}
	bRearming = State == ERTSAircraftState::Parked && Airfield.IsValid() && GetAmmo() < AircraftData->AmmoPerSortie;
	ApplyWeaponMode();
}

void URTSAircraftComponent::TakeOff()
{
	if (State != ERTSAircraftState::Parked)
	{
		return;
	}
	if (!Airfield.IsValid())
	{
		LastGroundZ = GetOwner()->GetActorLocation().Z - GetUnit()->GetGroundOffset();
	}
	State = ERTSAircraftState::Flying;
	RearmProgress = 0.f;
}

void URTSAircraftComponent::ReturnToPad()
{
	Mission = ERTSAircraftMission::Return;
	Target = nullptr;
	LoiterPoint = GetOwner()->GetActorLocation();
	PadSearchCooldown = 0.f;
	if (State == ERTSAircraftState::Parked)
	{
		if (Airfield.IsValid())
		{
			Mission = ERTSAircraftMission::None;
		}
		else
		{
			TakeOff();
		}
	}
}

void URTSAircraftComponent::UpdateFlight(float DeltaSeconds)
{
	const URTSWeaponComponent* Weapon = GetUnit()->GetWeaponComponent();
	if (Mission == ERTSAircraftMission::Attack && (!Weapon->CanTarget(Target.Get()) || GetAmmo() == 0))
	{
		ReturnToPad();
	}
	else if (Mission == ERTSAircraftMission::None)
	{
		ReturnToPad();
	}

	FVector Goal = LoiterPoint;
	if (Mission == ERTSAircraftMission::Attack)
	{
		Goal = Target->GetActorLocation();
	}
	else if (Mission == ERTSAircraftMission::Move)
	{
		Goal = MovePoint;
	}
	else
	{
		if (!Airfield.IsValid())
		{
			PadSearchCooldown -= DeltaSeconds;
			if (PadSearchCooldown <= 0.f)
			{
				PadSearchCooldown = PadSearchSeconds;
				ClaimNearestPad();
			}
		}
		if (Airfield.IsValid())
		{
			Goal = GetPadParkLocation();
			if (FVector::Dist2D(GetOwner()->GetActorLocation(), Goal) <= 2.f * GetTurnRadius() + LandingMargin)
			{
				State = ERTSAircraftState::Landing;
				UpdateLanding(DeltaSeconds);
				return;
			}
		}
	}
	FlyToward(Goal, DeltaSeconds);
}

void URTSAircraftComponent::FlyToward(const FVector& Goal, float DeltaSeconds)
{
	AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	const float Yaw = Owner->GetActorRotation().Yaw;

	// Fixed-wing: always forward at full speed, turning no faster than the turn rate. A goal inside the turning circle is circled.
	const FVector ToGoal = (Goal - Location).GetSafeNormal2D();
	const float MaxTurn = AircraftData->TurnRate * DeltaSeconds;
	const float Turn = ToGoal.IsNearlyZero() || MaxTurn <= 0.f
		? 0.f
		: FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaw, ToGoal.Rotation().Yaw), -MaxTurn, MaxTurn);
	const float NewYaw = Yaw + Turn;

	const float Horizontal = AircraftData->MoveSpeed * DeltaSeconds;
	FVector NewLocation = Location + FRotator(0.f, NewYaw, 0.f).Vector() * Horizontal;
	const float CruiseZ = GetTerrainZ(NewLocation, NewYaw) + GetUnit()->GetGroundOffset() + AircraftData->CruiseAltitude;
	NewLocation.Z = FMath::FInterpConstantTo(Location.Z, CruiseZ, DeltaSeconds, AircraftData->ClimbRate);

	// Positive roll dips the right wing, into a turn to the right.
	const float Bank = MaxTurn > 0.f ? AircraftData->MaxBankAngle * Turn / MaxTurn : 0.f;
	Roll = FMath::FInterpConstantTo(Roll, Bank, DeltaSeconds, AircraftData->MaxBankAngle * BankRatePerMaxBank);
	const float Pitch = Horizontal > 0.f ? FMath::RadiansToDegrees(FMath::Atan2(NewLocation.Z - Location.Z, Horizontal)) : 0.f;
	Owner->SetActorLocationAndRotation(NewLocation, FRotator(Pitch, NewYaw, Roll));
}

void URTSAircraftComponent::UpdateLanding(float DeltaSeconds)
{
	if (!Airfield.IsValid())
	{
		State = ERTSAircraftState::Flying;
		ReturnToPad();
		return;
	}

	AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	const FVector Pad = GetPadParkLocation();
	const FVector ToPad = Pad - Location;
	const float Step = AircraftData->MoveSpeed * DeltaSeconds;
	const FRotator Rotation = Owner->GetActorRotation();
	if (ToPad.Size() <= Step)
	{
		// Parked facing the way it came in; the next take-off leaves that way.
		Owner->SetActorLocationAndRotation(Pad, FRotator(0.f, Rotation.Yaw, 0.f));
		State = ERTSAircraftState::Parked;
		Roll = 0.f;
		RearmProgress = 0.f;
		if (Mission == ERTSAircraftMission::Return)
		{
			Mission = ERTSAircraftMission::None;
		}
		return;
	}

	// A straight glide onto the pad, nose turning toward it and wings levelling.
	const FVector Flat = ToPad.GetSafeNormal2D();
	const float MaxTurn = AircraftData->TurnRate * DeltaSeconds;
	const float Yaw = Flat.IsNearlyZero()
		? Rotation.Yaw
		: Rotation.Yaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(Rotation.Yaw, Flat.Rotation().Yaw), -MaxTurn, MaxTurn);
	Roll = FMath::FInterpConstantTo(Roll, 0.f, DeltaSeconds, AircraftData->MaxBankAngle * BankRatePerMaxBank);
	const FVector NewLocation = Location + ToPad.GetSafeNormal() * Step;
	const float Horizontal = FVector::Dist2D(NewLocation, Location);
	const float Pitch = Horizontal > 0.f ? FMath::RadiansToDegrees(FMath::Atan2(NewLocation.Z - Location.Z, Horizontal)) : 0.f;
	Owner->SetActorLocationAndRotation(NewLocation, FRotator(Pitch, Yaw, Roll));
}

void URTSAircraftComponent::UpdateRearm(float DeltaSeconds)
{
	const int32 Ammo = GetAmmo();
	const int32 Full = AircraftData->AmmoPerSortie;
	// Only a pad rearms.
	if (!Airfield.IsValid() || Ammo >= Full)
	{
		RearmProgress = 0.f;
		return;
	}
	const float PerShot = AircraftData->RearmSeconds / Full;
	RearmProgress += DeltaSeconds;
	int32 Rearmed = Ammo;
	// Tolerance so a rearm that is a whole number of frames does not slip one frame to float error.
	while (Rearmed < Full && RearmProgress + KINDA_SMALL_NUMBER >= PerShot)
	{
		++Rearmed;
		RearmProgress -= PerShot;
	}
	SetAmmo(Rearmed);
}

void URTSAircraftComponent::ApplyWeaponMode()
{
	URTSWeaponComponent* Weapon = GetUnit()->GetWeaponComponent();
	// Aircraft only fire at what they were sent to attack.
	Weapon->SetAutoAcquire(false);
	const bool bAttacking = State == ERTSAircraftState::Flying && Mission == ERTSAircraftMission::Attack;
	Weapon->SetOrderedTarget(bAttacking ? Target.Get() : nullptr);
}

void URTSAircraftComponent::NotifyPadLost()
{
	Airfield = nullptr;
	PadIndex = INDEX_NONE;
	if (State == ERTSAircraftState::Landing)
	{
		State = ERTSAircraftState::Flying;
	}
	if (State == ERTSAircraftState::Parked || Mission == ERTSAircraftMission::Return)
	{
		ReturnToPad();
	}
	ApplyWeaponMode();
}

void URTSAircraftComponent::ReleasePad()
{
	ARTSUnit* Unit = GetUnit();
	if (Airfield.IsValid() && Unit)
	{
		Airfield->ReleasePad(*Unit);
	}
	Airfield = nullptr;
	PadIndex = INDEX_NONE;
}

bool URTSAircraftComponent::ClaimNearestPad()
{
	ARTSUnit* Unit = GetUnit();
	ARTSBuilding* Found = FindFreeAirfield(*GetWorld(), Unit->GetTeamId(), Unit->GetActorLocation());
	const int32 Index = Found ? Found->ClaimPad(*Unit) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Airfield = Found;
	PadIndex = Index;
	return true;
}

ARTSBuilding* URTSAircraftComponent::FindFreeAirfield(const UWorld& World, int32 TeamId, const FVector& From)
{
	ARTSBuilding* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
	{
		if (It->GetTeamId() != TeamId || !It->IsConstructed() || It->IsActorBeingDestroyed() || It->GetHealthComponent()->IsDead()
			|| It->GetFreePadCount() <= 0)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(From, It->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	return Nearest;
}
