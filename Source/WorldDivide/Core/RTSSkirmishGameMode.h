#pragma once

#include "CoreMinimal.h"
#include "Core/RTSGameMode.h"
#include "RTSSkirmishGameMode.generated.h"

/** The game mode of a skirmish map such as L_Skirmish: AI opponents take the team seats no player has. */
UCLASS()
class WORLDDIVIDE_API ARTSSkirmishGameMode : public ARTSGameMode
{
	GENERATED_BODY()

public:
	ARTSSkirmishGameMode()
	{
		bSpawnAIOpponents = true;
	}
};
