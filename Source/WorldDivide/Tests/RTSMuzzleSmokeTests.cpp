#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMuzzleSmokeTests
{
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 1.f / 30.f;

	URTSUnitData* LoadM20(FAutomationTestBase& Test)
	{
		URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
		if (!Test.TestNotNull(TEXT("DA_TestM20Guardian loads"), M20) || !Test.TestNotNull(TEXT("It has a weapon"), M20->Weapon.Get())
			|| !Test.TestNotNull(TEXT("Its gun has a muzzle smoke"), M20->Weapon->MuzzleSmoke.Get()))
		{
			return nullptr;
		}
		return M20;
	}

	URTSUnitData* MakeTarget()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 1.0e9f;
		return Data;
	}

	/** Sets a console variable for the scope, restoring it afterwards. */
	struct FScopedCVar
	{
		FScopedCVar(const TCHAR* Name, const TCHAR* Value)
			: CVar(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (CVar)
			{
				Saved = CVar->GetString();
				CVar->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedCVar()
		{
			if (CVar)
			{
				CVar->Set(*Saved, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* CVar;
		FString Saved;
	};

	/** Fires Tank's gun once along its bore through the shots players see. */
	void Fire(URTSShotSubsystem& Shots, ARTSUnit& Tank)
	{
		const FTransform Muzzle = Tank.GetMuzzleTransform();
		Shots.PlayShot(Tank, *Tank.GetUnitData()->Weapon, nullptr, Muzzle.GetLocation() + Muzzle.GetUnitAxis(EAxis::X) * 3000.f, 0.2f, Muzzle);
	}
}

/**
 * The muzzle smoke the setup builds (#221): NS_MuzzleSmoke with the user parameters URTSShotSubsystem sets, simulated in
 * world space and drawn with the translucent M_MuzzleSmoke, and the M20's gun pointing at it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleSmokeAssetTest, "WorldDivide.Combat.MuzzleSmoke.Asset", RTSTests::TestFlags)

bool FRTSMuzzleSmokeAssetTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleSmokeTests;

	URTSUnitData* M20 = LoadM20(*this);
	const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_MuzzleSmoke.NS_MuzzleSmoke"));
	if (!M20 || !TestNotNull(TEXT("NS_MuzzleSmoke loads"), System))
	{
		return false;
	}
	TestTrue(TEXT("The M20's gun leaves NS_MuzzleSmoke"), M20->Weapon->MuzzleSmoke == System);
	const FNiagaraParameterStore& User = System->GetExposedParameters();
	TestTrue(TEXT("It takes User.Count"), User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.Count"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Direction"),
		User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Direction"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Scale"), User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Scale"))) != INDEX_NONE);
	if (TestEqual(TEXT("One emitter of puffs"), System->GetEmitterHandles().Num(), 1))
	{
		const FVersionedNiagaraEmitterData* Emitter = System->GetEmitterHandles()[0].GetEmitterData();
		TestTrue(TEXT("The puffs live in world space, left behind as the turret turns"), Emitter && !Emitter->bLocalSpace);
	}
	TestTrue(TEXT("It is ready to run"), System->IsValid());
	const UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_MuzzleSmoke.M_MuzzleSmoke"));
	if (TestNotNull(TEXT("M_MuzzleSmoke loads"), Material))
	{
		TestTrue(TEXT("It draws Niagara sprites"), Material->bUsedWithNiagaraSprites != 0);
		TestTrue(TEXT("It is translucent"), Material->GetBlendMode() == BLEND_Translucent);
	}
	return true;
}

/**
 * The smoke stays behind (#221): an M20 fires at a target to its side, then turns to one on its other side and raises its
 * gun. The puff starts at the muzzle along the bore, belongs to no actor, and stays where it was fired while the barrel
 * swings away.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleSmokeWorldSpaceTest, "WorldDivide.Combat.MuzzleSmoke.WorldSpace", RTSTests::TestFlags)

bool FRTSMuzzleSmokeWorldSpaceTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleSmokeTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	ARTSUnit* Target = TestWorld.SpawnUnit(MakeTarget(), 1, FVector(1500.f, 1500.f, 0.f));

	TArray<FTransform> EventMuzzles;
	Tank->OnShot().AddLambda([&EventMuzzles](const FRTSShotEvent& Event) { EventMuzzles.Add(Event.Muzzle); });
	for (int32 Frame = 0; Frame < 300 && EventMuzzles.IsEmpty(); ++Frame)
	{
		Tank->UpdateTurret(Step);
		Tank->GetWeaponComponent()->UpdateWeapon(Step);
		Shots->AdvanceEffects(Step);
	}
	UNiagaraComponent* Smoke = Shots->GetLatestMuzzleSmoke(*Tank);
	if (!TestEqual(TEXT("The M20 fired"), EventMuzzles.Num(), 1) || !TestNotNull(TEXT("It left a puff"), Smoke))
	{
		return false;
	}
	const FTransform Fired = EventMuzzles[0];
	TestEqual(TEXT("One puff for the shot"), Shots->GetLiveMuzzleSmokes(*Tank), 1);
	TestTrue(TEXT("It plays the gun's system"), Smoke->GetAsset() == M20->Weapon->MuzzleSmoke);
	TestNull(TEXT("It belongs to no actor"), Smoke->GetOwner());
	TestNull(TEXT("It is attached to nothing"), Smoke->GetAttachParent());
	const float Offset = FVector::Dist(Smoke->GetComponentLocation(), Fired.GetLocation());
	TestTrue(FString::Printf(TEXT("It starts at the muzzle (%.3f cm off)"), Offset), Offset <= 1.f);
	const FVector3f* Direction = reinterpret_cast<const FVector3f*>(Smoke->GetOverrideParameters().GetParameterData(
		FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Direction"))));
	if (TestNotNull(TEXT("It is told the bore"), Direction))
	{
		TestTrue(TEXT("It leaves along the bore"), FVector::DotProduct(FVector(*Direction), Fired.GetUnitAxis(EAxis::X)) > 0.9999f);
	}

	// A new target on the other side, above it: the turret swings round and the gun rises, before the next shot.
	Target->Destroy();
	TestWorld.SpawnUnit(MakeTarget(), 1, FVector(-1500.f, -1500.f, Tank->GetNavAgentLocation().Z + 600.f));
	const float StartYaw = Tank->GetTurretYaw();
	for (float Time = 0.f; Time < 1.5f; Time += Step)
	{
		Tank->UpdateTurret(Step);
		Shots->AdvanceEffects(Step);
	}
	const FTransform Now = Tank->GetMuzzleTransform();
	TestTrue(FString::Printf(TEXT("The turret turned (%.1f to %.1f degrees)"), StartYaw, Tank->GetTurretYaw()),
		FMath::Abs(FRotator::NormalizeAxis(Tank->GetTurretYaw() - StartYaw)) > 40.f);
	TestTrue(FString::Printf(TEXT("The muzzle moved away (%.0f cm)"), FVector::Dist(Now.GetLocation(), Fired.GetLocation())),
		FVector::Dist(Now.GetLocation(), Fired.GetLocation()) > 200.f);
	TestTrue(TEXT("The same puff still drifts"), Shots->GetLatestMuzzleSmoke(*Tank) == Smoke);
	const float Stayed = FVector::Dist(Smoke->GetComponentLocation(), Fired.GetLocation());
	TestTrue(FString::Printf(TEXT("It stays where it was fired (%.3f cm off)"), Stayed), Stayed <= 1.f);

	// Its life over, the slot is free and nothing drifts.
	for (float Time = 0.f; Time < URTSShotSubsystem::MuzzleSmokeSeconds; Time += Step)
	{
		Shots->AdvanceEffects(Step);
	}
	TestEqual(TEXT("No puff after its lifetime"), Shots->GetLiveMuzzleSmokes(), 0);
	TestEqual(TEXT("The pool keeps its one component"), Shots->GetMuzzleSmokeComponents(), 1);
	return true;
}

/**
 * The smoke never piles up (#221): for 60 s eight M20s fire every reload and one more on every frame. On every frame each
 * shooter has at most rts.Fx.MuzzleSmokePerShooter puffs and all of them at most rts.Fx.MuzzleSmokePoolSize, and the pool
 * never makes more components than that; then again with a pool smaller than the shooters' share.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleSmokeCapTest, "WorldDivide.Combat.MuzzleSmoke.Cap", RTSTests::TestFlags)

bool FRTSMuzzleSmokeCapTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleSmokeTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	for (const TCHAR* PoolSize : { TEXT("64"), TEXT("6") })
	{
		const FScopedCVar Pool(TEXT("rts.Fx.MuzzleSmokePoolSize"), PoolSize);
		const FScopedCVar PerShooter(TEXT("rts.Fx.MuzzleSmokePerShooter"), TEXT("2"));
		if (!TestNotNull(TEXT("rts.Fx.MuzzleSmokePoolSize exists"), Pool.CVar)
			|| !TestNotNull(TEXT("rts.Fx.MuzzleSmokePerShooter exists"), PerShooter.CVar))
		{
			return false;
		}
		const int32 MaxTotal = URTSShotSubsystem::GetMuzzleSmokePoolSize();
		const int32 MaxEach = URTSShotSubsystem::GetMuzzleSmokePerShooter();
		const FRTSTestWorld TestWorld;
		URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
		TArray<ARTSUnit*> Tanks;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Tanks.Add(TestWorld.SpawnUnit(M20, 0, FVector(1500.f * Index, 0.f, 0.f)));
		}
		ARTSUnit* Rapid = Tanks.Last();
		const float Reload = M20->Weapon->ReloadSeconds;
		TArray<float> NextShot;
		NextShot.Init(0.f, Tanks.Num() - 1);

		const int32 PlayedBefore = Shots->GetMuzzleSmokesPlayed();
		int32 MostTotal = 0;
		int32 MostEach = 0;
		int32 MostComponents = 0;
		int32 Shot = 0;
		for (float Time = 0.f; Time < 60.f; Time += Step)
		{
			for (int32 Index = 0; Index < NextShot.Num(); ++Index)
			{
				if (Time >= NextShot[Index])
				{
					Fire(*Shots, *Tanks[Index]);
					NextShot[Index] += Reload;
					++Shot;
				}
			}
			Fire(*Shots, *Rapid);
			++Shot;
			Shots->AdvanceEffects(Step);
			MostTotal = FMath::Max(MostTotal, Shots->GetLiveMuzzleSmokes());
			MostComponents = FMath::Max(MostComponents, Shots->GetMuzzleSmokeComponents());
			for (const ARTSUnit* Tank : Tanks)
			{
				MostEach = FMath::Max(MostEach, Shots->GetLiveMuzzleSmokes(*Tank));
			}
		}
		const FString Case = FString::Printf(TEXT("Pool of %d"), MaxTotal);
		TestEqual(*FString::Printf(TEXT("%s: every shot left a puff (%d)"), *Case, Shot), Shots->GetMuzzleSmokesPlayed() - PlayedBefore, Shot);
		TestTrue(FString::Printf(TEXT("%s: at most %d puffs drift at once (%d)"), *Case, MaxTotal, MostTotal), MostTotal <= MaxTotal);
		TestTrue(FString::Printf(TEXT("%s: at most %d puffs per shooter (%d)"), *Case, MaxEach, MostEach), MostEach <= MaxEach);
		TestTrue(FString::Printf(TEXT("%s: the pool made at most %d components (%d)"), *Case, MaxTotal, MostComponents),
			MostComponents <= MaxTotal);
		TestTrue(FString::Printf(TEXT("%s: the caps were reached (%d puffs, %d per shooter)"), *Case, MostTotal, MostEach),
			MostEach == MaxEach && (Tanks.Num() * MaxEach <= MaxTotal || MostTotal == MaxTotal));
	}
	return true;
}

/**
 * Fewer particles further away and at lower effects quality, none past rts.Fx.MuzzleSmokeDistance, and none for a weapon
 * without smoke (#221).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleSmokeCullTest, "WorldDivide.Combat.MuzzleSmoke.Cull", RTSTests::TestFlags)

bool FRTSMuzzleSmokeCullTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleSmokeTests;

	URTSUnitData* M20 = LoadM20(*this);
	const FScopedCVar Distance(TEXT("rts.Fx.MuzzleSmokeDistance"), TEXT("30000"));
	if (!M20 || !TestNotNull(TEXT("rts.Fx.MuzzleSmokeDistance exists"), Distance.CVar))
	{
		return false;
	}
	TestEqual(TEXT("Ten particles up close at epic"), URTSShotSubsystem::GetMuzzleSmokeCount(0.f, 3), 10);
	TestEqual(TEXT("Cinematic is epic"), URTSShotSubsystem::GetMuzzleSmokeCount(0.f, 4), 10);
	TestEqual(TEXT("Half at the cull distance"), URTSShotSubsystem::GetMuzzleSmokeCount(30000.f, 3), 5);
	for (int32 Quality = 1; Quality <= 3; ++Quality)
	{
		TestTrue(FString::Printf(TEXT("Quality %d has more than quality %d"), Quality, Quality - 1),
			URTSShotSubsystem::GetMuzzleSmokeCount(0.f, Quality) > URTSShotSubsystem::GetMuzzleSmokeCount(0.f, Quality - 1));
	}
	TestTrue(TEXT("Fewer further away"), URTSShotSubsystem::GetMuzzleSmokeCount(20000.f, 2) < URTSShotSubsystem::GetMuzzleSmokeCount(2000.f, 2));
	TestEqual(TEXT("At least two at low quality far away"), URTSShotSubsystem::GetMuzzleSmokeCount(30000.f, 0), 2);

	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const FVector Muzzle = Tank->GetMuzzleTransform().GetLocation();
	struct FCase
	{
		const TCHAR* Name;
		float Distance;
		bool bSmoke;
	};
	const FCase Cases[] = {
		{ TEXT("Close up"), 3000.f, true },
		{ TEXT("From the default RTS view"), 18000.f, true },
		{ TEXT("Past the smoke distance"), 35000.f, false },
	};
	for (const FCase& Case : Cases)
	{
		Shots->SetViewLocationOverride(Muzzle + FVector(0.f, 0.f, Case.Distance));
		const int32 Before = Shots->GetMuzzleSmokesPlayed();
		Fire(*Shots, *Tank);
		TestEqual(*FString::Printf(TEXT("%s: a puff"), Case.Name), Shots->GetMuzzleSmokesPlayed() - Before, Case.bSmoke ? 1 : 0);
		for (float Time = 0.f; Time < URTSShotSubsystem::MuzzleSmokeSeconds + 0.5f; Time += Step)
		{
			Shots->AdvanceEffects(Step);
		}
	}
	Shots->SetViewLocationOverride({});

	URTSWeaponData* Rifle = NewObject<URTSWeaponData>();
	const int32 Before = Shots->GetMuzzleSmokesPlayed();
	Shots->PlayShot(*Tank, *Rifle, nullptr, Muzzle + FVector(3000.f, 0.f, 0.f), 0.2f, Tank->GetMuzzleTransform());
	TestEqual(TEXT("A weapon without smoke leaves none"), Shots->GetMuzzleSmokesPlayed() - Before, 0);
	return true;
}

#endif
