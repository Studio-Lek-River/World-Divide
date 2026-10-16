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

	/** The barrel of Data's turret, with the hull mesh standing at MeshOffset; unset without a turret. */
	TOptional<FRTSBarrel> GetBarrel(const URTSUnitData& Data, const FVector& MeshOffset)
	{
		if (!Data.TurretMesh)
		{
			return {};
		}
		const FVector Scale3D = Data.MeshScale;
		FRTSBarrel Barrel;
		Barrel.TurretPivot = MeshOffset + Data.TurretOffset * Scale3D;
		if (Data.GunMesh)
		{
			Barrel.Start = Data.GunOffset * Scale3D;
			Barrel.Muzzle = Data.GunMuzzleOffset * Scale3D;
			Barrel.bElevates = true;
			// The gun's thinner side: a mantlet at the trunnion makes it thicker than the tube, which errs on the safe side.
			const FVector Extent = Data.GunMesh->GetBoundingBox().GetExtent() * Scale3D;
			Barrel.Radius = FMath::Min(Extent.Y, Extent.Z);
		}
		else
		{
			Barrel.Start = FVector(0.f, 0.f, Data.MuzzleOffset.Z) * Scale3D;
			Barrel.Muzzle = Data.MuzzleOffset * Scale3D - Barrel.Start;
		}
		return Barrel;
	}
}

float FRTSBarrel::GetReach() const
{
	// The gun turns about Start, so its muzzle stays within its length of it.
	return FVector2D(TurretPivot).Size() + FVector2D(Start).Size() + Muzzle.Size() + Radius;
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
		Shape.Barrel = GetBarrel(Data, Shape.MeshOffset);
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
	Shape.Barrel = GetBarrel(Data, Shape.MeshOffset);
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

void RTSUnitShape::GetClosestSegmentPoints(const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1,
	FVector2D& OutA, FVector2D& OutB)
{
	// Ericson, Real-Time Collision Detection, 5.1.9: S along A and T along B, each clamped to its segment.
	const FVector2D DA = A1 - A0;
	const FVector2D DB = B1 - B0;
	const FVector2D R = A0 - B0;
	const float LengthA = DA.SizeSquared();
	const float LengthB = DB.SizeSquared();
	const float F = FVector2D::DotProduct(DB, R);
	float S = 0.f;
	float T = 0.f;
	if (LengthA <= UE_SMALL_NUMBER)
	{
		T = LengthB <= UE_SMALL_NUMBER ? 0.f : FMath::Clamp(F / LengthB, 0.f, 1.f);
	}
	else
	{
		const float C = FVector2D::DotProduct(DA, R);
		if (LengthB <= UE_SMALL_NUMBER)
		{
			S = FMath::Clamp(-C / LengthA, 0.f, 1.f);
		}
		else
		{
			const float B = FVector2D::DotProduct(DA, DB);
			const float Denominator = LengthA * LengthB - B * B;
			// Parallel segments: any S does; T then finds the matching point.
			S = Denominator > UE_SMALL_NUMBER ? FMath::Clamp((B * F - C * LengthB) / Denominator, 0.f, 1.f) : 0.f;
			T = (B * S + F) / LengthB;
			if (T < 0.f)
			{
				T = 0.f;
				S = FMath::Clamp(-C / LengthA, 0.f, 1.f);
			}
			else if (T > 1.f)
			{
				T = 1.f;
				S = FMath::Clamp((B - C) / LengthA, 0.f, 1.f);
			}
		}
	}
	OutA = A0 + DA * S;
	OutB = B0 + DB * T;
}

float RTSUnitShape::GetGap(const FRTSFootprint& A, const FRTSFootprint& B)
{
	const FVector2D CenterA(A.Center);
	const FVector2D AxisA = FVector2D(A.Axis) * A.HalfSegment;
	const FVector2D CenterB(B.Center);
	const FVector2D AxisB = FVector2D(B.Axis) * B.HalfSegment;
	FVector2D OnA;
	FVector2D OnB;
	GetClosestSegmentPoints(CenterA - AxisA, CenterA + AxisA, CenterB - AxisB, CenterB + AxisB, OnA, OnB);
	return FVector2D::Distance(OnA, OnB) - A.Radius - B.Radius;
}
