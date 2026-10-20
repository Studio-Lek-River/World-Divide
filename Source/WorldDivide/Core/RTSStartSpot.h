#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "RTSStartSpot.generated.h"

class UTextRenderComponent;

/**
 * A start spot a designer drops into a level and numbers. It tags itself Team<SpotIndex>, so the game mode, the seats and the AI read it
 * like any tagged PlayerStart, and in the editor it shows its number (SpotIndex + 1) above itself.
 */
UCLASS()
class WORLDDIVIDE_API ARTSStartSpot : public APlayerStart
{
	GENERATED_BODY()

public:
	ARTSStartSpot(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	/** 0-based; spots 0..N-1 of a map must each exist once. The editor shows it as SpotIndex + 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start Spot", meta = (ClampMin = "0"))
	int32 SpotIndex = 0;

#if WITH_EDITORONLY_DATA
private:
	/** The number in the viewport; not in game, not in a cooked build. */
	UPROPERTY()
	TObjectPtr<UTextRenderComponent> NumberText;
#endif
};
