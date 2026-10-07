#include "Data/RTSUpgradeData.h"

float URTSUpgradeData::GetMultiplier(const TArray<TObjectPtr<URTSUpgradeData>>& Upgrades, const URTSEntityData& Data, ERTSUpgradeStat Stat)
{
	float Multiplier = 1.f;
	for (const TObjectPtr<URTSUpgradeData>& Upgrade : Upgrades)
	{
		if (Upgrade && Upgrade->Stat == Stat && Upgrade->Targets.Contains(&Data))
		{
			Multiplier *= Upgrade->Multiplier;
		}
	}
	return Multiplier;
}
