#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UWorld;

/**
 * The free list the vehicle effects that run per vehicle share (the exhaust, #222, and the dust, #224): a vehicle whose
 * emitters have been stopped for rts.Fx.ReleaseDelay gives them back, and the next vehicle to need one takes it, so the
 * emitters held follow the budget instead of every vehicle that ever came near a camera (#225).
 */
namespace RTSEffectPool
{
	/** rts.Fx.ReleaseDelay: seconds a vehicle's emitters stay stopped before it gives them back; 0 or less keeps them. */
	float GetReleaseDelay();

	/**
	 * A stopped emitter of System attached to Parent at RelativeLocation in World: one from Free, or a new one when it is
	 * empty. bOutNew says which, for the settings a new one needs before it is registered.
	 */
	UNiagaraComponent* Take(TArray<TObjectPtr<UNiagaraComponent>>& Free, UWorld& World, UNiagaraSystem* System,
		USceneComponent& Parent, const FVector& RelativeLocation, bool& bOutNew);

	/** Stops Component at once, along with what it emitted, and keeps it in Free, or destroys it once Free holds MaxFree. */
	void Release(TArray<TObjectPtr<UNiagaraComponent>>& Free, UNiagaraComponent* Component, int32 MaxFree);

	/** Destroys every component in Free. */
	void DestroyAll(TArray<TObjectPtr<UNiagaraComponent>>& Free);
}
