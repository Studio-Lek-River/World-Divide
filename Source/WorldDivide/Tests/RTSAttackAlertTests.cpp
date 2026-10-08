#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSPlayerController.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSAttackAlertTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	/** A human player on TeamId. The test world runs standalone, so the controller's client RPCs run on the spot. */
	ARTSPlayerController* SpawnHuman(const FRTSTestWorld& TestWorld, int32 TeamId)
	{
		ARTSPlayerController* Controller = TestWorld.World->SpawnActor<ARTSPlayerController>();
		Controller->SetPlayerState(TestWorld.SpawnPlayer(TeamId, 0));
		return Controller;
	}

	void Hit(ARTSBuilding& Building, AActor* Attacker)
	{
		Building.GetHealthComponent()->ApplyDamage(10.f, Attacker);
	}

	void Hit(ARTSUnit& Unit, AActor* Attacker)
	{
		Unit.GetHealthComponent()->ApplyDamage(10.f, Attacker);
	}
}

using namespace RTSAttackAlertTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAttackAlertTest, "WorldDivide.Combat.AttackAlert", TestFlags)

bool FRTSAttackAlertTest::RunTest(const FString& Parameters)
{
	constexpr int32 TeamA = 0;
	constexpr int32 TeamB = 1;
	constexpr int32 AITeam = 2;

	const FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	ARTSPlayerController* PlayerA = SpawnHuman(TestWorld, TeamA);
	ARTSPlayerController* PlayerB = SpawnHuman(TestWorld, TeamB);
	ARTSAIPlayer* AI = TestWorld.World->SpawnActor<ARTSAIPlayer>();
	AI->SetPlayerState(TestWorld.SpawnPlayer(AITeam, 0));

	URTSBuildingData* BuildingData = NewObject<URTSBuildingData>();
	BuildingData->Footprint = FIntPoint(3, 3);
	BuildingData->MaxHealth = 1000.f;
	URTSUnitData* UnitData = NewObject<URTSUnitData>();
	UnitData->MaxHealth = 100.f;

	ARTSBuilding* BaseA = TestWorld.SpawnBuilding(BuildingData, TeamA, FVector::ZeroVector);
	ARTSBuilding* NeighbourA = TestWorld.SpawnBuilding(BuildingData, TeamA, FVector(600.f, 0.f, 0.f));
	ARTSBuilding* OutpostA = TestWorld.SpawnBuilding(BuildingData, TeamA, FVector(20000.f, 0.f, 0.f));
	ARTSBuilding* BaseB = TestWorld.SpawnBuilding(BuildingData, TeamB, FVector(0.f, 20000.f, 0.f));
	ARTSBuilding* BaseAI = TestWorld.SpawnBuilding(BuildingData, AITeam, FVector(20000.f, 20000.f, 0.f));
	ARTSUnit* TankA = TestWorld.SpawnUnit(UnitData, TeamA, FVector(0.f, 1000.f, 0.f));
	ARTSUnit* TankB = TestWorld.SpawnUnit(UnitData, TeamB, FVector(1000.f, 0.f, 0.f));

	Hit(*BaseA, TankA);
	TestEqual(TEXT("Damage from the own team warns nobody"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 0);

	Hit(*BaseA, TankB);
	TestEqual(TEXT("An enemy hit on a building warns its owner once"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 1);
	TestEqual(TEXT("The attacker's player gets no warning"), PlayerB->GetAttackAlertCount(ERTSAttackAlert::Base), 0);
	FVector Location;
	float Age = -1.f;
	TestTrue(TEXT("The warning is there for the HUD"), PlayerA->GetLastAttackAlert(ERTSAttackAlert::Base, Location, Age));
	TestEqual(TEXT("The warning points at the attacked building"), Location, BaseA->GetActorLocation());
	TestEqual(TEXT("The warning has just arrived"), Age, 0.f);

	Hit(*BaseA, TankB);
	Hit(*NeighbourA, nullptr);
	TestEqual(TEXT("Hits near the warned spot within the cooldown send nothing more"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 1);

	Hit(*BaseB, TankA);
	TestEqual(TEXT("Damage to an enemy's building sends this player nothing"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 1);
	TestEqual(TEXT("The enemy is warned about their own building"), PlayerB->GetAttackAlertCount(ERTSAttackAlert::Base), 1);

	Hit(*OutpostA, nullptr);
	TestEqual(TEXT("A hit far from the warned spot warns again, also when the shooter is gone"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 2);

	Hit(*BaseAI, TankA);
	TestEqual(TEXT("A hit on the AI's building warns no human player"),
		PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base) + PlayerB->GetAttackAlertCount(ERTSAttackAlert::Base), 3);
	TestEqual(TEXT("Building hits send no unit warning"),
		PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units) + PlayerB->GetAttackAlertCount(ERTSAttackAlert::Units), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUnitAttackAlertTest, "WorldDivide.Combat.UnitAttackAlert", TestFlags)

bool FRTSUnitAttackAlertTest::RunTest(const FString& Parameters)
{
	constexpr int32 TeamA = 0;
	constexpr int32 TeamB = 1;
	constexpr int32 AITeam = 2;

	const FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	ARTSPlayerController* PlayerA = SpawnHuman(TestWorld, TeamA);
	ARTSPlayerController* PlayerB = SpawnHuman(TestWorld, TeamB);
	ARTSAIPlayer* AI = TestWorld.World->SpawnActor<ARTSAIPlayer>();
	AI->SetPlayerState(TestWorld.SpawnPlayer(AITeam, 0));

	URTSBuildingData* BuildingData = NewObject<URTSBuildingData>();
	BuildingData->Footprint = FIntPoint(3, 3);
	BuildingData->MaxHealth = 1000.f;
	URTSUnitData* UnitData = NewObject<URTSUnitData>();
	UnitData->MaxHealth = 1000.f;

	// A fight in the field, away from any base.
	ARTSUnit* TankA = TestWorld.SpawnUnit(UnitData, TeamA, FVector(10000.f, 0.f, 0.f));
	ARTSUnit* NeighbourA = TestWorld.SpawnUnit(UnitData, TeamA, FVector(10600.f, 0.f, 0.f));
	ARTSUnit* ScoutA = TestWorld.SpawnUnit(UnitData, TeamA, FVector(-10000.f, 0.f, 0.f));
	ARTSUnit* TankB = TestWorld.SpawnUnit(UnitData, TeamB, FVector(11000.f, 0.f, 0.f));
	ARTSUnit* TankAI = TestWorld.SpawnUnit(UnitData, AITeam, FVector(0.f, 20000.f, 0.f));
	ARTSBuilding* DepotA = TestWorld.SpawnBuilding(BuildingData, TeamA, FVector(10000.f, 1000.f, 0.f));

	Hit(*TankA, NeighbourA);
	TestEqual(TEXT("Damage from the own team warns nobody"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units), 0);

	Hit(*TankA, TankB);
	TestEqual(TEXT("An enemy hit on a unit warns its owner once"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units), 1);
	TestEqual(TEXT("A unit hit is not a base warning"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 0);
	TestEqual(TEXT("The attacker's player gets no warning"), PlayerB->GetAttackAlertCount(ERTSAttackAlert::Units), 0);
	FVector Location;
	float Age = -1.f;
	TestTrue(TEXT("The warning is there for the HUD"), PlayerA->GetLastAttackAlert(ERTSAttackAlert::Units, Location, Age));
	TestEqual(TEXT("The warning points at the attacked unit"), Location, TankA->GetActorLocation());
	TestEqual(TEXT("The warning has just arrived"), Age, 0.f);

	Hit(*TankA, TankB);
	Hit(*NeighbourA, TankB);
	Hit(*NeighbourA, nullptr);
	TestEqual(TEXT("Hits in the same fight within the cooldown send nothing more"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units), 1);

	Hit(*DepotA, TankB);
	TestEqual(TEXT("A building hit in the same area still warns about the base"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Base), 1);

	Hit(*TankB, TankA);
	TestEqual(TEXT("Damage to an enemy's unit sends this player nothing"), PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units), 1);
	TestEqual(TEXT("The enemy is warned about their own unit"), PlayerB->GetAttackAlertCount(ERTSAttackAlert::Units), 1);

	Hit(*ScoutA, nullptr);
	TestEqual(TEXT("A hit far from the warned fight warns again, also when the shooter is gone"),
		PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units), 2);

	Hit(*TankAI, TankA);
	TestEqual(TEXT("A hit on the AI's unit warns no human player"),
		PlayerA->GetAttackAlertCount(ERTSAttackAlert::Units) + PlayerB->GetAttackAlertCount(ERTSAttackAlert::Units), 3);
	return true;
}

#endif
