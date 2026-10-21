#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMuzzleFlashTests
{
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 1.f / 60.f;

	URTSUnitData* LoadM20(FAutomationTestBase& Test)
	{
		URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
		if (!Test.TestNotNull(TEXT("DA_TestM20Guardian loads"), M20) || !Test.TestNotNull(TEXT("It has a weapon"), M20->Weapon.Get())
			|| !Test.TestNotNull(TEXT("Its gun has a Niagara muzzle flash"), M20->Weapon->MuzzleFlash.Get()))
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

	/** Every shot effect of World that holds a muzzle light slot. */
	int32 CountMuzzleLights(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSShotEffect> It(&World); It; ++It)
		{
			Count += It->HasMuzzleLight() ? 1 : 0;
		}
		return Count;
	}

	/** Sets a float console variable for the scope, restoring it afterwards. */
	struct FScopedFloatCVar
	{
		FScopedFloatCVar(const TCHAR* Name, float Value)
			: CVar(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (CVar)
			{
				Saved = CVar->GetString();
				CVar->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedFloatCVar()
		{
			if (CVar)
			{
				CVar->Set(*Saved, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* CVar;
		FString Saved;
	};
}

/**
 * The muzzle flash the setup builds (#220, #294, #354): NS_MuzzleFlash with the user parameters ARTSShotEffect sets, its
 * layers (tongue, blast cone, brake jets, fireball, sparks, glare) drawn with M_MuzzleFlash, M_MuzzleFire and M_MuzzleGlare,
 * and the M20's gun pointing at it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleFlashAssetTest, "WorldDivide.Combat.MuzzleFlash.Asset", RTSTests::TestFlags)

bool FRTSMuzzleFlashAssetTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleFlashTests;

	URTSUnitData* M20 = LoadM20(*this);
	const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_MuzzleFlash.NS_MuzzleFlash"));
	if (!M20 || !TestNotNull(TEXT("NS_MuzzleFlash loads"), System))
	{
		return false;
	}
	TestTrue(TEXT("The M20's gun flashes NS_MuzzleFlash"), M20->Weapon->MuzzleFlash == System);
	const FNiagaraParameterStore& User = System->GetExposedParameters();
	TestTrue(TEXT("It takes User.Color"), User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Scale"), User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Scale"))) != INDEX_NONE);
	TestTrue(TEXT("It takes User.Brake"), User.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.Brake"))) != INDEX_NONE);
	TestEqual(TEXT("A tongue, a blast cone, two brake jets, a fireball, sparks and a glare"), System->GetEmitterHandles().Num(), 7);
	TestFalse(TEXT("The M20's gun has no muzzle brake"), M20->Weapon->bMuzzleBrake);
	TestTrue(TEXT("It is ready to run"), System->IsValid());
	const UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_MuzzleFlash.M_MuzzleFlash"));
	if (TestNotNull(TEXT("M_MuzzleFlash loads"), Material))
	{
		TestTrue(TEXT("It draws Niagara sprites"), Material->bUsedWithNiagaraSprites != 0);
		TestTrue(TEXT("It is additive"), Material->GetBlendMode() == BLEND_Additive);
	}
	const UMaterial* Fire = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_MuzzleFire.M_MuzzleFire"));
	if (TestNotNull(TEXT("M_MuzzleFire loads"), Fire))
	{
		TestTrue(TEXT("The fire draws Niagara sprites"), Fire->bUsedWithNiagaraSprites != 0);
		TestTrue(TEXT("The fire is additive"), Fire->GetBlendMode() == BLEND_Additive);
	}
	const UMaterial* Glare = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_MuzzleGlare.M_MuzzleGlare"));
	if (TestNotNull(TEXT("M_MuzzleGlare loads"), Glare))
	{
		TestTrue(TEXT("The glare draws Niagara sprites"), Glare->bUsedWithNiagaraSprites != 0);
		TestTrue(TEXT("The glare is additive"), Glare->GetBlendMode() == BLEND_Additive);
	}
	return true;
}

/**
 * One flash per shot (#220): an M20 firing at a target to its side plays exactly one Niagara flash per shot, at the muzzle
 * the shot event names and along its bore, with a muzzle light; once the shot effects are over no flash plays and no light
 * shines.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleFlashPerShotTest, "WorldDivide.Combat.MuzzleFlash.PerShot", RTSTests::TestFlags)

bool FRTSMuzzleFlashPerShotTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleFlashTests;

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
	const int32 FlashesBefore = Shots->GetMuzzleFlashesPlayed();
	int32 Mismatches = 0;
	float WorstOffset = 0.f;
	float WorstTurn = 0.f;
	int32 MostLights = 0;
	const float Seconds = M20->Weapon->ReloadSeconds * 3.5f;
	for (float Time = 0.f; Time < Seconds; Time += Step)
	{
		const int32 Fired = Tank->GetWeaponComponent()->GetShotsFired();
		Tank->UpdateTurret(Step);
		Tank->GetWeaponComponent()->UpdateWeapon(Step);
		Shots->AdvanceEffects(Step);
		if (Tank->GetWeaponComponent()->GetShotsFired() != Fired && !EventMuzzles.IsEmpty())
		{
			// The newest effect plays this shot.
			const ARTSShotEffect* Newest = nullptr;
			for (TActorIterator<ARTSShotEffect> It(TestWorld.World); It; ++It)
			{
				Newest = It->IsNiagaraFlashLive() && It->GetNiagaraFlash()->GetComponentLocation().Equals(EventMuzzles.Last().GetLocation(), 1.f)
					? *It : Newest;
			}
			if (TestNotNull(TEXT("A flash plays at the shot's muzzle"), Newest))
			{
				const UNiagaraComponent* Flash = Newest->GetNiagaraFlash();
				WorstOffset = FMath::Max(WorstOffset, static_cast<float>(FVector::Dist(Flash->GetComponentLocation(), EventMuzzles.Last().GetLocation())));
				WorstTurn = FMath::Max(WorstTurn, static_cast<float>(FMath::RadiansToDegrees(
					Flash->GetComponentQuat().AngularDistance(EventMuzzles.Last().GetRotation()))));
				TestTrue(TEXT("It plays the gun's system"), Flash->GetAsset() == M20->Weapon->MuzzleFlash);
				TestTrue(TEXT("It lights its muzzle"), Newest->HasMuzzleLight());
			}
		}
		Mismatches += Shots->GetMuzzleFlashesPlayed() - FlashesBefore != Tank->GetWeaponComponent()->GetShotsFired() ? 1 : 0;
		MostLights = FMath::Max(MostLights, CountMuzzleLights(*TestWorld.World));
	}
	const int32 Fired = Tank->GetWeaponComponent()->GetShotsFired();
	TestTrue(FString::Printf(TEXT("The M20 turned and fired again and again (%d shots)"), Fired), Fired >= 3);
	TestEqual(TEXT("One shot event per shot"), EventMuzzles.Num(), Fired);
	TestEqual(TEXT("One flash per shot"), Shots->GetMuzzleFlashesPlayed() - FlashesBefore, Fired);
	TestEqual(TEXT("Never more or fewer flashes than shots on any frame"), Mismatches, 0);
	TestTrue(FString::Printf(TEXT("Each flash at the muzzle (%.3f cm off)"), WorstOffset), WorstOffset <= 1.f);
	TestTrue(FString::Printf(TEXT("Along the bore (%.3f degrees off)"), WorstTurn), WorstTurn <= 0.5f);
	TestTrue(TEXT("The muzzle lights shone"), MostLights > 0);
	const float TurretYaw = EventMuzzles.IsEmpty() ? 0.f : EventMuzzles.Last().Rotator().Yaw;
	TestTrue(FString::Printf(TEXT("The turret turned toward the target (%.1f degrees)"), TurretYaw), FMath::Abs(TurretYaw - 45.f) < 10.f);

	// No more shots; the effects play out.
	Target->Destroy();
	for (float Time = 0.f; Time < 3.f && Shots->GetLiveEffects() > 0; Time += Step)
	{
		Shots->AdvanceEffects(Step);
	}
	TestEqual(TEXT("Every effect is over"), Shots->GetLiveEffects(), 0);
	TestEqual(TEXT("No flash plays after its lifetime"), Shots->GetLiveMuzzleFlashes(), 0);
	TestEqual(TEXT("No muzzle light after its lifetime"), CountMuzzleLights(*TestWorld.World), 0);
	TestEqual(TEXT("The light budget is whole again"), Shots->GetLiveShotLights(), 0);
	return true;
}

/**
 * A weapon without a Niagara muzzle flash, such as infantry's, keeps the placeholder glow and its light, and makes no
 * Niagara component.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleFlashPlaceholderTest, "WorldDivide.Combat.MuzzleFlash.Placeholder", RTSTests::TestFlags)

bool FRTSMuzzleFlashPlaceholderTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleFlashTests;

	URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
	Weapon->ShellSpeed = 1000.f;
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	ARTSUnit* Shooter = TestWorld.SpawnUnit(MakeTarget(), 0, FVector::ZeroVector);
	Shots->PlayShot(*Shooter, *Weapon, nullptr, FVector(800.f, 0.f, 0.f), 0.8f, FTransform(FVector(100.f, 0.f, 100.f)));

	TActorIterator<ARTSShotEffect> It(TestWorld.World);
	if (!TestTrue(TEXT("The shot plays an effect"), static_cast<bool>(It)))
	{
		return false;
	}
	TestTrue(TEXT("The placeholder glow shows"), It->GetGlows()[0]->IsVisible());
	TestTrue(TEXT("Its light shines"), It->HasMuzzleLight());
	TestNull(TEXT("No Niagara component"), It->GetNiagaraFlash());
	TestEqual(TEXT("No Niagara flash counted"), Shots->GetMuzzleFlashesPlayed(), 0);
	return true;
}

/**
 * Culling and the light budget (#220): far from every camera a shot has no muzzle light, farther still no flash either,
 * while the shell still flies; a muzzle light seen from afar is brighter and wider. 240 M20s firing at once light at most
 * rts.Fx.MaxShotLights muzzles.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMuzzleFlashCullTest, "WorldDivide.Combat.MuzzleFlash.Cull", RTSTests::TestFlags)

bool FRTSMuzzleFlashCullTest::RunTest(const FString& Parameters)
{
	using namespace RTSMuzzleFlashTests;

	URTSUnitData* M20 = LoadM20(*this);
	const FScopedFloatCVar LightDistance(TEXT("rts.Fx.MuzzleLightDistance"), 25000.f);
	const FScopedFloatCVar FlashDistance(TEXT("rts.Fx.MuzzleFlashDistance"), 50000.f);
	if (!M20 || !TestNotNull(TEXT("rts.Fx.MuzzleLightDistance exists"), LightDistance.CVar)
		|| !TestNotNull(TEXT("rts.Fx.MuzzleFlashDistance exists"), FlashDistance.CVar))
	{
		return false;
	}
	const URTSWeaponData& Weapon = *M20->Weapon;
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const FTransform Muzzle = Tank->GetMuzzleTransform();

	TestEqual(TEXT("No boost up close"), ARTSShotEffect::GetMuzzleLightBoost(0.f), 1.f);
	TestTrue(TEXT("Brighter further out"), ARTSShotEffect::GetMuzzleLightBoost(15000.f) > ARTSShotEffect::GetMuzzleLightBoost(5000.f));
	TestEqual(TEXT("Three times at the far end"), ARTSShotEffect::GetMuzzleLightBoost(100000.f), 3.f);

	struct FCase
	{
		const TCHAR* Name;
		float Distance;
		bool bFlash;
		bool bLight;
	};
	const FCase Cases[] = {
		{ TEXT("Close up"), 3000.f, true, true },
		{ TEXT("From the default RTS view"), 18000.f, true, true },
		{ TEXT("Past the light distance"), 35000.f, true, false },
		{ TEXT("Past the flash distance"), 60000.f, false, false },
	};
	for (const FCase& Case : Cases)
	{
		Shots->SetViewLocationOverride(Muzzle.GetLocation() + FVector(0.f, 0.f, Case.Distance));
		const int32 FlashesBefore = Shots->GetMuzzleFlashesPlayed();
		Shots->PlayShot(*Tank, Weapon, nullptr, FVector(3000.f, 0.f, 0.f), 0.2f, Muzzle);
		TestEqual(*FString::Printf(TEXT("%s: a flash"), Case.Name), Shots->GetMuzzleFlashesPlayed() - FlashesBefore, Case.bFlash ? 1 : 0);
		TestEqual(*FString::Printf(TEXT("%s: a muzzle light"), Case.Name), CountMuzzleLights(*TestWorld.World), Case.bLight ? 1 : 0);
		TestEqual(*FString::Printf(TEXT("%s: the shell still flies"), Case.Name), Shots->GetLiveEffects(), 1);
		bool bGlow = false;
		for (TActorIterator<ARTSShotEffect> It(TestWorld.World); It; ++It)
		{
			bGlow |= It->GetGlows()[0]->IsVisible();
		}
		TestFalse(*FString::Printf(TEXT("%s: no placeholder glow"), Case.Name), bGlow);
		for (float Time = 0.f; Time < 2.f && Shots->GetLiveEffects() > 0; Time += Step)
		{
			Shots->AdvanceEffects(Step);
		}
	}

	// 240 M20s fire on the same frame, close to the camera.
	Shots->SetViewLocationOverride(Muzzle.GetLocation() + FVector(0.f, 0.f, 3000.f));
	constexpr int32 Firing = 240;
	const int32 FlashesBefore = Shots->GetMuzzleFlashesPlayed();
	for (int32 Index = 0; Index < Firing; ++Index)
	{
		const FTransform At(Muzzle.GetRotation(), Muzzle.GetLocation() + FVector(1200.f * (Index % 16), 1200.f * (Index / 16), 0.f));
		Shots->PlayShot(*Tank, Weapon, nullptr, At.GetLocation() + FVector(3000.f, 0.f, 0.f), 0.2f, At);
	}
	const int32 MaxLights = URTSShotSubsystem::GetMaxShotLights();
	int32 Lit = 0;
	for (TActorIterator<ARTSShotEffect> It(TestWorld.World); It; ++It)
	{
		TInlineComponentArray<UPointLightComponent*> PointLights(*It);
		for (const UPointLightComponent* Light : PointLights)
		{
			Lit += Light->IsVisible() ? 1 : 0;
		}
	}
	TestEqual(TEXT("Every one of them flashes"), Shots->GetMuzzleFlashesPlayed() - FlashesBefore, Firing);
	TestTrue(FString::Printf(TEXT("Muzzle lights (%d) stay within the budget of %d"), CountMuzzleLights(*TestWorld.World), MaxLights),
		CountMuzzleLights(*TestWorld.World) <= MaxLights && Shots->GetLiveShotLights() <= MaxLights);
	TestTrue(FString::Printf(TEXT("Lights shining (%d) stay within the budget of %d"), Lit, MaxLights), Lit <= MaxLights);
	Shots->SetViewLocationOverride({});
	return true;
}

#endif
