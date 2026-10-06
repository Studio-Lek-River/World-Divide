#include "Buildings/RTSPlacementGhost.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FName ColorParameter(TEXT("Color"));
	const FLinearColor ValidColor(0.1f, 1.f, 0.2f);
	const FLinearColor InvalidColor(1.f, 0.1f, 0.05f);
}

ARTSPlacementGhost::ARTSPlacementGhost()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);

	bReplicates = false;
	SetActorEnableCollision(false);
}

void ARTSPlacementGhost::SetBuildingData(const URTSBuildingData& Data, UMaterialInterface* Material)
{
	float BaseOffset = 0.f;
	float Height = 0.f;
	RTSBuildPlacement::GetMeshPlacement(Data, BaseOffset, Height);

	Mesh->SetStaticMesh(Data.Mesh);
	Mesh->SetRelativeScale3D(Data.MeshScale);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, BaseOffset));

	MaterialInstance = Material
		? UMaterialInstanceDynamic::Create(Material, this)
		: Mesh->CreateDynamicMaterialInstance(0);
	if (MaterialInstance)
	{
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Mesh->SetMaterial(Slot, MaterialInstance);
		}
	}
	SetValid(false);
}

void ARTSPlacementGhost::SetValid(bool bValid)
{
	if (MaterialInstance)
	{
		MaterialInstance->SetVectorParameterValue(ColorParameter, bValid ? ValidColor : InvalidColor);
	}
}
