#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Scalability.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSDustSubsystem.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSNearestToView.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "Units/RTSUnitShape.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSDustTests
{
	using RTSTests::FRTSTestWorld;

	constexpr double Rate = 30.0;

	/** User.Rate as the unit's emitters were last told, or -1 without one. */
	float GetSentRate(const UNiagaraComponent& Component)
	{
		const float* Value = reinterpret_cast<const float*>(Component.GetOverrideParameters().GetParameterData(
			FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Rate"))));
		return Value ? *Value : -1.f;
	}

	/** One update of the unit's gear, as the engine ticks it, then of the dust, as the frame's end does. */
	void Step(ARTSUnit& Unit, URTSDustSubsystem& Dust, float Dt)
	{
		Unit.GetWorld()->TimeSeconds += Dt;
		Unit.GetUnitMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
		Dust.UpdateAll(Dt);
	}

	void Run(ARTSUnit& Unit, URTSDustSubsystem& Dust, const RTSLocomotion::FCommand& Command, double Seconds)
	{
		Unit.GetUnitMovement()->SetCommandOverride(Command);
		const int32 Steps = FMath::RoundToInt32(Seconds * Rate);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Step(Unit, Dust, static_cast<float>(1.0 / Rate));
		}
	}

	RTSLocomotion::FCommand Ahead(float Speed, float Curvature = 0.f)
	{
		RTSLocomotion::FCommand Command;
		Command.Speed = Speed;
		Command.Curvature = Curvature;
		return Command;
	}

	/** A floor of Material 400 m across, its top at Z, so at z 0 it is the ground the trace finds under the units. */
	AStaticMeshActor* SpawnFloor(const FRTSTestWorld& TestWorld, UMaterialInterface* Material, float Z = 0.f)
	{
		AStaticMeshActor* Floor = TestWorld.World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, Z - 50.f), FRotator::ZeroRotator);
		// Static meshes cannot change on a static component in a game world.
		Floor->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->GetStaticMeshComponent()->SetMaterial(0, Material);
		Floor->SetActorScale3D(FVector(400.f, 400.f, 1.f));
		return Floor;
	}

	/** The unit standing on the floor at X, its hull's bottom at z 0. */
	ARTSUnit* SpawnOnFloor(const FRTSTestWorld& TestWorld, URTSUnitData* Data, float X)
	{
		const float Bottom = static_cast<float>(RTSUnitShape::FromData(*Data).Hull.Min.Z);
		return TestWorld.SpawnUnit(Data, 0, FVector(X, 0.f, -Bottom));
	}
}

/**
 * Dust behind ground vehicles follows the hull's motion, not its orders (#224): none at rest, more with speed and with
 * turning, pivoting included, scaled and coloured by the ground's physical material, which is asked for at most once per
 * vehicle per rts.Fx.DustSurfaceInterval. The M20 (tracked, its contact points from its model) and the flak truck (wheeled,
 * its points from its locomotion) drive on M_Ground, which is dirt, and on water, which raises none. Past
 * rts.Fx.DustMaxVehicles only the nearest show any, and a dedicated server draws none. Niagara does not run under
 * -nullrhi, so the emitters' state and parameters are checked, not their particles.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleDustTest, "WorldDivide.Vehicle.Dust", RTSTests::TestFlags)

bool FRTSVehicleDustTest::RunTest(const FString& Parameters)
{
	using namespace RTSDustTests;

	// The rate alone: 0 at rest and creeping, rising with speed and with turning, none on ground that raises none.
	{
		const FRTSVehicleDust Default;
		TestEqual(TEXT("Rate: none at rest"), URTSDustSubsystem::GetRate(Default, 0.f, 0.f), 0.f);
		TestEqual(TEXT("Rate: none creeping under MinSpeed"), URTSDustSubsystem::GetRate(Default, 0.5f * Default.MinSpeed, 0.f), 0.f);
		const float Slow = URTSDustSubsystem::GetRate(Default, Default.MinSpeed + 200.f, 0.f);
		const float Fast = URTSDustSubsystem::GetRate(Default, Default.MinSpeed + 800.f, 0.f);
		TestTrue(TEXT("Rate: rises with speed"), Slow > 0.f && Fast > Slow);
		TestEqual(TEXT("Rate: the same in reverse"), URTSDustSubsystem::GetRate(Default, -(Default.MinSpeed + 200.f), 0.f), Slow);
		TestTrue(TEXT("Rate: a pivot on the spot raises some"), URTSDustSubsystem::GetRate(Default, 0.f, 60.f) > 0.f);
		TestTrue(TEXT("Rate: turning raises more"), URTSDustSubsystem::GetRate(Default, Default.MinSpeed + 200.f, -30.f) > Slow);
		TestEqual(TEXT("Rate: never past MaxRate"), URTSDustSubsystem::GetRate(Default, 1.e6f, 1.e3f), Default.MaxRate);
		TestEqual(TEXT("Rate: none on ground that raises none"), URTSDustSubsystem::GetRate(Default, Default.MinSpeed + 800.f, 0.f, 0.f), 0.f);
	}

	// A dedicated server draws none, so it registers none.
	TestFalse(TEXT("A dedicated server draws no dust"), URTSDustSubsystem::DrawsOn(NM_DedicatedServer));
	TestTrue(TEXT("A listen server draws dust"), URTSDustSubsystem::DrawsOn(NM_ListenServer));
	TestTrue(TEXT("A client draws dust"), URTSDustSubsystem::DrawsOn(NM_Client));
	TestTrue(TEXT("A single player draws dust"), URTSDustSubsystem::DrawsOn(NM_Standalone));

	const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_Dust.NS_Dust"));
	if (!TestNotNull(TEXT("NS_Dust loads"), System))
	{
		return false;
	}
	for (const FName User : { FName(TEXT("User.Rate")), FName(TEXT("User.Opacity")), FName(TEXT("User.Size")) })
	{
		TestTrue(*FString::Printf(TEXT("It takes %s"), *User.ToString()),
			System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), User)) != INDEX_NONE);
	}
	TestTrue(TEXT("It takes User.Color"),
		System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Direction"),
		System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Direction"))) != INDEX_NONE);
	TestEqual(TEXT("One stream of dust"), System->GetEmitterHandles().Num(), 1);
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Emitter = Handle.GetEmitterData();
		TestTrue(TEXT("It lives in world space, left behind as the hull drives on"), Emitter && !Emitter->bLocalSpace);
	}
	TestTrue(TEXT("It is ready to run"), System->IsValid());

	// The maps' ground is dirt, and their water water.
	UMaterialInterface* Ground = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ground.M_Ground"));
	UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Water.M_Water"));
	const UPhysicalMaterial* Dirt = LoadObject<UPhysicalMaterial>(nullptr, TEXT("/Game/Materials/Physical/PM_Dirt.PM_Dirt"));
	if (!TestNotNull(TEXT("M_Ground loads"), Ground) || !TestNotNull(TEXT("M_Water loads"), Water) || !TestNotNull(TEXT("PM_Dirt loads"), Dirt))
	{
		return false;
	}
	TestTrue(TEXT("M_Ground is dirt"), Ground->GetPhysicalMaterial() == Dirt);
	TestTrue(TEXT("Dirt has a surface type of its own"), Dirt->SurfaceType != SurfaceType_Default);
	const UPhysicalMaterial* WaterSurface = Water->GetPhysicalMaterial();
	TestTrue(TEXT("M_Water has a surface type of its own"), WaterSurface && WaterSurface->SurfaceType != SurfaceType_Default);

	// The physics engine's own of each, as a map that uses them makes them when it loads, before a body is made with them.
	const_cast<UPhysicalMaterial*>(Dirt)->GetPhysicsMaterial();
	if (WaterSurface)
	{
		const_cast<UPhysicalMaterial*>(WaterSurface)->GetPhysicsMaterial();
	}

	const int32 Quality = Scalability::GetQualityLevels().EffectsQuality;
	const float Interval = URTSDustSubsystem::GetSurfaceInterval();
	for (const TCHAR* Path : { TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"), TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck") })
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Path), Data))
		{
			continue;
		}
		const FString What = Data->GetName();
		const FRTSVehicleDust& UnitDust = Data->Dust;
		if (!TestTrue(*FString::Printf(TEXT("%s raises dust"), *What), UnitDust.HasDust() && UnitDust.System == System))
		{
			continue;
		}
		const bool bTracked = Data->Locomotion.Type == ERTSLocomotionType::Tracked;
		const FRTSDustSurface* DirtDust = UnitDust.FindSurface(Dirt->SurfaceType);
		const FRTSDustSurface* WaterDust = WaterSurface ? UnitDust.FindSurface(WaterSurface->SurfaceType) : nullptr;
		TestTrue(*FString::Printf(TEXT("%s: dirt raises dust"), *What), DirtDust && DirtDust->RateScale > 0.f);
		TestTrue(*FString::Printf(TEXT("%s: water raises none"), *What), WaterDust && WaterDust->RateScale == 0.f);

		// Behind the hull's middle, one each side, on the ground.
		TArray<FVector> Contacts;
		URTSDustSubsystem::GetContactPoints(*Data, Contacts);
		const FBox HullBox = RTSUnitShape::GetHullBox(*Data);
		if (TestEqual(*FString::Printf(TEXT("%s: two contact points"), *What), Contacts.Num(), 2))
		{
			for (const FVector& Contact : Contacts)
			{
				TestTrue(*FString::Printf(TEXT("%s: %s behind the middle"), *What, *Contact.ToString()), Contact.X < HullBox.GetCenter().X);
				TestTrue(*FString::Printf(TEXT("%s: %s on the ground"), *What, *Contact.ToString()),
					FMath::Abs(Contact.Z - HullBox.Min.Z) < 0.1 * HullBox.GetSize().Z);
			}
			TestTrue(*FString::Printf(TEXT("%s: one each side"), *What), Contacts[0].Y * Contacts[1].Y < 0.0);
		}

		const FRTSTestWorld TestWorld;
		AStaticMeshActor* Floor = SpawnFloor(TestWorld, Ground);
		// Water far below, which the units only drive on later: a physical material is ready for the traces only once the
		// world has taken it in, which a test world that never ticks does when its first unit spawns.
		AStaticMeshActor* Deep = SpawnFloor(TestWorld, Water, -100000.f);
		URTSDustSubsystem& Dust = *TestWorld.World->GetSubsystem<URTSDustSubsystem>();
		ARTSUnit* Unit = SpawnOnFloor(TestWorld, Data, -15000.f);
		auto Expected = [&]()
		{
			const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
			return URTSDustSubsystem::GetRate(UnitDust, Motion.ForwardSpeed, Motion.YawRate, DirtDust ? DirtDust->RateScale : 1.f)
				* RTSNearestToView::GetRateScale(0.f, URTSDustSubsystem::GetDistance(), Quality);
		};
		auto SentRate = [&]()
		{
			const TConstArrayView<TObjectPtr<UNiagaraComponent>> Components = Dust.GetComponents(*Unit);
			return Components.IsEmpty() || !Components[0] ? -1.f : GetSentRate(*Components[0]);
		};
		auto CheckRunning = [&](const TCHAR* When)
		{
			TestEqual(*FString::Printf(TEXT("%s %s: an emitter per contact point"), *What, When), Dust.GetComponents(*Unit).Num(), Contacts.Num());
			TestEqual(*FString::Printf(TEXT("%s %s: all running"), *What, When), Dust.GetActiveEmitters(), Contacts.Num());
			for (const UNiagaraComponent* Component : Dust.GetComponents(*Unit))
			{
				TestTrue(*FString::Printf(TEXT("%s %s: on the hull"), *What, When), Component && Component->GetAttachParent() == Unit->GetMesh());
				TestTrue(*FString::Printf(TEXT("%s %s: casts no shadow"), *What, When), Component && !Component->CastShadow);
				const float Sent = Component ? GetSentRate(*Component) : -1.f;
				TestTrue(*FString::Printf(TEXT("%s %s: told %.2f puffs a second (%.2f)"), *What, When, Expected(), Sent),
					FMath::IsNearlyEqual(Sent, Expected(), 0.06f * Expected() + 0.01f));
			}
		};

		// Standing, no dust.
		Run(*Unit, Dust, RTSLocomotion::FCommand(), 1.0);
		TestEqual(*FString::Printf(TEXT("%s standing: no emitter running"), *What), Dust.GetActiveEmitters(), 0);
		TestEqual(*FString::Printf(TEXT("%s standing: nothing asked of the ground"), *What), Dust.GetSurfaceQueries(*Unit), 0);

		// Driving slowly, then fast: more dust; the ground under it is dirt.
		Run(*Unit, Dust, Ahead(0.4f * Data->MoveSpeed), 2.0);
		CheckRunning(TEXT("driving slowly"));
		const float SlowRate = SentRate();
		TestEqual(*FString::Printf(TEXT("%s on M_Ground: dirt"), *What), Dust.GetSurface(*Unit), Dirt->SurfaceType.GetValue());
		Run(*Unit, Dust, Ahead(Data->MoveSpeed), 2.0);
		CheckRunning(TEXT("driving fast"));
		TestTrue(*FString::Printf(TEXT("%s: more dust fast (%.2f) than slowly (%.2f)"), *What, SentRate(), SlowRate), SentRate() > SlowRate);

		// The ground is asked for at most once per interval while it drives.
		if (TestTrue(TEXT("rts.Fx.DustSurfaceInterval spaces the traces"), Interval > 0.f))
		{
			const int32 Before = Dust.GetSurfaceQueries(*Unit);
			const double Seconds = 3.0;
			Run(*Unit, Dust, Ahead(0.6f * Data->MoveSpeed), Seconds);
			const int32 Queries = Dust.GetSurfaceQueries(*Unit) - Before;
			AddInfo(FString::Printf(TEXT("%s: %d traces in %.1f s at %.2f s apart"), *What, Queries, Seconds, Interval));
			TestTrue(*FString::Printf(TEXT("%s: at most one trace per interval (%d)"), *What, Queries),
				Queries >= 1 && Queries <= FMath::FloorToInt32(Seconds / Interval) + 1);
		}

		// Turning raises more than driving straight at the same speed; a tracked hull's pivot on the spot raises some too.
		Run(*Unit, Dust, Ahead(0.6f * Data->MoveSpeed), 1.0);
		const float StraightRate = SentRate();
		Run(*Unit, Dust, Ahead(0.6f * Data->MoveSpeed, 1.f / 2500.f), 1.5);
		CheckRunning(TEXT("turning"));
		TestTrue(*FString::Printf(TEXT("%s: more dust turning (%.2f) than straight (%.2f)"), *What, SentRate(), StraightRate), SentRate() > StraightRate);
		if (bTracked)
		{
			Run(*Unit, Dust, RTSLocomotion::FCommand(), 3.0);
			Run(*Unit, Dust, RTSLocomotion::FCommand::FromTracks(Unit->GetUnitMovement()->GetLimits(), -200.f, 200.f), 1.0);
			AddInfo(FString::Printf(TEXT("%s pivoting: %.0f cm/s, %.1f deg/s"), *What, Unit->GetRealizedMotion().ForwardSpeed,
				Unit->GetRealizedMotion().YawRate));
			CheckRunning(TEXT("pivoting"));
		}

		// Stopped, the dust stops.
		Run(*Unit, Dust, RTSLocomotion::FCommand(), 3.0);
		TestEqual(*FString::Printf(TEXT("%s stopped: no emitter running"), *What), Dust.GetActiveEmitters(), 0);
		for (const UNiagaraComponent* Component : Dust.GetComponents(*Unit))
		{
			TestTrue(*FString::Printf(TEXT("%s stopped: emitter stopped"), *What), Component && !Component->IsActive());
		}

		// Far from the camera, and with rts.Fx.Dust off, none either.
		const float Distance = URTSDustSubsystem::GetDistance();
		if (TestTrue(TEXT("rts.Fx.DustDistance culls"), Distance > 0.f))
		{
			Dust.SetViewLocationOverride(Unit->GetActorLocation() + FVector(0.f, 0.f, 2.f * Distance));
			Run(*Unit, Dust, Ahead(Data->MoveSpeed), 1.0);
			TestEqual(*FString::Printf(TEXT("%s far away: no emitter running"), *What), Dust.GetActiveEmitters(), 0);
			Dust.SetViewLocationOverride({});
		}
		IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Fx.Dust"));
		if (TestNotNull(TEXT("rts.Fx.Dust exists"), Enabled))
		{
			Enabled->Set(false, ECVF_SetByCode);
			Run(*Unit, Dust, Ahead(Data->MoveSpeed), 0.5);
			TestEqual(*FString::Printf(TEXT("%s with dust off: no emitter running"), *What), Dust.GetActiveEmitters(), 0);
			Enabled->Set(true, ECVF_SetByCode);
		}

		// On water, driving raises none.
		// The water floor in its place, rather than a new material on the old one, whose body keeps the one it was made with.
		Floor->Destroy();
		Deep->SetActorLocation(Floor->GetActorLocation());
		Run(*Unit, Dust, Ahead(Data->MoveSpeed), 1.0);
		TestTrue(*FString::Printf(TEXT("%s on water: the ground is water"), *What), WaterSurface && Dust.GetSurface(*Unit) == WaterSurface->SurfaceType);
		TestEqual(*FString::Printf(TEXT("%s on water: no emitter running"), *What), Dust.GetActiveEmitters(), 0);
		Unit->GetUnitMovement()->SetCommandOverride({});
	}

	// Past the budget only the nearest show dust: of two M20s driving, the one by the camera.
	IConsoleVariable* MaxVehicles = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Fx.DustMaxVehicles"));
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
	if (TestNotNull(TEXT("rts.Fx.DustMaxVehicles exists"), MaxVehicles) && M20)
	{
		const int32 Budget = MaxVehicles->GetInt();
		MaxVehicles->Set(1, ECVF_SetByCode);
		const FRTSTestWorld TestWorld;
		SpawnFloor(TestWorld, Ground);
		URTSDustSubsystem& Dust = *TestWorld.World->GetSubsystem<URTSDustSubsystem>();
		ARTSUnit* Near = SpawnOnFloor(TestWorld, M20, 0.f);
		ARTSUnit* Far = TestWorld.SpawnUnit(M20, 0, Near->GetActorLocation() + FVector(0.f, 3000.f, 0.f));
		Dust.SetViewLocationOverride(Near->GetActorLocation() + FVector(0.f, 0.f, 2000.f));
		Near->GetUnitMovement()->SetCommandOverride(Ahead(M20->MoveSpeed));
		Far->GetUnitMovement()->SetCommandOverride(Ahead(M20->MoveSpeed));
		for (int32 Index = 0; Index < 15; ++Index)
		{
			const float Dt = static_cast<float>(1.0 / Rate);
			TestWorld.World->TimeSeconds += Dt;
			Near->GetUnitMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Far->GetUnitMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Dust.UpdateAll(Dt);
		}
		TArray<FVector> Contacts;
		URTSDustSubsystem::GetContactPoints(*M20, Contacts);
		TestEqual(TEXT("One vehicle within a budget of one"), Dust.GetActiveEmitters(), Contacts.Num());
		TestEqual(TEXT("The one by the camera"), Dust.GetComponents(*Near).Num(), Contacts.Num());
		TestEqual(TEXT("None for the other"), Dust.GetComponents(*Far).Num(), 0);
		MaxVehicles->Set(Budget, ECVF_SetByCode);
	}
	return true;
}

#endif
