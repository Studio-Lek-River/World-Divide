#include "Data/RTSKitPieceData.h"

#include "Engine/StaticMesh.h"
#include "Units/RTSPrimitiveData.h"

namespace
{
	const FName FixturesGroup = TEXT("Fixtures");
}

bool RTSKit::GetMountFrame(const URTSBuildingData& Data, const FRTSKitMount& Mount, FTransform& OutFrame, FString& OutError)
{
	if (!Mount.Piece || !Mount.Piece->Mesh)
	{
		OutError = FString::Printf(TEXT("mount %s has no piece or its piece no mesh"), *Mount.Name.ToString());
		return false;
	}
	FTransform Socket = FTransform::Identity;
	if (!Mount.Socket.IsNone())
	{
		const FVector* Location = Data.FindSocket(Mount.Socket);
		if (!Location)
		{
			OutError = FString::Printf(TEXT("mount %s: %s has no socket %s"), *Mount.Name.ToString(), *Data.GetName(),
				*Mount.Socket.ToString());
			return false;
		}
		Socket.SetLocation(*Location);
	}
	FTransform Point = FTransform::Identity;
	const TMap<FName, FTransform>& Points = Mount.Piece->MountPoints;
	if (!Mount.MountPoint.IsNone())
	{
		const FTransform* Found = Points.Find(Mount.MountPoint);
		if (!Found)
		{
			OutError = FString::Printf(TEXT("mount %s: %s has no mount point %s"), *Mount.Name.ToString(), *Mount.Piece->GetName(),
				*Mount.MountPoint.ToString());
			return false;
		}
		Point = *Found;
	}
	else if (!Points.IsEmpty())
	{
		Point = Points.CreateConstIterator()->Value;
	}
	// The piece's mount point meets the mount's frame: its origin stands at the point's inverse in that frame.
	OutFrame = Point.Inverse() * Mount.Transform * Socket;
	return true;
}

FRTSKitExpansion RTSKit::Expand(const URTSBuildingData& Data)
{
	FRTSKitExpansion Result;
	for (int32 Index = 0; Index < Data.Mounts.Num(); ++Index)
	{
		const FRTSKitMount& Mount = Data.Mounts[Index];
		FTransform Frame;
		FString Error;
		if (!GetMountFrame(Data, Mount, Frame, Error))
		{
			Result.Errors.Add(MoveTemp(Error));
			continue;
		}
		const URTSKitPieceData& Piece = *Mount.Piece;
		const FName Name = Mount.Name.IsNone() ? FName(TEXT("Kit"), Index + 1) : Mount.Name;

		FRTSBuildingPart& Part = Result.Parts.AddDefaulted_GetRef();
		Part.Name = Name;
		Part.Mesh = Piece.Mesh;
		Part.Transform = Frame;
		Part.bCastShadow = Piece.CastsShadow();
		Part.WPODisableDistance = Piece.WPODisableDistance;

		const FName Group = !Mount.Group.IsNone() ? Mount.Group : Piece.bHasLight ? Piece.Light.Group : NAME_None;
		if (Piece.bHasLight)
		{
			const FRTSKitLight& Kit = Piece.Light;
			FRTSBuildingLight& Light = Result.Lights.AddDefaulted_GetRef();
			Light.Name = Name;
			Light.Group = Group;
			Light.Fixture = Name;
			Light.Type = Kit.Type;
			Light.Location = Frame.TransformPosition(Kit.Location);
			Light.Rotation = (Frame.GetRotation() * Kit.Rotation.Quaternion()).Rotator();
			Light.Color = FLinearColor::MakeFromColorTemperature(Kit.Kelvin);
			Light.Candelas = Kit.Candelas;
			Light.Radius = Kit.Radius;
			Light.InnerConeAngle = Kit.InnerConeAngle;
			Light.OuterConeAngle = Kit.OuterConeAngle;
			Light.SourceWidth = Kit.SourceWidth;
			Light.SourceHeight = Kit.SourceHeight;
			Light.Behaviour = Kit.Behaviour;
		}

		float Lens[RTSKitLensData::Floats] = {};
		Lens[RTSKitLensData::Level] = 1.f;
		Lens[RTSKitLensData::Group] = Group == FixturesGroup ? 1.f : 0.f;
		Lens[RTSKitLensData::Color + 0] = Mount.IndicatorColor.R;
		Lens[RTSKitLensData::Color + 1] = Mount.IndicatorColor.G;
		Lens[RTSKitLensData::Color + 2] = Mount.IndicatorColor.B;
		Result.InstanceData.Append(Lens, RTSKitLensData::Floats);
	}
	return Result;
}
