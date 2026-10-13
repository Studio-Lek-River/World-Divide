#pragma once

#include "CoreMinimal.h"

class URTSEntityData;
class URTSUIStyleSet;
struct FRTSUISprite;

/** Helpers every panel shares, so they all show an entity the same way. */
namespace RTSUI
{
	/** Icon.Placeholder: the icon of an entity without one, or whose icon no set has. */
	WORLDDIVIDE_API extern const FName PlaceholderIcon;

	/**
	 * The entity's URTSEntityData::IconSprite in the chain, then Icon.Placeholder in the chain, then the built-in grey
	 * placeholder. Never fails; public for the tests.
	 */
	WORLDDIVIDE_API const FRTSUISprite& FindEntityIcon(TConstArrayView<const URTSUIStyleSet*> Chain, const URTSEntityData* Entity);

	/**
	 * The icon of an entity, resolved by the faction of the entity (its FactionTag, see URTSFactionData::FindByTag), not
	 * of the local player: that faction's style, then Common, then Neutral. An entity without a known faction uses Common
	 * and Neutral.
	 */
	WORLDDIVIDE_API const FRTSUISprite& GetEntityIcon(const UObject* WorldContextObject, const URTSEntityData* Entity);
}
