#include "Navigation/RTSMoveRequestSubsystem.h"

#include "Core/RTSPlayerState.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	TAutoConsoleVariable<float> CVarMoveRequestBudgetMs(
		TEXT("rts.Nav.MoveRequestBudgetMs"), URTSMoveRequestSubsystem::DefaultBudgetMs,
		TEXT("Server ms of path work (search and hull fit) that move and attack-move orders may take per frame (#174); the ")
		TEXT("rest wait for the next frames. Every frame with requests waiting runs at least one. 0 runs every request at once."));

	TAutoConsoleVariable<int32> CVarMoveRequestsPerFrame(
		TEXT("rts.Nav.MoveRequestsPerFrame"), 0,
		TEXT("Move and attack-move requests run per frame instead of rts.Nav.MoveRequestBudgetMs, so the schedule does not ")
		TEXT("depend on the machine's speed (#179). The fixed-step automation tests set it; 0, the default, keeps the ms budget."));
}

void URTSMoveRequestSubsystem::Request(ARTSUnit& Unit, uint32 Ticket, int32 OrderId, const ARTSPlayerState* Issuer)
{
	FLane* Lane = Lanes.FindByPredicate([Issuer](const FLane& Candidate) { return Candidate.Issuer.Get() == Issuer; });
	if (!Lane)
	{
		Lane = &Lanes.AddDefaulted_GetRef();
		Lane->Issuer = Issuer;
	}
	Lane->Entries.Add({ &Unit, Ticket, OrderId, GFrameCounter, GetWorld()->GetTimeSeconds() });
	++Stats.Queued;

	// A request queued by one Process runs is served by that pass.
	if (!bProcessing)
	{
		Process();
	}
}

int32 URTSMoveRequestSubsystem::GetPendingCount() const
{
	int32 Count = 0;
	for (const FLane& Lane : Lanes)
	{
		Count += Lane.Entries.Num() - Lane.Head;
	}
	return Count;
}

double URTSMoveRequestSubsystem::GetBudgetMs()
{
	return CVarMoveRequestBudgetMs.GetValueOnGameThread();
}

int32 URTSMoveRequestSubsystem::GetRequestsPerFrame()
{
	return FMath::Max(CVarMoveRequestsPerFrame.GetValueOnGameThread(), 0);
}

bool URTSMoveRequestSubsystem::HasBudget()
{
	if (BudgetFrame != GFrameCounter)
	{
		BudgetFrame = GFrameCounter;
		FrameSpentMs = 0.0;
		FrameWorstMs = 0.0;
		FrameRuns = 0;
	}
	// A count of requests instead of real time: the same requests run in the same frames on any machine.
	if (const int32 PerFrame = GetRequestsPerFrame(); PerFrame > 0)
	{
		return FrameRuns < PerFrame;
	}
	const double Budget = GetBudgetMs();
	// The costliest request of the frame so far stands for the next: one that would not fit waits for the next frame.
	return Budget <= 0.0 || FrameSpentMs <= 0.0 || FrameSpentMs + FrameWorstMs <= Budget;
}

bool URTSMoveRequestSubsystem::Run(const FEntry& Entry)
{
	ARTSUnit* Unit = Entry.Unit.Get();
	if (!IsValid(Unit) || !Unit->IsMoveRequestCurrent(Entry.Ticket))
	{
		++Stats.Stale;
		return false;
	}

	Stats.MaxSpentBeforeLastMs = FMath::Max(Stats.MaxSpentBeforeLastMs, FrameSpentMs);
	const double Begin = FPlatformTime::Seconds();
	if (Entry.OrderId != 0)
	{
		FOrderHint& Hint = RaisedByOrder.FindOrAdd(Entry.OrderId);
		Hint.Frame = GFrameCounter;
		const RTSPathSearch::FRaisedStart Raised(Hint.NavData);
		Unit->RunMoveRequest();
	}
	else
	{
		Unit->RunMoveRequest();
	}
	const double Ms = (FPlatformTime::Seconds() - Begin) * 1000.0;
	if (Ms > GetBudgetMs() && GetBudgetMs() > 0.0 && GetRequestsPerFrame() == 0)
	{
		// One request alone over the budget: a frame it cannot be split across (#174). The log names it.
		UE_LOG(LogWorldDivide, Verbose, TEXT("Move request of %s to %s took %.3f ms, more than the %.1f ms budget"), *Unit->GetName(),
			Unit->GetMoveTarget() ? *Unit->GetMoveTarget()->ToCompactString() : TEXT("nowhere"), Ms, GetBudgetMs());
	}

	if (FrameRuns == 0)
	{
		++Stats.Frames;
	}
	++FrameRuns;
	FrameSpentMs += Ms;
	FrameWorstMs = FMath::Max(FrameWorstMs, Ms);
	Stats.TotalMs += Ms;
	Stats.PeakFrameMs = FMath::Max(Stats.PeakFrameMs, FrameSpentMs);
	Stats.WorstRequestMs = FMath::Max(Stats.WorstRequestMs, Ms);
	++(Entry.Frame == GFrameCounter ? Stats.RunAtOnce : Stats.RunLater);
	Stats.MaxFramesWaited = FMath::Max(Stats.MaxFramesWaited, static_cast<int32>(GFrameCounter - Entry.Frame));
	Stats.MaxSecondsWaited = FMath::Max(Stats.MaxSecondsWaited, GetWorld()->GetTimeSeconds() - Entry.Time);
	return true;
}

void URTSMoveRequestSubsystem::Process()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSMoveRequestSubsystem::Process);

	if (GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	TGuardValue<bool> Guard(bProcessing, true);
	// Whether any request was taken off a lane, run or stale; otherwise there is nothing to tidy.
	bool bTaken = false;
	while (HasBudget())
	{
		// The next lane with a request that is not stale; stale ones on the way cost nothing.
		bool bRan = false;
		for (int32 Tried = 0; !bRan && Tried < Lanes.Num(); ++Tried)
		{
			const int32 LaneIndex = (NextLane + Tried) % Lanes.Num();
			while (!bRan && !Lanes[LaneIndex].IsEmpty())
			{
				// Copied: the request may queue another and grow the lanes.
				const FEntry Entry = Lanes[LaneIndex].Entries[Lanes[LaneIndex].Head++];
				bTaken = true;
				bRan = Run(Entry);
			}
			if (bRan)
			{
				NextLane = (LaneIndex + 1) % Lanes.Num();
			}
		}
		if (!bRan)
		{
			break;
		}
	}
	if (!bTaken)
	{
		return;
	}

	// Drop the lanes and the orders' hints nothing waits on any more.
	TSet<int32> Waiting;
	for (int32 LaneIndex = Lanes.Num() - 1; LaneIndex >= 0; --LaneIndex)
	{
		FLane& Lane = Lanes[LaneIndex];
		if (Lane.IsEmpty())
		{
			Lanes.RemoveAt(LaneIndex);
			NextLane = NextLane > LaneIndex ? NextLane - 1 : NextLane;
			continue;
		}
		if (Lane.Head > 64)
		{
			Lane.Entries.RemoveAt(0, Lane.Head);
			Lane.Head = 0;
		}
		for (int32 Index = Lane.Head; Index < Lane.Entries.Num(); ++Index)
		{
			Waiting.Add(Lane.Entries[Index].OrderId);
		}
	}
	NextLane = Lanes.IsEmpty() ? 0 : NextLane % Lanes.Num();
	for (auto It = RaisedByOrder.CreateIterator(); It; ++It)
	{
		if (!Waiting.Contains(It->Key) && It->Value.Frame != GFrameCounter)
		{
			It.RemoveCurrent();
		}
	}
}

void URTSMoveRequestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Process();
}

TStatId URTSMoveRequestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSMoveRequestSubsystem, STATGROUP_Tickables);
}

bool URTSMoveRequestSubsystem::IsTickable() const
{
	return !Lanes.IsEmpty();
}

bool URTSMoveRequestSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URTSMoveRequestSubsystem::Deinitialize()
{
	Lanes.Reset();
	RaisedByOrder.Reset();
	Super::Deinitialize();
}
