#include "RTSConsoleToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/StringOutputDevice.h"
#include "UObject/Script.h"

FString URTSConsoleToolset::ExecuteConsoleCommand(const FString& Command)
{
	const FString Trimmed = Command.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		FFrame::KismetExecutionMessage(TEXT("ExecuteConsoleCommand: the command is empty."), ELogVerbosity::Error);
		return FString();
	}
	if (!GEditor)
	{
		FFrame::KismetExecutionMessage(TEXT("ExecuteConsoleCommand: no editor is running."), ELogVerbosity::Error);
		return FString();
	}

	// Most commands report through the log rather than the device they are given, so capture both.
	FStringOutputDevice Output;
	Output.SetAutoEmitLineTerminator(true);
	GLog->AddOutputDevice(&Output);

	bool bHandled = true;
	APlayerController* PC = GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
	if (PC && PC->Player)
	{
		// The same path the in-game console takes, so viewport commands such as stat reach the PIE viewport.
		Output += PC->ConsoleCommand(Trimmed, false);
	}
	else
	{
		bHandled = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Trimmed, Output);
	}

	GLog->RemoveOutputDevice(&Output);

	if (!bHandled)
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("ExecuteConsoleCommand: '%s' is not a known command."), *Trimmed), ELogVerbosity::Error);
		return FString();
	}
	return Output.IsEmpty() ? FString::Printf(TEXT("'%s' ran and printed nothing."), *Trimmed) : FString(Output);
}
