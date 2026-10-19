#include "Player/RTSCommandButton.h"

namespace RTSCommandPanel
{
	const FName StopId(TEXT("Stop"));
	const FName AttackMoveId(TEXT("AttackMove"));
	const FName ForceFireId(TEXT("ForceFire"));

	FKey GetPositionKey(int32 Position)
	{
		static const FKey PositionKeys[] = {
			EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
		};
		return Position >= 0 && Position < UE_ARRAY_COUNT(PositionKeys) ? PositionKeys[Position] : FKey();
	}

	void ResolveHotkeys(TArray<FRTSCommandButton>& Buttons, const TSet<FKey>& Reserved, TArray<FString>& OutWarnings)
	{
		TSet<FKey> Taken;
		for (const FRTSCommandButton& Button : Buttons)
		{
			if (Button.Category == ERTSCommandCategory::Unit && Button.Hotkey.IsValid())
			{
				Taken.Add(Button.Hotkey);
			}
		}
		for (int32 Position = 0; Position < Buttons.Num(); ++Position)
		{
			FRTSCommandButton& Button = Buttons[Position];
			if (Button.Category == ERTSCommandCategory::Unit)
			{
				continue;
			}
			const FKey Own = Button.Hotkey;
			if (Own.IsValid())
			{
				if (!Reserved.Contains(Own) && !Taken.Contains(Own))
				{
					Taken.Add(Own);
					continue;
				}
				OutWarnings.Add(FString::Printf(TEXT("Command %s: hotkey %s is %s; it falls back to its position key."),
					*Button.Id.ToString(), *Own.ToString(), Reserved.Contains(Own) ? TEXT("reserved") : TEXT("taken by an earlier command")));
			}
			const FKey PositionKey = GetPositionKey(Position);
			Button.Hotkey = PositionKey.IsValid() && !Taken.Contains(PositionKey) ? PositionKey : FKey();
			if (Button.Hotkey.IsValid())
			{
				Taken.Add(Button.Hotkey);
			}
		}
	}
}
