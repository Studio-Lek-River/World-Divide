#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSShotSubsystem.generated.h"

/**
 * Server-side shells in flight. Each one homes on its target and deals its damage when it lands, even when the shooter
 * died in the meantime. A target that is gone or dead by then takes nothing. Shells hold while the match is over.
 * The visible shell is ARTSShotEffect, which flies for the same time.
 */
UCLASS()
class WORLDDIVIDE_API URTSShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Server only. Target must be an IRTSTargetable. */
	void AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime);

	/** Server only. Flies every shell DeltaSeconds on and deals the damage of those that land. Public so tests can step it. */
	void Advance(float DeltaSeconds);

	int32 GetShellsInFlight() const { return Shells.Num(); }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FShell
	{
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<AActor> Instigator;
		float Damage = 0.f;
		float TimeLeft = 0.f;
	};

	TArray<FShell> Shells;
};
