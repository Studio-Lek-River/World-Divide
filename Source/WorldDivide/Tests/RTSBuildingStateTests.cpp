#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSBuildingRuin.h"
#include "Buildings/RTSBuildingStateComponent.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSPrimitiveData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	FRTSBuildingStateInputs MakeInputs(float HealthFraction, bool bLowPower, bool bProducing, bool bConstructed = true)
	{
		FRTSBuildingStateInputs Inputs;
		Inputs.HealthFraction = HealthFraction;
		Inputs.bLowPower = bLowPower;
		Inputs.bProducing = bProducing;
		Inputs.bConstructed = bConstructed;
		return Inputs;
	}

	/** What the building's meshes were last told for their lamps, RTSPrimitiveData::LampOn; -1 when never or when they differ. */
	float GetLampOn(const ARTSBuilding& Building)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Building.GetComponents(Meshes);
		float Value = -1.f;
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			const TArray<float>& Data = Mesh->GetCustomPrimitiveData().Data;
			const float Each = Data.IsValidIndex(RTSPrimitiveData::LampOn) ? Data[RTSPrimitiveData::LampOn] : -1.f;
			if (Value >= 0.f && Each != Value)
			{
				return -1.f;
			}
			Value = Each;
		}
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingStatePriorityTest, "WorldDivide.Buildings.PresentationState.Priority", TestFlags)

bool FRTSBuildingStatePriorityTest::RunTest(const FString& Parameters)
{
	using RTSBuildingState::Resolve;
	using RTSBuildingState::GetLook;

	// Each state on its own.
	TestEqual(TEXT("Full health, powered, nothing queued is Idle"), Resolve(MakeInputs(1.f, false, false)), ERTSBuildingState::Idle);
	TestEqual(TEXT("Something queued is Producing"), Resolve(MakeInputs(1.f, false, true)), ERTSBuildingState::Producing);
	TestEqual(TEXT("The owner low on power is LowPower"), Resolve(MakeInputs(1.f, true, false)), ERTSBuildingState::LowPower);
	TestEqual(TEXT("A construction site is LowPower, off the grid"), Resolve(MakeInputs(1.f, false, false, false)), ERTSBuildingState::LowPower);
	TestEqual(TEXT("Half health is Damaged"), Resolve(MakeInputs(0.5f, false, false)), ERTSBuildingState::Damaged);
	TestEqual(TEXT("A quarter is Critical"), Resolve(MakeInputs(0.25f, false, false)), ERTSBuildingState::Critical);
	TestEqual(TEXT("No health left is Destroyed"), Resolve(MakeInputs(0.f, false, false)), ERTSBuildingState::Destroyed);

	// Priority: Destroyed > Critical > Damaged > LowPower > Producing > Idle.
	TestEqual(TEXT("Low power outranks producing"), Resolve(MakeInputs(1.f, true, true)), ERTSBuildingState::LowPower);
	TestEqual(TEXT("Damaged outranks low power"), Resolve(MakeInputs(0.4f, true, true)), ERTSBuildingState::Damaged);
	TestEqual(TEXT("Critical outranks everything alive"), Resolve(MakeInputs(0.1f, true, true)), ERTSBuildingState::Critical);
	TestEqual(TEXT("Destroyed during production"), Resolve(MakeInputs(0.f, false, true)), ERTSBuildingState::Destroyed);

	// Recovery and repair run back down the order.
	TestEqual(TEXT("Power back while producing"), Resolve(MakeInputs(1.f, false, true)), ERTSBuildingState::Producing);
	TestEqual(TEXT("Repaired from Critical above a quarter"), Resolve(MakeInputs(0.3f, false, false)), ERTSBuildingState::Damaged);
	TestEqual(TEXT("Repaired above half"), Resolve(MakeInputs(0.51f, false, false)), ERTSBuildingState::Idle);

	// The look takes the most restrictive of every condition, so states never contradict.
	TestTrue(TEXT("Idle and Producing look alike"), GetLook(MakeInputs(1.f, false, false)) == GetLook(MakeInputs(1.f, false, true)));
	const FRTSBuildingLook Dark = GetLook(MakeInputs(1.f, true, false));
	TestEqual(TEXT("Low power stops the equipment"), Dark.MotionScale, 0.f);
	TestFalse(TEXT("Low power puts the lamps out"), Dark.bLampsOn || Dark.bWindowsOn || Dark.bFixturesOn || Dark.bAccentsOn);
	const FRTSBuildingLook Damaged = GetLook(MakeInputs(0.5f, false, false));
	TestTrue(TEXT("Damaged slows the equipment"), Damaged.MotionScale > 0.f && Damaged.MotionScale < 1.f);
	TestTrue(TEXT("Damaged puts the accents out, the rest on"), !Damaged.bAccentsOn && Damaged.bLampsOn && Damaged.bWindowsOn && Damaged.bFixturesOn);
	const FRTSBuildingLook Critical = GetLook(MakeInputs(0.2f, false, false));
	TestTrue(TEXT("Critical is slower still"), Critical.MotionScale > 0.f && Critical.MotionScale < Damaged.MotionScale);
	TestTrue(TEXT("Critical puts the fixtures out too"), !Critical.bFixturesOn && !Critical.bAccentsOn && Critical.bWindowsOn);
	TestTrue(TEXT("Damaged and low on power stays dark and still"), GetLook(MakeInputs(0.2f, true, false)) == Dark);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingStateHooksTest, "WorldDivide.Buildings.PresentationState.Hooks", TestFlags)

bool FRTSBuildingStateHooksTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 10000);
	URTSFactionData* Faction = NewObject<URTSFactionData>();
	Faction->bUsesPower = true;
	Player->SetFaction(Faction);

	URTSUnitData* Dozer = NewObject<URTSUnitData>();
	Dozer->MaxHealth = 100.f;
	Dozer->BuildTime = 10.f;
	URTSBuildingData* PlantData = NewObject<URTSBuildingData>();
	PlantData->Footprint = FIntPoint(2, 2);
	PlantData->PowerOutput = 5;
	URTSBuildingData* HQData = NewObject<URTSBuildingData>();
	HQData->Footprint = FIntPoint(4, 4);
	HQData->MaxHealth = 1000.f;
	HQData->BuildTime = 2.f;
	HQData->PowerDraw = 3;
	HQData->Units.Add(Dozer);

	ARTSBuilding* Plant = TestWorld.SpawnBuilding(PlantData, 0, FVector(0.f, -3000.f, 0.f));
	Plant->UpdateConstruction(PlantData->BuildTime);
	ARTSBuilding* HQ = TestWorld.SpawnBuilding(HQData, 0, FVector::ZeroVector);
	URTSBuildingStateComponent* State = HQ->GetStateComponent();
	TArray<ERTSBuildingState> Changes;
	State->OnStateChanged.AddLambda([&Changes](ERTSBuildingState, ERTSBuildingState New) { Changes.Add(New); });

	State->Refresh();
	TestEqual(TEXT("A construction site shows LowPower"), State->GetState(), ERTSBuildingState::LowPower);
	TestEqual(TEXT("Its equipment stands still"), HQ->GetEquipmentMotionScale(), 0.f);
	TestEqual(TEXT("Its lamps are out"), GetLampOn(*HQ), 0.f);

	HQ->UpdateConstruction(HQData->BuildTime);
	State->Refresh();
	TestEqual(TEXT("Constructed and powered it is Idle"), State->GetState(), ERTSBuildingState::Idle);
	TestEqual(TEXT("Its equipment runs"), HQ->GetEquipmentMotionScale(), 1.f);
	TestEqual(TEXT("Its lamps are on"), GetLampOn(*HQ), 1.f);
	TestTrue(TEXT("Its windows are on"), HQ->IsLightGroupOn(TEXT("Windows")));

	TestTrue(TEXT("Queue a dozer"), HQ->GetProductionComponent()->Enqueue(*Player, Dozer));
	State->Refresh();
	TestEqual(TEXT("Queued it is Producing"), State->GetState(), ERTSBuildingState::Producing);

	// The plant falls: the owner is low on power, which every machine sees through the building.
	Plant->GetHealthComponent()->ApplyDamage(PlantData->MaxHealth, nullptr);
	TestTrue(TEXT("The HQ records the owner low on power"), HQ->IsOwnerLowPower());
	State->Refresh();
	TestEqual(TEXT("Low on power it is LowPower"), State->GetState(), ERTSBuildingState::LowPower);
	TestEqual(TEXT("Low power stops the equipment"), HQ->GetEquipmentMotionScale(), 0.f);
	TestEqual(TEXT("Low power puts the lamps out"), GetLampOn(*HQ), 0.f);
	TestFalse(TEXT("Low power puts the windows out"), HQ->IsLightGroupOn(TEXT("Windows")));
	TestEqual(TEXT("Production carries on, the state only reads"), HQ->GetProductionComponent()->GetQueue().Num(), 1);

	// Low on power, construction runs at two thirds.
	Plant = TestWorld.SpawnBuilding(PlantData, 0, FVector(0.f, -3000.f, 0.f));
	Plant->UpdateConstruction(PlantData->BuildTime * 2.f);
	State->Refresh();
	TestEqual(TEXT("Power back, it produces again"), State->GetState(), ERTSBuildingState::Producing);
	TestEqual(TEXT("Its lamps are on again"), GetLampOn(*HQ), 1.f);

	HQ->GetHealthComponent()->ApplyDamage(HQData->MaxHealth * 0.6f, nullptr);
	State->Refresh();
	TestEqual(TEXT("At 40 % it is Damaged"), State->GetState(), ERTSBuildingState::Damaged);
	TestFalse(TEXT("Damaged puts the accents out"), HQ->IsLightGroupOn(TEXT("Accents")));
	HQ->GetHealthComponent()->ApplyDamage(HQData->MaxHealth * 0.2f, nullptr);
	State->Refresh();
	TestEqual(TEXT("At 20 % it is Critical"), State->GetState(), ERTSBuildingState::Critical);
	TestFalse(TEXT("Critical puts the fixtures out"), HQ->IsLightGroupOn(TEXT("Fixtures")));
	TestTrue(TEXT("Critical keeps the windows"), HQ->IsLightGroupOn(TEXT("Windows")));

	// Destroyed during production: the building goes at once and leaves cosmetic rubble.
	HQ->GetHealthComponent()->ApplyDamage(HQData->MaxHealth, nullptr);
	TestFalse(TEXT("The HQ is gone at once"), IsValid(HQ));
	TestEqual(TEXT("The last state shown was Destroyed"), Changes.Num() > 0 ? Changes.Last() : ERTSBuildingState::Idle, ERTSBuildingState::Destroyed);
	ARTSBuildingRuin* Ruin = nullptr;
	for (TActorIterator<ARTSBuildingRuin> It(TestWorld.World); It; ++It)
	{
		Ruin = It->GetBuildingData() == HQData ? *It : Ruin;
	}
	if (!TestNotNull(TEXT("It leaves a ruin"), Ruin))
	{
		return false;
	}
	TestEqual(TEXT("The ruin is the HQ's"), Ruin->GetBuildingData(), HQData);
	TArray<UPrimitiveComponent*> Primitives;
	Ruin->GetComponents(Primitives);
	bool bCollides = false;
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		bCollides |= Primitive->IsCollisionEnabled() || Primitive->CanEverAffectNavigation();
	}
	TestFalse(TEXT("The ruin blocks nothing, so the footprint is free"), bCollides);
	TestEqual(TEXT("The ruin goes after its lifetime"), Ruin->GetLifeSpan(),
		HQData->RuinCollapseSeconds + HQData->RuinSeconds + HQData->RuinFadeSeconds, 0.01f);
	return true;
}

#endif
