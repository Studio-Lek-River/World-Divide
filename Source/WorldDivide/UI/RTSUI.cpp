#include "UI/RTSUI.h"

#include "Data/RTSEntityData.h"
#include "Data/RTSFactionData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/RTSUIStyleSubsystem.h"

const FName RTSUI::PlaceholderIcon(TEXT("Icon.Placeholder"));

const FRTSUISprite& RTSUI::FindEntityIcon(TConstArrayView<const URTSUIStyleSet*> Chain, const URTSEntityData* Entity)
{
	if (Entity && !Entity->IconSprite.IsNone())
	{
		const FRTSUISprite& Icon = URTSUIStyleSubsystem::FindSpriteIn(Chain, Entity->IconSprite);
		if (&Icon != &URTSUIStyleSubsystem::GetPlaceholderSprite())
		{
			return Icon;
		}
	}
	return URTSUIStyleSubsystem::FindSpriteIn(Chain, PlaceholderIcon);
}

const FRTSUISprite& RTSUI::GetEntityIcon(const UObject* WorldContextObject, const URTSEntityData* Entity)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const URTSUIStyleSubsystem* Styles = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<URTSUIStyleSubsystem>() : nullptr;
	if (!Styles)
	{
		return URTSUIStyleSubsystem::GetPlaceholderSprite();
	}
	const URTSFactionData* Faction = Entity ? URTSFactionData::FindByTag(Entity->FactionTag) : nullptr;
	return FindEntityIcon(Styles->GetStyleChain(Faction), Entity);
}
