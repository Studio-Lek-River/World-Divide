#include "Units/Debug/RTSUnitLOD.h"

#if !UE_BUILD_SHIPPING

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "StaticMeshResources.h"
#include "UnrealClient.h"

int32 RTSUnitLOD::ChooseLOD(const UStaticMesh& Mesh, double Radius, double Distance, const FMatrix& Projection)
{
	const FStaticMeshRenderData* RenderData = Mesh.GetRenderData();
	if (!RenderData)
	{
		return 0;
	}
	static const IConsoleVariable* DistanceScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.StaticMeshLODDistanceScale"));
	const float Scale = DistanceScale ? DistanceScale->GetFloat() : 1.f;
	const float ScreenSize = ComputeBoundsScreenSize(FVector4(0.0, 0.0, Distance, 1.0), Radius, FVector4(0.0, 0.0, 0.0, 1.0), Projection);
	// The renderer looks from the last LOD down for the first whose screen size still covers the bounds.
	for (int32 Index = Mesh.GetNumLODs() - 1; Index > 0; --Index)
	{
		if (RenderData->ScreenSize[Index].GetValue() * Scale >= ScreenSize)
		{
			return Index;
		}
	}
	return 0;
}

const UStaticMeshComponent* RTSUnitLOD::FindHull(const AActor& Actor)
{
	TInlineComponentArray<UStaticMeshComponent*> Components(&Actor);
	for (const UStaticMeshComponent* Component : Components)
	{
		if (Component && Component->GetStaticMesh() && !Component->IsA<UInstancedStaticMeshComponent>()
			&& (Component == Actor.GetRootComponent() || Component->GetAttachParent() == Actor.GetRootComponent()))
		{
			return Component;
		}
	}
	return nullptr;
}

int32 RTSUnitLOD::FPartLODs::Total() const
{
	int32 Sum = 0;
	for (const int32 Count : Counts)
	{
		Sum += Count;
	}
	return Sum;
}

bool RTSUnitLOD::FPartLODs::IsCoherent() const
{
	return Counts.IsValidIndex(RootLOD) && Counts[RootLOD] == Total();
}

FString RTSUnitLOD::FPartLODs::ToString() const
{
	TArray<FString> Parts;
	for (int32 Index = 0; Index < Counts.Num(); ++Index)
	{
		if (Counts[Index] > 0)
		{
			Parts.Add(FString::Printf(TEXT("LOD%d x %d"), Index, Counts[Index]));
		}
	}
	FString Text = FString::Join(Parts, TEXT(", "));
	if (!Outliers.IsEmpty())
	{
		Text += FString::Printf(TEXT(" [%s]"), *FString::Join(Outliers, TEXT(", ")));
	}
	return Text;
}

RTSUnitLOD::FPartLODs RTSUnitLOD::GetPartLODs(const AActor& Actor, const FVector& ViewOrigin, const FMatrix& Projection)
{
	FPartLODs Result;
	TArray<TTuple<const UStaticMesh*, int32, double>> Pieces;
	auto Count = [&Result, &Pieces](const UStaticMesh* Mesh, int32 LOD, double Distance)
	{
		if (Result.Counts.Num() <= LOD)
		{
			Result.Counts.SetNumZeroed(LOD + 1);
		}
		++Result.Counts[LOD];
		Pieces.Emplace(Mesh, LOD, Distance);
	};
	const UStaticMeshComponent* Hull = FindHull(Actor);
	TInlineComponentArray<UStaticMeshComponent*> Components(&Actor);
	for (const UStaticMeshComponent* Component : Components)
	{
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		if (!Mesh || !Component->IsVisible())
		{
			continue;
		}
		if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
			for (int32 Instance = 0; Instance < Instanced->GetInstanceCount(); ++Instance)
			{
				// The GPU measures an instance by its mesh's box, whichever way the instance turns (a wheel rolls).
				FTransform Transform;
				Instanced->GetInstanceTransform(Instance, Transform, /*bWorldSpace*/ true);
				const double Distance = FVector::Dist(Transform.TransformPosition(MeshBounds.Origin), ViewOrigin);
				const double Radius = MeshBounds.BoxExtent.Size() * Transform.GetMaximumAxisScale();
				Count(Mesh, ChooseLOD(*Mesh, Radius, Distance, Projection), Distance);
			}
			continue;
		}
		const FBoxSphereBounds& Bounds = Component->Bounds;
		const double Distance = FVector::Dist(Bounds.Origin, ViewOrigin);
		const int32 LOD = ChooseLOD(*Mesh, Bounds.SphereRadius, Distance, Projection);
		Count(Mesh, LOD, Distance);
		if (Component == Hull)
		{
			Result.RootLOD = LOD;
		}
	}
	for (const TTuple<const UStaticMesh*, int32, double>& Piece : Pieces)
	{
		if (Piece.Get<1>() != Result.RootLOD)
		{
			Result.Outliers.Add(FString::Printf(TEXT("%s LOD%d at %.1f m"), *Piece.Get<0>()->GetName(), Piece.Get<1>(), Piece.Get<2>() / 100.0));
		}
	}
	return Result;
}

bool RTSUnitLOD::GetPlayerView(const APlayerController& Controller, FVector& OutOrigin, FMatrix& OutProjection)
{
	const ULocalPlayer* Player = Controller.GetLocalPlayer();
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport)
	{
		return false;
	}
	FSceneViewProjectionData Projection;
	if (!Player->GetProjectionData(Player->ViewportClient->Viewport, Projection))
	{
		return false;
	}
	OutOrigin = Projection.ViewOrigin;
	OutProjection = Projection.ProjectionMatrix;
	return true;
}

#endif
