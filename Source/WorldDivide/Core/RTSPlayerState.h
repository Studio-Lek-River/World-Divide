#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTSPlayerState.generated.h"

UCLASS()
class WORLDDIVIDE_API ARTSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	int32 GetTeamId() const { return TeamId; }

	/** Server only. */
	void SetTeamId(int32 InTeamId);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	int32 TeamId = 0;
};
