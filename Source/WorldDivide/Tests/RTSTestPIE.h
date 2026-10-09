#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace RTSTests
{
	/**
	 * Starts a standalone, single-player PIE session of the open map. Unlike FStartPIECommand it does not take the
	 * editor's saved play settings, so a local Listen Server setup does not add a second client the map has no seat for.
	 */
	class FRTSStartStandalonePIE : public IAutomationLatentCommand
	{
	public:
		virtual bool Update() override
		{
			// A new settings object starts from the saved ones, so every field that adds players is set here.
			ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();
			PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
			PlaySettings->SetPlayNumberOfClients(1);
			PlaySettings->bLaunchSeparateServer = false;
			PlaySettings->SetRunUnderOneProcess(true);

			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			Params.EditorPlaySettings = PlaySettings;
			GEditor->RequestPlaySession(Params);
			return true;
		}
	};
}

#endif
