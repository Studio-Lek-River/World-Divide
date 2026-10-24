#include "Buildings/RTSBuildingStateComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSKitPieceData.h"
#include "HAL/IConsoleManager.h"
#include "Units/RTSPrimitiveData.h"

namespace
{
	constexpr float DamagedMotionScale = 0.6f;
	constexpr float CriticalMotionScale = 0.2f;

	TAutoConsoleVariable<int32> CVarDebugBuildingState(
		TEXT("rts.Debug.BuildingState"), -1,
		TEXT("Shows every building in one presentation state on this machine, for building shots (#391): 0 Idle, 1 Producing, ")
		TEXT("2 LowPower, 3 Damaged, 4 Critical, 5 Destroyed; -1 as the game has it. Looks only: health and power stay as they are."));

	/** A mount's flash or flicker phase from its place among the mounts: the same on every machine, never 0 (none). */
	float GetPhase(int32 MountIndex)
	{
		return 0.1f + 0.8f * FMath::Frac(0.618034f * static_cast<float>(MountIndex + 1));
	}
}

ERTSBuildingState RTSBuildingState::Resolve(const FRTSBuildingStateInputs& Inputs)
{
	if (Inputs.HealthFraction <= 0.f)
	{
		return ERTSBuildingState::Destroyed;
	}
	if (Inputs.HealthFraction <= Inputs.CriticalFraction)
	{
		return ERTSBuildingState::Critical;
	}
	if (Inputs.HealthFraction <= Inputs.DamagedFraction)
	{
		return ERTSBuildingState::Damaged;
	}
	if (Inputs.bLowPower || !Inputs.bConstructed)
	{
		return ERTSBuildingState::LowPower;
	}
	return Inputs.bProducing ? ERTSBuildingState::Producing : ERTSBuildingState::Idle;
}

FRTSBuildingLook RTSBuildingState::GetLook(const FRTSBuildingStateInputs& Inputs)
{
	FRTSBuildingLook Look;
	if (Inputs.HealthFraction <= Inputs.DamagedFraction)
	{
		Look.MotionScale = DamagedMotionScale;
		Look.bAccentsOn = false;
	}
	if (Inputs.HealthFraction <= Inputs.CriticalFraction)
	{
		Look.MotionScale = CriticalMotionScale;
		Look.bFixturesOn = false;
	}
	if (Inputs.bLowPower || !Inputs.bConstructed || Inputs.HealthFraction <= 0.f)
	{
		Look = FRTSBuildingLook{ 0.f, false, false, false, false };
	}
	return Look;
}

FRTSKitLook RTSBuildingState::GetKitLook(const FRTSBuildingStateInputs& Inputs, const FRTSKitPart& Part, int32 FlickerRank)
{
	FRTSKitLook Kit;
	if (Inputs.HealthFraction <= 0.f)
	{
		Kit.Level = 0.f;
		return Kit;
	}
	const bool bBattery = Inputs.bLowPower || !Inputs.bConstructed;
	const bool bDamaged = Inputs.HealthFraction <= Inputs.DamagedFraction;
	const bool bCritical = Inputs.HealthFraction <= Inputs.CriticalFraction;
	if (Part.IsWarning())
	{
		// Off while all is well and merely damaged; on, flashing, when the building is low on power or critical. A turning
		// beacon's lens glows steady; its light turns in URTSBuildingLightSubsystem's tick.
		Kit.Level = bBattery || bCritical ? 1.f : 0.f;
		Kit.Flash = Kit.Level > 0.f && Part.Behaviour != ERTSKitLightBehaviour::Rotate ? GetPhase(Part.MountIndex) : 0.f;
		return Kit;
	}
	const bool bEmergency = Part.bEmergency || Part.Group == RTSKit::EmergencyGroup;
	if (bEmergency && !Part.bHasLight)
	{
		// An LED on battery (the mast beacons): it draws next to nothing and stays as it is.
		return Kit;
	}
	if ((bDamaged && Part.FaultResponse == ERTSKitFaultResponse::Off)
		|| (bCritical && Part.FaultResponse == ERTSKitFaultResponse::Flicker))
	{
		Kit.Level = 0.f;
		return Kit;
	}
	if (bEmergency && (bBattery || bCritical))
	{
		// On battery: dimmed, cooler and steady.
		Kit.Level = EmergencyLevel;
		Kit.bBattery = true;
		return Kit;
	}
	const FRTSBuildingLook Look = GetLook(Inputs);
	const bool bGroupOn = Part.Group == TEXT("Windows") ? Look.bWindowsOn
		: Part.Group == TEXT("Fixtures") ? Look.bFixturesOn
		: Part.Group == TEXT("Accents") ? Look.bAccentsOn
		: Look.bLampsOn;
	if (!bGroupOn)
	{
		Kit.Level = 0.f;
		return Kit;
	}
	if (bDamaged && Part.FaultResponse == ERTSKitFaultResponse::Flicker && FlickerRank < MaxFlickering)
	{
		Kit.Fault = GetPhase(Part.MountIndex);
	}
	return Kit;
}

FRTSBuildingStateInputs RTSBuildingState::ForceState(FRTSBuildingStateInputs Inputs, ERTSBuildingState State)
{
	Inputs.bConstructed = true;
	Inputs.bLowPower = State == ERTSBuildingState::LowPower;
	Inputs.bProducing = State == ERTSBuildingState::Producing;
	Inputs.HealthFraction = State == ERTSBuildingState::Destroyed ? 0.f
		: State == ERTSBuildingState::Critical ? Inputs.CriticalFraction
		: State == ERTSBuildingState::Damaged ? Inputs.DamagedFraction
		: 1.f;
	return Inputs;
}

URTSBuildingStateComponent::URTSBuildingStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

ARTSBuilding* URTSBuildingStateComponent::GetBuilding() const
{
	return Cast<ARTSBuilding>(GetOwner());
}

FRTSBuildingStateInputs URTSBuildingStateComponent::GatherInputs() const
{
	FRTSBuildingStateInputs Inputs;
	const ARTSBuilding* Building = GetBuilding();
	if (!Building)
	{
		return Inputs;
	}
	Inputs.bConstructed = Building->IsConstructed();
	Inputs.HealthFraction = Building->GetHealthComponent()->GetHealthFraction();
	Inputs.bLowPower = Building->IsOwnerLowPower();
	Inputs.bProducing = !Building->GetProductionComponent()->GetQueue().IsEmpty();
	if (const URTSBuildingData* Data = Building->GetBuildingData())
	{
		Inputs.DamagedFraction = Data->DamagedHealthFraction;
		Inputs.CriticalFraction = Data->CriticalHealthFraction;
	}
	return Inputs;
}

void URTSBuildingStateComponent::Update(float DeltaSeconds)
{
	SinceUpdate += DeltaSeconds;
	if (SinceUpdate >= UpdateInterval)
	{
		SinceUpdate = 0.f;
		Refresh();
	}
}

void URTSBuildingStateComponent::GetKitLooks(const FRTSBuildingStateInputs& Inputs, TArray<FRTSKitLook>& OutLooks) const
{
	OutLooks.Reset();
	const ARTSBuilding* Building = GetBuilding();
	if (!Building)
	{
		return;
	}
	int32 FlickerRank = 0;
	for (const FRTSKitPart& Part : Building->GetKitParts())
	{
		OutLooks.Add(RTSBuildingState::GetKitLook(Inputs, Part, FlickerRank));
		FlickerRank += Part.FaultResponse == ERTSKitFaultResponse::Flicker ? 1 : 0;
	}
}

void URTSBuildingStateComponent::Refresh(bool bForce)
{
	FRTSBuildingStateInputs Inputs = GatherInputs();
#if !UE_BUILD_SHIPPING
	const int32 Forced = CVarDebugBuildingState.GetValueOnGameThread();
	if (Forced >= 0 && Forced <= static_cast<int32>(ERTSBuildingState::Destroyed))
	{
		Inputs = RTSBuildingState::ForceState(Inputs, static_cast<ERTSBuildingState>(Forced));
	}
	else if (DebugState)
	{
		Inputs = RTSBuildingState::ForceState(Inputs, *DebugState);
	}
#endif
	const ERTSBuildingState NewState = RTSBuildingState::Resolve(Inputs);
	const FRTSBuildingLook NewLook = RTSBuildingState::GetLook(Inputs);
	const bool bLookChanged = !bApplied || !(NewLook == Look);
	Look = NewLook;
	if (bForce || bLookChanged)
	{
		ApplyLook();
		bApplied = true;
	}
	// The kit pieces' lenses and lights, written only when one of them changes.
	GetKitLooks(Inputs, KitLooks);
	ARTSBuilding* Building = GetBuilding();
	if (Building && (bForce || KitLooks != Building->GetKitLooks()))
	{
		Building->SetKitLooks(KitLooks);
	}
	if (NewState != State)
	{
		const ERTSBuildingState Old = State;
		State = NewState;
		OnStateChanged.Broadcast(Old, NewState);
	}
}

void URTSBuildingStateComponent::ApplyLook() const
{
	ARTSBuilding* Building = GetBuilding();
	if (!Building)
	{
		return;
	}
	// The motion subsystem eases the parts to the new scale, so a power cut spins the radar down rather than stopping it dead.
	Building->SetEquipmentMotionScale(Look.MotionScale);
	Building->SetLightGroupOn(TEXT("Windows"), Look.bWindowsOn);
	Building->SetLightGroupOn(TEXT("Fixtures"), Look.bFixturesOn);
	Building->SetLightGroupOn(TEXT("Accents"), Look.bAccentsOn);

	const float LampOn = Look.bLampsOn ? 1.f : 0.f;
	TArray<UStaticMeshComponent*> Meshes;
	Building->GetComponents(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Mesh->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LampOn, LampOn);
	}
}
