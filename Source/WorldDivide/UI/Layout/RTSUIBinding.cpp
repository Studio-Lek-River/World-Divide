#include "UI/Layout/RTSUIBinding.h"

#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSFactionData.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"

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
	const FName PortraitPlaceholder(TEXT("Portrait.Placeholder"));
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

	FString FormatNumber(int32 Number)
	{
		return FText::AsNumber(Number).ToString();
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
				OutValue = FRTSUIValue::MakeSprite(PortraitPlaceholder);
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
}

TArray<const IRTSUIBindingSource*> RTSUIBindings::GetDefaultSources()
{
	static const FRTSPlayerBindings PlayerBindings;
	static const FRTSMatchBindings MatchBindings;
	static const FRTSCommanderBindings CommanderBindings;
	static const FRTSMapButtonsBindings MapButtonsBindings;
	return { &PlayerBindings, &MatchBindings, &CommanderBindings, &MapButtonsBindings };
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
