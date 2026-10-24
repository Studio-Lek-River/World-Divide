#include "Combat/RTSAreaDamage.h"

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSCivilBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"

namespace RTSCombat
{
	float ComputeFalloffDamage(float Damage, float Distance, float Radius, float EdgeFraction)
	{
		if (Distance > Radius)
		{
			return 0.f;
		}
		if (Radius <= 0.f)
		{
			return Damage;
		}
		const float Alpha = FMath::Clamp(Distance / Radius, 0.f, 1.f);
		return Damage * FMath::Lerp(1.f, FMath::Clamp(EdgeFraction, 0.f, 1.f), Alpha);
	}

	void ApplyAreaDamage(UWorld& World, const FVector& Center, float Damage, float Radius, float EdgeFraction, AActor* Instigator)
	{
		if (Radius <= 0.f)
		{
			return;
		}
		// Gathered first: damage destroys what it kills, which must not happen while iterating.
		TArray<TPair<TWeakObjectPtr<AActor>, float>> Hits;
		auto Consider = [&](AActor& Actor, const IRTSTargetable& Target)
		{
			if (Actor.IsActorBeingDestroyed() || Target.GetHealthComponent()->IsDead())
			{
				return;
			}
			// A unit's footprint point lies on its selection radius; its centre is what stands in the blast.
			const FVector Point = Actor.IsA<ARTSUnit>() ? Actor.GetActorLocation() : Target.GetClosestFootprintPoint(Center);
			const float Dealt = ComputeFalloffDamage(Damage, FVector::Dist2D(Point, Center), Radius, EdgeFraction);
			if (Dealt > 0.f)
			{
				Hits.Emplace(&Actor, Dealt);
			}
		};
		auto ConsiderUnit = [&](ARTSUnit& Unit)
		{
			// A ground blast: aircraft in flight are out of it.
			if (!Unit.IsAirborne())
			{
				Consider(Unit, Unit);
			}
		};
		if (const URTSAvoidanceSubsystem* Avoidance = World.GetSubsystem<URTSAvoidanceSubsystem>())
		{
			TArray<ARTSUnit*> Units;
			Avoidance->QueryRadius(Center, Radius, Units);
			for (ARTSUnit* Unit : Units)
			{
				ConsiderUnit(*Unit);
			}
		}
		else
		{
			for (TActorIterator<ARTSUnit> It(&World); It; ++It)
			{
				ConsiderUnit(**It);
			}
		}
		if (const ARTSGameState* GameState = World.GetGameState<ARTSGameState>())
		{
			GameState->ForEachBuilding([&](ARTSBuilding& Building) { Consider(Building, Building); });
			GameState->ForEachCivilBuilding([&](ARTSCivilBuilding& Building) { Consider(Building, Building); });
		}
		else
		{
			for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
			{
				Consider(**It, **It);
			}
			for (TActorIterator<ARTSCivilBuilding> It(&World); It; ++It)
			{
				Consider(**It, **It);
			}
		}

		for (const TPair<TWeakObjectPtr<AActor>, float>& Hit : Hits)
		{
			const IRTSTargetable* Target = Cast<IRTSTargetable>(Hit.Key.Get());
			if (Target && !Target->GetHealthComponent()->IsDead())
			{
				Target->GetHealthComponent()->ApplyDamage(Hit.Value, Instigator);
			}
		}
	}
}
