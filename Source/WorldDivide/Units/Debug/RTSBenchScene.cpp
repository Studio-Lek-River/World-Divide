#include "Units/Debug/RTSBenchScene.h"

#if !UE_BUILD_SHIPPING

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Units/RTSUnitShape.h"

namespace RTSBenchScene
{
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	FBox2D GetHullBox(const FRTSFootprint& Footprint)
	{
		const FVector2D Along = FVector2D(Footprint.Axis) * (Footprint.HalfSegment + Footprint.Radius);
		const FVector2D Across = FVector2D(-Footprint.Axis.Y, Footprint.Axis.X) * Footprint.Radius;
		const FVector2D Reach(FMath::Abs(Along.X) + FMath::Abs(Across.X), FMath::Abs(Along.Y) + FMath::Abs(Across.Y));
		return FBox2D(FVector2D(Footprint.Center) - Reach, FVector2D(Footprint.Center) + Reach);
	}

	TArray<FVector> MakeGrid(int32 Count, const FVector2D& Cell, const FVector& Centre, int32& OutRows, int32& OutColumns)
	{
		OutColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(Count * Cell.X / Cell.Y)));
		OutRows = FMath::DivideAndRoundUp(Count, OutColumns);
		const FVector2D Size((OutRows - 1) * Cell.X, (OutColumns - 1) * Cell.Y);
		TArray<FVector> Places;
		Places.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Places.Add(Centre + FVector(Index / OutColumns * Cell.X - Size.X / 2.f, Index % OutColumns * Cell.Y - Size.Y / 2.f, 0.f));
		}
		return Places;
	}

	FBox2D GetSolidsArea(TConstArrayView<FBox2D> Hulls, float Drive)
	{
		FBox2D Area(ForceInit);
		for (const FBox2D& Hull : Hulls)
		{
			Area += Hull;
		}
		Area += FBox2D(Area.Min + FVector2D(Drive, 0.f), Area.Max + FVector2D(Drive, 0.f));
		return Area.ExpandBy(Solids::AreaMargin);
	}

	TArray<FSolid> MakeSolids(const FBox2D& Area, TConstArrayView<FBox2D> Hulls)
	{
		using namespace Solids;
		FRandomStream Random(SolidsSeed);
		TArray<FSolid> Layout;
		const FVector2D AreaSize = Area.GetSize();
		const FBox2D Copse(FVector2D(Area.Min.X, Area.Max.Y - AreaSize.Y * CopseNorth), FVector2D(Area.Min.X + AreaSize.X * CopseWest, Area.Max.Y));
		auto SquareMetres = [](const FBox2D& Box) { return Box.GetArea() / 1.0e4; };

		auto Fits = [&Area, &Hulls, &Layout](const FSolid& Solid)
		{
			const FBox2D Box = Solid.GetBox();
			if (!Area.IsInside(Box.Min) || !Area.IsInside(Box.Max))
			{
				return false;
			}
			const FBox2D Clear = Box.ExpandBy(KeepClear);
			for (const FBox2D& Hull : Hulls)
			{
				if (Clear.Intersect(Hull))
				{
					return false;
				}
			}
			const FBox2D Spaced = Box.ExpandBy(Spacing);
			for (const FSolid& Other : Layout)
			{
				if (Spaced.Intersect(Other.GetBox()))
				{
					return false;
				}
			}
			return true;
		};

		// The longest first, while there is the most room for them; each kind tries its count's worth of places, so a grid
		// that leaves less room gets fewer solids, never solids on its hulls.
		auto Place = [&](ESolidKind Kind, const FBox2D& Zone, float PerM2, TFunctionRef<FVector()> MakeSize)
		{
			const int32 Count = FMath::RoundToInt(SquareMetres(Zone) * PerM2);
			int32 Placed = 0;
			for (int32 Attempt = 0; Attempt < Count * Attempts && Placed < Count; ++Attempt)
			{
				FSolid Solid;
				Solid.Kind = Kind;
				Solid.Size = MakeSize();
				Solid.Center = FVector(Random.FRandRange(Zone.Min.X, Zone.Max.X), Random.FRandRange(Zone.Min.Y, Zone.Max.Y), 0.f);
				if (Fits(Solid))
				{
					Layout.Add(Solid);
					++Placed;
				}
			}
		};

		// Walls take turns along the drive, where they fit a gap of the grid and are passed, and across it, where they are met.
		bool bAlong = true;
		Place(ESolidKind::Wall, Area, WallsPerM2, [&Random, &bAlong]()
		{
			const float Length = Random.FRandRange(WallLength.X, WallLength.Y);
			const FVector Size = bAlong ? FVector(Length, WallThickness, WallHeight) : FVector(WallThickness, Length, WallHeight);
			bAlong = !bAlong;
			return Size;
		});
		Place(ESolidKind::Boulder, Area, BouldersPerM2, [&Random]()
		{
			return FVector(Random.FRandRange(BoulderSide.X, BoulderSide.Y), Random.FRandRange(BoulderSide.X, BoulderSide.Y),
				Random.FRandRange(BoulderHeight.X, BoulderHeight.Y));
		});
		Place(ESolidKind::Trunk, Copse, TrunksPerM2, [&Random]()
		{
			const float Diameter = Random.FRandRange(TrunkDiameter.X, TrunkDiameter.Y);
			return FVector(Diameter, Diameter, TrunkHeight);
		});
		return Layout;
	}

	TArray<TWeakObjectPtr<AActor>> SpawnSolids(UWorld& World, TConstArrayView<FSolid> Layout, float GroundZ)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderPath);
		TArray<TWeakObjectPtr<AActor>> Spawned;
		for (const FSolid& Solid : Layout)
		{
			// The engine's basic shapes are 1 m across, centred on their pivot.
			const FTransform Transform(FRotator::ZeroRotator, FVector(Solid.Center.X, Solid.Center.Y, GroundZ + Solid.Size.Z / 2.f), Solid.Size / 100.f);
			AStaticMeshActor* Actor = World.SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
			if (!Actor)
			{
				continue;
			}
			UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Solid.Kind == ESolidKind::Trunk ? Cylinder : Cube);
			Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Component->SetCanEverAffectNavigation(false);
			Actor->FinishSpawning(Transform);
			Spawned.Add(Actor);
		}
		return Spawned;
	}
}

#endif
