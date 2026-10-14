#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/RTSSettings.h"
#include "Data/RTSEntityData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Engine/GameInstance.h"
#include "GameplayTagsManager.h"
#include "UI/RTSUI.h"
#include "UI/RTSUIStyleSet.h"
#include "UI/RTSUIStyleSubsystem.h"

namespace
{
	using RTSTests::TestFlags;

	const TCHAR* USAFactionPath = TEXT("/Game/Data/Factions/DA_FactionUSA.DA_FactionUSA");
	const TCHAR* USAStylePath = TEXT("/Game/UI/Styles/DA_UIStyle_USA.DA_UIStyle_USA");

	FRTSUISprite MakeSprite(const FLinearColor& Tint)
	{
		FRTSUISprite Sprite;
		Sprite.Brush.TintColor = Tint;
		return Sprite;
	}

	FLinearColor TintOf(const FRTSUISprite& Sprite)
	{
		return Sprite.Brush.TintColor.GetSpecifiedColor();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEntityIconFallbackTest, "WorldDivide.UI.EntityIcon.Fallback", TestFlags)

bool FRTSEntityIconFallbackTest::RunTest(const FString& Parameters)
{
	// The faction draws its own tank; Common has a jet and the placeholder icon.
	URTSUIStyleSet* Faction = NewObject<URTSUIStyleSet>();
	URTSUIStyleSet* Common = NewObject<URTSUIStyleSet>();
	Faction->Sprites.Add(TEXT("Icon.Tank"), MakeSprite(FLinearColor::Red));
	Common->Sprites.Add(TEXT("Icon.Tank"), MakeSprite(FLinearColor::Green));
	Common->Sprites.Add(TEXT("Icon.Jet"), MakeSprite(FLinearColor::Blue));
	Common->Sprites.Add(RTSUI::PlaceholderIcon, MakeSprite(FLinearColor::Yellow));
	const TArray<const URTSUIStyleSet*> Chain = { Faction, Common };

	URTSUnitData* Tank = NewObject<URTSUnitData>();
	Tank->IconSprite = TEXT("Icon.Tank");
	URTSUnitData* Jet = NewObject<URTSUnitData>();
	Jet->IconSprite = TEXT("Icon.Jet");
	URTSUnitData* Unnamed = NewObject<URTSUnitData>();
	URTSUnitData* Unknown = NewObject<URTSUnitData>();
	Unknown->IconSprite = TEXT("Icon.Missing");

	TestEqual(TEXT("An icon the entity's faction has comes from the faction"), TintOf(RTSUI::FindEntityIcon(Chain, Tank)), FLinearColor::Red);
	TestEqual(TEXT("An icon the faction has no own entry for comes from Common"), TintOf(RTSUI::FindEntityIcon(Chain, Jet)), FLinearColor::Blue);
	TestEqual(TEXT("An entity without an icon shows Icon.Placeholder"), TintOf(RTSUI::FindEntityIcon(Chain, Unnamed)), FLinearColor::Yellow);
	TestEqual(TEXT("An icon no set has shows Icon.Placeholder"), TintOf(RTSUI::FindEntityIcon(Chain, Unknown)), FLinearColor::Yellow);
	TestEqual(TEXT("No entity shows Icon.Placeholder"), TintOf(RTSUI::FindEntityIcon(Chain, nullptr)), FLinearColor::Yellow);

	const TArray<const URTSUIStyleSet*> FactionOnly = { Faction };
	TestTrue(TEXT("Without Icon.Placeholder anywhere it is the built-in placeholder"),
		&RTSUI::FindEntityIcon(FactionOnly, Unknown) == &URTSUIStyleSubsystem::GetPlaceholderSprite());
	TestTrue(TEXT("An empty chain gives the built-in placeholder"), &RTSUI::FindEntityIcon({}, Tank) == &URTSUIStyleSubsystem::GetPlaceholderSprite());
	TestTrue(TEXT("Without a world it is the built-in placeholder"), &RTSUI::GetEntityIcon(nullptr, Tank) == &URTSUIStyleSubsystem::GetPlaceholderSprite());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEntityIconFactionChainTest, "WorldDivide.UI.EntityIcon.FactionChain", TestFlags)

bool FRTSEntityIconFactionChainTest::RunTest(const FString& Parameters)
{
	const FGameplayTag USATag = UGameplayTagsManager::Get().RequestGameplayTag(TEXT("Faction.USA"), false);
	if (!TestTrue(TEXT("Faction.USA is registered"), USATag.IsValid()))
	{
		return false;
	}
	const URTSFactionData* USAFaction = LoadObject<URTSFactionData>(nullptr, USAFactionPath);
	TestTrue(TEXT("Faction.USA finds DA_FactionUSA"), USAFaction && URTSFactionData::FindByTag(USATag) == USAFaction);
	TestNull(TEXT("No tag finds no faction"), URTSFactionData::FindByTag(FGameplayTag()));

	const URTSSettings* Settings = GetDefault<URTSSettings>();
	const URTSUIStyleSet* Common = Settings->CommonUIStyle.LoadSynchronous();
	const URTSUIStyleSet* Neutral = Settings->NeutralUIStyle.LoadSynchronous();
	URTSUIStyleSet* USA = LoadObject<URTSUIStyleSet>(nullptr, USAStylePath);
	if (!TestNotNull(TEXT("Common loads"), Common) || !TestNotNull(TEXT("Neutral loads"), Neutral) || !TestNotNull(TEXT("USA loads"), USA))
	{
		return false;
	}
	TestTrue(TEXT("DA_FactionUSA uses the USA style"), USAFaction && USAFaction->UIStyle.LoadSynchronous() == USA);

	URTSUIStyleSubsystem* Styles = NewObject<URTSUIStyleSubsystem>(NewObject<UGameInstance>());
	URTSFactionData* Styled = NewObject<URTSFactionData>();
	Styled->UIStyle = USA;
	const TArray<const URTSUIStyleSet*> Expected = { USA, Common, Neutral };
	TestTrue(TEXT("A faction's chain is its style, then Common, then Neutral"), Styles->GetStyleChain(Styled) == Expected);
	const TArray<const URTSUIStyleSet*> Fallbacks = { Common, Neutral };
	TestTrue(TEXT("Without a faction the chain is Common, then Neutral"), Styles->GetStyleChain(nullptr) == Fallbacks);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEntityIconTestDataTest, "WorldDivide.UI.EntityIcon.TestData", TestFlags)

bool FRTSEntityIconTestDataTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	for (const TCHAR* Directory : { TEXT("/Game/Data/Units"), TEXT("/Game/Data/Buildings"), TEXT("/Game/Data/Upgrades") })
	{
		Registry.GetAssetsByPath(FName(Directory), Assets, true);
	}

	URTSUIStyleSubsystem* Styles = NewObject<URTSUIStyleSubsystem>(NewObject<UGameInstance>());
	const FRTSUISprite& Placeholder = URTSUIStyleSubsystem::FindSpriteIn(Styles->GetStyleChain(nullptr), RTSUI::PlaceholderIcon);
	TestTrue(TEXT("Common has Icon.Placeholder"), &Placeholder != &URTSUIStyleSubsystem::GetPlaceholderSprite());

	int32 Entities = 0;
	for (const FAssetData& Asset : Assets)
	{
		const URTSEntityData* Entity = Cast<URTSEntityData>(Asset.GetAsset());
		if (!Entity)
		{
			continue;
		}
		++Entities;
		const FString Name = Asset.AssetName.ToString();
		TestFalse(*FString::Printf(TEXT("%s has a description"), *Name), Entity->Description.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s has an icon"), *Name), Entity->IconSprite.IsNone());
		const FRTSUISprite& Icon = RTSUI::FindEntityIcon(Styles->GetStyleChain(URTSFactionData::FindByTag(Entity->FactionTag)), Entity);
		TestTrue(*FString::Printf(TEXT("%s's icon %s resolves to a sprite"), *Name, *Entity->IconSprite.ToString()),
			&Icon != &Placeholder && &Icon != &URTSUIStyleSubsystem::GetPlaceholderSprite());
	}
	TestTrue(TEXT("The test units, buildings and upgrades are generated"), Entities > 0);
	return true;
}

#endif
