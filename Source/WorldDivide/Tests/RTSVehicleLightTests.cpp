#include "Algo/Count.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSDayNightSubsystem.h"
#include "Data/RTSUnitData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"
#include "Units/RTSVehicleLightSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSVehicleLightTests
{
	using RTSTests::FRTSTestWorld;

	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* LampMaterialPath = TEXT("/Game/Materials/M_VehicleLamp.M_VehicleLamp");

	/** The vehicles and the budget in real lights: many more than may light the ground. An M20 has four, one per lamp cluster. */
	constexpr int32 Vehicles = 40;
	constexpr int32 MaxLights = 18;
	constexpr int32 LightsPerM20 = 4;
	constexpr int32 LitVehicles = MaxLights / LightsPerM20;

	/** What Part's lamps were last told, RTSPrimitiveData::LampOn; -1 when never. */
	float GetLampOn(const UStaticMeshComponent& Part)
	{
		const TArray<float>& Data = Part.GetCustomPrimitiveData().Data;
		return Data.IsValidIndex(RTSPrimitiveData::LampOn) ? Data[RTSPrimitiveData::LampOn] : -1.f;
	}

	/** Sets a console variable for as long as it lives, then gives it its value back. */
	struct FScopedCVar
	{
		FScopedCVar(const TCHAR* Name, const TCHAR* Value) : CVar(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (CVar)
			{
				Saved = CVar->GetString();
				CVar->Set(Value, ECVF_SetByCode);
			}
		}

		~FScopedCVar()
		{
			if (CVar)
			{
				CVar->Set(*Saved, ECVF_SetByCode);
			}
		}

		IConsoleVariable* CVar = nullptr;
		FString Saved;
	};
}

/**
 * Vehicle head- and rear lights (#223). The M20's data puts its lamps at the front and the back, and M_VehicleLamp reads
 * RTSPrimitiveData::LampOn. Of 40 M20s in the dark with a budget of 18 real lights, every one's lamps are on, but only the 4
 * nearest the camera light the ground (four lights each, 16, the fifth would pass it), with a white spot at each headlight
 * cluster pointing ahead and a red one at each tail lamp pointing back (#329): the light starts at the lamp it belongs to.
 * By day no real light is on and every lamp is off. The sun decides when nothing forces it: a test world without one is day.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleLightTest, "WorldDivide.Vehicle.Lights", RTSTests::TestFlags)

bool FRTSVehicleLightTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleLightTests;

	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
	if (!TestNotNull(TEXT("The M20 loads"), M20))
	{
		return false;
	}
	const FRTSVehicleLights& Lights = M20->Lights;
	TestTrue(TEXT("The M20 has headlights and tail lamps"), !Lights.HeadlightOffsets.IsEmpty() && !Lights.TaillightOffsets.IsEmpty());
	TestTrue(TEXT("Its lights are valid"), Lights.Validate());
	const TArray<FVector, TInlineAllocator<2>> Heads = FRTSVehicleLights::GetClusterMidpoints(Lights.HeadlightOffsets);
	const TArray<FVector, TInlineAllocator<2>> Tails = FRTSVehicleLights::GetClusterMidpoints(Lights.TaillightOffsets);
	TestEqual(TEXT("Its four headlights form a left and a right cluster"), Heads.Num(), 2);
	TestEqual(TEXT("Its two tail lamps one on each side"), Tails.Num(), 2);
	TestEqual(TEXT("It lights the ground with four real lights"), Lights.GetLightCount(), LightsPerM20);
	if (Heads.Num() != 2 || Tails.Num() != 2)
	{
		return false;
	}
	TestTrue(TEXT("Its headlights are at the front, its tail lamps at the back"), Heads[0].X > 0.f && Heads[1].X > 0.f && Tails[0].X < 0.f && Tails[1].X < 0.f);
	TestTrue(TEXT("Each side's cluster on its own side, the left first"), Heads[0].Y < 0.f && Heads[1].Y > 0.f && Tails[0].Y < 0.f && Tails[1].Y > 0.f);

	// The lamps' material reads the switch from where the game writes it.
	const UMaterial* LampMaterial = LoadObject<UMaterial>(nullptr, LampMaterialPath);
	if (TestNotNull(TEXT("M_VehicleLamp loads"), LampMaterial))
	{
		bool bReads = false;
		for (const TObjectPtr<UMaterialExpression>& Expression : LampMaterial->GetExpressions())
		{
			const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression);
			bReads |= Scalar && Scalar->bUseCustomPrimitiveData && Scalar->PrimitiveDataIndex == RTSPrimitiveData::LampOn;
		}
		TestTrue(TEXT("M_VehicleLamp reads RTSPrimitiveData::LampOn"), bReads);
	}

	const FScopedCVar Budget(TEXT("rts.Fx.VehicleLightMaxLights"), *FString::FromInt(MaxLights));
	const FRTSTestWorld TestWorld;
	URTSVehicleLightSubsystem& VehicleLights = *TestWorld.World->GetSubsystem<URTSVehicleLightSubsystem>();
	const URTSDayNightSubsystem& DayNight = *TestWorld.World->GetSubsystem<URTSDayNightSubsystem>();
	TestFalse(TEXT("A world without a sun is day"), DayNight.IsDark());

	// A row along X, the camera over its first vehicle: the nearest are the first.
	TArray<ARTSUnit*> Units;
	for (int32 Index = 0; Index < Vehicles; ++Index)
	{
		Units.Add(TestWorld.SpawnUnit(M20, 0, FVector(Index * 1200.f, 0.f, 0.f)));
	}
	VehicleLights.SetViewLocationOverride(FVector(0.f, 0.f, 2000.f));

	auto CountLamps = [&Units](float Value)
	{
		return static_cast<int32>(Algo::CountIf(Units, [Value](const ARTSUnit* Unit) { return Unit && GetLampOn(*Unit->GetMesh()) == Value; }));
	};

	VehicleLights.UpdateAll(1.f / 30.f);
	TestEqual(TEXT("By day: no real light"), VehicleLights.GetActiveLightCount(), 0);
	TestEqual(TEXT("By day: every lamp off"), CountLamps(0.f), Vehicles);

	{
		const FScopedCVar Dark(TEXT("rts.Light.ForceDark"), TEXT("1"));
		TestTrue(TEXT("rts.Light.ForceDark 1 is dark"), DayNight.IsDark());
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			VehicleLights.UpdateAll(1.f / 30.f);
		}
		TestEqual(TEXT("In the dark: every lamp on"), CountLamps(1.f), Vehicles);
		TestEqual(TEXT("In the dark: the vehicles whose lights fit the budget light the ground"), VehicleLights.GetActiveVehicleCount(), LitVehicles);
		TestEqual(TEXT("In the dark: four real lights each, within the budget"), VehicleLights.GetActiveLightCount(), LitVehicles * LightsPerM20);
		for (int32 Index = 0; Index < Vehicles; ++Index)
		{
			const TArray<USpotLightComponent*> Headlights = VehicleLights.GetHeadlights(*Units[Index]);
			const TArray<USpotLightComponent*> Taillights = VehicleLights.GetTaillights(*Units[Index]);
			const bool bNear = Index < LitVehicles;
			const int32 Visible = static_cast<int32>(Algo::CountIf(Headlights, [](const USpotLightComponent* Light) { return Light && Light->IsVisible(); })
				+ Algo::CountIf(Taillights, [](const USpotLightComponent* Light) { return Light && Light->IsVisible(); }));
			TestEqual(*FString::Printf(TEXT("M20 %d %s the camera: its lights on the ground"), Index, bNear ? TEXT("near") : TEXT("far from")),
				Visible, bNear ? LightsPerM20 : 0);
			if (Index == 0 && TestTrue(TEXT("The nearest has a light at each lamp cluster"), Headlights.Num() == 2 && Taillights.Num() == 2))
			{
				const FTransform Hull = Units[Index]->GetMesh()->GetComponentTransform();
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const USpotLightComponent& Headlight = *Headlights[Side];
					const USpotLightComponent& Taillight = *Taillights[Side];
					TestTrue(*FString::Printf(TEXT("Headlight %d stands at its cluster"), Side),
						Headlight.GetComponentLocation().Equals(Hull.TransformPosition(Heads[Side]), 0.5));
					TestTrue(TEXT("and shines ahead and down"), Headlight.GetForwardVector().X > 0.5f && Headlight.GetForwardVector().Z < 0.f);
					TestTrue(*FString::Printf(TEXT("Tail light %d stands at its tail lamp"), Side),
						Taillight.GetComponentLocation().Equals(Hull.TransformPosition(Tails[Side]), 0.5));
					TestTrue(TEXT("and shines back and down"), Taillight.GetForwardVector().X < -0.5f && Taillight.GetForwardVector().Z < 0.f);
					TestTrue(TEXT("The tail light is red"), Taillight.GetLightColor().R > 0.5f && Taillight.GetLightColor().G < 0.2f);
					TestTrue(TEXT("Neither casts shadows"), !Headlight.CastShadows && !Taillight.CastShadows);
				}
			}
		}
	}

	{
		const FScopedCVar Day(TEXT("rts.Light.ForceDark"), TEXT("0"));
		VehicleLights.UpdateAll(1.f / 30.f);
		TestEqual(TEXT("Day again: no real light"), VehicleLights.GetActiveLightCount(), 0);
		TestEqual(TEXT("Day again: every lamp off"), CountLamps(0.f), Vehicles);
	}
	return true;
}

#endif
