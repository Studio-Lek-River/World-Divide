#include "Data/RTSUnitData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Scalability.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSExhaustSubsystem.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSExhaustTests
{
	using RTSTests::FRTSTestWorld;

	constexpr double Rate = 30.0;

	/** The float user parameter Name as the unit's emitters were last told, or -1 without one. */
	float GetSentFloat(const UNiagaraComponent& Component, const TCHAR* Name)
	{
		const float* Value = reinterpret_cast<const float*>(Component.GetOverrideParameters().GetParameterData(
			FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), Name)));
		return Value ? *Value : -1.f;
	}

	/** User.Rate as the unit's emitters were last told, or -1 without one. */
	float GetSentRate(const UNiagaraComponent& Component)
	{
		return GetSentFloat(Component, TEXT("User.Rate"));
	}

	/** User.Direction as the unit's emitters were last told, or zero without one. */
	FVector GetSentDirection(const UNiagaraComponent& Component)
	{
		const FVector3f* Value = reinterpret_cast<const FVector3f*>(Component.GetOverrideParameters().GetParameterData(
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Direction"))));
		return Value ? FVector(*Value) : FVector::ZeroVector;
	}

	/** User.Color as the unit's emitters were last told, or transparent black without one. */
	FLinearColor GetSentColor(const UNiagaraComponent& Component)
	{
		const FLinearColor* Value = reinterpret_cast<const FLinearColor*>(Component.GetOverrideParameters().GetParameterData(
			FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"))));
		return Value ? *Value : FLinearColor::Transparent;
	}

	/** One update of the unit's gear, as the engine ticks it, then of the exhaust, as the frame's end does. */
	void Step(ARTSUnit& Unit, URTSExhaustSubsystem& Exhaust, float Dt)
	{
		Unit.GetWorld()->TimeSeconds += Dt;
		Unit.GetUnitMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
		Exhaust.UpdateAll(Dt);
	}

	/** Runs the unit's gear on Command for Seconds, counting the frames in each exhaust state; returns the state at the end. */
	ERTSExhaustState Run(ARTSUnit& Unit, URTSExhaustSubsystem& Exhaust, const RTSLocomotion::FCommand& Command, double Seconds,
		int32 (&OutFrames)[3])
	{
		Unit.GetUnitMovement()->SetCommandOverride(Command);
		FMemory::Memzero(OutFrames);
		const int32 Steps = FMath::RoundToInt32(Seconds * Rate);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Step(Unit, Exhaust, static_cast<float>(1.0 / Rate));
			++OutFrames[static_cast<int32>(Exhaust.GetState(Unit))];
		}
		return Exhaust.GetState(Unit);
	}

	RTSLocomotion::FCommand Ahead(float Speed)
	{
		RTSLocomotion::FCommand Command;
		Command.Speed = Speed;
		return Command;
	}
}

/**
 * Engine exhaust follows the hull's motion, not its orders (#222): the M20 (tracked) and the flak truck (wheeled) stand,
 * set off, drive on at a steady speed and stop again, and their exhaust goes idle, accelerating, moving and idle, its
 * emitters told the rate, the colour (#325) and the puff life and lift (#340) of each state. Past rts.Fx.ExhaustDistance from the camera, and with rts.Fx.Exhaust off, their
 * emitters stop; past rts.Fx.ExhaustMaxVehicles only the nearest show any. Niagara does not run under -nullrhi, so the
 * emitters' state and parameters are checked, not their puffs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleExhaustTest, "WorldDivide.Vehicle.Exhaust", RTSTests::TestFlags)

bool FRTSVehicleExhaustTest::RunTest(const FString& Parameters)
{
	using namespace RTSExhaustTests;

	const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_Exhaust.NS_Exhaust"));
	if (!TestNotNull(TEXT("NS_Exhaust loads"), System))
	{
		return false;
	}
	for (const FName User : { FName(TEXT("User.Rate")), FName(TEXT("User.Opacity")), FName(TEXT("User.Size")), FName(TEXT("User.Lifetime")),
		FName(TEXT("User.Lift")) })
	{
		TestTrue(*FString::Printf(TEXT("It takes %s"), *User.ToString()),
			System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), User)) != INDEX_NONE);
	}
	TestTrue(TEXT("It takes User.Color"),
		System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Direction"),
		System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Direction"))) != INDEX_NONE);
	TestEqual(TEXT("One stream of puffs"), System->GetEmitterHandles().Num(), 1);
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Emitter = Handle.GetEmitterData();
		TestTrue(TEXT("It lives in world space, left behind as the hull drives on"), Emitter && !Emitter->bLocalSpace);
		TestTrue(TEXT("It spreads a frame's puffs along the hull's move"),
			Emitter && Emitter->InterpolatedSpawnMode == ENiagaraInterpolatedSpawnMode::Interpolation);
	}
	TestTrue(TEXT("It is ready to run"), System->IsValid());
	const int32 Quality = Scalability::GetQualityLevels().EffectsQuality;

	for (const TCHAR* Path : { TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"), TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck") })
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Path), Data))
		{
			continue;
		}
		const FString What = Data->GetName();
		const FRTSVehicleExhaust& UnitExhaust = Data->Exhaust;
		if (!TestTrue(*FString::Printf(TEXT("%s has exhaust"), *What), UnitExhaust.HasExhaust() && UnitExhaust.System == System))
		{
			continue;
		}
		const FRTSTestWorld TestWorld;
		URTSExhaustSubsystem& Exhaust = *TestWorld.World->GetSubsystem<URTSExhaustSubsystem>();
		ARTSUnit* Unit = TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector);
		int32 Frames[3];
		auto Rates = [&](ERTSExhaustState State)
		{
			const float Base = State == ERTSExhaustState::Accelerating ? UnitExhaust.AcceleratingRate
				: State == ERTSExhaustState::Moving ? UnitExhaust.MovingRate : UnitExhaust.IdleRate;
			const float Speed = FMath::Abs(Unit->GetRealizedMotion().ForwardSpeed);
			const float PuffsPerMetre = State == ERTSExhaustState::Moving ? UnitExhaust.MovingPuffsPerMetre : UnitExhaust.PuffsPerMetre;
			return URTSExhaustSubsystem::GetRate(Base + Speed / 100.f * PuffsPerMetre, 0.f, Quality);
		};
		auto CheckEmitters = [&](const TCHAR* When, ERTSExhaustState State)
		{
			const TConstArrayView<TObjectPtr<UNiagaraComponent>> Components = Exhaust.GetComponents(*Unit);
			TestEqual(*FString::Printf(TEXT("%s %s: an emitter per outlet"), *What, When), Components.Num(), UnitExhaust.Outlets.Num());
			TestEqual(*FString::Printf(TEXT("%s %s: all running"), *What, When), Exhaust.GetActiveEmitters(), UnitExhaust.Outlets.Num());
			for (const UNiagaraComponent* Component : Components)
			{
				TestTrue(*FString::Printf(TEXT("%s %s: on the hull"), *What, When), Component && Component->GetAttachParent() == Unit->GetMesh());
				TestTrue(*FString::Printf(TEXT("%s %s: casts no shadow"), *What, When), Component && !Component->CastShadow);
				const float Sent = Component ? GetSentRate(*Component) : -1.f;
				TestTrue(*FString::Printf(TEXT("%s %s: told %.2f puffs a second (%.2f)"), *What, When, Rates(State), Sent),
					FMath::IsNearlyEqual(Sent, Rates(State), 0.06f * Rates(State) + 0.01f));
				const FLinearColor& Color = State == ERTSExhaustState::Accelerating ? UnitExhaust.AcceleratingColor
					: State == ERTSExhaustState::Moving ? UnitExhaust.MovingColor : UnitExhaust.IdleColor;
				const FLinearColor SentColor = Component ? GetSentColor(*Component) : FLinearColor::Transparent;
				TestTrue(*FString::Printf(TEXT("%s %s: told the state's colour %s (%s)"), *What, When, *Color.ToString(), *SentColor.ToString()),
					SentColor.Equals(Color));
				// Cruising, the puffs live briefly and rise off the ground (#340).
				const float Lifetime = State == ERTSExhaustState::Accelerating ? UnitExhaust.AcceleratingLifetime
					: State == ERTSExhaustState::Moving ? UnitExhaust.MovingLifetime : UnitExhaust.IdleLifetime;
				const float Lift = State == ERTSExhaustState::Moving ? UnitExhaust.MovingLift : 0.f;
				TestTrue(*FString::Printf(TEXT("%s %s: told the state's puff life %.2f s and lift %.0f cm/s"), *What, When, Lifetime, Lift),
					Component && GetSentFloat(*Component, TEXT("User.Lifetime")) == Lifetime && GetSentFloat(*Component, TEXT("User.Lift")) == Lift);
				const FVector Out = Unit->GetMesh()->GetComponentTransform().TransformVectorNoScale(UnitExhaust.Direction.GetSafeNormal());
				TestTrue(*FString::Printf(TEXT("%s %s: told the outlet's direction in the world"), *What, When),
					Component && FVector::DotProduct(GetSentDirection(*Component), Out) > 0.99f);
			}
		};

		// Standing, the engine idles.
		TestEqual(*FString::Printf(TEXT("%s standing: idle"), *What), Run(*Unit, Exhaust, RTSLocomotion::FCommand(), 1.0, Frames),
			ERTSExhaustState::Idle);
		TestEqual(*FString::Printf(TEXT("%s standing: idle throughout"), *What), Frames[static_cast<int32>(ERTSExhaustState::Idle)],
			FMath::RoundToInt32(Rate));
		CheckEmitters(TEXT("standing"), ERTSExhaustState::Idle);

		// Setting off: accelerating while the speed climbs.
		const ERTSExhaustState SettingOff = Run(*Unit, Exhaust, Ahead(Data->MoveSpeed), 0.5, Frames);
		TestEqual(*FString::Printf(TEXT("%s setting off: accelerating"), *What), SettingOff, ERTSExhaustState::Accelerating);
		CheckEmitters(TEXT("setting off"), ERTSExhaustState::Accelerating);

		// Holding a steady speed below the top: accelerating until it is reached, then moving.
		const float Cruise = 0.25f * Data->MoveSpeed;
		TestEqual(*FString::Printf(TEXT("%s at a steady speed: moving"), *What), Run(*Unit, Exhaust, Ahead(Cruise), 3.0, Frames),
			ERTSExhaustState::Moving);
		AddInfo(FString::Printf(TEXT("%s to %.0f cm/s: %d frames accelerating, then %d moving"), *What, Cruise,
			Frames[static_cast<int32>(ERTSExhaustState::Accelerating)], Frames[static_cast<int32>(ERTSExhaustState::Moving)]));
		TestTrue(*FString::Printf(TEXT("%s at a steady speed: only moving after the climb"), *What),
			Frames[static_cast<int32>(ERTSExhaustState::Idle)] == 0 && Frames[static_cast<int32>(ERTSExhaustState::Moving)] > Rate);
		CheckEmitters(TEXT("at a steady speed"), ERTSExhaustState::Moving);

		// Stopping: never accelerating while it brakes, idle once it stands.
		TestEqual(*FString::Printf(TEXT("%s stopped: idle"), *What), Run(*Unit, Exhaust, RTSLocomotion::FCommand(), 3.0, Frames),
			ERTSExhaustState::Idle);
		TestEqual(*FString::Printf(TEXT("%s braking: never accelerating"), *What), Frames[static_cast<int32>(ERTSExhaustState::Accelerating)], 0);
		CheckEmitters(TEXT("stopped"), ERTSExhaustState::Idle);

		// Far from the camera the emitters stop, and start again when it comes near.
		const float Distance = URTSExhaustSubsystem::GetDistance();
		if (TestTrue(TEXT("rts.Fx.ExhaustDistance culls"), Distance > 0.f))
		{
			Exhaust.SetViewLocationOverride(Unit->GetActorLocation() + FVector(0.f, 0.f, 2.f * Distance));
			Run(*Unit, Exhaust, RTSLocomotion::FCommand(), 0.1, Frames);
			TestEqual(*FString::Printf(TEXT("%s far away: no emitter running"), *What), Exhaust.GetActiveEmitters(), 0);
			for (const UNiagaraComponent* Component : Exhaust.GetComponents(*Unit))
			{
				TestTrue(*FString::Printf(TEXT("%s far away: stopped"), *What), Component && !Component->IsActive());
			}
			Exhaust.SetViewLocationOverride(Unit->GetActorLocation() + FVector(0.f, 0.f, 0.5f * Distance));
			Run(*Unit, Exhaust, RTSLocomotion::FCommand(), 0.1, Frames);
			TestEqual(*FString::Printf(TEXT("%s near again: running"), *What), Exhaust.GetActiveEmitters(), UnitExhaust.Outlets.Num());
			Exhaust.SetViewLocationOverride({});
		}

		// Off, every emitter stops.
		IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Fx.Exhaust"));
		if (TestNotNull(TEXT("rts.Fx.Exhaust exists"), Enabled))
		{
			Enabled->Set(false, ECVF_SetByCode);
			Run(*Unit, Exhaust, RTSLocomotion::FCommand(), 0.1, Frames);
			TestEqual(*FString::Printf(TEXT("%s with exhaust off: no emitter running"), *What), Exhaust.GetActiveEmitters(), 0);
			Enabled->Set(true, ECVF_SetByCode);
		}
		Unit->GetUnitMovement()->SetCommandOverride({});
	}

	// Past the budget only the nearest show exhaust: of two M20s, the one by the camera.
	IConsoleVariable* MaxVehicles = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Fx.ExhaustMaxVehicles"));
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
	if (TestNotNull(TEXT("rts.Fx.ExhaustMaxVehicles exists"), MaxVehicles) && M20)
	{
		const int32 Budget = MaxVehicles->GetInt();
		MaxVehicles->Set(1, ECVF_SetByCode);
		const FRTSTestWorld TestWorld;
		URTSExhaustSubsystem& Exhaust = *TestWorld.World->GetSubsystem<URTSExhaustSubsystem>();
		ARTSUnit* Near = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
		ARTSUnit* Far = TestWorld.SpawnUnit(M20, 0, FVector(3000.f, 0.f, 0.f));
		Exhaust.SetViewLocationOverride(Near->GetActorLocation() + FVector(0.f, 0.f, 2000.f));
		Exhaust.UpdateAll(static_cast<float>(1.0 / Rate));
		TestEqual(TEXT("One vehicle within a budget of one"), Exhaust.GetActiveEmitters(), M20->Exhaust.Outlets.Num());
		TestEqual(TEXT("The one by the camera"), Exhaust.GetComponents(*Near).Num(), M20->Exhaust.Outlets.Num());
		TestEqual(TEXT("None for the other"), Exhaust.GetComponents(*Far).Num(), 0);
		MaxVehicles->Set(Budget, ECVF_SetByCode);
	}
	return true;
}

#endif
