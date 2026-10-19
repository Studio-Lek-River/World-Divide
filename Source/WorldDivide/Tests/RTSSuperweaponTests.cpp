#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Combat/RTSAreaDamage.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSHUD.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSSuperweaponTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float ChargeTime = 10.f;
	constexpr float Delay = 2.f;
	constexpr float Radius = 1000.f;
	constexpr float Damage = 400.f;
	constexpr float EdgeFraction = 0.25f;

	URTSBuildingData* MakeBuilding(int32 PowerOutput, int32 PowerDraw)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(2, 2);
		Data->MaxHealth = 1000.f;
		Data->BuildTime = 1.f;
		Data->PowerOutput = PowerOutput;
		Data->PowerDraw = PowerDraw;
		return Data;
	}

	URTSBuildingData* MakeSuperweapon()
	{
		URTSBuildingData* Data = MakeBuilding(0, 3);
		Data->Footprint = FIntPoint(4, 4);
		Data->MaxHealth = 5000.f;
		Data->SuperweaponChargeTime = ChargeTime;
		Data->SuperweaponDelay = Delay;
		Data->SuperweaponRadius = Radius;
		Data->SuperweaponDamage = Damage;
		Data->SuperweaponEdgeDamage = EdgeFraction;
		return Data;
	}

	URTSUnitData* MakeUnit()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 1000.f;
		return Data;
	}

	URTSAircraftData* MakeJet()
	{
		URTSAircraftData* Data = NewObject<URTSAircraftData>();
		Data->MaxHealth = 1000.f;
		Data->MoveSpeed = 1000.f;
		Data->TurnRate = 90.f;
		Data->CruiseAltitude = 800.f;
		Data->ClimbRate = 400.f;
		Data->MaxBankAngle = 45.f;
		return Data;
	}

	URTSFactionData* MakePowerFaction()
	{
		URTSFactionData* Faction = NewObject<URTSFactionData>();
		Faction->bUsesPower = true;
		return Faction;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	void Kill(ARTSBuilding& Building)
	{
		Building.GetHealthComponent()->ApplyDamage(Building.GetHealthComponent()->GetMaxHealth(), nullptr);
	}

	float DamageTaken(const IRTSTargetable& Target)
	{
		return Target.GetHealthComponent()->GetMaxHealth() - Target.GetHealthComponent()->GetHealth();
	}

	FRTSCommand MakeLaunch(ARTSBuilding* Superweapon, const FVector& Location)
	{
		FRTSCommand Command;
		Command.Type = ERTSCommandType::LaunchSuperweapon;
		Command.TargetActor = Superweapon;
		Command.TargetLocation = Location;
		return Command;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSuperweaponFalloffTest, "WorldDivide.Superweapon.Falloff", TestFlags)

	bool FRTSSuperweaponFalloffTest::RunTest(const FString& Parameters)
	{
		using RTSCombat::ComputeFalloffDamage;
		TestEqual(TEXT("Full damage at the target point"), ComputeFalloffDamage(400.f, 0.f, 1000.f, 0.25f), 400.f);
		TestEqual(TEXT("The edge fraction at the radius"), ComputeFalloffDamage(400.f, 1000.f, 1000.f, 0.25f), 100.f);
		TestEqual(TEXT("Halfway between at half the radius"), ComputeFalloffDamage(400.f, 500.f, 1000.f, 0.25f), 250.f);
		TestEqual(TEXT("Linear in between"), ComputeFalloffDamage(400.f, 800.f, 1000.f, 0.25f), 160.f, 0.01f);
		TestEqual(TEXT("Nothing beyond the radius"), ComputeFalloffDamage(400.f, 1000.1f, 1000.f, 0.25f), 0.f);
		TestEqual(TEXT("An edge fraction of zero fades out at the radius"), ComputeFalloffDamage(400.f, 1000.f, 1000.f, 0.f), 0.f);
		TestEqual(TEXT("An edge fraction of one deals full damage throughout"), ComputeFalloffDamage(400.f, 900.f, 1000.f, 1.f), 400.f);
		TestEqual(TEXT("A zero radius hits the point itself"), ComputeFalloffDamage(400.f, 0.f, 0.f, 0.25f), 400.f);
		TestEqual(TEXT("And nothing beside it"), ComputeFalloffDamage(400.f, 1.f, 0.f, 0.25f), 0.f);

		TestEqual(TEXT("A charged superweapon reads Ready"), ARTSHUD::FormatCountdown(0.f), FString(TEXT("Ready")));
		TestEqual(TEXT("The countdown rounds up to whole seconds"), ARTSHUD::FormatCountdown(124.2f), FString(TEXT("2:05")));
		TestEqual(TEXT("Under a minute"), ARTSHUD::FormatCountdown(9.f), FString(TEXT("0:09")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSuperweaponChargeTest, "WorldDivide.Superweapon.Charge", TestFlags)

	bool FRTSSuperweaponChargeTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
		Player->SetFaction(MakePowerFaction());
		URTSBuildingData* PlantData = MakeBuilding(5, 0);
		ARTSBuilding* Plant = SpawnCompleted(TestWorld, PlantData, 0, FVector(0.f, -3000.f, 0.f));

		ARTSBuilding* Other = SpawnCompleted(TestWorld, MakeBuilding(0, 0), 0, FVector(0.f, 3000.f, 0.f));
		TestFalse(TEXT("A building without a charge time is no superweapon"), Other->GetSuperweaponComponent()->IsSuperweapon());

		URTSBuildingData* SuperweaponData = MakeSuperweapon();
		ARTSBuilding* Building = TestWorld.SpawnBuilding(SuperweaponData, 0, FVector::ZeroVector);
		URTSSuperweaponComponent* Superweapon = Building->GetSuperweaponComponent();
		TestTrue(TEXT("Its data makes it a superweapon"), Superweapon->IsSuperweapon());
		Superweapon->UpdateCharge(ChargeTime);
		TestFalse(TEXT("A construction site does not charge"), Superweapon->IsOperational());
		TestFalse(TEXT("Nor can it launch"), Superweapon->CanLaunch());

		Building->UpdateConstruction(SuperweaponData->BuildTime);
		TestTrue(TEXT("Completed, it is operational"), Superweapon->IsOperational());
		TestEqual(TEXT("And starts its full charge"), Superweapon->GetChargeRemaining(), ChargeTime);
		TestFalse(TEXT("Charging, it cannot launch"), Superweapon->CanLaunch());

		Superweapon->UpdateCharge(4.f);
		TestEqual(TEXT("The charge counts down"), Superweapon->GetChargeRemaining(), 6.f);
		TestFalse(TEXT("Not paused at full power"), Superweapon->IsChargePaused());

		// Losing the only plant holds the charge outright.
		Kill(*Plant);
		TestTrue(TEXT("Without the plant the player is low on power"), Player->IsLowPower());
		Superweapon->UpdateCharge(3.f);
		TestEqual(TEXT("The charge holds on low power"), Superweapon->GetChargeRemaining(), 6.f);
		TestTrue(TEXT("Shown as paused"), Superweapon->IsChargePaused());

		ARTSBuilding* NewPlant = TestWorld.SpawnBuilding(PlantData, 0, FVector(0.f, -3000.f, 0.f));
		NewPlant->UpdateConstruction(PlantData->BuildTime * 2.f);
		TestFalse(TEXT("A new plant restores power"), Player->IsLowPower());
		Superweapon->UpdateCharge(3.f);
		TestEqual(TEXT("The charge resumes"), Superweapon->GetChargeRemaining(), 3.f);
		TestFalse(TEXT("No longer paused"), Superweapon->IsChargePaused());

		Superweapon->UpdateCharge(5.f);
		TestEqual(TEXT("The charge ends at zero"), Superweapon->GetChargeRemaining(), 0.f);
		TestTrue(TEXT("Charged, it can launch"), Superweapon->CanLaunch());
		Kill(*NewPlant);
		Superweapon->UpdateCharge(1.f);
		TestTrue(TEXT("A charged superweapon stays ready on low power"), Superweapon->CanLaunch());
		TestFalse(TEXT("And shows no pause"), Superweapon->IsChargePaused());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSuperweaponLaunchTest, "WorldDivide.Superweapon.Launch", TestFlags)

	bool FRTSSuperweaponLaunchTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSGameState* GameState = TestWorld.SpawnGameState();
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 0);
		// Without a plant, a superweapon that draws no power is never held by low power.
		URTSBuildingData* SuperweaponData = MakeSuperweapon();
		SuperweaponData->PowerDraw = 0;
		ARTSBuilding* Building = SpawnCompleted(TestWorld, SuperweaponData, 0, FVector::ZeroVector);
		ARTSBuilding* EnemyBase = SpawnCompleted(TestWorld, MakeBuilding(0, 0), 1, FVector(0.f, 20000.f, 0.f));
		URTSSuperweaponComponent* Superweapon = Building->GetSuperweaponComponent();
		const FVector Target(5000.f, 0.f, 0.f);

		RTSCommands::Issue(*Player, {}, MakeLaunch(Building, Target));
		TestFalse(TEXT("A launch before the charge is done is refused"), Superweapon->IsStrikePending());

		Superweapon->UpdateCharge(ChargeTime);
		TestTrue(TEXT("Charged"), Superweapon->CanLaunch());
		RTSCommands::Issue(*Enemy, {}, MakeLaunch(Building, Target));
		TestFalse(TEXT("Another team cannot launch it"), Superweapon->IsStrikePending());

		// Around the target: an enemy on the point, an own unit halfway out, an enemy building whose edge is 800 cm out, an
		// enemy just outside the radius and an enemy aircraft in flight over the point.
		URTSUnitData* UnitData = MakeUnit();
		ARTSUnit* OnPoint = TestWorld.SpawnUnit(UnitData, 1, Target);
		ARTSUnit* Own = TestWorld.SpawnUnit(UnitData, 0, Target + FVector(0.f, 500.f, 0.f));
		ARTSBuilding* EnemyBuilding = TestWorld.SpawnBuilding(MakeBuilding(0, 0), 1, Target + FVector(900.f, 0.f, 0.f));
		ARTSUnit* Outside = TestWorld.SpawnUnit(UnitData, 1, Target + FVector(0.f, -1100.f, 0.f));
		ARTSUnit* Jet = TestWorld.SpawnUnit(MakeJet(), 1, Target);
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = Target + FVector(200.f, 0.f, 0.f);
		Jet->ExecuteCommand(Move);
		Jet->GetAircraftComponent()->UpdateAircraft(0.1f);
		TestTrue(TEXT("The jet is in flight"), Jet->IsAirborne());

		RTSCommands::Issue(*Player, {}, MakeLaunch(Building, Target));
		TestTrue(TEXT("The owner launches it"), Superweapon->IsStrikePending());
		TestEqual(TEXT("At the target"), Superweapon->GetStrikeLocation(), Target);
		TestEqual(TEXT("The charge starts over"), Superweapon->GetChargeRemaining(), ChargeTime);
		TestFalse(TEXT("It cannot launch again"), Superweapon->CanLaunch());

		Superweapon->UpdateCharge(Delay * 0.5f);
		TestEqual(TEXT("Nothing is hit before the delay is over"), DamageTaken(*OnPoint), 0.f);
		Superweapon->UpdateCharge(Delay * 0.5f);
		TestFalse(TEXT("The strike has landed"), Superweapon->IsStrikePending());
		TestEqual(TEXT("The unit on the point takes full damage"), DamageTaken(*OnPoint), Damage);
		TestEqual(TEXT("The own unit halfway out takes the falloff for its distance"), DamageTaken(*Own),
			RTSCombat::ComputeFalloffDamage(Damage, 500.f, Radius, EdgeFraction));
		TestEqual(TEXT("The building takes the falloff for the distance to its edge"), DamageTaken(*EnemyBuilding),
			RTSCombat::ComputeFalloffDamage(Damage, 800.f, Radius, EdgeFraction), 0.01f);
		TestEqual(TEXT("A unit outside the radius is untouched"), DamageTaken(*Outside), 0.f);
		TestEqual(TEXT("So is an aircraft in flight"), DamageTaken(*Jet), 0.f);
		TestEqual(TEXT("And the superweapon far away"), DamageTaken(*Building), 0.f);
		TestEqual(TEXT("The charge went on during the delay"), Superweapon->GetChargeRemaining(), ChargeTime - Delay);

		// Once the match is over a charged superweapon is refused.
		Superweapon->UpdateCharge(ChargeTime);
		TestTrue(TEXT("Charged again"), Superweapon->CanLaunch());
		Kill(*EnemyBuilding);
		Kill(*EnemyBase);
		TestTrue(TEXT("The match is over"), GameState->IsMatchOver());
		RTSCommands::Issue(*Player, {}, MakeLaunch(Building, Target));
		TestFalse(TEXT("A launch after the match is refused"), Superweapon->IsStrikePending());
		return true;
	}
}

#endif
