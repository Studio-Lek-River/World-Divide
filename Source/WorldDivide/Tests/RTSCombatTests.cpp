#include "Algo/AllOf.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSound.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	URTSUnitData* MakeUnitData(float MaxHealth, URTSWeaponData* Weapon)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = MaxHealth;
		Data->Weapon = Weapon;
		return Data;
	}

	/** A weapon whose shell flies 1000 cm/s, slow enough to watch the damage wait for it. */
	URTSWeaponData* MakeShellWeapon()
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 1000.f;
		Weapon->Damage = 25.f;
		Weapon->ReloadSeconds = 10.f;
		Weapon->ShellSpeed = 1000.f;
		return Weapon;
	}

	int32 CountShotEffects(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSShotEffect> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
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

/**
 * Target acquisition as the game runs it: units from the avoidance grid, rebuilt every step as the subsystem does every
 * frame, and buildings from the game state's lists. The nearest enemy unit wins, units before a nearer building, a lost
 * target is replaced at once and a newcomer is found within one search interval (0.2 s) whenever it arrives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAcquireTest, "WorldDivide.Combat.Acquire", TestFlags)

bool FRTSAcquireTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 1.f / 60.f;
	constexpr float SearchInterval = 0.2f;
	constexpr int32 GunnerCount = 12;

	URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
	Weapon->Range = 1000.f;
	Weapon->Damage = 1.f;
	Weapon->ReloadSeconds = 100.f;
	URTSBuildingData* Depot = NewObject<URTSBuildingData>();
	Depot->Footprint = FIntPoint(2, 2);
	Depot->MaxHealth = 500.f;

	const FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	URTSAvoidanceSubsystem* Avoidance = TestWorld.World->GetSubsystem<URTSAvoidanceSubsystem>();
	if (!TestNotNull(TEXT("The avoidance subsystem exists"), Avoidance))
	{
		return false;
	}

	ARTSUnit* Gunner = TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector::ZeroVector);
	ARTSBuilding* Building = TestWorld.SpawnBuilding(Depot, 1, FVector(300.f, 0.f, 0.f));
	ARTSUnit* Far = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(0.f, 900.f, 0.f));
	ARTSUnit* Near = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(-600.f, 0.f, 0.f));
	ARTSUnit* Friend = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 0, FVector(-200.f, 0.f, 0.f));
	ARTSUnit* OutOfRange = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(3000.f, 0.f, 0.f));
	URTSWeaponComponent* Gun = Gunner->GetWeaponComponent();
	auto StepGun = [&](float DeltaSeconds)
	{
		Avoidance->Separate(DeltaSeconds);
		Gun->UpdateWeapon(DeltaSeconds);
	};

	StepGun(Step);
	TestTrue(TEXT("The nearest enemy unit is picked, before a nearer building, a friend or one out of range"), Gun->GetTarget() == Near);

	Near->Destroy();
	StepGun(Step);
	TestTrue(TEXT("A lost target is replaced in the same frame"), Gun->GetTarget() == Far);

	Far->Destroy();
	StepGun(Step);
	TestTrue(TEXT("With no enemy unit in range, the building from the game state's list"), Gun->GetTarget() == Building);

	// A newcomer pulls fire off the building within one search interval.
	ARTSUnit* Newcomer = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(0.f, -700.f, 0.f));
	float Waited = 0.f;
	while (Gun->GetTarget() != Newcomer && Waited < 2.f * SearchInterval)
	{
		StepGun(Step);
		Waited += Step;
	}
	TestTrue(FString::Printf(TEXT("A unit coming into range pulls fire off the building after %.3f s"), Waited),
		Gun->GetTarget() == Newcomer && Waited <= SearchInterval + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("The unit out of range is never picked"), IsValid(OutOfRange) && IsValid(Friend));

	// Idle gunners, each searching in its own phase; an enemy appears at any frame and every gunner has it within 0.2 s.
	TArray<URTSWeaponComponent*> Gunners;
	for (int32 Index = 0; Index < GunnerCount; ++Index)
	{
		Gunners.Add(TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector(Index * 50.f, 10000.f, 0.f))->GetWeaponComponent());
	}
	float WorstWait = 0.f;
	for (int32 Arrival = 0; Arrival < 12; ++Arrival)
	{
		// Some idle frames first, a different number each round, so the arrival meets every phase.
		for (int32 Frame = 0; Frame < 3 + Arrival; ++Frame)
		{
			Avoidance->Separate(Step);
			for (URTSWeaponComponent* Each : Gunners)
			{
				Each->UpdateWeapon(Step);
			}
		}
		ARTSUnit* Enemy = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(300.f, 10500.f, 0.f));
		float Elapsed = 0.f;
		bool bAllOnIt = false;
		while (!bAllOnIt && Elapsed < 1.f)
		{
			// The grid picks the newcomer up at the end of its frame, as the subsystem ticks after the weapons.
			for (URTSWeaponComponent* Each : Gunners)
			{
				Each->UpdateWeapon(Step);
			}
			Avoidance->Separate(Step);
			Elapsed += Step;
			bAllOnIt = Algo::AllOf(Gunners, [Enemy](const URTSWeaponComponent* Each) { return Each->GetTarget() == Enemy; });
		}
		WorstWait = FMath::Max(WorstWait, Elapsed);
		Enemy->Destroy();
	}
	TestTrue(FString::Printf(TEXT("Every gunner acquires a newcomer within %.1f s (worst %.3f s)"), SearchInterval, WorstWait),
		WorstWait <= SearchInterval + Step + KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSShellDelayTest, "WorldDivide.Combat.ShellDelay", TestFlags)

bool FRTSShellDelayTest::RunTest(const FString& Parameters)
{
	URTSWeaponData* Weapon = MakeShellWeapon();
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	if (!TestNotNull(TEXT("The world has a shot subsystem"), Shots))
	{
		return false;
	}
	ARTSUnit* Shooter = TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector::ZeroVector);
	ARTSUnit* Target = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, FVector(500.f, 0.f, 0.f));
	URTSHealthComponent* Health = Target->GetHealthComponent();
	const FVector Origin = Shooter->GetActorLocation();
	const float FlightTime = Weapon->GetFlightTime(FVector::Dist2D(Origin, Target->GetClosestFootprintPoint(Origin)));
	TestTrue(TEXT("The shell takes a while to arrive"), FlightTime > 0.1f);

	Shooter->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("A shell is in flight"), Shots->GetShellsInFlight(), 1);
	TestEqual(TEXT("No damage when the shot is fired"), Health->GetHealth(), 100.f);

	Shots->Advance(FlightTime * 0.5f);
	TestEqual(TEXT("No damage halfway"), Health->GetHealth(), 100.f);

	// The target moves away mid-flight; the shell homes.
	Target->SetActorLocation(FVector(0.f, 700.f, 0.f));
	Shots->Advance(FlightTime * 0.5f);
	TestEqual(TEXT("The damage lands with the shell"), Health->GetHealth(), 75.f);
	TestEqual(TEXT("The shell is gone"), Shots->GetShellsInFlight(), 0);

	// A second shooter dies before its shell lands.
	ARTSUnit* Doomed = TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector(0.f, 200.f, 0.f));
	Doomed->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("The second shooter fired"), Shots->GetShellsInFlight(), 1);
	Doomed->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	TestFalse(TEXT("The second shooter is dead"), IsValid(Doomed));
	Shots->Advance(10.f);
	TestEqual(TEXT("The shell of a dead shooter still lands"), Health->GetHealth(), 50.f);

	// A target that dies before the shell lands takes nothing, and nothing breaks.
	Shooter->GetWeaponComponent()->UpdateWeapon(Weapon->ReloadSeconds);
	TestEqual(TEXT("The first shooter fired again"), Shots->GetShellsInFlight(), 1);
	Health->ApplyDamage(1000.f, nullptr);
	TestFalse(TEXT("The target is dead"), IsValid(Target));
	Shots->Advance(10.f);
	TestEqual(TEXT("The shell at a dead target is dropped"), Shots->GetShellsInFlight(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSShotEffectTest, "WorldDivide.Combat.ShotEffect", TestFlags)

bool FRTSShotEffectTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;

	URTSWeaponData* Weapon = MakeShellWeapon();
	const FRTSTestWorld TestWorld;
	ARTSUnit* Shooter = TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector::ZeroVector);
	TestWorld.SpawnUnit(MakeUnitData(1000.f, nullptr), 1, FVector(500.f, 0.f, 0.f));

	Shooter->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("The shot shows an effect"), CountShotEffects(*TestWorld.World), 1);
	Shooter->GetWeaponComponent()->UpdateWeapon(Step);
	TestEqual(TEXT("No effect while reloading"), CountShotEffects(*TestWorld.World), 1);

	TActorIterator<ARTSShotEffect> It(TestWorld.World);
	if (!TestTrue(TEXT("The effect exists"), static_cast<bool>(It)))
	{
		return false;
	}
	ARTSShotEffect* Effect = *It;
	const float Lifetime = Effect->GetLifetime();
	float Age = 0.f;
	// The test world does not tick, so the effect is stepped by hand.
	while (IsValid(Effect) && Age < Lifetime + 1.f)
	{
		Effect->Tick(Step);
		Age += Step;
	}
	TestFalse(TEXT("The effect removes itself"), IsValid(Effect));
	TestTrue(FString::Printf(TEXT("It lived %.2f s, its lifetime is %.2f s"), Age, Lifetime),
		Age >= Lifetime - KINDA_SMALL_NUMBER && Age <= Lifetime + Step + KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSShotSoundTest, "WorldDivide.Combat.ShotSound", TestFlags)

bool FRTSShotSoundTest::RunTest(const FString& Parameters)
{
	const TArray<uint8>& Rifle = RTSShotSound::GetPcm(0.4f, false);
	const TArray<uint8>& Cannon = RTSShotSound::GetPcm(1.6f, false);
	const TArray<uint8>& Blast = RTSShotSound::GetPcm(1.6f, true);

	TestTrue(TEXT("A shot has sound"), Rifle.Num() > 0);
	TestEqual(TEXT("Whole 16-bit samples"), Rifle.Num() % 2, 0);
	TestTrue(TEXT("A bigger weapon sounds longer"), Cannon.Num() > Rifle.Num());
	TestTrue(TEXT("A blast outlasts its shot"), Blast.Num() > Cannon.Num());
	TestTrue(TEXT("Sounds are cached"), &RTSShotSound::GetPcm(0.4f, false) == &Rifle);

	const int16* Samples = reinterpret_cast<const int16*>(Cannon.GetData());
	int32 Peak = 0;
	for (int32 Index = 0; Index < Cannon.Num() / 2; ++Index)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Samples[Index])));
	}
	TestTrue(FString::Printf(TEXT("Loud but not clipping (peak %d)"), Peak), Peak > MAX_int16 / 2 && Peak < MAX_int16);
	return true;
}

#endif
