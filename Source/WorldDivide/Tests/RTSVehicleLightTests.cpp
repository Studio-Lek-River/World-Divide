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

	/** The vehicles and the budget: many more than may light the ground. */
	constexpr int32 Vehicles = 40;
	constexpr int32 MaxVehicles = 8;

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
 * RTSPrimitiveData::LampOn. Of 40 M20s in the dark with a budget of 8, every one's lamps are on, but only the 8 nearest the
 * camera light the ground, with a white spot at the middle of their headlights pointing ahead and a red one at the middle of
 * their tail lamps pointing back: never more than twice the budget of real lights. By day no real light is on and every
 * lamp is off. The sun decides when nothing forces it: a test world without one is day.
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
	const FVector Head = FRTSVehicleLights::GetMidpoint(Lights.HeadlightOffsets);
	const FVector Tail = FRTSVehicleLights::GetMidpoint(Lights.TaillightOffsets);
	TestTrue(TEXT("Its headlights are at the front, its tail lamps at the back"), Head.X > 0.f && Tail.X < 0.f);

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

	const FScopedCVar Budget(TEXT("rts.Fx.VehicleLightMaxVehicles"), *FString::FromInt(MaxVehicles));
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
		TestEqual(TEXT("In the dark: the budget's vehicles light the ground"), VehicleLights.GetActiveVehicleCount(), MaxVehicles);
		TestTrue(*FString::Printf(TEXT("In the dark: %d real lights, at most twice the budget"), VehicleLights.GetActiveLightCount()),
			VehicleLights.GetActiveLightCount() <= 2 * MaxVehicles && VehicleLights.GetActiveLightCount() > 0);
		for (int32 Index = 0; Index < Vehicles; ++Index)
		{
			const USpotLightComponent* Headlight = VehicleLights.GetHeadlight(*Units[Index]);
			const USpotLightComponent* Taillight = VehicleLights.GetTaillight(*Units[Index]);
			const bool bNear = Index < MaxVehicles;
			TestEqual(*FString::Printf(TEXT("M20 %d %s the camera: lights the ground"), Index, bNear ? TEXT("near") : TEXT("far from")),
				Headlight && Headlight->IsVisible() && Taillight && Taillight->IsVisible(), bNear);
			if (Index == 0 && TestTrue(TEXT("The nearest has both lights"), Headlight && Taillight))
			{
				const FTransform Hull = Units[Index]->GetMesh()->GetComponentTransform();
				TestTrue(TEXT("Its headlight stands at the middle of its headlights"),
					Headlight->GetComponentLocation().Equals(Hull.TransformPosition(Head), 0.5));
				TestTrue(TEXT("and shines ahead and down"), Headlight->GetForwardVector().X > 0.5f && Headlight->GetForwardVector().Z < 0.f);
				TestTrue(TEXT("Its tail light stands at the middle of its tail lamps"),
					Taillight->GetComponentLocation().Equals(Hull.TransformPosition(Tail), 0.5));
				TestTrue(TEXT("and shines back and down"), Taillight->GetForwardVector().X < -0.5f && Taillight->GetForwardVector().Z < 0.f);
				TestTrue(TEXT("The tail light is red"), Taillight->GetLightColor().R > 0.5f && Taillight->GetLightColor().G < 0.2f);
				TestTrue(TEXT("Neither casts shadows"), !Headlight->CastShadows && !Taillight->CastShadows);
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
