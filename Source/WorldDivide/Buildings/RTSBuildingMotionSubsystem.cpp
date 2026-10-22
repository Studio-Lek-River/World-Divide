#include "Buildings/RTSBuildingMotionSubsystem.h"

#include "Buildings/RTSBuilding.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSNearestToView.h"

namespace
{
	TAutoConsoleVariable<bool> CVarBuildingMotion(
		TEXT("rts.Fx.BuildingMotion"), true,
		TEXT("Buildings turn their moving parts, the HQ's fans, radar and wing dish (#351); off, they stand at rest where they are."));

	/** A building counts as drawn when one of its parts was on screen within this many seconds (the engine's own default). */
	constexpr float RecentlyRenderedSeconds = 0.2f;
}

void FRTSBuildingMotionTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->UpdateAll(DeltaTime);
	}
}

FString FRTSBuildingMotionTickFunction::DiagnosticMessage()
{
	return TEXT("URTSBuildingMotionSubsystem::UpdateAll");
}

void URTSBuildingMotionSubsystem::Register(ARTSBuilding& Building)
{
	Unregister(Building);
	const UWorld* World = GetWorld();
	const URTSBuildingData* Data = Building.GetBuildingData();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Data)
	{
		return;
	}

	// Each building's own rates and phases, from where it stands, so every machine picks the same ones.
	const FVector Where = Building.GetActorLocation();
	const uint32 BuildingSeed = GetTypeHash(FIntVector(FMath::RoundToInt(Where.X), FMath::RoundToInt(Where.Y), FMath::RoundToInt(Where.Z)));

	FRTSMotionEntry Entry;
	Entry.Building = &Building;
	TArray<FName> Names;
	for (const FRTSBuildingPart& PartData : Data->Parts)
	{
		UStaticMeshComponent* Component = nullptr;
		int32 Instance = INDEX_NONE;
		// Moving parts are drawn on their own (ARTSBuilding::ApplyParts), never as an instance.
		if (!PartData.Motion.IsAnimated() || !Building.FindPart(PartData.Name, Component, Instance) || Instance != INDEX_NONE)
		{
			continue;
		}
		FRTSMovingPart& Part = Entry.Parts.AddDefaulted_GetRef();
		Part.Component = Component;
		Part.Rest = PartData.Transform;
		Part.Motion = PartData.Motion;
		Part.MaxDrawDistance = PartData.MaxDrawDistance;
		FRandomStream Stream(HashCombine(BuildingSeed, GetTypeHash(PartData.Name)));
		Part.RateScale = 1.f + PartData.Motion.RateJitter * (2.f * Stream.FRand() - 1.f);
		const float Start = 360.f * Stream.FRand();
		Part.Spin = FRotator(PartData.Motion.SpinRate.Pitch != 0.f ? Start : 0.f, PartData.Motion.SpinRate.Yaw != 0.f ? Start : 0.f,
			PartData.Motion.SpinRate.Roll != 0.f ? Start : 0.f);
		Part.SweepPhase = Stream.FRand();
		Advance(Part, 0.f, 0.f);
		Names.Add(PartData.Name);
	}
	if (Entry.Parts.IsEmpty())
	{
		return;
	}
	for (FRTSMovingPart& Part : Entry.Parts)
	{
		Part.Follow = Part.Motion.FollowPart.IsNone() ? INDEX_NONE : Names.IndexOfByKey(Part.Motion.FollowPart);
	}
	Entries.Add(MoveTemp(Entry));
}

void URTSBuildingMotionSubsystem::Unregister(ARTSBuilding& Building)
{
	const int32 Index = Entries.IndexOfByPredicate([&Building](const FRTSMotionEntry& Entry) { return Entry.Building.Get() == &Building; });
	if (Index != INDEX_NONE)
	{
		Entries.RemoveAtSwap(Index);
	}
}

int32 URTSBuildingMotionSubsystem::GetMovingPartCount(const ARTSBuilding& Building) const
{
	const FRTSMotionEntry* Entry = Entries.FindByPredicate([&Building](const FRTSMotionEntry& E) { return E.Building.Get() == &Building; });
	return Entry ? Entry->Parts.Num() : 0;
}

void URTSBuildingMotionSubsystem::Advance(FRTSMovingPart& Part, float DeltaSeconds, float Scale)
{
	const FRTSPartMotion& Motion = Part.Motion;
	const float Step = DeltaSeconds * Scale * Part.RateScale;
	Part.Spin = (Part.Spin + Motion.SpinRate * Step).GetNormalized();
	if (Motion.SweepPeriod > 0.f)
	{
		Part.SweepPhase = FMath::Frac(Part.SweepPhase + Step / Motion.SweepPeriod);
	}
	const FRotator Rotation = Part.Spin + Motion.SweepAmplitude * FMath::Sin(UE_TWO_PI * Part.SweepPhase);
	// About the pivot: there, the part turns in place.
	const FQuat Turn = Rotation.Quaternion();
	Part.Turn = FTransform(Turn, Motion.PivotOffset - Turn.RotateVector(Motion.PivotOffset));
}

FTransform URTSBuildingMotionSubsystem::GetPose(const FRTSMotionEntry& Entry, const FRTSMovingPart& Part)
{
	FTransform Pose = Part.Turn * Part.Rest;
	// Up the chain of parts it rides on: each one's turn, moved from its own frame into the building's.
	int32 Follow = Part.Follow;
	for (int32 Depth = 0; Entry.Parts.IsValidIndex(Follow) && Depth < Entry.Parts.Num(); ++Depth)
	{
		const FRTSMovingPart& Parent = Entry.Parts[Follow];
		Pose = Pose * Parent.Rest.Inverse() * Parent.Turn * Parent.Rest;
		Follow = Parent.Follow;
	}
	return Pose;
}

void URTSBuildingMotionSubsystem::UpdateAll(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSBuildingMotionSubsystem::UpdateAll);
	if (!CVarBuildingMotion.GetValueOnGameThread())
	{
		return;
	}
	RTSNearestToView::GatherCameras(GetWorld(), CameraLocations);

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FRTSMotionEntry& Entry = Entries[Index];
		const ARTSBuilding* Building = Entry.Building.Get();
		if (!Building)
		{
			Entries.RemoveAtSwap(Index);
			continue;
		}
		const float Scale = Building->GetEquipmentMotionScale();
		bool bDrawn = bPoseUnseen;
		for (FRTSMovingPart& Part : Entry.Parts)
		{
			Advance(Part, DeltaSeconds, Scale);
			const UStaticMeshComponent* Component = Part.Component.Get();
			bDrawn = bDrawn || (Component && Component->WasRecentlyRendered(RecentlyRenderedSeconds));
		}
		if (!bDrawn)
		{
			continue;
		}
		const float Distance = bPoseUnseen ? 0.f : RTSNearestToView::GetDistance(CameraLocations, {}, Building->GetActorLocation());
		for (const FRTSMovingPart& Part : Entry.Parts)
		{
			UStaticMeshComponent* Component = Part.Component.Get();
			if (Component && (Part.MaxDrawDistance <= 0.f || Distance <= Part.MaxDrawDistance))
			{
				Component->SetRelativeTransform(GetPose(Entry, Part));
			}
		}
	}
}

void URTSBuildingMotionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After everything else moved, as the vehicle effects, before the render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSBuildingMotionSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	Entries.Reset();
	Super::Deinitialize();
}
