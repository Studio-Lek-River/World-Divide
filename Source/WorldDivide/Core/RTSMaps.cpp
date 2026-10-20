#include "Core/RTSMaps.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "LandscapeProxy.h"
#include "NavMesh/NavMeshBoundsVolume.h"

int32 RTSMaps::ParseTeamTag(FName Tag)
{
	FString Text = Tag.ToString();
	return Text.RemoveFromStart(TEXT("Team")) && !Text.IsEmpty() && Text.IsNumeric() ? FCString::Atoi(*Text) : INDEX_NONE;
}

FName RTSMaps::MakeTeamTag(int32 SpotIndex)
{
	return FName(*FString::Printf(TEXT("Team%d"), SpotIndex));
}

TMap<int32, const APlayerStart*> RTSMaps::FindStartSpots(const UWorld& World, TArray<FString>* OutProblems)
{
	TMap<int32, const APlayerStart*> Starts;
	if (!World.PersistentLevel)
	{
		return Starts;
	}
	for (const AActor* Actor : World.PersistentLevel->Actors)
	{
		const APlayerStart* Start = Cast<APlayerStart>(Actor);
		const int32 Spot = Start ? ParseTeamTag(Start->PlayerStartTag) : INDEX_NONE;
		if (Spot == INDEX_NONE)
		{
			continue;
		}
		if (const APlayerStart* const* First = Starts.Find(Spot))
		{
			if (OutProblems)
			{
				OutProblems->Add(FString::Printf(TEXT("Start spot %d is used by both %s and %s"), Spot + 1, *(*First)->GetName(),
					*Start->GetName()));
			}
			continue;
		}
		Starts.Add(Spot, Start);
	}
	Starts.KeySort(TLess<int32>());

	if (OutProblems && !Starts.IsEmpty())
	{
		int32 Highest = 0;
		for (const TPair<int32, const APlayerStart*>& Start : Starts)
		{
			Highest = FMath::Max(Highest, Start.Key);
		}
		for (int32 Spot = 0; Spot < Highest; ++Spot)
		{
			if (!Starts.Contains(Spot))
			{
				OutProblems->Add(FString::Printf(TEXT("Start spot %d is missing (Team%d)"), Spot + 1, Spot));
			}
		}
	}
	return Starts;
}

FBox2D RTSMaps::FindMapArea(const UWorld& World)
{
	FBox2D NavArea(ForceInit);
	FBox2D LandscapeArea(ForceInit);
	if (World.PersistentLevel)
	{
		for (const AActor* Actor : World.PersistentLevel->Actors)
		{
			if (const ANavMeshBoundsVolume* Volume = Cast<ANavMeshBoundsVolume>(Actor))
			{
				const FBox Bounds = Volume->GetComponentsBoundingBox(true);
				NavArea += FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max));
			}
			else if (const ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor))
			{
				const FBox Bounds = Landscape->GetComponentsBoundingBox(true);
				LandscapeArea += FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max));
			}
		}
	}
	const FBox2D Area = NavArea.bIsValid ? NavArea : LandscapeArea;
	if (!Area.bIsValid)
	{
		return Area;
	}
	const FVector2D Center = Area.GetCenter();
	const double HalfSize = 0.5 * FMath::Max(Area.GetSize().X, Area.GetSize().Y);
	return FBox2D(Center - FVector2D(HalfSize), Center + FVector2D(HalfSize));
}
