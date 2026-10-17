#pragma once

#include "CoreMinimal.h"
#include "Navigation/RTSPathSearch.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSMoveRequestSubsystem.generated.h"

class ARTSPlayerState;
class ARTSUnit;

/**
 * Server only. Spreads the path searches of move and attack-move orders over frames (#174), so a large group sent far does
 * not stall the frame of its order. ARTSUnit queues its move here instead of searching its path at once; each frame the
 * requests waiting run, oldest first and the players' in turn, until rts.Nav.MoveRequestBudgetMs of path work (the search
 * and the hull fit) is spent. A request runs at once when none waits and the frame has budget left, so short orders start
 * in their own frame as before.
 * Every frame that has requests waiting runs at least one, so each gets its result; a single request is never split, so a
 * frame runs over the budget by up to one request that alone costs more. Requests of one order share an FRaisedStart:
 * once one of them runs out of the default node limit on a navmesh, the others on it start raised.
 * A request belongs to the unit's front order: a new order, a dropped order, a destroyed unit or the match's end makes it
 * stale (ARTSUnit's ticket), and a stale one is skipped without cost.
 */
UCLASS()
class WORLDDIVIDE_API URTSMoveRequestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** What the requests cost since the last ResetStats, for tests and benches. Times in ms of real time. */
	struct FStats
	{
		/** Requests queued, run in the frame they were queued, run in a later frame, and skipped as stale. */
		int32 Queued = 0;
		int32 RunAtOnce = 0;
		int32 RunLater = 0;
		int32 Stale = 0;
		/** The longest a request run waited: frames, and game seconds. */
		int32 MaxFramesWaited = 0;
		double MaxSecondsWaited = 0.0;
		/** Path work run in all, in the frame that ran the most, and by the costliest single request. */
		double TotalMs = 0.0;
		double PeakFrameMs = 0.0;
		double WorstRequestMs = 0.0;
		/** The most a frame had spent when it started its last request: the budget it kept to, the overrun aside. */
		double MaxSpentBeforeLastMs = 0.0;
		/** Frames that ran at least one request. */
		int32 Frames = 0;
	};

	/**
	 * Queues Unit's move for its front order; ARTSUnit::RunMoveRequest runs it, with Ticket still Unit's, unless it went stale
	 * first. Runs it before returning when no request waits and the frame has budget left. Issuer's requests take turns with
	 * other players' (null is a player of its own); OrderId groups the requests that share their node-limit hint.
	 */
	void Request(ARTSUnit& Unit, uint32 Ticket, int32 OrderId, const ARTSPlayerState* Issuer);

	/** Requests waiting, stale ones included until their turn comes. */
	int32 GetPendingCount() const;

	/** The per-frame budget from rts.Nav.MoveRequestBudgetMs; zero or less runs every request at once. */
	static double GetBudgetMs();

	/** rts.Nav.MoveRequestBudgetMs's default, agreed for #174 from L_LongPath's group orders. */
	static constexpr float DefaultBudgetMs = 2.0f;

	const FStats& GetStats() const { return Stats; }
	void ResetStats() { Stats = FStats(); }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	struct FEntry
	{
		TWeakObjectPtr<ARTSUnit> Unit;
		uint32 Ticket = 0;
		int32 OrderId = 0;
		/** When it was queued: GFrameCounter and game seconds. */
		uint64 Frame = 0;
		double Time = 0.0;
	};

	/** One player's requests, oldest first from Head. */
	struct FLane
	{
		TWeakObjectPtr<const ARTSPlayerState> Issuer;
		TArray<FEntry> Entries;
		int32 Head = 0;

		bool IsEmpty() const { return Head >= Entries.Num(); }
	};

	/** Runs waiting requests, the lanes in turn, while the frame's budget allows. */
	void Process();

	/** Whether the frame's budget allows one more request: always for the frame's first. */
	bool HasBudget();

	/** Runs Entry's request unless it went stale; false when it was stale. */
	bool Run(const FEntry& Entry);

	TArray<FLane> Lanes;
	/** The lane Process serves first, so no player's requests wait behind another's. */
	int32 NextLane = 0;

	/** The frame (GFrameCounter) whose spent work and costliest request these are. */
	uint64 BudgetFrame = MAX_uint64;
	double FrameSpentMs = 0.0;
	double FrameWorstMs = 0.0;

	/** Set while Process runs, so requests queued by the ones it runs wait for it instead of starting another pass. */
	bool bProcessing = false;

	/** Per OrderId: the navmeshes on which its searches ran out of the default node limit, and the frame one last ran. */
	struct FOrderHint
	{
		RTSPathSearch::FRaisedNavData NavData;
		uint64 Frame = 0;
	};

	/** Kept while the order has requests waiting or ran one this frame: Dispatch queues a group's units one by one. */
	TMap<int32, FOrderHint> RaisedByOrder;

	FStats Stats;
};
