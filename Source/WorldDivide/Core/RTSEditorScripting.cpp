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
	int32 QuadsPerSection, FVector Location, FVector Scale, UMaterialInterface* Material)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || Components <= 0 || QuadsPerSection <= 0)
	{
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

	// As the editor's New Landscape tool: the heights of the default edit layer and no paint layers.
	TMap<FGuid, TArray<uint16>> HeightsPerLayer;
	HeightsPerLayer.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> PaintLayersPerLayer;
	PaintLayersPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
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
