#include "Algo/AllOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Buildings/RTSBuilding.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSKitPieceData.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSPrimitiveData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const TCHAR* HeadquartersDataPath = TEXT("/Game/Data/Buildings/DA_TestHeadquarters.DA_TestHeadquarters");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** A part of the HQ as usa_hq.json placed it before its kit pieces were mounts (#390); the beacons cast a shadow since
	 * 12 is a class M piece (#393). */
	struct FExpectedPart
	{
		const TCHAR* Name;
		const TCHAR* Mesh;
		FVector Location;
		FRotator Rotation;
		FVector Scale;
		bool bCastShadow;
		float WPODisableDistance;
	};

	const FExpectedPart HeadquartersKitParts[] = {
		{ TEXT("Beacon_MastTipCore"), TEXT("SM_Kit_MastBeaconSingle"), FVector(-898.7123, 0.0, 2800.0), FRotator::ZeroRotator,
			FVector(1.75), true, 0.f },
		{ TEXT("Beacon_MastTipEast"), TEXT("SM_Kit_MastBeaconSingle"), FVector(-1090.0, -1995.0001, 2400.0), FRotator::ZeroRotator,
			FVector(1.75), true, 0.f },
		{ TEXT("NationalFlag"), TEXT("SM_Kit_FlagCloth"), FVector(1150.0, 10.5, 690.0), FRotator(0.0, 90.0, 0.0),
			FVector(4.18, 1.0, 2.2), true, 12500.f },
	};

	int32 CountTickingComponents(const AActor& Actor)
	{
		TInlineComponentArray<UActorComponent*> Components(&Actor);
		return Components.FilterByPredicate([](const UActorComponent* C) { return C->PrimaryComponentTick.bCanEverTick; }).Num();
	}

	int32 CountActors(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

/**
 * RTSKit::Expand on made-up data (BUILDING_KIT.md, Mounts, #390): a piece's origin stands at its socket times the mount's
 * transform, its mount point on that frame, and its light (in the piece's frame) composes with all of them, scale
 * included; Fixture is the mount, the group the mount's or the piece's, and the lens data follows. A socket or mount point
 * that is not there is an error and no part.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSKitMountExpandTest, "WorldDivide.Buildings.KitMount.Expand", TestFlags)

bool FRTSKitMountExpandTest::RunTest(const FString& Parameters)
{
	URTSKitPieceData* Lamp = NewObject<URTSKitPieceData>();
	Lamp->Mesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	Lamp->Class = ERTSKitClass::M;
	Lamp->bHasLight = true;
	Lamp->Light.Type = ERTSBuildingLightType::Spot;
	Lamp->Light.Location = FVector(5.f, 0.f, -3.f);
	Lamp->Light.Rotation = FRotator(-90.f, 0.f, 0.f);
	Lamp->Light.Candelas = 150.f;
	Lamp->Light.Kelvin = 3000.f;
	Lamp->Light.Group = TEXT("Fixtures");
	Lamp->Light.Behaviour = ERTSKitLightBehaviour::Flash;
	Lamp->MountPoints.Add(TEXT("Wall"), FTransform(FVector(0.f, 0.f, 10.f)));
	if (!TestNotNull(TEXT("The engine's cube loads"), Lamp->Mesh.Get()))
	{
		return false;
	}

	URTSBuildingData* Data = NewObject<URTSBuildingData>();
	FRTSBuildingSocket& DoorSocket = Data->Sockets.AddDefaulted_GetRef();
	DoorSocket.Name = TEXT("Door");
	DoorSocket.Location = FVector(100.f, 200.f, 300.f);
	auto AddMount = [Data, Lamp](const TCHAR* Name, const TCHAR* Socket, const FTransform& Transform) -> FRTSKitMount&
	{
		FRTSKitMount& Mount = Data->Mounts.AddDefaulted_GetRef();
		Mount.Name = Name;
		Mount.Piece = Lamp;
		Mount.Socket = Socket;
		Mount.Transform = Transform;
		return Mount;
	};
	AddMount(TEXT("AtDoor"), TEXT("Door"), FTransform(FRotator(0.f, 90.f, 0.f)));
	FRTSKitMount& Free = AddMount(TEXT("Free"), TEXT(""), FTransform(FQuat::Identity, FVector(10.f, 0.f, 0.f), FVector(2.f)));
	Free.Group = TEXT("Accents");
	Free.IndicatorColor = FLinearColor::Red;
	AddMount(TEXT("Lost"), TEXT("Nowhere"), FTransform::Identity);
	AddMount(TEXT("BadPoint"), TEXT("Door"), FTransform::Identity).MountPoint = TEXT("Ceiling");

	const FRTSKitExpansion Kit = RTSKit::Expand(*Data);
	TestEqual(TEXT("An unknown socket and an unknown mount point are errors"), Kit.Errors.Num(), 2);
	TestTrue(TEXT("The unknown socket's error names it"), Kit.Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("Nowhere")); }));
	if (!TestEqual(TEXT("Only the mounts that resolve make parts"), Kit.Parts.Num(), 2)
		|| !TestEqual(TEXT("And lights"), Kit.Lights.Num(), 2)
		|| !TestEqual(TEXT("And lens data"), Kit.InstanceData.Num(), 2 * RTSKitLensData::Floats))
	{
		return false;
	}

	// At the door: the socket, turned a quarter, the mount point 10 cm up the piece brought down onto it.
	const FRTSBuildingPart& Door = Kit.Parts[0];
	TestEqual(TEXT("The part is named after its mount"), Door.Name, FName(TEXT("AtDoor")));
	TestEqual(TEXT("It draws the piece"), Door.Mesh.Get(), Lamp->Mesh.Get());
	TestTrue(*FString::Printf(TEXT("The socket resolves: %s"), *Door.Transform.GetLocation().ToString()),
		Door.Transform.GetLocation().Equals(FVector(100.f, 200.f, 290.f), 0.01));
	TestTrue(TEXT("The mount turns it"), Door.Transform.GetRotation().Rotator().Equals(FRotator(0.f, 90.f, 0.f), 0.01));
	TestTrue(TEXT("A medium piece casts a shadow"), Door.bCastShadow);
	const FRTSBuildingLight& DoorLight = Kit.Lights[0];
	TestTrue(*FString::Printf(TEXT("Its light composes with the frame: %s"), *DoorLight.Location.ToString()),
		DoorLight.Location.Equals(FVector(100.f, 205.f, 287.f), 0.01));
	TestTrue(TEXT("It shines down, as the piece's"), DoorLight.Rotation.Vector().Equals(FVector(0.f, 0.f, -1.f), 0.001));
	TestEqual(TEXT("Its fixture is the mount"), DoorLight.Fixture, FName(TEXT("AtDoor")));
	TestEqual(TEXT("It keeps the piece's group"), DoorLight.Group, FName(TEXT("Fixtures")));
	TestEqual(TEXT("And behaviour"), DoorLight.Behaviour, ERTSKitLightBehaviour::Flash);
	TestEqual(TEXT("And candela"), DoorLight.Candelas, 150.f);
	TestTrue(TEXT("Its colour is the piece's temperature"), DoorLight.Color.Equals(FLinearColor::MakeFromColorTemperature(3000.f)));
	TestEqual(TEXT("The lens glows"), Kit.InstanceData[RTSKitLensData::Level], 1.f);
	TestEqual(TEXT("The Fixtures group switches the lens"), Kit.InstanceData[RTSKitLensData::Group], 1.f);

	// Free: in the building's frame, scaled twice, its group overridden.
	const int32 FreeData = RTSKitLensData::Floats;
	TestTrue(*FString::Printf(TEXT("The mount point scales with the mount: %s"), *Kit.Parts[1].Transform.GetLocation().ToString()),
		Kit.Parts[1].Transform.GetLocation().Equals(FVector(10.f, 0.f, -20.f), 0.01));
	TestTrue(TEXT("The piece scales with it"), Kit.Parts[1].Transform.GetScale3D().Equals(FVector(2.f), 0.001));
	TestTrue(*FString::Printf(TEXT("So does its light's place: %s"), *Kit.Lights[1].Location.ToString()),
		Kit.Lights[1].Location.Equals(FVector(20.f, 0.f, -26.f), 0.01));
	TestEqual(TEXT("The mount's group overrides the piece's"), Kit.Lights[1].Group, FName(TEXT("Accents")));
	TestEqual(TEXT("A lens outside Fixtures ignores that group"), Kit.InstanceData[FreeData + RTSKitLensData::Group], 0.f);
	TestEqual(TEXT("The indicator colour goes to the lens"), Kit.InstanceData[FreeData + RTSKitLensData::Color], 1.f);
	TestEqual(TEXT("Red alone"), Kit.InstanceData[FreeData + RTSKitLensData::Color + 1], 0.f);
	return true;
}

/**
 * The HQ's flag and mast beacons as mounts (#390): they expand to the parts usa_hq.json placed by hand before, add no light,
 * draw instanced with their lens data, one instance changing without its neighbour, and mounting them spawns no actor and
 * no ticking component. The whole HQ, kit pieces included, takes one forced LOD (BUILDING_KIT.md, option C2).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSKitMountHeadquartersTest, "WorldDivide.Buildings.KitMount.Headquarters", TestFlags)

bool FRTSKitMountHeadquartersTest::RunTest(const FString& Parameters)
{
	URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, HeadquartersDataPath);
	if (!TestNotNull(TEXT("DA_TestHeadquarters loads"), Data))
	{
		return false;
	}
	const FRTSKitExpansion Kit = RTSKit::Expand(*Data);
	TestTrue(*FString::Printf(TEXT("Every mount resolves: %s"), *FString::Join(Kit.Errors, TEXT("; "))), Kit.Errors.IsEmpty());
	TestEqual(TEXT("The kit pieces add no light: the HQ's lights are its own"), Kit.Lights.Num(), 0);
	if (!TestEqual(TEXT("The HQ mounts its flag and two beacons"), Kit.Parts.Num(), static_cast<int32>(UE_ARRAY_COUNT(HeadquartersKitParts))))
	{
		return false;
	}
	for (const FExpectedPart& Expected : HeadquartersKitParts)
	{
		const FRTSBuildingPart* Part = Kit.Parts.FindByPredicate([&Expected](const FRTSBuildingPart& P) { return P.Name == Expected.Name; });
		if (!TestNotNull(*FString::Printf(TEXT("%s is mounted"), Expected.Name), Part))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s's mesh"), Expected.Name), Part->Mesh ? Part->Mesh->GetName() : FString(), FString(Expected.Mesh));
		TestTrue(*FString::Printf(TEXT("%s stands where it did: %s"), Expected.Name, *Part->Transform.GetLocation().ToString()),
			Part->Transform.GetLocation().Equals(Expected.Location, 0.01));
		TestTrue(*FString::Printf(TEXT("%s turns as it did"), Expected.Name), Part->Transform.GetRotation().Rotator().Equals(Expected.Rotation, 0.01));
		TestTrue(*FString::Printf(TEXT("%s is as big: %s"), Expected.Name, *Part->Transform.GetScale3D().ToString()),
			Part->Transform.GetScale3D().Equals(Expected.Scale, 0.0001));
		TestEqual(*FString::Printf(TEXT("%s's shadow"), Expected.Name), Part->bCastShadow, Expected.bCastShadow);
		TestEqual(*FString::Printf(TEXT("%s's WPO disable distance"), Expected.Name), Part->WPODisableDistance, Expected.WPODisableDistance);
		TestFalse(*FString::Printf(TEXT("%s stands still"), Expected.Name), Part->bMovable || Part->Motion.IsAnimated());
	}

	// The same HQ without mounts, to compare what mounting adds.
	URTSBuildingData* Bare = DuplicateObject(Data, GetTransientPackage());
	Bare->Mounts.Reset();

	const FRTSTestWorld TestWorld;
	const int32 ActorsBefore = CountActors(*TestWorld.World);
	ARTSBuilding* HQ = TestWorld.SpawnBuilding(Data, 0, FVector::ZeroVector);
	TestEqual(TEXT("Mounting spawns no actor"), CountActors(*TestWorld.World), ActorsBefore + 1);
	ARTSBuilding* BareHQ = TestWorld.SpawnBuilding(Bare, 0, FVector(20000.f, 0.f, 0.f));
	TestEqual(TEXT("Mounting adds no tick function"), CountTickingComponents(*HQ), CountTickingComponents(*BareHQ));
	TestEqual(TEXT("Mounting adds no light"), HQ->GetLightComponents().Num(), BareHQ->GetLightComponents().Num());

	UStaticMeshComponent* CoreComponent = nullptr;
	UStaticMeshComponent* EastComponent = nullptr;
	UStaticMeshComponent* FlagComponent = nullptr;
	int32 Core = INDEX_NONE;
	int32 East = INDEX_NONE;
	int32 Flag = INDEX_NONE;
	if (!TestTrue(TEXT("The HQ draws its mounted pieces"), HQ->FindPart(TEXT("Beacon_MastTipCore"), CoreComponent, Core)
		&& HQ->FindPart(TEXT("Beacon_MastTipEast"), EastComponent, East) && HQ->FindPart(TEXT("NationalFlag"), FlagComponent, Flag)))
	{
		return false;
	}
	UInstancedStaticMeshComponent* Beacons = Cast<UInstancedStaticMeshComponent>(CoreComponent);
	TestTrue(TEXT("The beacons share one instanced component"), Beacons && CoreComponent == EastComponent && Core != East);
	TestTrue(TEXT("The flag draws instanced too, alone"), Cast<UInstancedStaticMeshComponent>(FlagComponent) && Flag == 0
		&& FlagComponent != CoreComponent);
	for (const UStaticMeshComponent* Component : { CoreComponent, FlagComponent })
	{
		TestFalse(*FString::Printf(TEXT("%s does not tick"), *Component->GetName()), Component->PrimaryComponentTick.bCanEverTick);
	}
	if (!Beacons || !TestEqual(TEXT("Each beacon carries its lens data"), Beacons->NumCustomDataFloats, RTSKitLensData::Floats))
	{
		return false;
	}
	auto InstanceData = [Beacons](int32 Instance)
	{
		return TArray<float>(MakeArrayView(Beacons->PerInstanceSMCustomData).Slice(Instance * RTSKitLensData::Floats, RTSKitLensData::Floats));
	};
	TestEqual(TEXT("Each beacon starts lit"), InstanceData(Core)[RTSKitLensData::Level], 1.f);
	const TArray<float> EastBefore = InstanceData(East);
	const float Dimmed[RTSKitLensData::Floats] = { 0.3f, 0.f, 0.5f, 0.f, 0.f, 0.f, 1.f };
	TestTrue(TEXT("A mounted piece takes new lens data"), HQ->SetKitInstanceData(TEXT("Beacon_MastTipCore"), Dimmed));
	TestTrue(TEXT("Its instance changes"), InstanceData(Core) == TArray<float>(Dimmed, RTSKitLensData::Floats));
	TestTrue(TEXT("Its neighbour does not"), InstanceData(East) == EastBefore);
	TestFalse(TEXT("A part that is not a mounted piece takes none"), HQ->SetKitInstanceData(TEXT("Shell"), Dimmed));

	// One LOD for the whole building: shell, parts and kit pieces with LODs, and back to the renderer.
	HQ->SetForcedLOD(1);
	TInlineComponentArray<UStaticMeshComponent*> Meshes(HQ);
	int32 Forced = 0;
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetNumLODs() > 1)
		{
			TestEqual(*FString::Printf(TEXT("%s draws LOD1"), *Mesh->GetStaticMesh()->GetName()), Mesh->ForcedLodModel, 2);
			++Forced;
		}
	}
	TestTrue(TEXT("The shell and the flag are among them"), Forced >= 2 && FlagComponent->ForcedLodModel == 2);
	HQ->SetForcedLOD(INDEX_NONE);
	TestTrue(TEXT("Then the renderer picks again"), Algo::AllOf(Meshes, [](const UStaticMeshComponent* Mesh) { return Mesh->ForcedLodModel == 0; }));
	return true;
}

/**
 * Every building's real lights within its size class's budget (BUILDING_KIT.md, Light budgets, #391): its own and its mounted
 * pieces', steady and warning apart. The HQ is Large.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSKitMountLightBudgetTest, "WorldDivide.Buildings.KitMount.LightBudget", TestFlags)

bool FRTSKitMountLightBudgetTest::RunTest(const FString& Parameters)
{
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(URTSBuildingData::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Found;
	Registry.GetAssets(Filter, Found);
	if (!TestTrue(TEXT("There are buildings"), Found.Num() > 0))
	{
		return false;
	}
	for (const FAssetData& Asset : Found)
	{
		const URTSBuildingData* Data = Cast<URTSBuildingData>(Asset.GetAsset());
		if (!Data)
		{
			continue;
		}
		const RTSKit::FLightBudget Budget = RTSKit::GetLightBudget(Data->Size);
		const RTSKit::FLightBudget Count = RTSKit::CountLights(*Data);
		TestTrue(*FString::Printf(TEXT("%s: %d steady lights within %d"), *Data->GetName(), Count.Steady, Budget.Steady),
			Count.Steady <= Budget.Steady);
		TestTrue(*FString::Printf(TEXT("%s: %d warning lights within %d"), *Data->GetName(), Count.Warning, Budget.Warning),
			Count.Warning <= Budget.Warning);
	}
	const URTSBuildingData* HQ = LoadObject<URTSBuildingData>(nullptr, HeadquartersDataPath);
	TestTrue(TEXT("The HQ is a Large building"), HQ && HQ->Size == ERTSBuildingSize::Large);
	return true;
}

#endif
