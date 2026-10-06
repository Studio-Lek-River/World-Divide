#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTSPlayerState.generated.h"

class ARTSBuilding;
class URTSFactionData;

UCLASS()
class WORLDDIVIDE_API ARTSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	int32 GetTeamId() const { return TeamId; }

	/** Server only. */
	void SetTeamId(int32 InTeamId);

	URTSFactionData* GetFaction() const { return Faction; }

	/** Server only. */
	void SetFaction(URTSFactionData* InFaction);

	/** Money to build with. Only the owning player sees it replicate. */
	int32 GetCredits() const { return Credits; }

	/** Server only. A negative Delta takes credits away, never below zero. */
	void AddCredits(int32 Delta);

	/** Server only. Takes Amount and returns true, or changes nothing and returns false when the player cannot afford it. */
	bool TrySpendCredits(int32 Amount);

	/** The player playing TeamId, or null when no player does (units placed by the map for an empty seat). */
	static ARTSPlayerState* FindForTeam(const UWorld& World, int32 InTeamId);

	/** Production speed while the player is low on power: everything builds a third slower. */
	static constexpr float LowPowerProductionRate = 0.67f;

	/** In [0, 1]: Produced / Consumed when more is consumed than produced, else 1. */
	static float ComputePowerEfficiency(int32 Produced, int32 Consumed);

	/** Power output of the player's completed buildings. Only the owning player sees it replicate. */
	int32 GetPowerProduced() const { return PowerProduced; }

	/** Power draw of the player's completed buildings. Only the owning player sees it replicate. */
	int32 GetPowerConsumed() const { return PowerConsumed; }

	/** 1 for a faction without power, else ComputePowerEfficiency of what the player produces and consumes. */
	float GetPowerEfficiency() const;

	bool IsLowPower() const { return GetPowerEfficiency() < 1.f; }

	/** Multiplier on construction, and on unit production once it exists: 1, or LowPowerProductionRate when low on power. */
	float GetProductionRate() const { return IsLowPower() ? LowPowerProductionRate : 1.f; }

	/** The production rate of TeamId's player; 1 for a team without a player, which has no power grid. */
	static float GetProductionRateForTeam(const UWorld& World, int32 InTeamId);

	/** Whether TeamId's player is low on power; false for a team without a player. */
	static bool IsLowPowerForTeam(const UWorld& World, int32 InTeamId);

	/**
	 * Server only. Sums the power of the team's completed buildings, leaving out Removed (a building being destroyed),
	 * and lets each of them arm or disarm its weapon for the new state.
	 */
	void RecalculatePower(const ARTSBuilding* Removed = nullptr);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	int32 TeamId = 0;

	UPROPERTY(Replicated)
	TObjectPtr<URTSFactionData> Faction;

	UPROPERTY(Replicated)
	int32 Credits = 0;

	UPROPERTY(Replicated)
	int32 PowerProduced = 0;

	UPROPERTY(Replicated)
	int32 PowerConsumed = 0;
};
