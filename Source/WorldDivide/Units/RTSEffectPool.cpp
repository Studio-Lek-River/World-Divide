#include "Units/RTSEffectPool.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace RTSEffectPool
{
	TAutoConsoleVariable<float> CVarReleaseDelay(
		TEXT("rts.Fx.ReleaseDelay"), 8.f,
		TEXT("Seconds a vehicle's exhaust or dust emitters stay stopped (idle, out of the budget or far away) before they go back to a free list the next vehicle takes them from (#225): longer than the dust takes to settle. 0 or less keeps them with the vehicle."));
}

float RTSEffectPool::GetReleaseDelay()
{
	return CVarReleaseDelay.GetValueOnGameThread();
}

UNiagaraComponent* RTSEffectPool::Take(TArray<TObjectPtr<UNiagaraComponent>>& Free, UWorld& World, UNiagaraSystem* System,
	USceneComponent& Parent, const FVector& RelativeLocation, bool& bOutNew)
{
	while (!Free.IsEmpty())
	{
		UNiagaraComponent* Component = Free.Pop(EAllowShrinking::No);
		if (!IsValid(Component))
		{
			continue;
		}
		bOutNew = false;
		if (Component->GetAsset() != System)
		{
			Component->SetAsset(System);
		}
		Component->AttachToComponent(&Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Component->SetRelativeLocation(RelativeLocation);
		return Component;
	}
	// Owned by no actor and attached to the hull, so it follows its tilt and rock and is not counted among its parts.
	bOutNew = true;
	UNiagaraComponent* Component = NewObject<UNiagaraComponent>(&World);
	Component->SetAutoActivate(false);
	Component->SetAutoDestroy(false);
	Component->SetCastShadow(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetAsset(System);
	// The hull's scale stays out of the effect.
	Component->SetUsingAbsoluteScale(true);
	Component->SetupAttachment(&Parent);
	Component->SetRelativeLocation(RelativeLocation);
	return Component;
}

void RTSEffectPool::Release(TArray<TObjectPtr<UNiagaraComponent>>& Free, UNiagaraComponent* Component, int32 MaxFree)
{
	if (!IsValid(Component))
	{
		return;
	}
	if (Free.Num() >= MaxFree)
	{
		Component->DestroyComponent();
		return;
	}
	// Its particles go with it, so the next vehicle does not start with another's; the dust has settled by now.
	Component->DeactivateImmediate();
	Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Free.Add(Component);
}

void RTSEffectPool::DestroyAll(TArray<TObjectPtr<UNiagaraComponent>>& Free)
{
	for (UNiagaraComponent* Component : Free)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	Free.Reset();
}
