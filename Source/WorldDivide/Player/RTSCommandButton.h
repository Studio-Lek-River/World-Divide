#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "RTSCommandButton.generated.h"

class URTSEntityData;

/** Which kind of order a command button gives; the upgrades panel shows Upgrade, the command panel the rest. */
UENUM()
enum class ERTSCommandCategory : uint8
{
	/** An order to the selected units: Stop or Attack-move. */
	Unit,
	/** A building the selected builder places. */
	Build,
	/** A unit the selected building queues. */
	Produce,
	/** An upgrade the selected building researches. */
	Upgrade,
};

/** Why a command button is disabled; None when it is enabled or disabled for a reason without its own name. */
UENUM()
enum class ERTSCommandDisabledReason : uint8
{
	None,
	CannotAfford,
	MissingPrerequisite,
	BuildLimit,
	QueueFull,
};

/**
 * One command the current selection offers, as the command and upgrades panels show it. Gameplay code fills it in
 * (ARTSPlayerController::GetCommandButtons); the panels only show it and hand it back to ExecuteCommandButton.
 */
USTRUCT()
struct WORLDDIVIDE_API FRTSCommandButton
{
	GENERATED_BODY()

	/** The entity data's name, or Stop or AttackMove. */
	FName Id;

	FText DisplayName;
	FText Description;

	/** Without the set prefix, as URTSEntityData::IconSprite. */
	FName IconSprite;

	/** The resolved key, RTSCommandPanel::ResolveHotkeys; invalid when the button has none. */
	FKey Hotkey;

	ERTSCommandCategory Category = ERTSCommandCategory::Unit;

	/** Credits it costs; 0 for unit orders. */
	int32 Cost = 0;

	bool bEnabled = true;
	ERTSCommandDisabledReason DisabledReason = ERTSCommandDisabledReason::None;

	/** The reason in words, such as "Needs Barracks"; empty while enabled. */
	FText DisabledText;

	/** An upgrade already researched. */
	bool bDone = false;

	/** Armed, such as attack-move waiting for its target. */
	bool bActive = false;

	/** What to build, produce or research; null for unit orders. */
	TWeakObjectPtr<URTSEntityData> EntityData;
};

namespace RTSCommandPanel
{
	/** The command panel's cards and the upgrades panel's rows. */
	constexpr int32 MaxCommands = 16;
	constexpr int32 MaxUpgrades = 6;

	/** Id of the unit orders. */
	extern WORLDDIVIDE_API const FName StopId;
	extern WORLDDIVIDE_API const FName AttackMoveId;

	/** The key 1-9 of the button at Position in its list, or an invalid key past the ninth. */
	WORLDDIVIDE_API FKey GetPositionKey(int32 Position);

	/**
	 * Gives each button its key, in order: its own Hotkey as the data set it, else its position key 1-9. An own key that is
	 * Reserved, or already taken by an earlier button, falls back to the position key and adds a warning to OutWarnings; a
	 * position key that is taken leaves the button without one. Unit orders keep their keys, which the controller binds itself.
	 */
	WORLDDIVIDE_API void ResolveHotkeys(TArray<FRTSCommandButton>& Buttons, const TSet<FKey>& Reserved, TArray<FString>& OutWarnings);
}
