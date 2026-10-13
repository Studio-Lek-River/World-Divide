#include "RTSConsoleToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Regex.h"
#include "Misc/ConfigCacheIni.h"
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

	// The AI team looks and plays; editor Python and commands that save or delete stay with the producer.
	TArray<FString> BlockedPatterns;
	GConfig->GetArray(TEXT("WorldDivideMCP"), TEXT("BlockedConsoleCommands"), BlockedPatterns, GEditorPerProjectIni);
	for (const FString& Pattern : BlockedPatterns)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern, ERegexPatternFlags::CaseInsensitive), Trimmed);
		if (Matcher.FindNext())
		{
			FFrame::KismetExecutionMessage(*FString::Printf(TEXT("ExecuteConsoleCommand: '%s' is not allowed over MCP; ask the producer."), *Trimmed), ELogVerbosity::Error);
			return FString();
		}
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
