#include "Combat/RTSShotSubsystem.h"

#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"

void URTSShotSubsystem::AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime)
{
	Shells.Add({ &Target, Instigator, Damage, FlightTime });
}

void URTSShotSubsystem::Advance(float DeltaSeconds)
{
	for (int32 Index = 0; Index < Shells.Num();)
	{
		FShell& Shell = Shells[Index];
		Shell.TimeLeft -= DeltaSeconds;
		// Same tolerance as the weapon reload, so a flight that is a whole number of frames lands on its last frame.
		if (Shell.TimeLeft > KINDA_SMALL_NUMBER)
		{
			++Index;
			continue;
		}
		// Off the list before the damage, which may kill the target and run its death handling.
		const FShell Landed = Shell;
		Shells.RemoveAtSwap(Index);
		const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Landed.Target.Get());
		if (Targetable && IsValid(Landed.Target.Get()) && !Targetable->GetHealthComponent()->IsDead())
		{
			Targetable->GetHealthComponent()->ApplyDamage(Landed.Damage, Landed.Instigator.Get());
		}
	}
}

void URTSShotSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Shells.IsEmpty() && !ARTSGameState::IsMatchOver(*GetWorld()))
	{
		Advance(DeltaTime);
	}
}

TStatId URTSShotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSShotSubsystem, STATGROUP_Tickables);
}
