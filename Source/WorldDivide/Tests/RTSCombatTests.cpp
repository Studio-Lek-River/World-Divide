#include "Algo/AllOf.h"
#include "Algo/Count.h"
#include "Buildings/RTSBuilding.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/RTSAreaDamage.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSound.h"
#include "Combat/RTSShotSoundWave.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSWeaponComponent.h"
#include "Core/RTSGameState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "UObject/UObjectIterator.h"
#include "Units/RTSAircraftComponent.h"
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

	float DamageTaken(const IRTSTargetable& Target)
	{
		return Target.GetHealthComponent()->GetMaxHealth() - Target.GetHealthComponent()->GetHealth();
	}

	/** A jet that takes off with its first move order, for a blast it must stay out of. */
	ARTSUnit* SpawnFlyingJet(const FRTSTestWorld& TestWorld, int32 TeamId, const FVector& Location)
	{
		URTSAircraftData* Data = NewObject<URTSAircraftData>();
		Data->MaxHealth = 1000.f;
		Data->MoveSpeed = 1000.f;
		Data->TurnRate = 90.f;
		Data->CruiseAltitude = 800.f;
		Data->ClimbRate = 400.f;
		Data->MaxBankAngle = 45.f;
		ARTSUnit* Jet = TestWorld.SpawnUnit(Data, TeamId, Location);
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = Location + FVector(200.f, 0.f, 0.f);
		Jet->ExecuteCommand(Move);
		Jet->GetAircraftComponent()->UpdateAircraft(0.1f);
		return Jet;
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

	/** Sets an integer console variable for the scope, restoring it afterwards. */
	struct FScopedIntCVar
	{
		FScopedIntCVar(const TCHAR* Name, int32 Value)
			: CVar(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (CVar)
			{
				Saved = CVar->GetString();
				CVar->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedIntCVar()
		{
			if (CVar)
			{
				CVar->Set(*Saved, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* CVar = nullptr;
		FString Saved;
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSForceFireGroundTest, "WorldDivide.Combat.ForceFire.Ground", TestFlags)

bool FRTSForceFireGroundTest::RunTest(const FString& Parameters)
{
	constexpr float Damage = 100.f;
	constexpr float Radius = 400.f;
	URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
	Weapon->Range = 2000.f;
	Weapon->Damage = Damage;
	Weapon->ReloadSeconds = 1.f;
	Weapon->SplashRadius = Radius;

	const FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	const FVector Point(1000.f, 0.f, 0.f);
	ARTSUnit* Shooter = TestWorld.SpawnUnit(MakeUnitData(1000.f, Weapon), 0, FVector::ZeroVector);
	// Around the point: an enemy on it, an own unit halfway out, an own building whose edge lies inside, an enemy just
	// outside and an enemy jet in flight over the point.
	URTSUnitData* VictimData = MakeUnitData(1000.f, nullptr);
	ARTSUnit* OnPoint = TestWorld.SpawnUnit(VictimData, 1, Point);
	ARTSUnit* Own = TestWorld.SpawnUnit(VictimData, 0, Point + FVector(0.f, 200.f, 0.f));
	URTSBuildingData* BuildingData = NewObject<URTSBuildingData>();
	BuildingData->Footprint = FIntPoint(2, 2);
	BuildingData->MaxHealth = 1000.f;
	ARTSBuilding* OwnBuilding = TestWorld.SpawnBuilding(BuildingData, 0, Point + FVector(0.f, -450.f, 0.f));
	ARTSUnit* Outside = TestWorld.SpawnUnit(VictimData, 1, Point + FVector(Radius + 10.f, 0.f, 0.f));
	ARTSUnit* Jet = SpawnFlyingJet(TestWorld, 1, Point);
	TestTrue(TEXT("The jet is in flight"), Jet->IsAirborne());
	const float BuildingDistance = FVector::Dist2D(OwnBuilding->GetClosestFootprintPoint(Point), Point);
	TestTrue(FString::Printf(TEXT("The building's edge is inside the radius (%.0f cm)"), BuildingDistance), BuildingDistance < Radius);

	URTSWeaponComponent* Gun = Shooter->GetWeaponComponent();
	TestTrue(TEXT("Force fire may target an own unit"), Gun->CanForceTarget(Own));
	TestFalse(TEXT("But not the shooter itself"), Gun->CanForceTarget(Shooter));
	TestFalse(TEXT("Nor does the ordinary check allow the own unit"), Gun->CanTarget(Own));

	Gun->SetOrderedLocation(Point);
	Gun->UpdateWeapon(0.f);
	TestEqual(TEXT("One shot at the point"), Gun->GetShotsFired(), 1);
	TestNull(TEXT("A ground shot has no target"), Gun->GetTarget());
	TestEqual(TEXT("Full damage on the point"), DamageTaken(*OnPoint), Damage);
	TestEqual(TEXT("An own unit takes the falloff"), DamageTaken(*Own),
		RTSCombat::ComputeFalloffDamage(Damage, 200.f, Radius, URTSWeaponData::SplashEdgeFraction), 0.01f);
	TestEqual(TEXT("An own building takes it at its nearest edge"), DamageTaken(*OwnBuilding),
		RTSCombat::ComputeFalloffDamage(Damage, BuildingDistance, Radius, URTSWeaponData::SplashEdgeFraction), 0.01f);
	TestEqual(TEXT("Nothing just outside the radius"), DamageTaken(*Outside), 0.f);
	TestEqual(TEXT("Nothing to an aircraft in flight"), DamageTaken(*Jet), 0.f);
	TestEqual(TEXT("Nothing to the shooter, far from the point"), DamageTaken(*Shooter), 0.f);

	// Without a splash radius a ground shot does nothing.
	URTSWeaponData* NoSplash = DuplicateObject(Weapon, GetTransientPackage());
	NoSplash->SplashRadius = 0.f;
	ARTSUnit* Plain = TestWorld.SpawnUnit(MakeUnitData(1000.f, NoSplash), 0, FVector(0.f, 300.f, 0.f));
	Plain->GetWeaponComponent()->SetOrderedLocation(Point);
	Plain->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("The plain weapon fired"), Plain->GetWeaponComponent()->GetShotsFired(), 1);
	TestEqual(TEXT("A zero radius hurts nothing on the point"), DamageTaken(*OnPoint), Damage);

	// Out of range it holds fire, and clearing the order drops the location.
	Gun->SetOrderedLocation(FVector(5000.f, 0.f, 0.f));
	Gun->UpdateWeapon(Weapon->ReloadSeconds);
	TestEqual(TEXT("No shot out of range"), Gun->GetShotsFired(), 1);
	Gun->ClearOrder();
	TestFalse(TEXT("The order is cleared"), Gun->GetOrderedLocation().IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSForceFireGroundShellTest, "WorldDivide.Combat.ForceFire.GroundShell", TestFlags)

bool FRTSForceFireGroundShellTest::RunTest(const FString& Parameters)
{
	URTSWeaponData* Weapon = MakeShellWeapon();
	Weapon->SplashRadius = 300.f;
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	if (!TestNotNull(TEXT("The world has a shot subsystem"), Shots))
	{
		return false;
	}
	const FVector Point(800.f, 0.f, 0.f);
	ARTSUnit* Shooter = TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector::ZeroVector);
	ARTSUnit* Victim = TestWorld.SpawnUnit(MakeUnitData(100.f, nullptr), 1, Point);
	const float FlightTime = Weapon->GetFlightTime(800.f);

	Shooter->GetWeaponComponent()->SetOrderedLocation(Point);
	Shooter->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("A ground shell is in flight"), Shots->GetShellsInFlight(), 1);
	TestEqual(TEXT("No damage when the shot is fired"), DamageTaken(*Victim), 0.f);

	Shots->Advance(FlightTime * 0.5f);
	TestEqual(TEXT("No damage halfway"), DamageTaken(*Victim), 0.f);

	// The shooter dies before the shell lands; the blast still comes, where it was aimed.
	Shooter->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	TestFalse(TEXT("The shooter is dead"), IsValid(Shooter));
	Victim->SetActorLocation(Point + FVector(0.f, 100.f, 0.f));
	Shots->Advance(FlightTime * 0.5f);
	TestEqual(TEXT("The splash lands with the shell, at the point"), DamageTaken(*Victim),
		RTSCombat::ComputeFalloffDamage(Weapon->Damage, 100.f, Weapon->SplashRadius, URTSWeaponData::SplashEdgeFraction), 0.01f);
	TestEqual(TEXT("The shell is gone"), Shots->GetShellsInFlight(), 0);
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

	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	Shooter->GetWeaponComponent()->UpdateWeapon(0.f);
	TestEqual(TEXT("The shot shows an effect"), CountShotEffects(*TestWorld.World), 1);
	TestEqual(TEXT("The effect plays"), Shots->GetLiveEffects(), 1);
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
	// The test world does not tick, so the pool is stepped by hand.
	while (Shots->GetLiveEffects() > 0 && Age < Lifetime + 1.f)
	{
		Shots->AdvanceEffects(Step);
		Age += Step;
	}
	TestEqual(TEXT("The effect is over"), Shots->GetLiveEffects(), 0);
	TestTrue(FString::Printf(TEXT("It lived %.2f s, its lifetime is %.2f s"), Age, Lifetime),
		Age >= Lifetime - KINDA_SMALL_NUMBER && Age <= Lifetime + Step + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("It went back to the pool instead of being destroyed"), IsValid(Effect) && Shots->GetPooledEffects() == 1);
	TestTrue(TEXT("It is hidden in the pool"), Algo::AllOf(Effect->GetGlows(), [](const UStaticMeshComponent* Glow) { return !Glow->IsVisible(); }));
	TestEqual(TEXT("It gave its lights back"), Shots->GetLiveShotLights(), 0);

	Shooter->GetWeaponComponent()->UpdateWeapon(Weapon->ReloadSeconds);
	TestEqual(TEXT("The next shot plays"), Shots->GetLiveEffects(), 1);
	TestEqual(TEXT("It takes the pooled effect"), CountShotEffects(*TestWorld.World), 1);
	TestEqual(TEXT("The pool is empty again"), Shots->GetPooledEffects(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSShotPoolTest, "WorldDivide.Perf.ShotPool", TestFlags)

bool FRTSShotPoolTest::RunTest(const FString& Parameters)
{
	constexpr int32 PoolSize = 8;
	constexpr int32 MaxLights = 3;
	constexpr int32 NumShooters = 6;
	constexpr float Step = 0.05f;
	const FScopedIntCVar Pool(TEXT("rts.Fx.ShotPoolSize"), PoolSize);
	const FScopedIntCVar Lights(TEXT("rts.Fx.MaxShotLights"), MaxLights);
	if (!TestNotNull(TEXT("rts.Fx.ShotPoolSize exists"), Pool.CVar) || !TestNotNull(TEXT("rts.Fx.MaxShotLights exists"), Lights.CVar))
	{
		return false;
	}

	// Quick reloads and slow shells, so many more shots are in the air than the pool holds.
	URTSWeaponData* Weapon = MakeShellWeapon();
	Weapon->ReloadSeconds = 0.2f;
	const FRTSTestWorld TestWorld;
	URTSShotSubsystem* Shots = TestWorld.World->GetSubsystem<URTSShotSubsystem>();
	TArray<ARTSUnit*> Shooters;
	for (int32 Index = 0; Index < NumShooters; ++Index)
	{
		Shooters.Add(TestWorld.SpawnUnit(MakeUnitData(100.f, Weapon), 0, FVector(0.f, 300.f * Index, 0.f)));
	}
	TestWorld.SpawnUnit(MakeUnitData(1.0e9f, nullptr), 1, FVector(600.f, 750.f, 0.f));

	int32 MostEffects = 0;
	int32 MostLights = 0;
	int32 MostLit = 0;
	int32 Glows = 0;
	int32 DynamicGlows = 0;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		for (ARTSUnit* Shooter : Shooters)
		{
			Shooter->GetWeaponComponent()->UpdateWeapon(Step);
		}
		Shots->Advance(Step);
		Shots->AdvanceEffects(Step);

		MostEffects = FMath::Max(MostEffects, CountShotEffects(*TestWorld.World));
		MostLights = FMath::Max(MostLights, Shots->GetLiveShotLights());
		int32 Lit = 0;
		for (TActorIterator<ARTSShotEffect> It(TestWorld.World); It; ++It)
		{
			TInlineComponentArray<UPointLightComponent*> PointLights(*It);
			Lit += Algo::CountIf(PointLights, [](const UPointLightComponent* Light) { return Light->IsVisible(); });
			for (const UStaticMeshComponent* Glow : It->GetGlows())
			{
				++Glows;
				DynamicGlows += Cast<UMaterialInstanceDynamic>(Glow->GetMaterial(0)) ? 1 : 0;
			}
		}
		MostLit = FMath::Max(MostLit, Lit);
	}
	TestEqual(TEXT("The pool fills"), MostEffects, PoolSize);
	TestTrue(FString::Printf(TEXT("Live effect actors (at most %d) stay within the pool of %d"), MostEffects, PoolSize), MostEffects <= PoolSize);
	TestTrue(TEXT("Glows were checked"), Glows > 0);
	TestEqual(TEXT("No glow has a dynamic material instance"), DynamicGlows, 0);
	TestEqual(TEXT("The light budget is used"), MostLights, MaxLights);
	TestTrue(FString::Printf(TEXT("Lights shining (at most %d) stay within the budget of %d"), MostLit, MaxLights), MostLit <= MaxLights);

	// Every shot of a kind plays the same wave.
	const auto CountWaves = []
	{
		int32 Count = 0;
		for (TObjectIterator<USoundWaveProcedural> It; It; ++It)
		{
			++Count;
		}
		return Count;
	};
	const int32 WavesBefore = CountWaves();
	const TArray<TPair<float, bool>> Keys = { { 0.4f, false }, { 1.6f, false }, { 1.6f, true } };
	for (int32 Shot = 0; Shot < 50; ++Shot)
	{
		const TPair<float, bool>& Key = Keys[Shot % Keys.Num()];
		TestTrue(TEXT("A wave for every shot"), RTSShotSound::GetWave(Key.Key, Key.Value) != nullptr);
	}
	TestTrue(TEXT("A sound's wave is reused"), RTSShotSound::GetWave(0.4f, false) == RTSShotSound::GetWave(0.4f, false));
	const int32 NewWaves = CountWaves() - WavesBefore;
	TestTrue(FString::Printf(TEXT("Procedural waves grew by %d for %d keys"), NewWaves, Keys.Num()), NewWaves <= Keys.Num());
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
