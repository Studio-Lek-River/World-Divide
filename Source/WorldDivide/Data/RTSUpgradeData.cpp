#include "Data/RTSUpgradeData.h"

float URTSUpgradeData::GetMultiplier(const TArray<TObjectPtr<URTSUpgradeData>>& Upgrades, const URTSEntityData& Data, ERTSUpgradeStat Stat)
{
	float Multiplier = 1.f;
	for (const TObjectPtr<URTSUpgradeData>& Upgrade : Upgrades)
	{
		if (Upgrade && Upgrade->Stat == Stat && Upgrade->AppliesTo(Data))
		{
			Multiplier *= Upgrade->Multiplier;
		}
	}
	return Multiplier;
}

FText URTSUpgradeData::GetEffectText() const
{
	const FText StatName = Stat == ERTSUpgradeStat::MaxHealth ? NSLOCTEXT("RTSUpgrade", "MaxHealth", "Maximum health")
		: NSLOCTEXT("RTSUpgrade", "WeaponDamage", "Weapon damage");
	FNumberFormattingOptions Format;
	Format.MinimumFractionalDigits = 0;
	Format.MaximumFractionalDigits = 2;
	return FText::Format(NSLOCTEXT("RTSUpgrade", "Effect", "{0} x{1}"), StatName, FText::AsNumber(Multiplier, &Format));
}

TArray<const URTSUpgradeData*> URTSUpgradeData::GetInstalled(const TArray<TObjectPtr<URTSUpgradeData>>& Upgrades, const URTSEntityData& Data)
{
	TArray<const URTSUpgradeData*> Installed;
	for (const TObjectPtr<URTSUpgradeData>& Upgrade : Upgrades)
	{
		if (Upgrade && Upgrade->AppliesTo(Data))
		{
			Installed.AddUnique(Upgrade.Get());
		}
	}
	return Installed;
}
