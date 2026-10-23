#include "Core/RTSEditorScripting.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ContentStreaming.h"
#include "Core/RTSMaps.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Level.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "WorldDivide.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "LandscapeBlueprintBrushBase.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyRiverComponent.h"
#include "WaterSplineComponent.h"
#endif

int32 URTSEditorScripting::BuildNavigation(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}
	// A commandlet's editor world comes without a navigation system.
	if (!World->GetNavigationSystem())
	{
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
	}
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		return 0;
	}
	// An editor world holds its navmesh build until the assets it loaded have compiled and registers new navmesh bounds and
	// geometry on its tick, none of which a commandlet reaches: finish the assets, lift the hold and tick once here.
	FAssetCompilingManager::Get().FinishAllCompilation();
	NavSystem->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	NavSystem->Tick(0.f);
	// Spawns a navmesh for every agent class the world has none for yet, rebuilds every tile and blocks until done.
	const double StartSeconds = FPlatformTime::Seconds();
	NavSystem->Build();
	const double BuildSeconds = FPlatformTime::Seconds() - StartSeconds;

	// Every agent class needs its own navmesh; the fewest tiles of any of them is the result.
	int32 FewestTiles = MAX_int32;
	for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
	{
		ARecastNavMesh* NavMesh = const_cast<ARecastNavMesh*>(RTSNavAgents::GetNavMesh(*World, Agent));
		const bool bOwn = NavMesh && NavMesh->GetConfig().Name == Agent.Name;
		const int32 Tiles = bOwn ? NavMesh->GetNavMeshTilesCount() : 0;
		FewestTiles = FMath::Min(FewestTiles, Tiles);
		// The tiles' polygons, and the compressed layers a dynamic navmesh keeps to rebuild them around new buildings.
		constexpr double Megabyte = 1024.0 * 1024.0;
		UE_LOG(LogWorldDivide, Display, TEXT("BuildNavigation %s: %s navmesh %d tiles, %.2f MB, tile cache %.2f MB"), *World->GetMapName(),
			*Agent.Name.ToString(), Tiles, bOwn ? NavMesh->LogMemUsed() / Megabyte : 0.0, bOwn ? NavMesh->GetCompressedTileCacheSize() / Megabyte : 0.0);
	}
	UE_LOG(LogWorldDivide, Display, TEXT("BuildNavigation %s: every agent built in %.2f s"), *World->GetMapName(), BuildSeconds);
	return FewestTiles;
}

bool URTSEditorScripting::CanRender()
{
	return FApp::CanEverRender();
}

void URTSEditorScripting::GetPreviewCamera(const FBox2D& Area, FVector& OutLocation, FRotator& OutRotation, double& OutOrthoWidth)
{
	const FVector2D Center = Area.GetCenter();
	OutOrthoWidth = Area.GetSize().Y;
	// High above anything on the map; orthographic, so the height does not change the image.
	OutLocation = FVector(Center.X, Center.Y, OutOrthoWidth);
	// Looking down with yaw 0 keeps the camera's up along world +X and its right along world +Y.
	OutRotation = FRotator(-90.0, 0.0, 0.0);
}

bool URTSEditorScripting::CapturePreview(UObject* WorldContextObject, FBox2D Area, int32 Size, const FString& FileName)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !CanRender() || Size <= 0)
	{
		return false;
	}

	FVector Location;
	FRotator Rotation;
	double OrthoWidth;
	GetPreviewCamera(Area, Location, Rotation, OrthoWidth);

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(Size, Size);
	Target->UpdateResourceImmediate(true);

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>(Location, Rotation, Params);
	if (!Camera)
	{
		return false;
	}
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = OrthoWidth;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// Keeps the sky light's real-time capture and the lighting history between the warm-up captures.
	Capture->bAlwaysPersistRenderingState = true;
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 0.0f;
	Capture->TextureTarget = Target;

	// A commandlet compiles shaders and meshes and streams textures lazily; the image must not show their placeholders.
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	IStreamingManager::Get().StreamAllResources(0.0f);
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		Capture->CaptureScene();
		FlushRenderingCommands();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
	}

	FImage Image;
	const bool bRead = FImageUtils::GetRenderTargetImage(Target, Image);
	World->DestroyActor(Camera);
	if (!bRead)
	{
		return false;
	}
	Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	// The final colour carries no meaningful alpha; the lobby shows the image opaque.
	for (FColor& Pixel : Image.AsBGRA8())
	{
		Pixel.A = 255;
	}
	return FImageUtils::SaveImageByExtension(*FileName, Image);
#else
	return false;
#endif
}

void URTSEditorScripting::ReadMapLayout(UObject* WorldContextObject, FBox2D& OutArea, TArray<FVector>& OutSpotLocations,
	TArray<FString>& OutProblems)
{
	OutArea = FBox2D(ForceInit);
	OutSpotLocations.Reset();
	OutProblems.Reset();
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		OutProblems.Add(TEXT("No level"));
		return;
	}

	OutArea = RTSMaps::FindMapArea(*World);
	if (!OutArea.bIsValid)
	{
		OutProblems.Add(TEXT("No NavMeshBoundsVolume and no Landscape to take the map's area from"));
	}
	const TMap<int32, const APlayerStart*> Starts = RTSMaps::FindStartSpots(*World, &OutProblems);
	if (Starts.IsEmpty())
	{
		OutProblems.Add(TEXT("No RTSStartSpot (or PlayerStart tagged Team<N>)"));
	}
	for (const TPair<int32, const APlayerStart*>& Start : Starts)
	{
		OutSpotLocations.SetNum(FMath::Max(OutSpotLocations.Num(), Start.Key + 1));
		OutSpotLocations[Start.Key] = Start.Value->GetActorLocation();
	}
}

ALandscape* URTSEditorScripting::CreateLandscape(UObject* WorldContextObject, const FString& HeightmapFileName, int32 Components,
	int32 QuadsPerSection, FVector Location, FVector Scale, UMaterialInterface* Material, const TArray<ULandscapeLayerInfoObject*>& Layers,
	const TArray<FString>& WeightmapFileNames)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || Components <= 0 || QuadsPerSection <= 0)
	{
		return nullptr;
	}
	if (Layers.Num() != WeightmapFileNames.Num() || Layers.Contains(nullptr))
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateLandscape: %d layers but %d weightmaps, or a layer is missing"), Layers.Num(), WeightmapFileNames.Num());
		return nullptr;
	}
	constexpr int32 SectionsPerComponent = 2;
	const int32 Size = Components * SectionsPerComponent * QuadsPerSection + 1;
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *HeightmapFileName) || Bytes.Num() != Size * Size * 2)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateLandscape: %s is missing or not %d x %d 16-bit heights"), *HeightmapFileName, Size, Size);
		return nullptr;
	}
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Size * Size);
	for (int32 Index = 0; Index < Heights.Num(); ++Index)
	{
		Heights[Index] = static_cast<uint16>(Bytes[Index * 2] | (Bytes[Index * 2 + 1] << 8));
	}

	// As the editor's New Landscape tool with its layers imported from files: the heights and the paint layers' weights of
	// the default edit layer.
	TArray<FLandscapeImportLayerInfo> PaintLayers;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		FLandscapeImportLayerInfo& PaintLayer = PaintLayers.Emplace_GetRef(Layers[Index]->GetLayerName());
		PaintLayer.LayerInfo = Layers[Index];
		if (!FFileHelper::LoadFileToArray(PaintLayer.LayerData, *WeightmapFileNames[Index]) || PaintLayer.LayerData.Num() != Size * Size)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("CreateLandscape: %s is missing or not %d x %d 8-bit weights"), *WeightmapFileNames[Index], Size, Size);
			return nullptr;
		}
	}
	TMap<FGuid, TArray<uint16>> HeightsPerLayer;
	HeightsPerLayer.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> PaintLayersPerLayer;
	PaintLayersPerLayer.Add(FGuid(), PaintLayers);
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
	for (const FLandscapeImportLayerInfo& PaintLayer : PaintLayers)
	{
		Landscape->AddTargetLayer(PaintLayer.LayerName, FLandscapeTargetLayerSettings(PaintLayer.LayerInfo), false);
	}
	Landscape->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, SectionsPerComponent, QuadsPerSection, HeightsPerLayer, TEXT(""),
		PaintLayersPerLayer, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	Landscape->SetActorLabel(TEXT("Landscape"));
	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	return Landscape;
#else
	return nullptr;
#endif
}

ULandscapeLayerInfoObject* URTSEditorScripting::CreateLandscapeLayerInfo(const FString& AssetPath, FName LayerName, UPhysicalMaterial* PhysMaterial)
{
#if WITH_EDITOR
	const FString Name = FPackageName::GetLongPackageAssetName(AssetPath);
	ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!LayerInfo)
	{
		UPackage* Package = CreatePackage(*AssetPath);
		LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(LayerInfo);
	}
	LayerInfo->SetLayerName(LayerName, true);
	LayerInfo->SetBlendMethod(ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending, true);
	LayerInfo->SetPhysicalMaterial(PhysMaterial, true);
	LayerInfo->MarkPackageDirty();
	return LayerInfo;
#else
	return nullptr;
#endif
}

int32 URTSEditorScripting::SetLandscapePhysicalMaterial(UObject* WorldContextObject, UPhysicalMaterial* Material)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}
	int32 Changed = 0;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (It->DefaultPhysMaterial != Material)
		{
			It->Modify();
			It->DefaultPhysMaterial = Material;
			It->RecreateCollisionComponents();
			++Changed;
		}
	}
	return Changed;
#else
	return 0;
#endif
}

#if WITH_EDITOR
namespace
{
	/** The most edit-layer updates MergeEditLayers runs before it gives up: the merge and its GPU readbacks take a few. */
	constexpr int32 MaxEditLayerUpdates = 300;

	/**
	 * Merges Landscape's edit layers, its brushes included, into its heights and collision and waits for the GPU readbacks,
	 * as the editor's tick does over several frames, which a commandlet never reaches. Returns whether it finished.
	 */
	bool MergeEditLayers(ALandscape& Landscape)
	{
		Landscape.ForceLayersFullUpdate();
		for (int32 Update = 0; Update < MaxEditLayerUpdates; ++Update)
		{
			if (Landscape.IsUpToDate())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("MergeEditLayers: %s merged after %d updates"), *Landscape.GetActorLabel(), Update);
				return true;
			}
			// The brushes draw through materials and render targets that compile and stream lazily in a commandlet.
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->FinishAllCompilation();
			}
			Landscape.ForceUpdateLayersContent();
			FlushRenderingCommands();
		}
		UE_LOG(LogWorldDivide, Error, TEXT("MergeEditLayers: %s still had edit-layer work after %d updates"), *Landscape.GetActorLabel(), MaxEditLayerUpdates);
		return false;
	}

	/**
	 * Writes Landscape's merged heights, its brushes' carving included, into its base edit layer, then destroys the brushes of
	 * BrushLayerIndex and deletes that layer. ALandscape::CollapseLayer would not do: it renders the layers without their
	 * brushes. Run after MergeEditLayers and merge again afterwards. Returns whether it baked.
	 */
	bool BakeBrushLayer(ALandscape& Landscape, int32 BrushLayerIndex)
	{
		ULandscapeInfo* Info = Landscape.GetLandscapeInfo();
		const ULandscapeEditLayerBase* BaseLayer = Landscape.GetEditLayerConst(0);
		FIntRect Extent;
		if (!Info || !BaseLayer || BrushLayerIndex <= 0 || !Info->GetLandscapeExtent(Extent))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("BakeBrushLayer: %s has no base edit layer under layer %d or no extent"), *Landscape.GetActorLabel(), BrushLayerIndex);
			return false;
		}
		// The components' final heightmaps hold the merged heights when no edit layer is being edited.
		const int32 Stride = Extent.Width() + 1;
		TArray<uint16> Heights;
		Heights.SetNumZeroed(Stride * (Extent.Height() + 1));
		{
			FLandscapeEditDataInterface Data(Info);
			Data.GetHeightDataFast(Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, Heights.GetData(), Stride);
		}
		for (ALandscapeBlueprintBrushBase* Brush : Landscape.GetBrushesForLayer(BrushLayerIndex))
		{
			Landscape.RemoveBrush(Brush);
			Brush->GetWorld()->EditorDestroyActor(Brush, true);
		}
		if (!Landscape.DeleteLayer(BrushLayerIndex))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("BakeBrushLayer: could not delete edit layer %d of %s"), BrushLayerIndex, *Landscape.GetActorLabel());
			return false;
		}
		FScopedSetLandscapeEditingLayer Editing(&Landscape, BaseLayer->GetGuid());
		FLandscapeEditDataInterface Data(Info);
		Data.SetHeightData(Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, Heights.GetData(), Stride, true);
		return true;
	}
}
#endif

AActor* URTSEditorScripting::CreateRiver(UObject* WorldContextObject, const TArray<FVector>& Points, const TArray<float>& Widths,
	const TArray<float>& Depths, float BankAngle, float ShoreHeight, UMaterialInterface* WaterMaterial)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !GEditor || Points.Num() < 2 || Widths.Num() != Points.Num() || Depths.Num() != Points.Num())
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: needs an editor world and a width and a depth for each of at least two points"));
		return nullptr;
	}
	if (!CanRender())
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: the water brush carves the landscape on the GPU; run with -AllowCommandletRendering"));
		return nullptr;
	}
	TActorIterator<ALandscape> LandscapeIt(World);
	ALandscape* Landscape = LandscapeIt ? *LandscapeIt : nullptr;
	if (!Landscape)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: %s has no landscape to carve"), *World->GetMapName());
		return nullptr;
	}
	// A new water body gets its water zone and the landscape its water brush from the Water editor module, and the brush its
	// edit layer from the landscape editor's services; a commandlet loads neither module.
	for (const TCHAR* Module : { TEXT("LandscapeEditor"), TEXT("WaterEditor") })
	{
		if (!FModuleManager::Get().LoadModule(Module))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: could not load the %s module"), Module);
			return nullptr;
		}
	}
	// The water brush that carves the landscape, on its Water edit layer. A new water body would make it itself, through the
	// landscape editor's services, which open a dialog and the level editor's mode tools, neither of which a commandlet
	// has: so it is made here first, and the river finds it.
	static const FName WaterLayer(TEXT("Water"));
	int32 WaterLayerIndex = Landscape->GetLayerIndex(WaterLayer);
	if (WaterLayerIndex == INDEX_NONE)
	{
		WaterLayerIndex = Landscape->CreateLayer(WaterLayer);
	}
	UClass* BrushClass = LoadClass<ALandscapeBlueprintBrushBase>(nullptr, TEXT("/Script/WaterEditor.WaterBrushManager"));
	if (WaterLayerIndex == INDEX_NONE || !BrushClass)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: no Water edit layer on %s or no water brush class"), *Landscape->GetActorLabel());
		return nullptr;
	}
	bool bHasBrush = false;
	for (const ALandscapeBlueprintBrushBase* Brush : Landscape->GetBrushesForLayer(WaterLayerIndex))
	{
		bHasBrush |= Brush && Brush->IsA(BrushClass);
	}
	if (!bHasBrush)
	{
		// Through its actor factory, as the editor makes it: the brush's materials and render target defaults.
		UActorFactory* BrushFactory = GEditor->FindActorFactoryForActorClass(BrushClass);
		const FTransform Center(Landscape->GetComponentsBoundingBox().GetCenter());
		ALandscapeBlueprintBrushBase* Brush = Cast<ALandscapeBlueprintBrushBase>(BrushFactory
			? BrushFactory->CreateActor(BrushClass, World->PersistentLevel, Center)
			: World->SpawnActor(BrushClass, &Center));
		if (!Brush)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: could not spawn the water brush"));
			return nullptr;
		}
		Brush->SetActorLabel(TEXT("Landscape_WaterBrushManager"));
		Landscape->AddBrushToLayer(WaterLayerIndex, Brush);
	}
	// Through the river's actor factory, as the Place Actors panel spawns it: the engine's river materials and brush defaults.
	UActorFactory* Factory = GEditor->FindActorFactoryForActorClass(AWaterBodyRiver::StaticClass());
	AWaterBodyRiver* River = Factory ? Cast<AWaterBodyRiver>(Factory->CreateActor(AWaterBodyRiver::StaticClass(), World->PersistentLevel,
		FTransform(Points[0]))) : nullptr;
	UWaterBodyRiverComponent* Body = River ? Cast<UWaterBodyRiverComponent>(River->GetWaterBodyComponent()) : nullptr;
	UWaterSplineComponent* Spline = River ? River->GetWaterSpline() : nullptr;
	if (!Body || !Spline)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: could not spawn a WaterBodyRiver"));
		return nullptr;
	}
	River->SetActorLabel(TEXT("River"));

	TArray<FVector> LocalPoints;
	for (const FVector& Point : Points)
	{
		LocalPoints.Add(Point - Points[0]);
	}
	Spline->ResetSpline(LocalPoints);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Body->SetRiverWidthAtSplineInputKey(Index, Widths[Index]);
		Body->SetRiverDepthAtSplineInputKey(Index, Depths[Index]);
	}
	Spline->K2_SynchronizeAndBroadcastDataChange();

	if (WaterMaterial)
	{
		Body->SetWaterMaterial(WaterMaterial);
	}
	// The bed follows the spline's depth alone, no extra curved channel; the banks rise at BankAngle straight from the edge.
	Body->bAffectsLandscape = true;
	Body->CurveSettings.bUseCurveChannel = false;
	Body->CurveSettings.ChannelDepth = 0.f;
	FWaterFalloffSettings& Falloff = Body->WaterHeightmapSettings.FalloffSettings;
	Falloff.FalloffMode = EWaterBrushFalloffMode::Angle;
	Falloff.FalloffAngle = BankAngle;
	Falloff.EdgeOffset = 0.f;
	Falloff.ZOffset = ShoreHeight;
	// Never ground: the plugin's own water profile is world-static, which RTSGround's traces would stop on (DefaultEngine.ini
	// points UWaterRuntimeSettings at this one too). Nor off: the water mesh lays a river's tiles over the boxes of its enabled collision only, so without
	// any the surface never draws (#381). RTSWaterSurface (DefaultEngine.ini) queries as its own object type that nothing
	// asks for and ignores every channel. The river's Water_ NavArea_Null volumes block the navmesh; the null nav area here
	// only matters if it ever affects it.
	Body->SetCollisionProfileName(TEXT("RTSWaterSurface"));
	Body->SetCanEverAffectNavigation(false);
	Body->SetNavAreaClass(UNavArea_Null::StaticClass());

	FOnWaterBodyChangedParams Changed;
	Changed.bShapeOrPositionChanged = true;
	Changed.bWeightmapSettingsChanged = true;
	Changed.bUserTriggered = true;
	Body->OnWaterBodyChanged(Changed);

	if (!MergeEditLayers(*Landscape))
	{
		return nullptr;
	}
	// The carve baked into the base layer, without the brush (#382): its class lives in the editor-only WaterEditor module,
	// which a -game run never loads, so a saved brush fails to load there. Editing the river no longer carves the landscape;
	// the map's script rebuilds it instead.
	if (!BakeBrushLayer(*Landscape, Landscape->GetLayerIndex(WaterLayer)) || !MergeEditLayers(*Landscape))
	{
		return nullptr;
	}
	Body->bAffectsLandscape = false;
	// The merge outside landscape mode only lists the landscape's packages as modified; the level must save them.
	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->MarkModifiedPackagesAsDirty();
	}

	// The carve must have reached the landscape's collision: the bed under the middle of the river, below the surface.
	int32 Carved = 0;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const TOptional<float> Ground = Landscape->GetHeightAtLocation(Points[Index]);
		if (Ground && *Ground < Points[Index].Z - 0.5f * Depths[Index])
		{
			++Carved;
		}
	}
	const TOptional<float> Middle = Landscape->GetHeightAtLocation(Points[Points.Num() / 2]);
	UE_LOG(LogWorldDivide, Display, TEXT("CreateRiver: %d points, the bed under %d of them; mid-river ground %.0f cm, surface %.0f cm"),
		Points.Num(), Carved, Middle ? *Middle : 0.f, Points[Points.Num() / 2].Z);
	if (Carved < Points.Num() / 2)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateRiver: the water brush did not carve the landscape (bed under %d of %d points)"), Carved, Points.Num());
		return nullptr;
	}
	return River;
#else
	return nullptr;
#endif
}

URuntimeVirtualTexture* URTSEditorScripting::CreateLandscapeVirtualTexture(UObject* WorldContextObject, const FString& AssetPath)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	TActorIterator<ALandscape> LandscapeIt(World);
	ALandscape* Landscape = World && LandscapeIt ? *LandscapeIt : nullptr;
	if (!Landscape)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("CreateLandscapeVirtualTexture: no world or no landscape"));
		return nullptr;
	}
	const FString Name = FPackageName::GetLongPackageAssetName(AssetPath);
	URuntimeVirtualTexture* Texture = LoadObject<URuntimeVirtualTexture>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Texture)
	{
		UPackage* Package = CreatePackage(*AssetPath);
		Texture = NewObject<URuntimeVirtualTexture>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Texture);
		Texture->MarkPackageDirty();
	}

	ARuntimeVirtualTextureVolume* Volume = nullptr;
	for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It && !Volume; ++It)
	{
		Volume = *It;
	}
	if (!Volume)
	{
		Volume = World->SpawnActor<ARuntimeVirtualTextureVolume>();
		Volume->SetActorLabel(TEXT("RVT_Landscape"));
	}
	// As the volume's "Set Bounds" button for an unrotated landscape: a unit box from the bounds' corner, scaled to their size,
	// with VolumeHeadroom over and under: the river and the roads carve and raise the ground after this, and ground outside
	// the volume draws nothing into the texture, so the landscape material showed it black (#156).
	constexpr double VolumeHeadroom = 2000.0;
	const FBox Bounds = Landscape->GetComponentsBoundingBox(true).ExpandBy(FVector(0.0, 0.0, VolumeHeadroom));
	Volume->SetActorTransform(FTransform(FRotator::ZeroRotator, Bounds.Min, Bounds.GetSize()));
	Volume->VirtualTextureComponent->SetBoundsAlignActor(Landscape);
	Volume->VirtualTextureComponent->SetVirtualTexture(Texture);

	if (!Landscape->RuntimeVirtualTextures.Contains(Texture))
	{
		Landscape->Modify();
		Landscape->RuntimeVirtualTextures.Add(Texture);
		Landscape->MarkComponentsRenderStateDirty();
	}
	return Texture;
#else
	return nullptr;
#endif
}

int32 URTSEditorScripting::AddLandscapeSplineRoad(UObject* WorldContextObject, const TArray<FVector>& Points, float Width, float SideFalloff,
	bool bShapeGround, UStaticMesh* Mesh, float TileLength, UMaterialInterface* Material, URuntimeVirtualTexture* VirtualTexture,
	FName LayerName)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	TActorIterator<ALandscape> LandscapeIt(World);
	ALandscape* Landscape = World && LandscapeIt ? *LandscapeIt : nullptr;
	if (!Landscape || Points.Num() < 2 || !Mesh || TileLength <= 0.f)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("AddLandscapeSplineRoad: needs a landscape, a mesh, a tile length and at least two points"));
		return 0;
	}
	// The roads' deformation goes to the reserved splines layer, as the landscape editor's spline tool makes it.
	if (!Landscape->FindEditLayerOfType(ULandscapeEditLayerSplines::StaticClass())
		&& Landscape->CreateLayer(TEXT("Splines"), ULandscapeEditLayerSplines::StaticClass()) == INDEX_NONE)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("AddLandscapeSplineRoad: could not make a splines edit layer on %s"), *Landscape->GetActorLabel());
		return 0;
	}
	// Unscaled under the landscape's scale, as the spline tool makes it, so the points and widths are in world cm.
	if (!Landscape->GetSplinesComponent())
	{
		Landscape->CreateSplineComponent(FVector::OneVector / Landscape->GetActorScale3D());
	}
	ULandscapeSplinesComponent* Splines = Landscape->GetSplinesComponent();
	if (!Splines)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("AddLandscapeSplineRoad: could not make the splines component of %s"), *Landscape->GetActorLabel());
		return 0;
	}
	Splines->Modify();
	const FTransform& ToWorld = Splines->GetComponentTransform();

	constexpr float JunctionTolerance = 100.f;
	// The road's top a few cm over the ground the splines flatten, so it never fights the landscape for the same depth, and
	// RoadThickness deep: a flat mesh's flat bounds lie in the ground and occlusion culling dropped whole stretches of it.
	constexpr float MeshLift = 10.f;
	constexpr float RoadThickness = 50.f;
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const double ThicknessScale = MeshBounds.BoxExtent.Z > UE_KINDA_SMALL_NUMBER ? RoadThickness / (2.0 * MeshBounds.BoxExtent.Z) : 1.0;
	const float MeshOffset = MeshLift - (MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z) * ThicknessScale;
	TArray<ULandscapeSplineControlPoint*> Road;
	for (const FVector& Point : Points)
	{
		const FVector Local = ToWorld.InverseTransformPosition(Point);
		ULandscapeSplineControlPoint* ControlPoint = nullptr;
		for (ULandscapeSplineControlPoint* Existing : Splines->GetControlPoints())
		{
			if (Existing && FVector::Dist(Existing->Location, Local) < JunctionTolerance)
			{
				ControlPoint = Existing;
				break;
			}
		}
		if (!ControlPoint)
		{
			ControlPoint = NewObject<ULandscapeSplineControlPoint>(Splines, NAME_None, RF_Transactional);
			ControlPoint->Location = Local;
			ControlPoint->Width = 0.5f * Width;
			ControlPoint->SideFalloff = SideFalloff;
			ControlPoint->EndFalloff = SideFalloff;
			ControlPoint->SegmentMeshOffset = MeshOffset;
			ControlPoint->bRaiseTerrain = bShapeGround;
			ControlPoint->bLowerTerrain = bShapeGround;
			ControlPoint->LayerName = LayerName;
			Splines->GetControlPoints().Add(ControlPoint);
		}
		Road.Add(ControlPoint);
	}

	FLandscapeSplineMeshEntry Entry;
	Entry.Mesh = Mesh;
	if (Material)
	{
		Entry.MaterialOverrides.Add(Material);
	}
	Entry.bScaleToWidth = true;
	Entry.bNoZScaling = true;
	Entry.Scale = FVector(TileLength / FMath::Max(MeshBounds.BoxExtent.X * 2.0, 1.0), 1.0, ThicknessScale);
	int32 Added = 0;
	for (int32 Index = 1; Index < Road.Num(); ++Index)
	{
		ULandscapeSplineControlPoint* Start = Road[Index - 1];
		ULandscapeSplineControlPoint* End = Road[Index];
		if (Start == End)
		{
			continue;
		}
		ULandscapeSplineSegment* Segment = NewObject<ULandscapeSplineSegment>(Splines, NAME_None, RF_Transactional);
		const float Length = FVector::Dist(Start->Location, End->Location);
		Segment->Connections[0].ControlPoint = Start;
		Segment->Connections[0].TangentLen = Length;
		Segment->Connections[1].ControlPoint = End;
		Segment->Connections[1].TangentLen = Length;
		Segment->bRaiseTerrain = bShapeGround;
		Segment->bLowerTerrain = bShapeGround;
		Segment->LayerName = LayerName;
		Segment->SplineMeshes.Add(Entry);
		Segment->bCastShadow = false;
		Segment->BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		if (VirtualTexture)
		{
			// Only into the texture, which the landscape material reads (#156): the slabs no longer draw in the main pass.
			// Drawn after the landscape, which writes the same texture, so the road lies on top.
			Segment->RuntimeVirtualTextures.Add(VirtualTexture);
			Segment->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Exclusive;
			Segment->TranslucencySortPriority = 1;
		}
		else
		{
			Segment->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
		}
		Splines->GetSegments().Add(Segment);
		Start->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 0));
		End->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 1));
		++Added;
	}
	// The tangents along the road and through its junctions, then the meshes.
	for (ULandscapeSplineControlPoint* ControlPoint : Road)
	{
		ControlPoint->AutoCalcRotation(true);
	}
	for (ULandscapeSplineControlPoint* ControlPoint : Road)
	{
		ControlPoint->UpdateSplinePoints(true, true);
	}
	Landscape->RequestSplineLayerUpdate();
	return Added;
#else
	return 0;
#endif
}

bool URTSEditorScripting::ApplyLandscapeSplines(UObject* WorldContextObject)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	TActorIterator<ALandscape> LandscapeIt(World);
	ALandscape* Landscape = World && LandscapeIt ? *LandscapeIt : nullptr;
	ULandscapeSplinesComponent* Splines = Landscape ? Landscape->GetSplinesComponent() : nullptr;
	if (!Splines)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("ApplyLandscapeSplines: no landscape with splines"));
		return false;
	}
	if (!CanRender())
	{
		UE_LOG(LogWorldDivide, Error, TEXT("ApplyLandscapeSplines: the edit layers merge on the GPU; run with -AllowCommandletRendering"));
		return false;
	}
	FModuleManager::Get().LoadModule(TEXT("LandscapeEditor"));
	Landscape->RequestSplineLayerUpdate();
	if (!MergeEditLayers(*Landscape))
	{
		return false;
	}
	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->MarkModifiedPackagesAsDirty();
	}

	// The ground must have reached the road at the middle of each segment.
	constexpr float Tolerance = 50.f;
	const FTransform& ToWorld = Splines->GetComponentTransform();
	int32 Segments = 0;
	int32 OnRoad = 0;
	float WorstMiss = 0.f;
	for (const ULandscapeSplineSegment* Segment : Splines->GetSegments())
	{
		if (!Segment || Segment->GetPoints().IsEmpty())
		{
			continue;
		}
		const TArray<FLandscapeSplineInterpPoint>& Points = Segment->GetPoints();
		++Segments;
		const FVector Center = ToWorld.TransformPosition(Points[Points.Num() / 2].Center);
		const TOptional<float> Ground = Landscape->GetHeightAtLocation(Center);
		const float Miss = Ground ? FMath::Abs(*Ground - Center.Z) : TNumericLimits<float>::Max();
		WorstMiss = FMath::Max(WorstMiss, Miss);
		OnRoad += Miss < Tolerance ? 1 : 0;
	}
	UE_LOG(LogWorldDivide, Display, TEXT("ApplyLandscapeSplines: %d control points, %d segments, the ground on the road under %d of them (worst %.0f cm)"),
		Splines->GetControlPoints().Num(), Segments, OnRoad, WorstMiss);
	if (Segments == 0 || OnRoad < Segments)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("ApplyLandscapeSplines: the splines did not shape the ground under %d of %d segments"), Segments - OnRoad, Segments);
		return false;
	}
	return true;
#else
	return false;
#endif
}

bool URTSEditorScripting::UseExternalActors(UObject* WorldContextObject)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !World->PersistentLevel)
	{
		return false;
	}
	World->PersistentLevel->SetUseExternalActors(true);
	World->PersistentLevel->ConvertAllActorsToPackaging(true);
	return true;
#else
	return false;
#endif
}

bool URTSEditorScripting::BuildFont(UFont* Font, UFontFace* Face)
{
	if (!Font || !Face)
	{
		return false;
	}
	Font->FontCacheType = EFontCacheType::Runtime;
	FCompositeFont& CompositeFont = Font->GetMutableInternalCompositeFont();
	CompositeFont = FCompositeFont();
	FTypefaceEntry& Entry = CompositeFont.DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Regular")));
	Entry.Font = FFontData(Face);
	Font->LegacyFontName = TEXT("Regular");
	// Not PostEditChange: a font's change notification flushes the Slate font cache, which a commandlet does not have.
	Font->MarkPackageDirty();
	return true;
}

UFontFace* URTSEditorScripting::ImportFontFace(const FString& FileName, const FString& AssetPath)
{
#if WITH_EDITOR
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *FileName) || Data.IsEmpty())
	{
		return nullptr;
	}
	const FString Name = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	Package->FullyLoad();
	UFontFace* Face = FindObject<UFontFace>(Package, *Name);
	const bool bCreated = !Face;
	if (bCreated)
	{
		Face = NewObject<UFontFace>(Package, *Name, RF_Public | RF_Standalone);
	}
	Face->LoadingPolicy = EFontLoadingPolicy::Inline;
	Face->InitializeFromBulkData(FileName, EFontHinting::Default, Data.GetData(), Data.Num());
	Face->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Face);
	}
	return Face;
#else
	return nullptr;
#endif
}

bool URTSEditorScripting::LoadEditorModule(FName ModuleName)
{
	return FModuleManager::Get().LoadModule(ModuleName) != nullptr;
}
