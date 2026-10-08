#include "Player/RTSLocalPlayer.h"

#include "Core/RTSGameMode.h"

FString URTSLocalPlayer::GetGameLoginOptions() const
{
	return SeatToken.IsEmpty() ? Super::GetGameLoginOptions()
		: FString::Printf(TEXT("%s=%s"), ARTSGameMode::SeatTokenOption, *SeatToken);
}
