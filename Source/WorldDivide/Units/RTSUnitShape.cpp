#include "Units/RTSUnitShape.h"

#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"

namespace
{
	FBox Scale(const FBox& Box, const FVector& Scale)
	{
		// Mesh scales are positive, so the corners stay in order.
		return FBox(Box.Min * Scale, Box.Max * Scale);
	}

	/** The turret mesh and the parts on it, in the hull mesh's space at the turret's rest yaw. */
	FBox GetTurretBox(const URTSUnitData& Data)
	{
		FBox Box(ForceInit);
		if (!Data.TurretMesh)
		{
			return Box;
		}
		const FTransform Turret(Data.TurretOffset);
		Box += Data.TurretMesh->GetBoundingBox().TransformBy(Turret);
		for (const FRTSVehiclePart& Part : Data.Parts)
		{
			if (Part.Mesh && Part.MountedOn == ERTSVehiclePartMount::Turret)
			{
				Box += Part.Mesh->GetBoundingBox().TransformBy(FTransform(Part.Rotation, Part.Offset) * Turret);
			}
		}
		return Box;
	}
}

FVector FRTSFootprint::GetClosestSegmentPoint(const FVector& From) const
{
	const float Along = FMath::Clamp(FVector::DotProduct(From - Center, Axis), -HalfSegment, HalfSegment);
	return Center + Axis * Along;
}

FVector FRTSFootprint::GetClosestEdgePoint(const FVector& From) const
{
	const FVector OnSegment = GetClosestSegmentPoint(From);
	return OnSegment + (From - OnSegment).GetSafeNormal2D() * Radius;
}

FBox RTSUnitShape::GetHullBox(const URTSUnitData& Data)
{
	FBox Box(ForceInit);
	if (Data.Mesh)
	{
		Box += Data.Mesh->GetBoundingBox();
	}
	for (const FRTSVehiclePart& Part : Data.Parts)
	{
		if (Part.Mesh && Part.MountedOn == ERTSVehiclePartMount::Hull)
		{
			Box += Part.Mesh->GetBoundingBox().TransformBy(FTransform(Part.Rotation, Part.Offset));
		}
	}
	return Box;
}

FRTSUnitShape RTSUnitShape::FromData(const URTSUnitData& Data)
{
	FRTSUnitShape Shape;
	FBox Hull = GetHullBox(Data);
	if (!Hull.IsValid)
	{
		const float Radius = Data.SelectionRadius;
		Shape.Hull = FBox(FVector(-Radius), FVector(Radius));
		Shape.Selection = Shape.Hull;
		if (const FBox Turret = GetTurretBox(Data); Turret.IsValid)
		{
			Shape.Selection += Scale(Turret, Data.MeshScale);
		}
		return Shape;
	}

	Hull = Scale(Hull, Data.MeshScale);
	Shape.MeshOffset = -Hull.GetCenter();
	Shape.Hull = Hull.ShiftBy(Shape.MeshOffset);
	Shape.Selection = Shape.Hull;
	if (const FBox Turret = GetTurretBox(Data); Turret.IsValid)
	{
		Shape.Selection += Scale(Turret, Data.MeshScale).ShiftBy(Shape.MeshOffset);
	}
	// Brackets stand on the ground, even under a turret that overhangs the hull's bottom.
	Shape.Selection.Min.Z = Shape.Hull.Min.Z;
	return Shape;
}

FRTSFootprint RTSUnitShape::MakeFootprint(const FRTSUnitShape& Shape, const FTransform& Transform)
{
	const float HalfLength = Shape.GetHalfLength();
	const float HalfWidth = Shape.GetHalfWidth();
	const bool bLong = HalfLength >= HalfWidth;

	FRTSFootprint Footprint;
	Footprint.Center = Transform.GetLocation();
	Footprint.Axis = Transform.TransformVectorNoScale(bLong ? FVector::ForwardVector : FVector::RightVector).GetSafeNormal2D();
	Footprint.HalfSegment = FMath::Abs(HalfLength - HalfWidth);
	Footprint.Radius = FMath::Min(HalfLength, HalfWidth);
	return Footprint;
}
