#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * A game world with initialized actors, so units bind their death handling, but no BeginPlay:
	 * nothing ticks and tests step the weapons themselves.
	 */
	struct FRTSTestWorld
	{
		FRTSTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FRTSTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		ARTSUnit* SpawnUnit(URTSUnitData* Data, int32 TeamId, const FVector& Location) const
		{
			const FTransform Transform(Location);
			ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform);
			Unit->InitFromData(Data, TeamId);
			Unit->FinishSpawning(Transform);
			return Unit;
		}

		UWorld* World = nullptr;
	};

	URTSUnitData* MakeUnitData(float MaxHealth, URTSWeaponData* Weapon)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = MaxHealth;
		Data->Weapon = Weapon;
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHealthTest, "WorldDivide.Combat.Health", TestFlags)

bool FRTSHealthTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 0, FVector::ZeroVector);
	URTSHealthComponent* Health = Unit->GetHealthComponent();

	TestEqual(TEXT("Starts at full health"), Health->GetHealth(), 100.f);
	TestEqual(TEXT("Max health comes from the unit data"), Health->GetMaxHealth(), 100.f);

	Health->ApplyDamage(30.f, nullptr);
	TestEqual(TEXT("Damage lowers health"), Health->GetHealth(), 70.f);

	Health->ApplyDamage(-50.f, nullptr);
	TestEqual(TEXT("Negative damage does not heal"), Health->GetHealth(), 70.f);
	TestFalse(TEXT("Still alive"), Health->IsDead());

	Health->ApplyDamage(500.f, nullptr);
	TestEqual(TEXT("Health clamps at zero"), Health->GetHealth(), 0.f);
	TestTrue(TEXT("Dead at zero"), Health->IsDead());
	TestFalse(TEXT("Death destroys the unit"), IsValid(Unit));

	Health->ApplyDamage(10.f, nullptr);
	TestEqual(TEXT("The dead take no further damage"), Health->GetHealth(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDuelTest, "WorldDivide.Combat.Duel", TestFlags)

bool FRTSDuelTest::RunTest(const FString& Parameters)
{
	constexpr float StrongHealth = 300.f;
	constexpr float WeakHealth = 100.f;
	constexpr float Step = 0.05f;

	URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
	Weapon->Range = 1000.f;
	Weapon->Damage = 25.f;
	Weapon->ReloadSeconds = 1.f;

	// Both idle and well within range of each other, so neither needs an order to open fire.
	const FRTSTestWorld TestWorld;
	ARTSUnit* Strong = TestWorld.SpawnUnit(MakeUnitData(StrongHealth, Weapon), 0, FVector::ZeroVector);
	ARTSUnit* Weak = TestWorld.SpawnUnit(MakeUnitData(WeakHealth, Weapon), 1, FVector(500.f, 0.f, 0.f));

	// The first shot is immediate, so the last of the killing shots lands after (shots - 1) reloads.
	const int32 ShotsToKill = FMath::CeilToInt(WeakHealth / Weapon->Damage);
	const float ExpectedDeathTime = (ShotsToKill - 1) * Weapon->ReloadSeconds;

	float DeathTime = -1.f;
	for (int32 Frame = 0; Frame * Step <= ExpectedDeathTime + 2.f; ++Frame)
	{
		for (ARTSUnit* Unit : { Strong, Weak })
		{
			if (IsValid(Unit))
			{
				Unit->GetWeaponComponent()->UpdateWeapon(Step);
			}
		}
		if (Frame == 0)
		{
			TestTrue(TEXT("Strong unit acquired the enemy on its own"), Strong->GetWeaponComponent()->GetTarget() == Weak);
			TestTrue(TEXT("Weak unit fires back without orders"), IsValid(Weak) && Weak->GetWeaponComponent()->GetTarget() == Strong);
		}
		if (!IsValid(Weak))
		{
			DeathTime = Frame * Step;
			break;
		}
	}

	TestTrue(TEXT("Weak unit died"), DeathTime >= 0.f);
	TestTrue(FString::Printf(TEXT("Weak unit died at %.2f s, expected %.2f s"), DeathTime, ExpectedDeathTime),
		FMath::Abs(DeathTime - ExpectedDeathTime) <= Step + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Strong unit survived"), IsValid(Strong));
	if (IsValid(Strong))
	{
		const float Remaining = Strong->GetHealthComponent()->GetHealth();
		TestTrue(FString::Printf(TEXT("Strong unit took return fire (%.0f HP left)"), Remaining),
			Remaining < StrongHealth && Remaining >= StrongHealth - ShotsToKill * Weapon->Damage);
	}
	return true;
}

#endif
