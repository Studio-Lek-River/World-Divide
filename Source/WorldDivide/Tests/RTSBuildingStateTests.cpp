#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSBuildingRuin.h"
#include "Buildings/RTSBuildingStateComponent.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSKitPieceData.h"
#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
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

/**
 * The kit's state table (BUILDING_KIT.md, States, #391) on made-up mounts: normal, Emergency and Warning lights, the mast
 * LEDs, and the faults, each state on its own and combined, most restrictive first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingStateKitTest, "WorldDivide.Buildings.PresentationState.Kit", TestFlags)

bool FRTSBuildingStateKitTest::RunTest(const FString& Parameters)
{
	using RTSBuildingState::GetKitLook;
	FRTSKitPart Normal;
	Normal.Group = TEXT("Fixtures");
	Normal.bHasLight = true;
	FRTSKitPart Emergency = Normal;
	Emergency.bEmergency = true;
	FRTSKitPart Warning = Normal;
	Warning.Group = RTSKit::WarningGroup;
	Warning.Behaviour = ERTSKitLightBehaviour::Flash;
	FRTSKitPart Mast;
	Mast.bEmergency = true;
	FRTSKitPart Flicker = Normal;
	Flicker.FaultResponse = ERTSKitFaultResponse::Flicker;
	FRTSKitPart Fragile = Normal;
	Fragile.FaultResponse = ERTSKitFaultResponse::Off;

	const FRTSBuildingStateInputs Idle = MakeInputs(1.f, false, false);
	const FRTSBuildingStateInputs LowPower = MakeInputs(1.f, true, false);
	const FRTSBuildingStateInputs Damaged = MakeInputs(0.5f, false, false);
	const FRTSBuildingStateInputs Critical = MakeInputs(0.2f, false, false);
	const FRTSBuildingStateInputs Destroyed = MakeInputs(0.f, false, false);

	// Idle: everything steady and on but the warning lights.
	TestEqual(TEXT("Idle: a normal light is on"), GetKitLook(Idle, Normal, 0).Level, 1.f);
	TestEqual(TEXT("Idle: an emergency light is on, at full"), GetKitLook(Idle, Emergency, 0).Level, 1.f);
	TestEqual(TEXT("Idle: a warning light is off"), GetKitLook(Idle, Warning, 0).Level, 0.f);
	TestEqual(TEXT("Idle: a Flicker mount is steady"), GetKitLook(Idle, Flicker, 0).Fault, 0.f);

	// Low power: the normal lights out, the emergency ones on battery, the warning lights flashing, the LEDs on.
	TestEqual(TEXT("LowPower: a normal light is out"), GetKitLook(LowPower, Normal, 0).Level, 0.f);
	const FRTSKitLook Battery = GetKitLook(LowPower, Emergency, 0);
	TestTrue(TEXT("LowPower: an emergency light is at 30 %, cool and steady"),
		Battery.Level == RTSBuildingState::EmergencyLevel && Battery.bBattery && Battery.Fault == 0.f && Battery.Flash == 0.f);
	const FRTSKitLook Flashing = GetKitLook(LowPower, Warning, 0);
	TestTrue(TEXT("LowPower: a warning light flashes"), Flashing.Level == 1.f && Flashing.Flash > 0.f);
	TestEqual(TEXT("LowPower: the mast LEDs stay on"), GetKitLook(LowPower, Mast, 0).Level, 1.f);
	TestEqual(TEXT("A construction site is on battery too"), GetKitLook(MakeInputs(1.f, false, false, false), Emergency, 0).Level,
		RTSBuildingState::EmergencyLevel);
	FRTSKitPart Turning = Warning;
	Turning.Behaviour = ERTSKitLightBehaviour::Rotate;
	const FRTSKitLook Turned = GetKitLook(LowPower, Turning, 0);
	TestTrue(TEXT("A turning beacon's lens does not flash: its light turns"), Turned.Flash == 0.f && Turned.Level == 1.f);

	// Damaged: only Flicker mounts flicker, at most two, the Off ones go out; the warning lights stay off.
	TestTrue(TEXT("Damaged: the first Flicker mount flickers"), GetKitLook(Damaged, Flicker, 0).Fault > 0.f
		&& GetKitLook(Damaged, Flicker, 0).Level == 1.f);
	TestTrue(TEXT("Damaged: so does the second"), GetKitLook(Damaged, Flicker, 1).Fault > 0.f);
	TestEqual(TEXT("Damaged: the third stays steady"), GetKitLook(Damaged, Flicker, RTSBuildingState::MaxFlickering).Fault, 0.f);
	TestEqual(TEXT("Damaged: a mount without a fault response is steady"), GetKitLook(Damaged, Normal, 0).Fault, 0.f);
	TestEqual(TEXT("Damaged: an emergency light without one too"), GetKitLook(Damaged, Emergency, 0).Fault, 0.f);
	TestEqual(TEXT("Damaged: an Off mount goes out"), GetKitLook(Damaged, Fragile, 0).Level, 0.f);
	TestEqual(TEXT("Damaged: a warning light is off"), GetKitLook(Damaged, Warning, 0).Level, 0.f);
	FRTSKitPart Shifted = Flicker;
	Shifted.MountIndex = 3;
	TestTrue(TEXT("The flicker phase follows the mount order"), GetKitLook(Damaged, Shifted, 0).Fault != GetKitLook(Damaged, Flicker, 0).Fault);

	// Critical: the Fixtures out, the Flicker mounts out, emergency on battery, warning on.
	TestEqual(TEXT("Critical: a Fixtures light is out"), GetKitLook(Critical, Normal, 0).Level, 0.f);
	TestEqual(TEXT("Critical: a Flicker mount goes out"), GetKitLook(Critical, Flicker, 0).Level, 0.f);
	TestEqual(TEXT("Critical: an emergency light is at 30 %"), GetKitLook(Critical, Emergency, 0).Level, RTSBuildingState::EmergencyLevel);
	TestEqual(TEXT("Critical: a warning light is on"), GetKitLook(Critical, Warning, 0).Level, 1.f);
	TestEqual(TEXT("Critical: the mast LEDs stay on"), GetKitLook(Critical, Mast, 0).Level, 1.f);
	FRTSKitPart Window = Normal;
	Window.Group = TEXT("Windows");
	TestEqual(TEXT("Critical: a light of a group still on stays on"), GetKitLook(Critical, Window, 0).Level, 1.f);

	// Damaged and low on power: as dark as low power alone, and the battery lights do not flicker.
	FRTSKitPart BatteryFlicker = Emergency;
	BatteryFlicker.FaultResponse = ERTSKitFaultResponse::Flicker;
	const FRTSKitLook DamagedBattery = GetKitLook(MakeInputs(0.4f, true, false), BatteryFlicker, 0);
	TestTrue(TEXT("Damaged on battery: steady at 30 %"), DamagedBattery.Level == RTSBuildingState::EmergencyLevel && DamagedBattery.Fault == 0.f);
	TestEqual(TEXT("Damaged on battery: a normal light is out"), GetKitLook(MakeInputs(0.4f, true, false), Flicker, 0).Level, 0.f);

	// Destroyed: everything out.
	for (const FRTSKitPart* Part : { &Normal, &Emergency, &Warning, &Mast, &Flicker })
	{
		TestEqual(TEXT("Destroyed: everything is out"), GetKitLook(Destroyed, *Part, 0).Level, 0.f);
	}
	return true;
}

/**
 * The kit's looks on a building (#391): its lenses and real lights switch together through each state, forced with
 * rts.Debug.BuildingState; only the first two Flicker mounts flicker; a light goes out with its lens's flash, and past the
 * budget, below its priority's quality and beyond its MaxDrawDistance.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingStateKitLightsTest, "WorldDivide.Buildings.PresentationState.KitLights", TestFlags)

bool FRTSBuildingStateKitLightsTest::RunTest(const FString& Parameters)
{
	URTSKitPieceData* Lamp = NewObject<URTSKitPieceData>();
	Lamp->Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Lamp->bHasLight = true;
	Lamp->Light.Type = ERTSBuildingLightType::Point;
	Lamp->Light.Candelas = 200.f;
	Lamp->Light.Group = TEXT("Fixtures");
	Lamp->FaultResponse = ERTSKitFaultResponse::Flicker;
	if (!TestNotNull(TEXT("The engine's cube loads"), Lamp->Mesh.Get()))
	{
		return false;
	}
	URTSKitPieceData* ExitLamp = DuplicateObject(Lamp, GetTransientPackage());
	ExitLamp->FaultResponse = ERTSKitFaultResponse::None;
	ExitLamp->bEmergencyDefault = true;
	ExitLamp->Light.Priority = ERTSKitLightPriority::Secondary;
	ExitLamp->Light.MaxDrawDistance = 1000.f;
	URTSKitPieceData* Beacon = DuplicateObject(ExitLamp, GetTransientPackage());
	Beacon->bEmergencyDefault = false;
	Beacon->Light.Group = RTSKit::WarningGroup;
	Beacon->Light.Behaviour = ERTSKitLightBehaviour::Flash;
	Beacon->Light.Priority = ERTSKitLightPriority::Primary;
	Beacon->Light.MaxDrawDistance = 0.f;

	URTSBuildingData* Data = NewObject<URTSBuildingData>();
	Data->Footprint = FIntPoint(4, 4);
	Data->MaxHealth = 1000.f;
	Data->BuildTime = 1.f;
	auto AddMount = [Data](const TCHAR* Name, URTSKitPieceData* Piece, float X)
	{
		FRTSKitMount& Mount = Data->Mounts.AddDefaulted_GetRef();
		Mount.Name = Name;
		Mount.Piece = Piece;
		Mount.Transform = FTransform(FVector(X, 0.f, 200.f));
	};
	AddMount(TEXT("BayA"), Lamp, 0.f);
	AddMount(TEXT("BayB"), Lamp, 100.f);
	AddMount(TEXT("BayC"), Lamp, 200.f);
	AddMount(TEXT("Exit"), ExitLamp, 300.f);
	AddMount(TEXT("Warning"), Beacon, 400.f);

	IConsoleVariable* Forced = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Debug.BuildingState"));
	if (!TestNotNull(TEXT("rts.Debug.BuildingState exists"), Forced))
	{
		return false;
	}
	ON_SCOPE_EXIT{ Forced->Set(-1, ECVF_SetByCode); };

	const FRTSTestWorld TestWorld;
	ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, 0, FVector::ZeroVector);
	Building->UpdateConstruction(Data->BuildTime);
	const TArray<FRTSKitPart>& Parts = Building->GetKitParts();
	if (!TestEqual(TEXT("Every mount is a kit part"), Parts.Num(), 5)
		|| !TestEqual(TEXT("Each with its light"), Building->GetLightComponents().Num(), 5))
	{
		return false;
	}

	// Lens and light of each part agree in every state. The building has no lights of its own, so light i is part i's.
	const TCHAR* Names[] = { TEXT("Idle"), TEXT("Producing"), TEXT("LowPower"), TEXT("Damaged"), TEXT("Critical"), TEXT("Destroyed") };
	for (int32 State = 0; State < UE_ARRAY_COUNT(Names); ++State)
	{
		Forced->Set(State, ECVF_SetByCode);
		Building->GetStateComponent()->Refresh();
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			UStaticMeshComponent* Component = nullptr;
			int32 Instance = INDEX_NONE;
			Building->FindPart(Parts[Index].Name, Component, Instance);
			const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
			if (!TestTrue(*FString::Printf(TEXT("%s draws instanced"), *Parts[Index].Name.ToString()), Instanced && Instance != INDEX_NONE))
			{
				continue;
			}
			const float Lens = Instanced->PerInstanceSMCustomData[Instance * RTSKitLensData::Floats + RTSKitLensData::Level];
			TestEqual(*FString::Printf(TEXT("%s: %s's lens is its look"), Names[State], *Parts[Index].Name.ToString()),
				Lens, Building->GetKitLooks()[Index].Level);
			TestEqual(*FString::Printf(TEXT("%s: %s's light shines as its lens glows"), Names[State], *Parts[Index].Name.ToString()),
				Building->GetLightComponents()[Index]->Intensity, 200.f * Lens, 0.01f);
		}
	}

	Forced->Set(static_cast<int32>(ERTSBuildingState::Damaged), ECVF_SetByCode);
	Building->GetStateComponent()->Refresh();
	TestTrue(TEXT("Damaged: the first two Flicker mounts flicker"), Building->GetKitLooks()[0].Fault > 0.f && Building->GetKitLooks()[1].Fault > 0.f);
	TestEqual(TEXT("Damaged: the third does not"), Building->GetKitLooks()[2].Fault, 0.f);

	// Shown in the dark on low power: the lights follow their lens's phase, and the budget, priority and distance keep them out.
	Forced->Set(static_cast<int32>(ERTSBuildingState::LowPower), ECVF_SetByCode);
	Building->GetStateComponent()->Refresh();
	Building->ShowLights(true);
	const FVector Near = Building->GetActorLocation();
	const double Phase = Building->GetKitLooks()[4].Flash;
	const double Lit = 1.75 - Phase;
	const double Dark = 1.25 - Phase;
	auto Update = [Building](double Time, int32 Budget, bool bSecondary, const FVector& View)
	{
		Building->UpdateKitLights(Time, {}, View, bSecondary, Budget);
		return Budget;
	};
	const ULocalLightComponent* ExitLight = Building->GetLightComponents()[3];
	const ULocalLightComponent* WarningLight = Building->GetLightComponents()[4];
	TestEqual(TEXT("The emergency and warning lights take two of the budget"), Update(Lit, 24, true, Near), 22);
	TestTrue(TEXT("The warning light shines with its lens's flash"), WarningLight->IsVisible());
	TestEqual(TEXT("Two lights are lit"), Building->GetLitKitLightCount(), 2);
	Update(Dark, 24, true, Near);
	TestFalse(TEXT("And goes out with it"), WarningLight->IsVisible());
	TestTrue(TEXT("The steady emergency light stays"), ExitLight->IsVisible());
	Update(Lit, 1, true, Near);
	TestEqual(TEXT("The budget keeps the rest out"), Building->GetLitKitLightCount(), 1);
	Update(Lit, 24, false, Near);
	TestFalse(TEXT("A Secondary light stays out below High"), ExitLight->IsVisible());
	Update(Lit, 24, true, Near + FVector(5000.f, 0.f, 0.f));
	TestFalse(TEXT("A light past its MaxDrawDistance stays out"), ExitLight->IsVisible());
	TestTrue(TEXT("One without stays"), WarningLight->IsVisible());
	Building->ShowLights(false);
	TestEqual(TEXT("Hidden, none is lit"), Building->GetLitKitLightCount(), 0);
	return true;
}

#endif
