#include "Buildings/RTSBuildingStateComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Units/RTSPrimitiveData.h"

namespace
{
	constexpr float DamagedMotionScale = 0.6f;
	constexpr float CriticalMotionScale = 0.2f;
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

void URTSBuildingStateComponent::Refresh(bool bForce)
{
	const FRTSBuildingStateInputs Inputs = GatherInputs();
	const ERTSBuildingState NewState = RTSBuildingState::Resolve(Inputs);
	const FRTSBuildingLook NewLook = RTSBuildingState::GetLook(Inputs);
	const bool bLookChanged = !bApplied || !(NewLook == Look);
	Look = NewLook;
	if (bForce || bLookChanged)
	{
		ApplyLook();
		bApplied = true;
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
