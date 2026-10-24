#include "Data/RTSKitPieceData.h"

#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Units/RTSPrimitiveData.h"

namespace
{
	const FName FixturesGroup = TEXT("Fixtures");

	TAutoConsoleVariable<FString> CVarKitMountOnly(
		TEXT("rts.Debug.KitMountOnly"), TEXT(""),
		TEXT("Mounts only the kit piece of the mount with this name on every building, to picture one lamp alone; empty mounts all."));
}

bool FRTSKitPart::IsWarning() const
{
	return Group == RTSKit::WarningGroup || Behaviour != ERTSKitLightBehaviour::Steady;
}

RTSKit::FLightBudget RTSKit::CountLights(const URTSBuildingData& Data)
{
	FLightBudget Count{ 0, 0 };
	for (const FRTSBuildingLight& Light : Data.Lights)
	{
		++(Light.Group == URTSBuildingData::GarageBeaconsGroup ? Count.Warning : Count.Steady);
	}
	for (const FRTSKitPart& Part : Expand(Data).PartInfo)
	{
		if (Part.bHasLight)
		{
			++(Part.IsWarning() ? Count.Warning : Count.Steady);
		}
	}
	return Count;
}

bool RTSKit::IsLensLit(float Fault, float Flash, double Time)
{
	// LENS_HLSL's step(0.35, ...) and step(0.5, ...), in the same game time the material reads.
	if (Fault > 0.f && FMath::Frac(Time * 2.3 + Fault + 0.5 * FMath::Sin(Time * 17.0 + Fault * 40.0)) < 0.35)
	{
		return false;
	}
	return Flash <= 0.f || FMath::Frac(Time + Flash) >= 0.5;
}

RTSKit::FSpin RTSKit::GetSpin(float On, float Off, double Time)
{
	// SPIN_HLSL: spin-up at constant acceleration over SpinUpSeconds, then RotateRate; once off, the speed it had falls to
	// nothing over CoastSeconds.
	const bool bOff = Off >= On;
	const double Since = FMath::Max((bOff ? FMath::Min(Time, static_cast<double>(Off)) : Time) - On, 0.0);
	const double Up = FMath::Min(Since, static_cast<double>(SpinUpSeconds));
	FSpin Spin;
	Spin.Turns = RotateRate * (0.5 * Up * Up / SpinUpSeconds + Since - Up);
	Spin.Lamp = FMath::Clamp(static_cast<float>(Since / WarmSeconds), 0.f, 1.f);
	if (bOff)
	{
		const double Coast = FMath::Clamp(Time - Off, 0.0, static_cast<double>(CoastSeconds));
		Spin.Turns += RotateRate * Up / SpinUpSeconds * (Coast - 0.5 * Coast * Coast / CoastSeconds);
		Spin.Lamp *= FMath::Clamp(1.f - static_cast<float>((Time - Off) / CoolSeconds), 0.f, 1.f);
	}
	return Spin;
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
		const FString Only = CVarKitMountOnly.GetValueOnAnyThread();
		if (!Only.IsEmpty() && Mount.Name != FName(*Only))
		{
			continue;
		}
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
		FRTSKitPart& Info = Result.PartInfo.AddDefaulted_GetRef();
		Info.Name = Name;
		Info.Piece = Piece.Id;
		Info.Brightness = Mount.Brightness;
		Info.Group = Group;
		Info.bEmergency = Mount.Emergency == ERTSKitOverride::Default ? Piece.bEmergencyDefault : Mount.Emergency == ERTSKitOverride::On;
		Info.FaultResponse = Mount.bOverrideFaultResponse ? Mount.FaultResponse : Piece.FaultResponse;
		Info.bHasLight = Piece.bHasLight;
		Info.Behaviour = Piece.Light.Behaviour;
		Info.Priority = Piece.Light.Priority;
		Info.MaxDrawDistance = Piece.Light.MaxDrawDistance;
		Info.IndicatorColor = Mount.IndicatorColor;
		Info.bBlink = Piece.bBlink;
		Info.MountIndex = Index;

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
			Light.Color = FLinearColor::MakeFromColorTemperature(Kit.Kelvin) * Kit.Filter;
			Light.Candelas = Kit.Candelas * Mount.Brightness;
			Light.Radius = Kit.Radius;
			Light.InnerConeAngle = Kit.InnerConeAngle;
			Light.OuterConeAngle = Kit.OuterConeAngle;
			Light.SourceWidth = Kit.SourceWidth;
			Light.SourceHeight = Kit.SourceHeight;
			Light.Behaviour = Kit.Behaviour;
			Light.Profile = Kit.Profile;
			Light.SourceRadius = Kit.SourceRadius;
			Light.SourceLength = Kit.SourceLength;
			if (Piece.BeamMesh && Piece.BeamStrength > 0.f)
			{
				// In the mount's frame, its scale included (a mount may undo its building's MeshScale).
				Result.Glows.Add({ Result.PartInfo.Num() - 1, Piece.BeamMesh, FTransform(Kit.Rotation, Kit.Location) * Frame,
					Light.Color * Piece.BeamStrength * Mount.Brightness, /*bBeam*/ true });
			}
		}
		if (Piece.GlareMesh && Piece.GlareStrength > 0.f && Piece.LensBounds.IsValid)
		{
			// A lamp that shines one way hangs its halo half out of its lens along its light, clear of the housing round the
			// lens (#454); the others' halo stands in their lens's middle, a turning beacon's too (#459): its beam turns round it.
			const bool bAimed = Piece.bHasLight && Piece.Light.Type != ERTSBuildingLightType::Point
				&& Piece.Light.Behaviour != ERTSKitLightBehaviour::Rotate;
			const FVector Centre = bAimed ? Piece.Light.Location + Piece.Light.Rotation.Vector() * (0.5f * Piece.GlareRadius)
				: Piece.LensBounds.GetCenter();
			// A tube's halo stretches along its lens's longest side (#456); a pair stands on its two domes along it (#460).
			const FVector Size = Piece.LensBounds.GetSize();
			const int32 Longest = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : Size.Y >= Size.Z ? 1 : 2;
			FVector Scale(Piece.GlareRadius);
			if (Piece.GlareLength > 0.f)
			{
				Scale[Longest] = FMath::Max(Piece.GlareLength, Piece.GlareRadius);
			}
			const FLinearColor Color = (Piece.bHasLight ? FLinearColor::MakeFromColorTemperature(Piece.Light.Kelvin) * Piece.Light.Filter
				: Mount.IndicatorColor) * Piece.GlareTint * Piece.GlareStrength * Mount.Brightness;
			const float Shift = Piece.bBlink ? 0.f : -1.f;
			const float Point = FMath::DegreesToRadians(Piece.GlarePoint);
			if (Piece.GlarePair > 0.f)
			{
				FVector Along = FVector::ZeroVector;
				Along[Longest] = Piece.GlarePair;
				// The far one is the lens's U >= 0.5 half, which blinks half a cycle later on an alternating lens.
				Result.Glows.Add({ Result.PartInfo.Num() - 1, Piece.GlareMesh, FTransform(FQuat::Identity, Centre - Along, Scale) * Frame,
					Color, /*bBeam*/ false, Shift, Point });
				Result.Glows.Add({ Result.PartInfo.Num() - 1, Piece.GlareMesh, FTransform(FQuat::Identity, Centre + Along, Scale) * Frame,
					Color, /*bBeam*/ false, Piece.bBlink && Piece.bAlternate ? 0.5f : Shift, Point });
			}
			else
			{
				Result.Glows.Add({ Result.PartInfo.Num() - 1, Piece.GlareMesh, FTransform(FQuat::Identity, Centre, Scale) * Frame,
					Color, /*bBeam*/ false, Shift, Point });
			}
		}

		float Lens[RTSKitLensData::Floats] = {};
		Lens[RTSKitLensData::Level] = Mount.Brightness;
		// A battery-backed lens stays on when the building's Fixtures go out (URTSBuildingStateComponent).
		Lens[RTSKitLensData::Group] = Group == FixturesGroup && !Info.bEmergency ? 1.f : 0.f;
		Lens[RTSKitLensData::Color + 0] = Mount.IndicatorColor.R;
		Lens[RTSKitLensData::Color + 1] = Mount.IndicatorColor.G;
		Lens[RTSKitLensData::Color + 2] = Mount.IndicatorColor.B;
		Result.InstanceData.Append(Lens, RTSKitLensData::Floats);
	}
	return Result;
}
