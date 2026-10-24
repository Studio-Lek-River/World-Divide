#include "Navigation/RTSTrunkObstaclesComponent.h"

#include "AI/NavigationModifier.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "Navigation/RTSNavArea_InfantryBlocked.h"

URTSTrunkObstaclesComponent::URTSTrunkObstaclesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bCanEverAffectNavigation = true;
}

void URTSTrunkObstaclesComponent::SetTrunks(const TArray<FVector>& Feet)
{
	// Registered anew: a component that was not relevant while it had no trunks never entered the navigation octree, and
	// an update does not add it.
	const bool bWasRegistered = IsRegistered();
	if (bWasRegistered)
	{
		UnregisterComponent();
	}
	Trunks = Feet;
	if (bWasRegistered)
	{
		RegisterComponent();
	}
}

void URTSTrunkObstaclesComponent::OnRegister()
{
	Super::OnRegister();
	FNavigationSystem::OnComponentRegistered(*this);
}

void URTSTrunkObstaclesComponent::OnUnregister()
{
	FNavigationSystem::OnComponentUnregistered(*this);
	Super::OnUnregister();
}

void URTSTrunkObstaclesComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	for (const FVector& Foot : Trunks)
	{
		Data.Modifiers.Add(FAreaNavModifier(Radius, Height, FTransform(Foot - FVector(0.f, 0.f, Sink)),
			URTSNavArea_InfantryBlocked::StaticClass()));
	}
}

FBox URTSTrunkObstaclesComponent::GetNavigationBounds() const
{
	FBox Bounds(ForceInit);
	for (const FVector& Foot : Trunks)
	{
		Bounds += Foot;
	}
	return Bounds.IsValid ? Bounds.ExpandBy(FVector(Radius, Radius, Height)) : Bounds;
}

bool URTSTrunkObstaclesComponent::IsNavigationRelevant() const
{
	return !Trunks.IsEmpty();
}
