#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTSPlayerState.generated.h"

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	int32 TeamId = 0;

	UPROPERTY(Replicated)
	TObjectPtr<URTSFactionData> Faction;
};
