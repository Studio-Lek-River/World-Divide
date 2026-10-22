#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"

using namespace RTSTests;

namespace RTSKitSpikeTests
{
	/** The kit lens's floats per instance (docs/art/BUILDING_KIT.md, "Per-instance and LOD"). */
	constexpr int32 Floats = 7;
	constexpr int32 Instances = 256;
	constexpr int32 Changed = 128;

	float GetValue(int32 Instance, int32 Float, int32 Pass)
	{
		return Instance * 0.01f + Float * 0.1f + Pass;
	}
}

/**
 * The shared building kit's per-instance lens state (#387): one instanced component of Instances kit pieces carries
 * Floats custom floats each; Changed of them change in one frame through SetCustomData and one MarkRenderInstancesDirty,
 * and the component keeps its registration and its scene proxy (no render state rebuild), the others keep their values.
 * Forcing a LOD (the building's LOD pushed to its kit, option C) keeps every instance and its data.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSKitSpikeInstanceDataTest, "WorldDivide.Art.KitSpike", TestFlags)

bool FRTSKitSpikeInstanceDataTest::RunTest(const FString& Parameters)
{
	using namespace RTSKitSpikeTests;

	FRTSTestWorld TestWorld;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("the engine cube"), Cube))
	{
		return false;
	}
	AActor* Holder = TestWorld.World->SpawnActor<AActor>();
	UInstancedStaticMeshComponent* Kit = NewObject<UInstancedStaticMeshComponent>(Holder);
	Kit->SetStaticMesh(Cube);
	Kit->SetNumCustomDataFloats(Floats);
	Holder->SetRootComponent(Kit);
	TArray<FTransform> Transforms;
	for (int32 Instance = 0; Instance < Instances; ++Instance)
	{
		Transforms.Emplace(FVector(Instance * 200.f, 0.f, 0.f));
	}
	Kit->AddInstances(Transforms, /*bShouldReturnIndices*/ false);
	for (int32 Instance = 0; Instance < Instances; ++Instance)
	{
		float Values[Floats];
		for (int32 Float = 0; Float < Floats; ++Float)
		{
			Values[Float] = GetValue(Instance, Float, 0);
		}
		Kit->SetCustomData(Instance, MakeArrayView(Values, Floats));
	}
	Kit->RegisterComponent();
	TestWorld.World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	const FPrimitiveSceneProxy* Proxy = Kit->SceneProxy;

	// Every other instance changes, as a building's lenses do when a group switches.
	const double Start = FPlatformTime::Seconds();
	for (int32 Change = 0; Change < Changed; ++Change)
	{
		const int32 Instance = Change * 2;
		float Values[Floats];
		for (int32 Float = 0; Float < Floats; ++Float)
		{
			Values[Float] = GetValue(Instance, Float, 1);
		}
		Kit->SetCustomData(Instance, MakeArrayView(Values, Floats));
	}
	Kit->MarkRenderInstancesDirty();
	const double CallMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	TestWorld.World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	AddInfo(FString::Printf(TEXT("%d of %d instances x %d floats changed in %.4f ms on the game thread; scene proxy %s"),
		Changed, Instances, Floats, CallMs, Proxy ? (Kit->SceneProxy == Proxy ? TEXT("kept") : TEXT("rebuilt")) : TEXT("none (no renderer)")));

	TestTrue(TEXT("the component stays registered"), Kit->IsRegistered());
	if (Proxy)
	{
		TestTrue(TEXT("the instance data update keeps the scene proxy"), Kit->SceneProxy == Proxy);
	}
	// The component keeps its floats private; the test reads them as the editor's details panel does.
	const FArrayProperty* DataProperty = FindFProperty<FArrayProperty>(UInstancedStaticMeshComponent::StaticClass(), TEXT("PerInstanceSMCustomData"));
	if (!TestNotNull(TEXT("UInstancedStaticMeshComponent::PerInstanceSMCustomData"), DataProperty))
	{
		return false;
	}
	const TArray<float>& Data = *DataProperty->ContainerPtrToValuePtr<TArray<float>>(Kit);
	auto CheckValues = [this, &Data](const TCHAR* When)
	{
		if (!TestEqual(FString::Printf(TEXT("custom floats %s"), When), Data.Num(), Instances * Floats))
		{
			return;
		}
		int32 Wrong = 0;
		for (int32 Instance = 0; Instance < Instances; ++Instance)
		{
			const int32 Pass = Instance % 2 == 0 && Instance / 2 < Changed ? 1 : 0;
			for (int32 Float = 0; Float < Floats; ++Float)
			{
				Wrong += FMath::IsNearlyEqual(Data[Instance * Floats + Float], GetValue(Instance, Float, Pass)) ? 0 : 1;
			}
		}
		TestEqual(FString::Printf(TEXT("custom floats with the wrong value %s"), When), Wrong, 0);
	};
	CheckValues(TEXT("after the update"));

	Kit->SetForcedLodModel(2);
	TestWorld.World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	TestEqual(TEXT("instances after forcing LOD1"), Kit->GetInstanceCount(), Instances);
	CheckValues(TEXT("after forcing LOD1"));
	return true;
}

#endif
