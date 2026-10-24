// River Crossing's vegetation (#158): URTSEditorScripting::SpawnTreeInstances, BuildVegetationGraph and GeneratePCG, on their
// own like the other builders.
#include "Core/RTSEditorScripting.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGDifferenceElement.h"
#include "Elements/PCGInnerIntersectionElement.h"
#include "Elements/PCGMergeElement.h"
#include "Elements/PCGRandomChoice.h"
#include "Elements/PCGCreateAttribute.h"
#include "Elements/PCGSkinnedMeshSpawner.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGSurfaceSampler.h"
#include "Elements/PCGTransformPoints.h"
#include "Elements/PCGTypedGetter.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "MeshSelectors/PCGSkinnedMeshSelector.h"
#include "Misc/PackageName.h"
#include "Navigation/RTSTrunkObstaclesComponent.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGPin.h"
#include "Subsystems/PCGSubsystem.h"
#include "UObject/Package.h"
#include "WorldDivide.h"

// Named, not anonymous: the unity build shares one scope among several files.
namespace RTSVegetationBuilder
{
	/** The tag of every tree component, for line of sight (M6) and crushing to find them. */
	const FName TreeTag(TEXT("WD_Tree"));
	/** The point attribute holding the skinned or static mesh each point spawns. */
	const FName MeshAttribute(TEXT("Mesh"));
	/** Seconds GeneratePCG waits for one component. */
	constexpr double GenerateTimeout = 900.0;

	/**
	 * A tree component never ticks, saved so: the trees stand in their reference pose, and an instanced skinned mesh's tick
	 * and the transform update it sends cost the game thread every frame (about 7 ms for River Crossing's woods, #158).
	 * bCanEverTick is serialized per component; the engine's class cannot be subclassed outside the engine.
	 */
	void StopTicking(UInstancedSkinnedMeshComponent& Trees)
	{
		Trees.SetComponentTickEnabled(false);
		Trees.PrimaryComponentTick.bStartWithTickEnabled = false;
		Trees.PrimaryComponentTick.bCanEverTick = false;
	}

	/** Trees: seen and shadowed, never collided with and never in the navmesh, so vehicles drive through them (D4). */
	void MakeTree(UInstancedSkinnedMeshComponent& Trees, int32 CullDistance)
	{
		StopTicking(Trees);
		Trees.SetMobility(EComponentMobility::Static);
		Trees.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trees.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Trees.SetCanEverAffectNavigation(false);
		Trees.SetGenerateOverlapEvents(false);
		Trees.SetCullDistances(0, CullDistance);
		Trees.ComponentTags.AddUnique(TreeTag);
	}

	/**
	 * The mesh of the hidden instances a graph leaves at its trees' trunks, which GeneratePCG turns into trunk obstacles:
	 * never drawn, only the instances' locations count.
	 */
	const TCHAR* TrunkPointsMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/** The trunk points: hidden, without collision, shadow or navigation; GeneratePCG reads them. */
	void MakeTrunkPoints(FPCGSoftISMComponentDescriptor& Descriptor)
	{
		Descriptor.Mobility = EComponentMobility::Static;
		Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Descriptor.bCanEverAffectNavigation = false;
		Descriptor.bVisible = false;
		Descriptor.bHiddenInGame = true;
		Descriptor.bCastShadow = false;
		Descriptor.bCastDynamicShadow = false;
		Descriptor.bAffectDistanceFieldLighting = false;
		Descriptor.bVisibleInRayTracing = false;
		Descriptor.bGenerateOverlapEvents = false;
	}

	/** Actor's trunk obstacles, set to Feet: its own component, added when it has none. */
	void SetTrunkObstacles(AActor& Actor, const TArray<FVector>& Feet)
	{
		URTSTrunkObstaclesComponent* Obstacles = Actor.FindComponentByClass<URTSTrunkObstaclesComponent>();
		if (!Obstacles)
		{
			Obstacles = NewObject<URTSTrunkObstaclesComponent>(&Actor, TEXT("TrunkObstacles"));
			Actor.AddInstanceComponent(Obstacles);
			Obstacles->SetTrunks(Feet);
			Obstacles->RegisterComponent();
			return;
		}
		Obstacles->SetTrunks(Feet);
	}

	/** A new or emptied asset of class T at AssetPath (/Game/...), registered with the asset registry when new. */
	template <typename T>
	T* MakeAsset(const FString& AssetPath, bool& bOutIsNew)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(AssetPath);
		UPackage* Package = CreatePackage(*AssetPath);
		Package->FullyLoad();
		T* Asset = FindObject<T>(Package, *Name);
		bOutIsNew = !Asset;
		if (!Asset)
		{
			Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		Package->MarkPackageDirty();
		return Asset;
	}

	/** Builds one graph's nodes: AddNode, connected to a source pin; a small wrapper so the graph reads top to bottom. */
	struct FGraphWriter
	{
		UPCGGraph& Graph;
		int32 NextSeed = 1;

		template <typename T>
		T* Add(UPCGNode*& OutNode)
		{
			T* Settings = nullptr;
			OutNode = Graph.AddNodeOfType<T>(Settings);
			// Each node its own seed, fixed by its place in the graph, so a rebuilt graph draws the same numbers.
			Settings->Seed = NextSeed++;
			return Settings;
		}

		/** False when the edge failed, a pin missing (after a PCG upgrade); BuildVegetationGraph then fails. */
		bool bConnected = true;

		void Connect(UPCGNode* From, FName FromPin, UPCGNode* To, FName ToPin)
		{
			if (!From->GetOutputPin(FromPin) || !To->GetInputPin(ToPin))
			{
				UE_LOG(LogWorldDivide, Error, TEXT("BuildVegetationGraph: no pin %s on %s or %s on %s"), *FromPin.ToString(),
					*From->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), *ToPin.ToString(),
					*To->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
				bConnected = false;
				return;
			}
			Graph.AddEdge(From, FromPin, To, ToPin);
		}
	};

	/** The points of every actor tagged Tag that overlaps the volume, one point with the actor's bounds each, merged. */
	UPCGNode* AddTaggedActors(FGraphWriter& Writer, FName Tag)
	{
		UPCGNode* Node = nullptr;
		UPCGDataFromActorSettings* Settings = Writer.Add<UPCGDataFromActorSettings>(Node);
		Settings->ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
		Settings->ActorSelector.ActorSelection = EPCGActorSelection::ByTag;
		Settings->ActorSelector.ActorSelectionTag = Tag;
		Settings->ActorSelector.bSelectMultiple = true;
		Settings->ActorSelector.bMustOverlapSelf = true;
		Settings->Mode = EPCGGetDataFromActorMode::GetSinglePoint;
		Settings->bMergeSinglePointData = true;
		return Node;
	}

	/** Random yaw and a uniform scale in Scale for In's points. */
	UPCGNode* AddPlacement(FGraphWriter& Writer, UPCGNode* In, FName InPin, const FVector2D& Scale)
	{
		UPCGNode* Node = nullptr;
		UPCGTransformPointsSettings* Settings = Writer.Add<UPCGTransformPointsSettings>(Node);
		Settings->RotationMin = FRotator(0.0, 0.0, 0.0);
		Settings->RotationMax = FRotator(0.0, 360.0, 0.0);
		Settings->ScaleMin = FVector(Scale.X);
		Settings->ScaleMax = FVector(Scale.Y);
		Settings->bUniformScale = true;
		Writer.Connect(In, InPin, Node, PCGPinConstants::DefaultInputLabel);
		return Node;
	}

	/**
	 * Splits In's points by weight over Entries, each share tagged with its mesh (MeshAttribute): a chain of random choices,
	 * each taking its entry's part of what the ones before it left. Adds the tagged shares of skinned meshes to Meshes, those
	 * of trees to Trunks as well, and those of static meshes to Statics.
	 */
	void AddMeshChoice(FGraphWriter& Writer, UPCGNode* In, FName InPin, const TArray<FRTSVegetationEntry>& Entries, UPCGNode* Meshes,
		UPCGNode* Trunks, UPCGNode* Statics)
	{
		float Remaining = 0.f;
		for (const FRTSVegetationEntry& Entry : Entries)
		{
			Remaining += Entry.Weight;
		}
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FRTSVegetationEntry& Entry = Entries[Index];
			UPCGNode* Chosen = In;
			FName ChosenPin = InPin;
			if (Index < Entries.Num() - 1)
			{
				UPCGNode* Choice = nullptr;
				UPCGRandomChoiceSettings* Settings = Writer.Add<UPCGRandomChoiceSettings>(Choice);
				Settings->bFixedMode = false;
				Settings->Ratio = Remaining > 0.f ? Entry.Weight / Remaining : 0.f;
				Settings->bOutputDiscardedEntries = true;
				Writer.Connect(In, InPin, Choice, PCGPinConstants::DefaultInputLabel);
				Chosen = Choice;
				ChosenPin = PCGRandomChoiceConstants::ChosenEntriesLabel;
				In = Choice;
				InPin = PCGRandomChoiceConstants::DiscardedEntriesLabel;
			}
			Remaining -= Entry.Weight;

			if (!FMath::IsNearlyEqual(Entry.ScaleFactor, 1.f))
			{
				UPCGNode* Scaled = nullptr;
				UPCGTransformPointsSettings* Scale = Writer.Add<UPCGTransformPointsSettings>(Scaled);
				Scale->ScaleMin = FVector(Entry.ScaleFactor);
				Scale->ScaleMax = FVector(Entry.ScaleFactor);
				Scale->bUniformScale = true;
				Writer.Connect(Chosen, ChosenPin, Scaled, PCGPinConstants::DefaultInputLabel);
				Chosen = Scaled;
				ChosenPin = PCGPinConstants::DefaultOutputLabel;
			}

			UPCGNode* Tag = nullptr;
			UPCGAddAttributeSettings* Settings = Writer.Add<UPCGAddAttributeSettings>(Tag);
			Settings->OutputTarget.SetAttributeName(MeshAttribute);
			Settings->AttributeTypes.Type = EPCGMetadataTypes::SoftObjectPath;
			Writer.Connect(Chosen, ChosenPin, Tag, PCGPinConstants::DefaultInputLabel);
			if (Entry.StaticMesh)
			{
				Settings->AttributeTypes.SoftObjectPathValue = FSoftObjectPath(Entry.StaticMesh);
				Writer.Connect(Tag, PCGPinConstants::DefaultOutputLabel, Statics, PCGPinConstants::DefaultInputLabel);
				continue;
			}
			Settings->AttributeTypes.SoftObjectPathValue = FSoftObjectPath(Entry.Mesh);
			Writer.Connect(Tag, PCGPinConstants::DefaultOutputLabel, Meshes, PCGPinConstants::DefaultInputLabel);
			if (Entry.bTrunk && Trunks)
			{
				Writer.Connect(Tag, PCGPinConstants::DefaultOutputLabel, Trunks, PCGPinConstants::DefaultInputLabel);
			}
		}
	}

	/** Hashes the instance locations of every instanced component of Actor, sorted, rounded to the centimetre. */
	uint32 HashInstances(const AActor& Actor, int32& OutCount)
	{
		TArray<FIntVector> Locations;
		TArray<UInstancedStaticMeshComponent*> Statics;
		Actor.GetComponents(Statics);
		for (const UInstancedStaticMeshComponent* Component : Statics)
		{
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Component->GetInstanceTransform(Index, Transform, true);
				Locations.Add(FIntVector(FMath::RoundToInt(Transform.GetLocation().X), FMath::RoundToInt(Transform.GetLocation().Y),
					FMath::RoundToInt(Transform.GetLocation().Z)));
			}
		}
		TArray<UInstancedSkinnedMeshComponent*> Skinned;
		Actor.GetComponents(Skinned);
		for (const UInstancedSkinnedMeshComponent* Component : Skinned)
		{
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Component->GetInstanceTransform(Component->GetInstanceId(Index), Transform, true);
				Locations.Add(FIntVector(FMath::RoundToInt(Transform.GetLocation().X), FMath::RoundToInt(Transform.GetLocation().Y),
					FMath::RoundToInt(Transform.GetLocation().Z)));
			}
		}
		Locations.Sort([](const FIntVector& A, const FIntVector& B)
		{
			return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
		});
		OutCount = Locations.Num();
		return Locations.IsEmpty() ? 0 : FCrc::MemCrc32(Locations.GetData(), Locations.Num() * sizeof(FIntVector));
	}
}

AActor* URTSEditorScripting::SpawnTreeInstances(UObject* WorldContextObject, const FString& Label, USkinnedAsset* Mesh,
	const TArray<FTransform>& Transforms, int32 CullDistance, bool bTrunks)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || (!Mesh && !bTrunks) || Transforms.IsEmpty())
	{
		UE_LOG(LogWorldDivide, Error, TEXT("SpawnTreeInstances %s: no world, neither a mesh nor trunks, or no transforms"), *Label);
		return nullptr;
	}
	// An actor of that label is refilled rather than replaced, so its file (One File Per Actor) stays the same.
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It && !Actor; ++It)
	{
		Actor = It->GetActorLabel() == Label ? *It : nullptr;
	}
	USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
	if (Actor && Root)
	{
		TArray<UInstancedStaticMeshComponent*> Statics;
		Actor->GetComponents(Statics);
		TArray<UInstancedSkinnedMeshComponent*> Skinned;
		Actor->GetComponents(Skinned);
		TArray<URTSTrunkObstaclesComponent*> Obstacles;
		Actor->GetComponents(Obstacles);
		TArray<UActorComponent*> Old(Statics);
		Old.Append(Skinned);
		Old.Append(Obstacles);
		for (UActorComponent* Component : Old)
		{
			Actor->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
	else
	{
		if (Actor)
		{
			World->DestroyActor(Actor);
		}
		Actor = World->SpawnActor<AActor>();
		Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Static);
		Actor->SetRootComponent(Root);
		Actor->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLabel(Label);
	}

	if (Mesh)
	{
		UInstancedSkinnedMeshComponent* Trees = NewObject<UInstancedSkinnedMeshComponent>(Actor, *Mesh->GetName());
		RTSVegetationBuilder::MakeTree(*Trees, CullDistance);
		Trees->SetSkinnedAssetAndUpdate(Mesh);
		Trees->SetupAttachment(Root);
		Actor->AddInstanceComponent(Trees);
		Trees->RegisterComponent();
		TArray<int32> Animations;
		Animations.SetNumZeroed(Transforms.Num());
		Trees->AddInstances(Transforms, Animations, false, true);
	}

	if (bTrunks)
	{
		TArray<FVector> Feet;
		for (const FTransform& Transform : Transforms)
		{
			Feet.Add(Transform.GetLocation());
		}
		RTSVegetationBuilder::SetTrunkObstacles(*Actor, Feet);
	}
	return Actor;
}

UPCGGraph* URTSEditorScripting::BuildVegetationGraph(const FString& AssetPath, const FRTSVegetationGraphSpec& Spec)
{
	using namespace RTSVegetationBuilder;
	// Each entry exactly one mesh, skinned or static.
	const auto BadEntry = [](const FRTSVegetationEntry& Entry) { return !Entry.Mesh == !Entry.StaticMesh; };
	if (!FPackageName::IsValidLongPackageName(AssetPath) || Spec.Entries.IsEmpty() || Spec.Entries.ContainsByPredicate(BadEntry)
		|| Spec.LaneEntries.ContainsByPredicate(BadEntry))
	{
		UE_LOG(LogWorldDivide, Error, TEXT("BuildVegetationGraph %s: a bad path, no entries or an entry without one mesh"), *AssetPath);
		return nullptr;
	}
	const auto IsSkinned = [](const FRTSVegetationEntry& Entry) { return Entry.Mesh != nullptr; };
	const auto IsStatic = [](const FRTSVegetationEntry& Entry) { return Entry.StaticMesh != nullptr; };
	const bool bSkinned = Spec.Entries.ContainsByPredicate(IsSkinned) || Spec.LaneEntries.ContainsByPredicate(IsSkinned);
	const bool bStatic = Spec.Entries.ContainsByPredicate(IsStatic) || Spec.LaneEntries.ContainsByPredicate(IsStatic);
	bool bIsNew = false;
	UPCGGraph* Graph = MakeAsset<UPCGGraph>(AssetPath, bIsNew);
	if (!bIsNew)
	{
		TArray<UPCGNode*> Nodes = Graph->GetNodes();
		Graph->RemoveNodes(Nodes);
	}
	FGraphWriter Writer{ *Graph };
	// The landscape inside the volume running the graph, minus the exclusions. A graph made outside the editor's asset
	// factory has no pins on its input and output nodes, so it reads the landscape and its volume itself.
	UPCGNode* Landscape = nullptr;
	Writer.Add<UPCGGetLandscapeSettings>(Landscape);
	UPCGNode* Volume = nullptr;
	UPCGDataFromActorSettings* VolumeSettings = Writer.Add<UPCGDataFromActorSettings>(Volume);
	VolumeSettings->ActorSelector.ActorFilter = EPCGActorFilter::Self;
	VolumeSettings->Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	UPCGNode* Sampler = nullptr;
	UPCGSurfaceSamplerSettings* SamplerSettings = Writer.Add<UPCGSurfaceSamplerSettings>(Sampler);
	SamplerSettings->PointsPerSquaredMeter = Spec.PointsPerSquaredMeter;
	SamplerSettings->Looseness = Spec.Looseness;
	SamplerSettings->PointExtents = FVector(50.0);
	SamplerSettings->bApplyDensityToPoints = false;
	Writer.Connect(Landscape, PCGPinConstants::DefaultOutputLabel, Sampler, PCGSurfaceSamplerConstants::SurfaceLabel);
	Writer.Connect(Volume, PCGPinConstants::DefaultOutputLabel, Sampler, PCGSurfaceSamplerConstants::BoundingShapeLabel);

	// Only inside the actors tagged IncludeTag, when the graph has one: strips along a river, too many for a volume each.
	UPCGNode* Sampled = Sampler;
	if (!Spec.IncludeTag.IsNone())
	{
		Writer.Add<UPCGInnerIntersectionSettings>(Sampled);
		Writer.Connect(Sampler, PCGPinConstants::DefaultOutputLabel, Sampled, PCGPinConstants::DefaultInputLabel);
		Writer.Connect(AddTaggedActors(Writer, Spec.IncludeTag), PCGPinConstants::DefaultOutputLabel, Sampled,
			PCGPinConstants::DefaultInputLabel);
	}
	UPCGNode* Excluded = nullptr;
	Writer.Add<UPCGDifferenceSettings>(Excluded);
	Writer.Connect(Sampled, PCGPinConstants::DefaultOutputLabel, Excluded, PCGDifferenceConstants::SourceLabel);
	Writer.Connect(AddTaggedActors(Writer, Spec.ExcludeTag), PCGPinConstants::DefaultOutputLabel, Excluded,
		PCGDifferenceConstants::DifferencesLabel);

	// Outside the lane bands every entry, inside them only the short ones.
	UPCGNode* Lanes = AddTaggedActors(Writer, Spec.LaneTag);
	UPCGNode* Open = nullptr;
	Writer.Add<UPCGDifferenceSettings>(Open);
	Writer.Connect(Excluded, PCGPinConstants::DefaultOutputLabel, Open, PCGDifferenceConstants::SourceLabel);
	Writer.Connect(Lanes, PCGPinConstants::DefaultOutputLabel, Open, PCGDifferenceConstants::DifferencesLabel);

	UPCGNode* Meshes = nullptr;
	if (bSkinned)
	{
		Writer.Add<UPCGMergeSettings>(Meshes);
	}
	UPCGNode* Statics = nullptr;
	if (bStatic)
	{
		Writer.Add<UPCGMergeSettings>(Statics);
	}
	UPCGNode* Trunks = nullptr;
	if (Spec.bTrunks && bSkinned)
	{
		Writer.Add<UPCGMergeSettings>(Trunks);
	}
	AddMeshChoice(Writer, AddPlacement(Writer, Open, PCGPinConstants::DefaultOutputLabel, Spec.Scale), PCGPinConstants::DefaultOutputLabel,
		Spec.Entries, Meshes, Trunks, Statics);
	if (!Spec.LaneEntries.IsEmpty())
	{
		UPCGNode* Lane = nullptr;
		Writer.Add<UPCGInnerIntersectionSettings>(Lane);
		Writer.Connect(Excluded, PCGPinConstants::DefaultOutputLabel, Lane, PCGPinConstants::DefaultInputLabel);
		Writer.Connect(Lanes, PCGPinConstants::DefaultOutputLabel, Lane, PCGPinConstants::DefaultInputLabel);
		AddMeshChoice(Writer, AddPlacement(Writer, Lane, PCGPinConstants::DefaultOutputLabel, Spec.LaneScale),
			PCGPinConstants::DefaultOutputLabel, Spec.LaneEntries, Meshes, Trunks, Statics);
	}

	// The trees: one instanced skinned mesh component per mesh.
	if (Meshes)
	{
		UPCGNode* Spawner = nullptr;
		UPCGSkinnedMeshSpawnerSettings* SpawnerSettings = Writer.Add<UPCGSkinnedMeshSpawnerSettings>(Spawner);
		UPCGSkinnedMeshSelector* Selector = SpawnerSettings->MeshSelectorParameters;
		if (!Selector)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("BuildVegetationGraph %s: the skinned mesh spawner has no mesh selector"), *AssetPath);
			return nullptr;
		}
		Selector->MeshAttribute.SetAttributeName(MeshAttribute);
		FPCGSoftSkinnedMeshComponentDescriptor& Tree = Selector->TemplateDescriptor;
		Tree.Mobility = EComponentMobility::Static;
		Tree.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Tree.bCastShadow = Spec.bCastShadow;
		Tree.bCastDynamicShadow = Spec.bCastShadow;
		Tree.InstanceEndCullDistance = Spec.CullDistance;
		Tree.ComponentTags.AddUnique(TreeTag);
		SpawnerSettings->bSynchronousLoad = true;
		Writer.Connect(Meshes, PCGPinConstants::DefaultOutputLabel, Spawner, PCGPinConstants::DefaultInputLabel);
	}

	// The static meshes: one instanced static mesh component per mesh, drawn only, as the trees.
	if (Statics)
	{
		UPCGNode* StaticSpawner = nullptr;
		UPCGStaticMeshSpawnerSettings* StaticSettings = Writer.Add<UPCGStaticMeshSpawnerSettings>(StaticSpawner);
		StaticSettings->SetMeshSelectorType(UPCGMeshSelectorByAttribute::StaticClass());
		UPCGMeshSelectorByAttribute* ByAttribute = Cast<UPCGMeshSelectorByAttribute>(StaticSettings->MeshSelectorParameters);
		if (!ByAttribute)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("BuildVegetationGraph %s: the static mesh spawner has no by-attribute selector"), *AssetPath);
			return nullptr;
		}
		ByAttribute->AttributeName = MeshAttribute;
		FPCGSoftISMComponentDescriptor& Static = ByAttribute->TemplateDescriptor;
		Static.Mobility = EComponentMobility::Static;
		Static.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Static.bCanEverAffectNavigation = false;
		Static.bGenerateOverlapEvents = false;
		Static.bCastShadow = Spec.bCastShadow;
		Static.bCastDynamicShadow = Spec.bCastShadow;
		Static.InstanceEndCullDistance = Spec.CullDistance;
		StaticSettings->bSynchronousLoad = true;
		Writer.Connect(Statics, PCGPinConstants::DefaultOutputLabel, StaticSpawner, PCGPinConstants::DefaultInputLabel);
	}
	// The trunks: a hidden point at every tree, which GeneratePCG turns into a hole in the infantry navmesh.
	if (Trunks)
	{
		UPCGNode* Proxies = nullptr;
		UPCGStaticMeshSpawnerSettings* ProxySettings = Writer.Add<UPCGStaticMeshSpawnerSettings>(Proxies);
		ProxySettings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
		UPCGMeshSelectorWeighted* Weighted = Cast<UPCGMeshSelectorWeighted>(ProxySettings->MeshSelectorParameters);
		if (!Weighted)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("BuildVegetationGraph %s: the trunk spawner has no weighted mesh selector"), *AssetPath);
			return nullptr;
		}
		Weighted->MeshEntries.Reset();
		FPCGMeshSelectorWeightedEntry& Entry = Weighted->MeshEntries.AddDefaulted_GetRef();
		Entry.Descriptor.StaticMesh = LoadObject<UStaticMesh>(nullptr, TrunkPointsMesh);
		MakeTrunkPoints(Entry.Descriptor);
		Entry.Weight = 1;
		ProxySettings->bApplyMeshBoundsToPoints = false;
		ProxySettings->bSynchronousLoad = true;
		Writer.Connect(Trunks, PCGPinConstants::DefaultOutputLabel, Proxies, PCGPinConstants::DefaultInputLabel);
	}

	if (!Writer.bConnected)
	{
		return nullptr;
	}
	Graph->ForceNotificationForEditor(EPCGChangeType::Structural);
	Graph->MarkPackageDirty();
	UE_LOG(LogWorldDivide, Display, TEXT("BuildVegetationGraph %s: %d nodes, %d entries, %d lane entries"), *AssetPath,
		Graph->GetNodes().Num(), Spec.Entries.Num(), Spec.LaneEntries.Num());
	return Graph;
}

int32 URTSEditorScripting::GeneratePCG(UObject* WorldContextObject, FName Tag)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return -1;
	}
	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);
	int32 Total = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(Tag))
		{
			continue;
		}
		TArray<UPCGComponent*> Components;
		It->GetComponents(Components);
		for (UPCGComponent* Component : Components)
		{
			const double Start = FPlatformTime::Seconds();
			// Removed first: a regeneration would otherwise reuse the components of the previous graph's spawners.
			Component->CleanupLocalImmediate(/*bRemoveComponents*/ true);
			Component->GenerateLocal(true);
			// A commandlet has no frame loop: tick the PCG scheduler and the asset compilers until the component is done.
			double Last = Start;
			while (Component->IsGenerating())
			{
				const double Now = FPlatformTime::Seconds();
				if (Now - Start > RTSVegetationBuilder::GenerateTimeout)
				{
					UE_LOG(LogWorldDivide, Error, TEXT("GeneratePCG %s: not done after %.0f s"), *It->GetActorLabel(), Now - Start);
					return -1;
				}
				const float Delta = float(Now - Last);
				Last = Now;
				FTSTicker::GetCoreTicker().Tick(Delta);
				if (Subsystem)
				{
					Subsystem->Tick(Delta);
				}
				FAssetCompilingManager::Get().ProcessAsyncTasks();
				FPlatformProcess::Sleep(0.005f);
			}
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		TArray<UInstancedSkinnedMeshComponent*> Trees;
		It->GetComponents(Trees);
		for (UInstancedSkinnedMeshComponent* Component : Trees)
		{
			RTSVegetationBuilder::StopTicking(*Component);
			Component->ComponentTags.AddUnique(RTSVegetationBuilder::TreeTag);
		}
		// The trunk points, known by their mesh: the spawner does not carry a descriptor's tags to its components.
		const UStaticMesh* PointsMesh = LoadObject<UStaticMesh>(nullptr, RTSVegetationBuilder::TrunkPointsMesh);
		TArray<FVector> Feet;
		TArray<UInstancedStaticMeshComponent*> Statics;
		It->GetComponents(Statics);
		for (const UInstancedStaticMeshComponent* Component : Statics)
		{
			for (int32 Index = 0; Component->GetStaticMesh() == PointsMesh && Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Component->GetInstanceTransform(Index, Transform, true);
				Feet.Add(Transform.GetLocation());
			}
		}
		RTSVegetationBuilder::SetTrunkObstacles(**It, Feet);
		int32 Count = 0;
		const uint32 Hash = RTSVegetationBuilder::HashInstances(**It, Count);
		UE_LOG(LogWorldDivide, Display, TEXT("GeneratePCG %s: %d instances, %d trunks, hash %08x"), *It->GetActorLabel(), Count, Feet.Num(), Hash);
		Total += Count;
	}
	return Total;
}
#else
AActor* URTSEditorScripting::SpawnTreeInstances(UObject* WorldContextObject, const FString& Label, USkinnedAsset* Mesh,
	const TArray<FTransform>& Transforms, int32 CullDistance, bool bTrunks)
{
	return nullptr;
}

UPCGGraph* URTSEditorScripting::BuildVegetationGraph(const FString& AssetPath, const FRTSVegetationGraphSpec& Spec)
{
	return nullptr;
}

int32 URTSEditorScripting::GeneratePCG(UObject* WorldContextObject, FName Tag)
{
	return -1;
}
#endif
