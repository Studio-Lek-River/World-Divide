#include "Data/RTSFactionData.h"

#include "Core/RTSSettings.h"

const URTSFactionData* URTSFactionData::FindByTag(FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}
	const URTSSettings* Settings = GetDefault<URTSSettings>();
	TArray<TSoftObjectPtr<URTSFactionData>> Candidates = Settings->Factions;
	Candidates.AddUnique(Settings->DefaultFaction);
	for (const TSoftObjectPtr<URTSFactionData>& Candidate : Candidates)
	{
		const URTSFactionData* Faction = Candidate.IsNull() ? nullptr : Candidate.LoadSynchronous();
		if (Faction && Faction->FactionTag == Tag)
		{
			return Faction;
		}
	}
	return nullptr;
}
