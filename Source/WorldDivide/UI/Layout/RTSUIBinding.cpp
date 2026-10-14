#include "UI/Layout/RTSUIBinding.h"

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"
#include "UI/Layout/RTSLayoutElement.h"
#include "UI/RTSUI.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"

FRTSUIValue FRTSUIValue::MakeText(const FString& InText)
{
	FRTSUIValue Value;
	Value.Type = EType::Text;
	Value.String = InText;
	return Value;
}

FRTSUIValue FRTSUIValue::MakeNumber(double InNumber)
{
	FRTSUIValue Value;
	Value.Type = EType::Number;
	Value.Number = InNumber;
	return Value;
}

FRTSUIValue FRTSUIValue::MakeBool(bool bInBool)
{
	FRTSUIValue Value;
	Value.Type = EType::Bool;
	Value.bBool = bInBool;
	return Value;
}

FRTSUIValue FRTSUIValue::MakeProgress(float InProgress)
{
	FRTSUIValue Value;
	Value.Type = EType::Progress;
	Value.Number = FMath::Clamp(InProgress, 0.f, 1.f);
	return Value;
}

FRTSUIValue FRTSUIValue::MakeSprite(FName InSprite)
{
	FRTSUIValue Value;
	Value.Type = EType::Sprite;
	Value.String = InSprite.IsNone() ? FString() : InSprite.ToString();
	return Value;
}

FRTSUIValue FRTSUIValue::MakeColor(FName InColor)
{
	FRTSUIValue Value;
	Value.Type = EType::Color;
	Value.String = InColor.IsNone() ? FString() : InColor.ToString();
	return Value;
}

bool FRTSUIValue::IsTruthy() const
{
	switch (Type)
	{
	case EType::Bool:
		return bBool;
	case EType::Number:
	case EType::Progress:
		return Number != 0.0;
	case EType::None:
		return false;
	default:
		return !String.IsEmpty();
	}
}

FString FRTSUIValue::ToString() const
{
	switch (Type)
	{
	case EType::Bool:
		return bBool ? TEXT("true") : TEXT("false");
	case EType::Number:
	case EType::Progress:
		return FText::AsNumber(Number).ToString();
	case EType::None:
		return FString();
	default:
		return String;
	}
}

void FRTSUIDeclarations::Append(const FRTSUIDeclarations& Other)
{
	Keys.Append(Other.Keys);
	Actions.Append(Other.Actions);
	for (const TPair<FName, TSet<FName>>& List : Other.Lists)
	{
		Lists.FindOrAdd(List.Key).Append(List.Value);
	}
}

namespace
{
	const FName CreditsKey(TEXT("player.credits"));
	const FName PowerProducedKey(TEXT("player.power.produced"));
	const FName PowerConsumedKey(TEXT("player.power.consumed"));
	const FName PowerLowKey(TEXT("player.power.low"));
	const FName PowerColorKey(TEXT("player.power.color"));
	const FName PowerRatioKey(TEXT("player.power.ratio"));
	const FName FactionKey(TEXT("player.faction"));
	const FName RankList(TEXT("player.rank"));
	const FName RankEarnedKey(TEXT("earned"));
	constexpr int32 RankStars = 5;

	const FName CommanderNameKey(TEXT("commander.name"));
	const FName CommanderPortraitKey(TEXT("commander.portrait"));
	const FName PowersList(TEXT("commander.powers"));
	const FName PowerIconKey(TEXT("icon"));
	const FName PowerNameKey(TEXT("name"));
	const FName PowerLockedKey(TEXT("locked"));
	const FName PowerReadyKey(TEXT("ready"));
	const FName PowerChargeKey(TEXT("charge"));
	const FName PowerLevelKey(TEXT("level"));
	const FName PowerLevelMaxKey(TEXT("levelMax"));
	const FName UsePowerAction(TEXT("UsePower"));
	const FName PortraitGeneral(TEXT("Portrait.General"));
	const FName PowerIconPlaceholder(TEXT("Icon.Placeholder"));
	constexpr int32 PowerSlots = 9;

	const FName OverlaysEnabledKey(TEXT("map.overlays.enabled"));
	const FName PowersEnabledKey(TEXT("map.powers.enabled"));
	const FName ObjectivesEnabledKey(TEXT("map.objectives.enabled"));
	const FName OpenOverlaysAction(TEXT("OpenOverlays"));
	const FName OpenGeneralPowersAction(TEXT("OpenGeneralPowers"));
	const FName OpenObjectivesAction(TEXT("OpenObjectives"));

	const FName ResultKey(TEXT("match.result"));
	const FName ResultTitleKey(TEXT("match.result.title"));
	const FName ResultMessageKey(TEXT("match.result.message"));
	const FName ResultColorKey(TEXT("match.result.color"));
	const FName CanRestartKey(TEXT("match.canRestart"));
	const FName RestartAction(TEXT("Restart"));
	const FName ResumeAction(TEXT("Resume"));
	const FName LeaveAction(TEXT("Leave"));

	const FName QueueVisibleKey(TEXT("queue.visible"));
	const FName QueueLowPowerKey(TEXT("queue.lowPower"));
	const FName QueueList(TEXT("queue"));
	const FName QueueActiveKey(TEXT("isActive"));
	const FName QueueProgressKey(TEXT("progress"));
	const FName QueueTimeLeftKey(TEXT("timeLeft"));
	const FName CancelQueueItemAction(TEXT("CancelQueueItem"));

	const FName SelectionModeKey(TEXT("selection.mode"));
	const FName SelectionIconKey(TEXT("selection.icon"));
	const FName SelectionNameKey(TEXT("selection.name"));
	const FName SelectionDescriptionKey(TEXT("selection.description"));
	const FName SelectionHealthKey(TEXT("selection.health"));
	const FName SelectionHealthTextKey(TEXT("selection.healthText"));
	const FName SelectionHealthColorKey(TEXT("selection.healthColor"));
	const FName SelectionTeamColorKey(TEXT("selection.teamColor"));
	const FName SelectionConstructingKey(TEXT("selection.isConstructing"));
	const FName SelectionConstructionProgressKey(TEXT("selection.constructionProgress"));
	const FName SelectionAircraftKey(TEXT("selection.isAircraft"));
	const FName SelectionAmmoKey(TEXT("selection.ammo"));
	const FName SelectionAmmoMaxKey(TEXT("selection.ammoMax"));
	const FName SelectionRearmingKey(TEXT("selection.isRearming"));
	const FName SelectionCanCancelKey(TEXT("selection.canCancelConstruction"));
	const FName SelectionUpgradesList(TEXT("selection.upgrades"));
	const FName SelectionGroupsList(TEXT("selection.groups"));
	const FName ItemIconKey(TEXT("icon"));
	const FName ItemNameKey(TEXT("name"));
	const FName ItemCountKey(TEXT("count"));
	const FName ItemHealthKey(TEXT("health"));
	const FName ItemHealthColorKey(TEXT("healthColor"));
	const FName SelectGroupAction(TEXT("SelectGroup"));
	const FName CancelConstructionAction(TEXT("CancelConstruction"));
	const TCHAR* const SelectionModeNone = TEXT("None");
	const TCHAR* const SelectionModeSingle = TEXT("Single");
	const TCHAR* const SelectionModeMulti = TEXT("Multi");

	/** Health at or above this fraction shows Positive, at or above HealthWarningAt Warning, below it Danger. */
	constexpr float HealthPositiveAt = 0.6f;
	constexpr float HealthWarningAt = 0.3f;

	FString FormatNumber(int32 Number)
	{
		return FText::AsNumber(Number).ToString();
	}

	FName GetHealthColor(float Fraction)
	{
		return Fraction >= HealthPositiveAt ? FName(TEXT("Positive")) : Fraction >= HealthWarningAt ? FName(TEXT("Warning")) : FName(TEXT("Danger"));
	}

	FName GetIconSprite(const URTSEntityData* Data)
	{
		return Data && !Data->IconSprite.IsNone() ? Data->IconSprite : RTSUI::PlaceholderIcon;
	}

	/** The local player's resources, for the resource bar. */
	class FRTSPlayerBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ CreditsKey, PowerProducedKey, PowerConsumedKey, PowerLowKey, PowerColorKey, PowerRatioKey, FactionKey });
			Out.Lists.Add(RankList, { RankEarnedKey });
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("ResourceBar") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			const ARTSPlayerState* State = Context.Player ? Context.Player->GetPlayerState<ARTSPlayerState>() : nullptr;
			const URTSFactionData* Faction = State ? State->GetFaction() : nullptr;
			const bool bLow = State && State->IsLowPower();
			if (Key == CreditsKey)
			{
				OutValue = FRTSUIValue::MakeText(FormatNumber(State ? State->GetCredits() : 0));
			}
			else if (Key == PowerProducedKey)
			{
				OutValue = FRTSUIValue::MakeText(FormatNumber(State ? State->GetPowerProduced() : 0));
			}
			else if (Key == PowerConsumedKey)
			{
				OutValue = FRTSUIValue::MakeText(FormatNumber(State ? State->GetPowerConsumed() : 0));
			}
			else if (Key == PowerLowKey)
			{
				OutValue = FRTSUIValue::MakeBool(bLow);
			}
			else if (Key == PowerColorKey)
			{
				OutValue = FRTSUIValue::MakeColor(bLow ? TEXT("Danger") : TEXT("Text"));
			}
			else if (Key == PowerRatioKey)
			{
				OutValue = FRTSUIValue::MakeProgress(State ? RTSUIBindings::GetPowerRatio(State->GetPowerProduced(), State->GetPowerConsumed()) : 0.f);
			}
			else if (Key == FactionKey)
			{
				OutValue = FRTSUIValue::MakeText(Faction ? Faction->DisplayName.ToString() : FString());
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const override
		{
			return List == RankList ? RankStars : 0;
		}

		virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			// Rank comes in M5; until then no star is earned.
			if (Context.List == RankList && Key == RankEarnedKey)
			{
				OutValue = FRTSUIValue::MakeBool(false);
				return true;
			}
			return false;
		}
	};

	/** The commander bar: the player's general and the general power slots. Generals (#73) and their powers (M5) come later. */
	class FRTSCommanderBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ CommanderNameKey, CommanderPortraitKey });
			Out.Lists.Add(PowersList, { PowerIconKey, PowerNameKey, PowerLockedKey, PowerReadyKey, PowerChargeKey, PowerLevelKey, PowerLevelMaxKey });
			Out.Actions.Add(UsePowerAction);
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("CommanderBar") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Key == CommanderNameKey)
			{
				// The faction stands in for the general until generals exist.
				const ARTSPlayerState* State = Context.Player ? Context.Player->GetPlayerState<ARTSPlayerState>() : nullptr;
				const URTSFactionData* Faction = State ? State->GetFaction() : nullptr;
				OutValue = FRTSUIValue::MakeText(Faction ? Faction->DisplayName.ToString() : FString());
			}
			else if (Key == CommanderPortraitKey)
			{
				// Each faction's sprite set shows its Standard general until #73 gives every general a portrait.
				OutValue = FRTSUIValue::MakeSprite(PortraitGeneral);
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const override
		{
			return List == PowersList ? PowerSlots : 0;
		}

		virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Context.List != PowersList)
			{
				return false;
			}
			// Every slot stays locked until general powers exist.
			if (Key == PowerIconKey)
			{
				OutValue = FRTSUIValue::MakeSprite(PowerIconPlaceholder);
			}
			else if (Key == PowerNameKey)
			{
				OutValue = FRTSUIValue::MakeText(FString());
			}
			else if (Key == PowerLockedKey || Key == PowerReadyKey)
			{
				OutValue = FRTSUIValue::MakeBool(Key == PowerLockedKey);
			}
			else if (Key == PowerChargeKey)
			{
				OutValue = FRTSUIValue::MakeProgress(0.f);
			}
			else if (Key == PowerLevelKey || Key == PowerLevelMaxKey)
			{
				OutValue = FRTSUIValue::MakeNumber(0.0);
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			// Nothing to use until general powers exist.
			return Action == UsePowerAction;
		}
	};

	/** The buttons under the minimap. Each screen they open comes in its own issue; until then they stay disabled. */
	class FRTSMapButtonsBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ OverlaysEnabledKey, PowersEnabledKey, ObjectivesEnabledKey });
			Out.Actions.Append({ OpenOverlaysAction, OpenGeneralPowersAction, OpenObjectivesAction });
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("MapButtons") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Key == OverlaysEnabledKey || Key == PowersEnabledKey || Key == ObjectivesEnabledKey)
			{
				OutValue = FRTSUIValue::MakeBool(false);
				return true;
			}
			return false;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			return Action == OpenOverlaysAction || Action == OpenGeneralPowersAction || Action == OpenObjectivesAction;
		}
	};

	/** The match result screen and the in-game menu. */
	class FRTSMatchBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ ResultKey, ResultTitleKey, ResultMessageKey, ResultColorKey, CanRestartKey });
			Out.Actions.Append({ RestartAction, ResumeAction, LeaveAction });
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("MatchResult"), TEXT("InGameMenu") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			const ERTSMatchResult Result = Controller ? Controller->GetMatchResult() : ERTSMatchResult::InProgress;
			if (Key == ResultKey)
			{
				OutValue = FRTSUIValue::MakeText(Result == ERTSMatchResult::Victory ? TEXT("Victory")
					: Result == ERTSMatchResult::Defeat ? TEXT("Defeat") : TEXT(""));
			}
			else if (Key == ResultTitleKey)
			{
				OutValue = FRTSUIValue::MakeText(ARTSHUD::GetMatchResultTitle(Result));
			}
			else if (Key == ResultMessageKey)
			{
				OutValue = FRTSUIValue::MakeText(ARTSHUD::GetMatchResultMessage(Result));
			}
			else if (Key == ResultColorKey)
			{
				OutValue = FRTSUIValue::MakeColor(Result == ERTSMatchResult::Victory ? TEXT("Positive") : TEXT("Danger"));
			}
			else if (Key == CanRestartKey)
			{
				OutValue = FRTSUIValue::MakeBool(Controller && Controller->CanRestart());
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			if (Action == RestartAction)
			{
				if (Controller)
				{
					Controller->RequestRestart();
				}
			}
			else if (Action == ResumeAction)
			{
				if (Controller)
				{
					Controller->SetInGameMenuOpen(false);
				}
			}
			else if (Action == LeaveAction)
			{
				if (Controller)
				{
					Controller->LeaveMatch();
				}
			}
			else
			{
				return false;
			}
			return true;
		}
	};

	/**
	 * The selection panel: the one selected unit or building in full, or the selected units grouped by type. Reads the
	 * controller's selection and the replicated state of what is selected; selecting a group stays local.
	 */
	class FRTSSelectionBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ SelectionModeKey, SelectionIconKey, SelectionNameKey, SelectionDescriptionKey, SelectionHealthKey,
				SelectionHealthTextKey, SelectionHealthColorKey, SelectionTeamColorKey, SelectionConstructingKey,
				SelectionConstructionProgressKey, SelectionAircraftKey, SelectionAmmoKey, SelectionAmmoMaxKey, SelectionRearmingKey,
				SelectionCanCancelKey });
			Out.Lists.Add(SelectionUpgradesList, { ItemIconKey, ItemNameKey });
			Out.Lists.Add(SelectionGroupsList, { ItemIconKey, ItemNameKey, ItemCountKey, ItemHealthKey, ItemHealthColorKey });
			Out.Actions.Append({ SelectGroupAction, CancelConstructionAction });
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("Selection") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			ARTSBuilding* Building = Controller ? Controller->GetSelectedBuilding() : nullptr;
			const ARTSUnit* Unit = GetSingleUnit(Controller);
			const URTSEntityData* Data = Building ? static_cast<const URTSEntityData*>(Building->GetBuildingData())
				: Unit ? static_cast<const URTSEntityData*>(Unit->GetUnitData()) : nullptr;
			const URTSHealthComponent* Health = Building ? Building->GetHealthComponent() : Unit ? Unit->GetHealthComponent() : nullptr;
			const URTSAircraftData* Aircraft = Unit ? Cast<URTSAircraftData>(Unit->GetUnitData()) : nullptr;
			const float HealthFraction = Health ? Health->GetHealthFraction() : 0.f;

			if (Key == SelectionModeKey)
			{
				const bool bMulti = !Building && Controller && Controller->GetSelectedUnits().Num() > 1;
				OutValue = FRTSUIValue::MakeText(bMulti ? SelectionModeMulti : Building || Unit ? SelectionModeSingle : SelectionModeNone);
			}
			else if (Key == SelectionIconKey)
			{
				OutValue = FRTSUIValue::MakeSprite(Data ? GetIconSprite(Data) : NAME_None);
			}
			else if (Key == SelectionNameKey)
			{
				OutValue = FRTSUIValue::MakeText(Data ? Data->DisplayName.ToString() : FString());
			}
			else if (Key == SelectionDescriptionKey)
			{
				OutValue = FRTSUIValue::MakeText(Data ? Data->Description.ToString() : FString());
			}
			else if (Key == SelectionHealthKey)
			{
				OutValue = FRTSUIValue::MakeProgress(HealthFraction);
			}
			else if (Key == SelectionHealthTextKey)
			{
				OutValue = FRTSUIValue::MakeText(Health ? FormatHealth(*Health) : FString());
			}
			else if (Key == SelectionHealthColorKey)
			{
				OutValue = FRTSUIValue::MakeColor(GetHealthColor(HealthFraction));
			}
			else if (Key == SelectionTeamColorKey)
			{
				OutValue = FRTSUIValue::MakeColor(RTSLayout::TeamColor);
			}
			else if (Key == SelectionConstructingKey)
			{
				OutValue = FRTSUIValue::MakeBool(Building && !Building->IsConstructed());
			}
			else if (Key == SelectionConstructionProgressKey)
			{
				OutValue = FRTSUIValue::MakeProgress(Building ? Building->GetConstructionProgress() : 0.f);
			}
			else if (Key == SelectionAircraftKey)
			{
				OutValue = FRTSUIValue::MakeBool(Aircraft != nullptr);
			}
			else if (Key == SelectionAmmoKey)
			{
				OutValue = FRTSUIValue::MakeNumber(Aircraft ? FMath::Max(Unit->GetWeaponComponent()->GetAmmo(), 0) : 0);
			}
			else if (Key == SelectionAmmoMaxKey)
			{
				OutValue = FRTSUIValue::MakeNumber(Aircraft ? Aircraft->AmmoPerSortie : 0);
			}
			else if (Key == SelectionRearmingKey)
			{
				OutValue = FRTSUIValue::MakeBool(Aircraft && Unit->GetAircraftComponent()->IsRearming());
			}
			else if (Key == SelectionCanCancelKey)
			{
				// Once the match is over every order is refused.
				OutValue = FRTSUIValue::MakeBool(Controller && Controller->GetMatchResult() == ERTSMatchResult::InProgress
					&& Controller->CanCancelConstruction(Building));
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const override
		{
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			if (!Controller)
			{
				return 0;
			}
			if (List == SelectionGroupsList)
			{
				// A single unit is shown in full, not as a group.
				return Controller->GetSelectedUnits().Num() > 1 ? Controller->GetSelectionGroups().Num() : 0;
			}
			if (List == SelectionUpgradesList)
			{
				return GetCompletedUpgrades(*Controller).Num();
			}
			return 0;
		}

		virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			if (!Controller)
			{
				return false;
			}
			if (Context.List == SelectionUpgradesList)
			{
				const TArray<const URTSUpgradeData*> Upgrades = GetCompletedUpgrades(*Controller);
				const URTSUpgradeData* Upgrade = Upgrades.IsValidIndex(Context.Index) ? Upgrades[Context.Index] : nullptr;
				if (Key == ItemIconKey)
				{
					OutValue = FRTSUIValue::MakeSprite(Upgrade ? GetIconSprite(Upgrade) : NAME_None);
				}
				else if (Key == ItemNameKey)
				{
					OutValue = FRTSUIValue::MakeText(Upgrade ? Upgrade->DisplayName.ToString() : FString());
				}
				else
				{
					return false;
				}
				return true;
			}
			if (Context.List != SelectionGroupsList)
			{
				return false;
			}
			const TArray<FRTSSelectionGroup>& Groups = Controller->GetSelectionGroups();
			const FRTSSelectionGroup* Group = Groups.IsValidIndex(Context.Index) ? &Groups[Context.Index] : nullptr;
			const float GroupHealth = Group ? GetGroupHealth(*Group) : 0.f;
			if (Key == ItemIconKey)
			{
				OutValue = FRTSUIValue::MakeSprite(Group ? GetIconSprite(Group->Data) : NAME_None);
			}
			else if (Key == ItemNameKey)
			{
				OutValue = FRTSUIValue::MakeText(Group && Group->Data ? Group->Data->DisplayName.ToString() : FString());
			}
			else if (Key == ItemCountKey)
			{
				OutValue = FRTSUIValue::MakeNumber(Group ? CountLiving(*Group) : 0);
			}
			else if (Key == ItemHealthKey)
			{
				OutValue = FRTSUIValue::MakeProgress(GroupHealth);
			}
			else if (Key == ItemHealthColorKey)
			{
				OutValue = FRTSUIValue::MakeColor(GetHealthColor(GroupHealth));
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			if (Action == SelectGroupAction)
			{
				if (Controller)
				{
					Controller->SelectGroup(Context.Index);
				}
			}
			else if (Action == CancelConstructionAction)
			{
				if (Controller)
				{
					Controller->RequestCancelConstruction(Controller->GetSelectedBuilding());
				}
			}
			else
			{
				return false;
			}
			return true;
		}

	private:
		/** The selected unit while it is the only one, or null. */
		static const ARTSUnit* GetSingleUnit(const ARTSPlayerController* Controller)
		{
			if (!Controller || Controller->GetSelectedUnits().Num() != 1)
			{
				return nullptr;
			}
			return Controller->GetSelectedUnits()[0].Get();
		}

		/** "350 / 500". */
		static FString FormatHealth(const URTSHealthComponent& Health)
		{
			return FString::Printf(TEXT("%s / %s"), *FormatNumber(FMath::CeilToInt(Health.GetHealth())),
				*FormatNumber(FMath::CeilToInt(Health.GetMaxHealth())));
		}

		/**
		 * What the selected building researches that the player has completed. Upgrades belong to the player, so this is
		 * the building type's list, not what this very building researched.
		 */
		static TArray<const URTSUpgradeData*> GetCompletedUpgrades(const ARTSPlayerController& Controller)
		{
			TArray<const URTSUpgradeData*> Completed;
			const ARTSBuilding* Building = Controller.GetSelectedBuilding();
			const URTSBuildingData* Data = Building ? Building->GetBuildingData() : nullptr;
			if (Data)
			{
				for (const URTSUpgradeData* Upgrade : Data->Upgrades)
				{
					if (Upgrade && Controller.HasUpgrade(*Upgrade))
					{
						Completed.Add(Upgrade);
					}
				}
			}
			return Completed;
		}

		static int32 CountLiving(const FRTSSelectionGroup& Group)
		{
			return Group.Units.FilterByPredicate([](const TWeakObjectPtr<ARTSUnit>& Unit) { return Unit.IsValid(); }).Num();
		}

		/** The group's health over its maximum health, summed over its units. */
		static float GetGroupHealth(const FRTSSelectionGroup& Group)
		{
			float Health = 0.f;
			float MaxHealth = 0.f;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Group.Units)
			{
				if (Unit.IsValid())
				{
					Health += Unit->GetHealthComponent()->GetHealth();
					MaxHealth += Unit->GetHealthComponent()->GetMaxHealth();
				}
			}
			return MaxHealth > 0.f ? Health / MaxHealth : 0.f;
		}
	};

	/**
	 * The production queue inside the selection panel: the selected production building's replicated queue, the front entry's
	 * progress and the time each entry takes at the owner's production rate. Clicking an entry asks the server to cancel it.
	 */
	class FRTSProductionQueueBindings : public IRTSUIBindingSource
	{
	public:
		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ QueueVisibleKey, QueueLowPowerKey });
			Out.Lists.Add(QueueList, { ItemIconKey, ItemNameKey, QueueActiveKey, QueueProgressKey, QueueTimeLeftKey });
			Out.Actions.Add(CancelQueueItemAction);
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("ProductionQueue") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			if (Key == QueueVisibleKey)
			{
				// Once the match is over every order is refused, as the produce menu hides.
				OutValue = FRTSUIValue::MakeBool(Controller && Controller->GetMatchResult() == ERTSMatchResult::InProgress
					&& Controller->GetProductionBuilding());
			}
			else if (Key == QueueLowPowerKey)
			{
				OutValue = FRTSUIValue::MakeBool(GetRate(Controller) < 1.f);
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const override
		{
			const URTSProductionComponent* Production = List == QueueList ? GetProduction(Cast<ARTSPlayerController>(Context.Player)) : nullptr;
			return Production ? Production->GetQueue().Num() : 0;
		}

		virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Context.List != QueueList)
			{
				return false;
			}
			const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			const URTSProductionComponent* Production = GetProduction(Controller);
			const URTSEntityData* Data = Production && Production->GetQueue().IsValidIndex(Context.Index) ? Production->GetQueue()[Context.Index].Get() : nullptr;
			const bool bActive = Data && Context.Index == 0;
			const float Progress = bActive ? Production->GetProgress() : 0.f;
			if (Key == ItemIconKey)
			{
				OutValue = FRTSUIValue::MakeSprite(Data ? GetIconSprite(Data) : NAME_None);
			}
			else if (Key == ItemNameKey)
			{
				OutValue = FRTSUIValue::MakeText(Data ? Data->DisplayName.ToString() : FString());
			}
			else if (Key == QueueActiveKey)
			{
				OutValue = FRTSUIValue::MakeBool(bActive);
			}
			else if (Key == QueueProgressKey)
			{
				OutValue = FRTSUIValue::MakeProgress(Progress);
			}
			else if (Key == QueueTimeLeftKey)
			{
				OutValue = FRTSUIValue::MakeText(Data
					? RTSUIBindings::FormatTimeLeft(RTSUIBindings::GetQueueTimeLeft(Data->BuildTime, Progress, GetRate(Controller)))
					: FString());
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			if (Action != CancelQueueItemAction)
			{
				return false;
			}
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
			ARTSBuilding* Building = Controller ? Controller->GetProductionBuilding() : nullptr;
			const TArray<TObjectPtr<URTSEntityData>>* Queue = Building ? &Building->GetProductionComponent()->GetQueue() : nullptr;
			if (Queue && Queue->IsValidIndex(Context.Index))
			{
				// The existing request takes the last queued entry of that kind, keeping progress on the front one.
				Controller->RequestCancelProduction(Building, (*Queue)[Context.Index]);
			}
			return true;
		}

	private:
		static const URTSProductionComponent* GetProduction(const ARTSPlayerController* Controller)
		{
			const ARTSBuilding* Building = Controller ? Controller->GetProductionBuilding() : nullptr;
			return Building ? Building->GetProductionComponent() : nullptr;
		}

		/** The rate the server builds the selected building's queue at: its team's, 1 with nothing selected. */
		static float GetRate(const ARTSPlayerController* Controller)
		{
			const ARTSBuilding* Building = Controller ? Controller->GetProductionBuilding() : nullptr;
			return Building ? ARTSPlayerState::GetProductionRateForTeam(*Building->GetWorld(), Building->GetTeamId()) : 1.f;
		}
	};
}

TArray<const IRTSUIBindingSource*> RTSUIBindings::GetDefaultSources()
{
	static const FRTSPlayerBindings PlayerBindings;
	static const FRTSMatchBindings MatchBindings;
	static const FRTSCommanderBindings CommanderBindings;
	static const FRTSMapButtonsBindings MapButtonsBindings;
	static const FRTSSelectionBindings SelectionBindings;
	static const FRTSProductionQueueBindings ProductionQueueBindings;
	return { &PlayerBindings, &MatchBindings, &CommanderBindings, &MapButtonsBindings, &SelectionBindings, &ProductionQueueBindings };
}

FRTSUIDeclarations RTSUIBindings::Declare(TConstArrayView<const IRTSUIBindingSource*> Sources)
{
	FRTSUIDeclarations Declarations;
	for (const IRTSUIBindingSource* Source : Sources)
	{
		Source->Declare(Declarations);
	}
	return Declarations;
}

float RTSUIBindings::GetPowerRatio(int32 Produced, int32 Consumed)
{
	if (Produced <= 0)
	{
		return Consumed > 0 ? 1.f : 0.f;
	}
	return FMath::Clamp(static_cast<float>(Consumed) / Produced, 0.f, 1.f);
}

float RTSUIBindings::GetQueueTimeLeft(float BuildTime, float Progress, float Rate)
{
	// The production component finishes an entry with no BuildTime at once.
	if (BuildTime <= 0.f)
	{
		return 0.f;
	}
	const float Left = (1.f - FMath::Clamp(Progress, 0.f, 1.f)) * BuildTime;
	return Rate > 0.f ? Left / Rate : Left;
}

FString RTSUIBindings::FormatTimeLeft(float Seconds)
{
	const int32 Whole = FMath::Max(FMath::CeilToInt(Seconds), 0);
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}
