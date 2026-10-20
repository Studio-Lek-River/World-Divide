#include "Units/RTSGround.h"

#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace RTSGround
{
	bool Trace(const UWorld& World, const FVector& Location, float Up, float Down, float& OutGroundZ, FVector* OutNormal)
	{
		// Buildings and supply sources are world-dynamic and units are pawns, so a unit standing on a new site still finds the
		// ground under it rather than the roof.
		static const FName TraceTag(TEXT("RTSGround"));
		FHitResult Hit;
		const bool bHit = World.LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, Up), Location - FVector(0.f, 0.f, Down),
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(TraceTag));
		if (!bHit)
		{
			return false;
		}
		OutGroundZ = Hit.ImpactPoint.Z;
		if (OutNormal)
		{
			*OutNormal = Hit.ImpactNormal;
		}
		return true;
	}

	bool TraceSurface(const UWorld& World, const FVector& Location, float Up, float Down, EPhysicalSurface& OutSurface)
	{
		static const FName TraceTag(TEXT("RTSGroundSurface"));
		FCollisionQueryParams Params(TraceTag);
		Params.bReturnPhysicalMaterial = true;
		FHitResult Hit;
		if (!World.LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, Up), Location - FVector(0.f, 0.f, Down),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return false;
		}
		const UPhysicalMaterial* Material = Hit.PhysMaterial.Get();
		OutSurface = Material ? UPhysicalMaterial::DetermineSurfaceType(Material) : SurfaceType_Default;
		return true;
	}
}
