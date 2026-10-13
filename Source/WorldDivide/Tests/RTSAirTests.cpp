#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSAirTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 0.05f;

	/** An instant-hit weapon, so a shot's damage lands in the frame it is fired. */
	URTSWeaponData* MakeWeapon(float Range, float Damage, bool bGround, bool bAir)
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = Range;
		Weapon->Damage = Damage;
		Weapon->ReloadSeconds = 1.f;
		Weapon->bCanTargetGround = bGround;
		Weapon->bCanTargetAir = bAir;
		return Weapon;
	}

	URTSAircraftData* MakeJet(URTSWeaponData* Weapon)
	{
		URTSAircraftData* Data = NewObject<URTSAircraftData>();
		Data->MaxHealth = 100.f;
		Data->Weapon = Weapon;
		Data->MoveSpeed = 1000.f;
		Data->TurnRate = 90.f;
		Data->CruiseAltitude = 800.f;
		Data->ClimbRate = 400.f;
		Data->MaxBankAngle = 45.f;
		Data->AmmoPerSortie = 4;
		Data->RearmSeconds = 8.f;
		Data->Cost = 100;
		Data->BuildTime = 2.f;
		return Data;
	}

	URTSUnitData* MakeGroundUnit(URTSWeaponData* Weapon)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 1000.f;
		Data->Weapon = Weapon;
		return Data;
	}

	URTSBuildingData* MakeAirfield(int32 Pads, URTSUnitData* Aircraft = nullptr)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(4, 8);
		Data->MaxHealth = 1000.f;
		Data->BuildTime = 1.f;
		Data->AirfieldPads = Pads;
		if (Aircraft)
		{
			Data->Units.Add(Aircraft);
		}
		return Data;
	}

	URTSBuildingData* MakeTargetBuilding(float MaxHealth, URTSWeaponData* Weapon = nullptr)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(2, 2);
		Data->MaxHealth = MaxHealth;
		Data->BuildTime = 1.f;
		Data->Weapon = Weapon;
		return Data;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	/** A jet of TeamId parked on a pad of Airfield. */
	ARTSUnit* SpawnParked(const FRTSTestWorld& TestWorld, URTSAircraftData* Data, ARTSBuilding& Airfield)
	{
		ARTSUnit* Jet = TestWorld.SpawnUnit(Data, Airfield.GetTeamId(), Airfield.GetActorLocation());
		Jet->GetAircraftComponent()->ParkAt(Airfield);
		return Jet;
	}

	/** An engine cube scaled to Size cm, centred on Center: ground or a mountain. */
	AStaticMeshActor* SpawnBlock(const FRTSTestWorld& TestWorld, const FVector& Center, const FVector& Size)
	{
		AStaticMeshActor* Block = TestWorld.World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		// Static meshes cannot change on a static component in a game world.
		Block->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Block->SetActorScale3D(Size / 100.f);
		return Block;
	}

	void Kill(AActor& Actor)
	{
		URTSHealthComponent* Health = CastChecked<IRTSTargetable>(&Actor)->GetHealthComponent();
		Health->ApplyDamage(Health->GetMaxHealth(), nullptr);
	}

	FRTSCommand MakeCommand(ERTSCommandType Type, AActor* Target = nullptr, const FVector& Location = FVector::ZeroVector)
	{
		FRTSCommand Command;
		Command.Type = Type;
		Command.TargetActor = Target;
		Command.TargetLocation = Location;
		return Command;
	}

	/** One frame of what the unit's tick does in a game: the pilot, then the weapon. */
	void StepUnits(TConstArrayView<ARTSUnit*> Units, float DeltaSeconds = Step)
	{
		for (ARTSUnit* Unit : Units)
		{
			if (IsValid(Unit))
			{
				Unit->GetAircraftComponent()->UpdateAircraft(DeltaSeconds);
				Unit->GetWeaponComponent()->UpdateWeapon(DeltaSeconds);
			}
		}
	}

	/** Steps Units until Done holds or Seconds pass; returns the seconds it took, or -1 when it never held. */
	float StepUntil(TConstArrayView<ARTSUnit*> Units, float Seconds, TFunctionRef<bool()> Done)
	{
		for (float Time = 0.f; Time <= Seconds; Time += Step)
		{
			if (Done())
			{
				return Time;
			}
			StepUnits(Units);
		}
		return Done() ? Seconds : -1.f;
	}

	bool IsParkedOnPad(const ARTSUnit& Jet, const ARTSBuilding& Airfield)
	{
		const URTSAircraftComponent* Aircraft = Jet.GetAircraftComponent();
		const FVector Pad = Airfield.GetPadLocation(Aircraft->GetPadIndex()) + FVector(0.f, 0.f, Jet.GetGroundOffset());
		return Aircraft->GetState() == ERTSAircraftState::Parked && Aircraft->GetAirfield() == &Airfield
			&& FVector::Dist(Jet.GetActorLocation(), Pad) < 1.f;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSGroundWeaponCannotTargetAircraftTest, "WorldDivide.Air.GroundWeaponCannotTargetAircraft", TestFlags)

	bool FRTSGroundWeaponCannotTargetAircraftTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(1), 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, MakeJet(MakeWeapon(600.f, 40.f, true, false)), *Airfield);
		ARTSUnit* Tank = TestWorld.SpawnUnit(MakeGroundUnit(MakeWeapon(1500.f, 10.f, true, false)), 1, FVector(1000.f, 0.f, 0.f));
		URTSWeaponComponent* Cannon = Tank->GetWeaponComponent();

		TestFalse(TEXT("A parked jet is on the ground"), Jet->IsAirborne());
		TestTrue(TEXT("A ground weapon can hit a parked jet"), Cannon->CanTarget(Jet));

		// Sent away, it takes off from its pad.
		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, FVector(1000.f, 0.f, 0.f)));
		StepUnits({ Jet });
		TestTrue(TEXT("The jet is airborne once it takes off"), Jet->IsAirborne());
		TestFalse(TEXT("A ground weapon cannot hit it in flight"), Cannon->CanTarget(Jet));

		Tank->ExecuteCommand(MakeCommand(ERTSCommandType::Attack, Jet));
		TestEqual(TEXT("An attack order on it is refused"), Tank->GetQueuedCommandCount(), 0);

		// The tank auto-acquires: never the jet, though it circles in range; the airfield instead.
		const float JetHealth = Jet->GetHealthComponent()->GetHealth();
		for (float Time = 0.f; Time < 5.f; Time += Step)
		{
			StepUnits({ Jet, Tank });
			TestFalse(TEXT("The tank never aims at the jet"), Cannon->GetTarget() == Jet);
		}
		TestEqual(TEXT("The jet took no damage"), Jet->GetHealthComponent()->GetHealth(), JetHealth);
		TestTrue(TEXT("The tank fires at the airfield instead"), Cannon->GetTarget() == Airfield);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAntiAirTargetsAircraftTest, "WorldDivide.Air.AntiAirTargetsAircraft", TestFlags)

	bool FRTSAntiAirTargetsAircraftTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* Jet = TestWorld.SpawnUnit(MakeJet(MakeWeapon(600.f, 40.f, true, false)), 0, FVector::ZeroVector);
		ARTSUnit* Tank = TestWorld.SpawnUnit(MakeGroundUnit(nullptr), 0, FVector(0.f, 500.f, 0.f));
		ARTSUnit* Flak = TestWorld.SpawnUnit(MakeGroundUnit(MakeWeapon(1500.f, 10.f, false, true)), 1, FVector(1500.f, 0.f, 0.f));
		ARTSBuilding* Tower = SpawnCompleted(TestWorld, MakeTargetBuilding(1000.f, MakeWeapon(1500.f, 10.f, true, true)), 1,
			FVector(-1000.f, 0.f, 0.f));
		URTSWeaponComponent* FlakGun = Flak->GetWeaponComponent();
		URTSWeaponComponent* TowerGun = Tower->GetWeaponComponent();

		TestFalse(TEXT("Anti-air cannot hit a jet on the ground"), FlakGun->CanTarget(Jet));
		TestFalse(TEXT("Nor a ground unit"), FlakGun->CanTarget(Tank));
		TestTrue(TEXT("A tower that hits both hits the tank"), TowerGun->CanTarget(Tank));

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, FVector::ZeroVector));
		StepUnits({ Jet });
		TestTrue(TEXT("The jet is airborne"), Jet->IsAirborne());
		TestTrue(TEXT("Anti-air can hit it in flight"), FlakGun->CanTarget(Jet));
		TestTrue(TEXT("So can the tower"), TowerGun->CanTarget(Jet));

		// The jet circles over the origin, in range of both.
		const float Before = Jet->GetHealthComponent()->GetHealth();
		FlakGun->UpdateWeapon(Step);
		TestTrue(TEXT("The flak picks the jet on its own"), FlakGun->GetTarget() == Jet);
		const float AfterFlak = Jet->GetHealthComponent()->GetHealth();
		TestEqual(TEXT("The flak hits the jet"), AfterFlak, Before - 10.f);
		TowerGun->UpdateWeapon(Step);
		TestTrue(TEXT("The tower picks a unit in range"), TowerGun->GetTarget() == Jet || TowerGun->GetTarget() == Tank);
		Tank->Destroy();
		TowerGun->UpdateWeapon(1.f);
		TestTrue(TEXT("The tower fires at the jet"), TowerGun->GetTarget() == Jet);
		TestTrue(TEXT("And hits it"), Jet->GetHealthComponent()->GetHealth() < AfterFlak);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFullSortieTest, "WorldDivide.Air.FullSortie", TestFlags)

	bool FRTSFullSortieTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(MakeWeapon(600.f, 40.f, true, false));
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(2), 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Airfield);
		ARTSBuilding* Target = SpawnCompleted(TestWorld, MakeTargetBuilding(1000.f), 1, FVector(8000.f, 0.f, 0.f));
		URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();
		URTSWeaponComponent* Weapon = Jet->GetWeaponComponent();
		const FVector Pad = Jet->GetActorLocation();

		TestTrue(TEXT("The jet starts parked on its pad"), IsParkedOnPad(*Jet, *Airfield));
		TestTrue(TEXT("Its pad is bound to it"), Airfield->GetPadAircraft(Aircraft->GetPadIndex()) == Jet);
		TestEqual(TEXT("It starts with a full load"), Weapon->GetAmmo(), JetData->AmmoPerSortie);

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Attack, Target));
		TestEqual(TEXT("The jet takes the attack"), Aircraft->GetMission(), ERTSAircraftMission::Attack);

		// Take off: climbing to cruise altitude above the ground.
		const float CruiseZ = Airfield->GetActorLocation().Z + Jet->GetGroundOffset() + JetData->CruiseAltitude;
		const float ClimbTime = StepUntil({ Jet }, 10.f, [&] { return Jet->GetActorLocation().Z >= CruiseZ - 1.f; });
		TestTrue(FString::Printf(TEXT("The jet climbs to cruise altitude (%.2f s)"), ClimbTime),
			ClimbTime >= 0.f && ClimbTime <= (CruiseZ - Pad.Z) / JetData->ClimbRate + 2.f * Step);
		TestTrue(TEXT("It is in flight"), Jet->IsAirborne());
		TestTrue(TEXT("It flies at the target"), Jet->GetActorLocation().X > Pad.X);

		// Attack until the load is spent.
		const float TargetHealth = Target->GetHealthComponent()->GetHealth();
		const float SpentTime = StepUntil({ Jet }, 30.f, [&] { return Weapon->GetAmmo() == 0; });
		TestTrue(FString::Printf(TEXT("The jet spends its load (%.2f s)"), SpentTime), SpentTime >= 0.f);
		TestEqual(TEXT("Every shot hit the target"), Target->GetHealthComponent()->GetHealth(),
			TargetHealth - JetData->AmmoPerSortie * 40.f);
		StepUnits({ Jet });
		TestEqual(TEXT("Empty, it heads home"), Aircraft->GetMission(), ERTSAircraftMission::Return);
		for (float Time = 0.f; Time < 3.f; Time += Step)
		{
			StepUnits({ Jet });
		}
		TestEqual(TEXT("It holds fire without ammo"), Target->GetHealthComponent()->GetHealth(),
			TargetHealth - JetData->AmmoPerSortie * 40.f);

		// Return and land on its own pad.
		const float LandTime = StepUntil({ Jet }, 30.f, [&] { return Aircraft->GetState() == ERTSAircraftState::Parked; });
		TestTrue(FString::Printf(TEXT("The jet lands (%.2f s)"), LandTime), LandTime >= 0.f);
		TestTrue(TEXT("On its own pad"), IsParkedOnPad(*Jet, *Airfield));
		TestFalse(TEXT("On the ground"), Jet->IsAirborne());
		TestEqual(TEXT("Its mission is over"), Aircraft->GetMission(), ERTSAircraftMission::None);
		TestTrue(TEXT("Wings level once parked"), FMath::IsNearlyZero(Jet->GetActorRotation().Roll));

		// A new attack waits for a full load.
		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Attack, Target));
		const float RearmTime = StepUntil({ Jet }, 20.f, [&] { return Aircraft->GetState() != ERTSAircraftState::Parked; });
		TestTrue(FString::Printf(TEXT("It takes off again once rearmed (%.2f s)"), RearmTime),
			FMath::Abs(RearmTime - JetData->RearmSeconds) <= 2.f * Step);
		TestEqual(TEXT("With a full load"), Weapon->GetAmmo(), JetData->AmmoPerSortie);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRearmTest, "WorldDivide.Air.Rearm", TestFlags)

	bool FRTSRearmTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(MakeWeapon(600.f, 40.f, true, false));
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(1), 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Airfield);
		URTSWeaponComponent* Weapon = Jet->GetWeaponComponent();

		Weapon->SetAmmo(0);
		const float PerShot = JetData->RearmSeconds / JetData->AmmoPerSortie;
		const float FirstShot = StepUntil({ Jet }, 20.f, [&] { return Weapon->GetAmmo() >= 1; });
		TestTrue(FString::Printf(TEXT("One shot comes back per %.1f s (%.2f s)"), PerShot, FirstShot),
			FMath::Abs(FirstShot - PerShot) <= Step + KINDA_SMALL_NUMBER);
		const float Full = StepUntil({ Jet }, 20.f, [&] { return Weapon->GetAmmo() == JetData->AmmoPerSortie; });
		TestTrue(FString::Printf(TEXT("Full after the rearm time (%.2f s)"), FirstShot + Full),
			FMath::Abs(FirstShot + Full - JetData->RearmSeconds) <= 2.f * Step);
		for (float Time = 0.f; Time < JetData->RearmSeconds; Time += Step)
		{
			StepUnits({ Jet });
		}
		TestEqual(TEXT("Never beyond a full load"), Weapon->GetAmmo(), JetData->AmmoPerSortie);

		// Off the pad nothing rearms.
		ARTSUnit* Stray = TestWorld.SpawnUnit(JetData, 0, FVector(0.f, 3000.f, 0.f));
		Stray->GetWeaponComponent()->SetAmmo(0);
		for (float Time = 0.f; Time < JetData->RearmSeconds + 1.f; Time += Step)
		{
			StepUnits({ Stray });
		}
		TestEqual(TEXT("A jet without a pad does not rearm"), Stray->GetWeaponComponent()->GetAmmo(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBanksIntoTurnsTest, "WorldDivide.Air.BanksIntoTurns", TestFlags)

	bool FRTSBanksIntoTurnsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(nullptr);
		ARTSUnit* Jet = TestWorld.SpawnUnit(JetData, 0, FVector::ZeroVector);

		// Facing +X, sent far off to its right: it turns right, dipping the right wing.
		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, FVector(0.f, 30000.f, 0.f)));
		for (float Time = 0.f; Time < 0.5f; Time += Step)
		{
			StepUnits({ Jet });
		}
		const FRotator Turning = Jet->GetActorRotation();
		TestTrue(FString::Printf(TEXT("It banks right into a right turn (roll %.1f)"), Turning.Roll),
			FMath::IsNearlyEqual(Turning.Roll, JetData->MaxBankAngle, 1.f));
		TestTrue(TEXT("Its nose is up while it climbs"), Turning.Pitch > 0.f);

		// Lined up with its goal, it flies straight and levels its wings.
		const float Straight = StepUntil({ Jet }, 10.f, [&] { return FMath::IsNearlyEqual(Jet->GetActorRotation().Yaw, 90.f, 0.5f); });
		TestTrue(TEXT("It turns onto its goal"), Straight >= 0.f);
		for (float Time = 0.f; Time < 3.f; Time += Step)
		{
			StepUnits({ Jet });
		}
		TestTrue(FString::Printf(TEXT("Wings level in straight flight (roll %.1f)"), Jet->GetActorRotation().Roll),
			FMath::IsNearlyZero(Jet->GetActorRotation().Roll, 0.5f));
		TestTrue(TEXT("Nose level at cruise altitude"), FMath::IsNearlyZero(Jet->GetActorRotation().Pitch, 0.5f));

		// Heading +Y and sent toward +X, to its left, it dips the left wing.
		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, FVector(30000.f, Jet->GetActorLocation().Y, 0.f)));
		for (float Time = 0.f; Time < 0.5f; Time += Step)
		{
			StepUnits({ Jet });
		}
		TestTrue(FString::Printf(TEXT("It banks left into a left turn (roll %.1f)"), Jet->GetActorRotation().Roll),
			FMath::IsNearlyEqual(Jet->GetActorRotation().Roll, -JetData->MaxBankAngle, 1.f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMoveCirclesTest, "WorldDivide.Air.MoveCircles", TestFlags)

	bool FRTSMoveCirclesTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(nullptr);
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(1), 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Airfield);
		URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();
		const FVector Point(6000.f, 4000.f, 0.f);
		const float Radius = Aircraft->GetTurnRadius();

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, Point));
		const float Arrival = StepUntil({ Jet }, 30.f, [&] { return FVector::Dist2D(Jet->GetActorLocation(), Point) <= 2.f * Radius; });
		TestTrue(TEXT("The jet reaches the point"), Arrival >= 0.f);
		float Farthest = 0.f;
		for (float Time = 0.f; Time < 20.f; Time += Step)
		{
			StepUnits({ Jet });
			Farthest = FMath::Max(Farthest, FVector::Dist2D(Jet->GetActorLocation(), Point));
		}
		TestTrue(FString::Printf(TEXT("It circles the point (at most %.0f cm off, turn radius %.0f)"), Farthest, Radius),
			Farthest <= 2.f * Radius + 1.f);
		TestTrue(TEXT("Still in the air"), Jet->IsAirborne());

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Stop));
		TestEqual(TEXT("Stop sends it home"), Aircraft->GetMission(), ERTSAircraftMission::Return);
		const float Landed = StepUntil({ Jet }, 30.f, [&] { return IsParkedOnPad(*Jet, *Airfield); });
		TestTrue(TEXT("It lands on its pad"), Landed >= 0.f);

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Harvest));
		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Stop));
		StepUnits({ Jet });
		TestTrue(TEXT("Harvest and Stop leave a parked jet on its pad"), IsParkedOnPad(*Jet, *Airfield));
		TestEqual(TEXT("Aircraft never queue orders"), Jet->GetQueuedCommandCount(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPadDestroyedRebindsTest, "WorldDivide.Air.PadDestroyedRebinds", TestFlags)

	bool FRTSPadDestroyedRebindsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(nullptr);
		URTSBuildingData* AirfieldData = MakeAirfield(1);
		ARTSBuilding* Home = SpawnCompleted(TestWorld, AirfieldData, 0, FVector::ZeroVector);
		ARTSBuilding* Taken = SpawnCompleted(TestWorld, AirfieldData, 0, FVector(3000.f, 0.f, 0.f));
		ARTSBuilding* Free = SpawnCompleted(TestWorld, AirfieldData, 0, FVector(6000.f, 4000.f, 0.f));
		SpawnCompleted(TestWorld, AirfieldData, 1, FVector(-3000.f, 0.f, 0.f));
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Home);
		ARTSUnit* Other = SpawnParked(TestWorld, JetData, *Taken);
		URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();

		Kill(*Home);
		TestTrue(TEXT("The airfield is gone"), !IsValid(Home));
		TestTrue(TEXT("The jet takes off"), Jet->IsAirborne());
		TestEqual(TEXT("And heads for a new pad"), Aircraft->GetMission(), ERTSAircraftMission::Return);

		const float Landed = StepUntil({ Jet, Other }, 30.f, [&] { return Aircraft->GetState() == ERTSAircraftState::Parked; });
		TestTrue(TEXT("It lands again"), Landed >= 0.f);
		TestTrue(TEXT("On the free pad of its own team, not the taken nearer one"), IsParkedOnPad(*Jet, *Free));
		TestTrue(TEXT("The other jet keeps its pad"), IsParkedOnPad(*Other, *Taken));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCirclesWithoutPadTest, "WorldDivide.Air.CirclesWithoutPad", TestFlags)

	bool FRTSCirclesWithoutPadTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSAircraftData* JetData = MakeJet(nullptr);
		URTSBuildingData* AirfieldData = MakeAirfield(1);
		ARTSBuilding* Home = SpawnCompleted(TestWorld, AirfieldData, 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Home);
		URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();
		const FVector LostAt = Jet->GetActorLocation();

		Kill(*Home);
		float Farthest = 0.f;
		for (float Time = 0.f; Time < 15.f; Time += Step)
		{
			StepUnits({ Jet });
			Farthest = FMath::Max(Farthest, FVector::Dist2D(Jet->GetActorLocation(), LostAt));
		}
		TestTrue(TEXT("Without a pad the jet stays in the air"), Jet->IsAirborne());
		TestNull(TEXT("Bound to no airfield"), Aircraft->GetAirfield());
		TestTrue(FString::Printf(TEXT("Circling where it lost its pad (at most %.0f cm off)"), Farthest),
			Farthest <= 2.f * Aircraft->GetTurnRadius() + 1.f);

		// A construction site has no pads yet; the completed airfield does.
		ARTSBuilding* Site = TestWorld.SpawnBuilding(AirfieldData, 0, FVector(-4000.f, 0.f, 0.f));
		for (float Time = 0.f; Time < 3.f; Time += Step)
		{
			StepUnits({ Jet });
		}
		TestNull(TEXT("A construction site offers no pad"), Aircraft->GetAirfield());
		Site->UpdateConstruction(AirfieldData->BuildTime);
		const float Landed = StepUntil({ Jet }, 30.f, [&] { return Aircraft->GetState() == ERTSAircraftState::Parked; });
		TestTrue(TEXT("Once it is complete the jet lands there"), Landed >= 0.f && IsParkedOnPad(*Jet, *Site));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAirfieldLimitsAircraftToPadsTest, "WorldDivide.Air.AirfieldLimitsAircraftToPads", TestFlags)

	bool FRTSAirfieldLimitsAircraftToPadsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 10000);
		URTSAircraftData* JetData = MakeJet(nullptr);
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(2, JetData), 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Airfield->GetProductionComponent();

		TestEqual(TEXT("Both pads start free"), Airfield->GetFreePadCount(), 2);
		TestTrue(TEXT("Queue a jet"), Production->Enqueue(*Player, JetData));
		TestTrue(TEXT("Queue a second"), Production->Enqueue(*Player, JetData));
		TestEqual(TEXT("Queued jets keep their pads"), Airfield->GetFreePadCount(), 0);
		TestFalse(TEXT("A third has no pad and is refused"), Production->Enqueue(*Player, JetData));
		TestEqual(TEXT("The refusal charges nothing"), Player->GetCredits(), 10000 - 2 * JetData->Cost);

		TArray<ARTSUnit*> Jets;
		for (float Time = 0.f; Time < 2.f * JetData->BuildTime + 1.f; Time += 0.1f)
		{
			Production->UpdateProduction(0.1f);
		}
		for (TActorIterator<ARTSUnit> It(TestWorld.World); It; ++It)
		{
			Jets.Add(*It);
		}
		if (!TestEqual(TEXT("The airfield produced both jets"), Jets.Num(), 2))
		{
			return true;
		}
		for (ARTSUnit* Jet : Jets)
		{
			TestTrue(FString::Printf(TEXT("%s parks on a pad, not at the exit"), *Jet->GetName()), IsParkedOnPad(*Jet, *Airfield));
			TestEqual(FString::Printf(TEXT("%s has no rally order"), *Jet->GetName()), Jet->GetAircraftComponent()->GetMission(),
				ERTSAircraftMission::None);
		}
		TestNotEqual(TEXT("Each on its own pad"), Jets[0]->GetAircraftComponent()->GetPadIndex(), Jets[1]->GetAircraftComponent()->GetPadIndex());
		TestFalse(TEXT("With both pads taken another is refused"), Production->Enqueue(*Player, JetData));

		Kill(*Jets[0]);
		TestEqual(TEXT("A lost jet frees its pad"), Airfield->GetFreePadCount(), 1);
		TestTrue(TEXT("So a replacement can be queued"), Production->Enqueue(*Player, JetData));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFollowsTerrainTest, "WorldDivide.Air.FollowsTerrain", TestFlags)

	bool FRTSFollowsTerrainTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		// Flat ground with its top at z = 0, and a mountain 15 m high from x = 7000 to 9000 across the jet's way east.
		SpawnBlock(TestWorld, FVector(10000.f, 0.f, -50.f), FVector(40000.f, 10000.f, 100.f));
		constexpr float MountainTop = 1500.f;
		SpawnBlock(TestWorld, FVector(8000.f, 0.f, MountainTop / 2.f), FVector(2000.f, 4000.f, MountainTop));
		URTSAircraftData* JetData = MakeJet(MakeWeapon(600.f, 40.f, true, false));
		ARTSBuilding* Airfield = SpawnCompleted(TestWorld, MakeAirfield(1), 0, FVector::ZeroVector);
		ARTSUnit* Jet = SpawnParked(TestWorld, JetData, *Airfield);

		Jet->ExecuteCommand(MakeCommand(ERTSCommandType::Move, nullptr, FVector(20000.f, 0.f, 0.f)));
		const float FlatCruiseZ = Jet->GetGroundOffset() + JetData->CruiseAltitude;
		const float MountainCruiseZ = MountainTop + FlatCruiseZ;

		TestTrue(TEXT("The jet reaches the mountain"), StepUntil({ Jet }, 20.f, [&] { return Jet->GetActorLocation().X >= 7000.f; }) >= 0.f);
		FVector LowestOver(0.f, 0.f, TNumericLimits<float>::Max());
		StepUntil({ Jet }, 10.f, [&]
		{
			const FVector Location = Jet->GetActorLocation();
			if (Location.X > 9000.f)
			{
				return true;
			}
			if (Location.Z < LowestOver.Z)
			{
				LowestOver = Location;
			}
			return false;
		});
		TestTrue(FString::Printf(TEXT("It climbed before the mountain and holds cruise altitude over it (lowest %s, needed %.0f)"),
			*LowestOver.ToCompactString(), MountainCruiseZ), LowestOver.Z >= MountainCruiseZ - 1.f);

		TestTrue(TEXT("It flies on east"), StepUntil({ Jet }, 20.f, [&] { return Jet->GetActorLocation().X >= 15000.f; }) >= 0.f);
		TestNearlyEqual(TEXT("Past the mountain it descends to cruise altitude over the ground again"), static_cast<float>(Jet->GetActorLocation().Z),
			FlatCruiseZ, 1.f);
		return true;
	}
}

#endif
