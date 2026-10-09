#include "Core/RTSEditorScripting.h"

#include "Components/SceneCaptureComponent2D.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
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
	// Spawns the navmesh if the world has none yet, rebuilds every tile and blocks until done.
	NavSystem->Build();
	const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance());
	return NavMesh ? NavMesh->GetNavMeshTilesCount() : 0;
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

	// A commandlet compiles shaders and streams textures lazily; the image must not show their placeholders.
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
