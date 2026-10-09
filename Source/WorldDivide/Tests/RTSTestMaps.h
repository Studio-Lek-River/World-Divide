#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/RTSSettings.h"
#include "Data/RTSMapData.h"
#include "Misc/AutomationTest.h"

namespace RTSTests
{
	/** The maps only the tests play: the lobby does not offer them, the lobby smoke tests swap them in. */
	inline TArray<TSoftObjectPtr<URTSMapData>> GetTestSkirmishMaps()
	{
		return {
			TSoftObjectPtr<URTSMapData>(FSoftObjectPath(TEXT("/Game/Data/Maps/DA_MapSkirmish.DA_MapSkirmish"))),
			TSoftObjectPtr<URTSMapData>(FSoftObjectPath(TEXT("/Game/Data/Maps/DA_MapDuel.DA_MapDuel"))),
		};
	}

	/** The maps the map tests check: those the lobby offers, then the test maps. */
	inline TArray<TSoftObjectPtr<URTSMapData>> GetCheckedMaps()
	{
		TArray<TSoftObjectPtr<URTSMapData>> Maps = GetDefault<URTSSettings>()->SkirmishMaps;
		Maps.Append(GetTestSkirmishMaps());
		return Maps;
	}

	/** The index of the map asset named AssetName in URTSSettings::SkirmishMaps, or INDEX_NONE. */
	inline int32 FindMapIndex(const TCHAR* AssetName)
	{
		return GetDefault<URTSSettings>()->SkirmishMaps.IndexOfByPredicate(
			[AssetName](const TSoftObjectPtr<URTSMapData>& Map) { return Map.GetAssetName() == AssetName; });
	}

	/**
	 * Makes the lobby offer the test maps, L_Skirmish first, while it lives. Add it as a test's last latent command: it is
	 * made when the test queues it, before the play session starts, and destroyed once the session has ended, or when a
	 * failure clears the queue, which gives the lobby its own maps back. Only the settings in memory change, never the ini.
	 */
	class FRTSUseTestLobbyMaps : public IAutomationLatentCommand
	{
	public:
		FRTSUseTestLobbyMaps()
			: SavedMaps(GetDefault<URTSSettings>()->SkirmishMaps)
		{
			GetMutableDefault<URTSSettings>()->SkirmishMaps = GetTestSkirmishMaps();
		}

		virtual ~FRTSUseTestLobbyMaps() override
		{
			GetMutableDefault<URTSSettings>()->SkirmishMaps = SavedMaps;
		}

		virtual bool Update() override { return true; }

	private:
		TArray<TSoftObjectPtr<URTSMapData>> SavedMaps;
	};
}

#endif
