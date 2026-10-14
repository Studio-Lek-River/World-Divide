#include "UI/Layout/RTSUICommandlet.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/Layout/RTSLayoutValidator.h"
#include "WorldDivide.h"

namespace
{
	/** Names as a sorted JSON array of strings, so the file reads the same on every run. */
	TArray<TSharedPtr<FJsonValue>> ToJson(const TSet<FName>& Names)
	{
		TArray<FString> Sorted;
		for (const FName Name : Names)
		{
			Sorted.Add(Name.ToString());
		}
		Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Name : Sorted)
		{
			Values.Add(MakeShared<FJsonValueString>(Name));
		}
		return Values;
	}

	FString ToProjectPath(const FString& Path)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
}

URTSUICommandlet::URTSUICommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

FString URTSUICommandlet::DescribeBindings(const FRTSLayoutValidationContext& Context)
{
	TArray<TSharedPtr<FJsonValue>> Sources;
	for (const IRTSUIBindingSource* Source : RTSUIBindings::GetDefaultSources())
	{
		FRTSUIDeclarations Declarations;
		Source->Declare(Declarations);
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetArrayField(TEXT("panels"), ToJson(TSet<FName>(Source->GetPanels())));
		Object->SetArrayField(TEXT("keys"), ToJson(Declarations.Keys));
		const TSharedRef<FJsonObject> Lists = MakeShared<FJsonObject>();
		for (const TPair<FName, TSet<FName>>& List : Declarations.Lists)
		{
			Lists->SetArrayField(List.Key.ToString(), ToJson(List.Value));
		}
		Object->SetObjectField(TEXT("lists"), Lists);
		Object->SetArrayField(TEXT("actions"), ToJson(Declarations.Actions));
		Sources.Add(MakeShared<FJsonValueObject>(Object));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("sources"), Sources);
	Root->SetArrayField(TEXT("widgets"), ToJson(Context.Widgets));
	Root->SetArrayField(TEXT("colors"), ToJson(Context.Colors));
	Root->SetArrayField(TEXT("fonts"), ToJson(Context.Fonts));
	Root->SetArrayField(TEXT("sprites"), ToJson(Context.Sprites));

	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

int32 URTSUICommandlet::Main(const FString& Params)
{
	FString BindingsFile;
	FString UIDir;
	FParse::Value(*Params, TEXT("Bindings="), BindingsFile);
	FParse::Value(*Params, TEXT("Validate="), UIDir);
	if (BindingsFile.IsEmpty() == UIDir.IsEmpty())
	{
		UE_LOG(LogWorldDivide, Error, TEXT("RTSUI: give either -Bindings=<file> or -Validate=<UI dir>"));
		return 1;
	}

	FRTSLayoutValidationContext Context = FRTSLayoutValidationContext::MakeDefault();
	if (!BindingsFile.IsEmpty())
	{
		const FString Path = ToProjectPath(BindingsFile);
		if (!FFileHelper::SaveStringToFile(DescribeBindings(Context), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSUI: could not write %s"), *Path);
			return 1;
		}
		UE_LOG(LogWorldDivide, Display, TEXT("RTSUI: wrote %s"), *Path);
		return 0;
	}

	const FString Dir = ToProjectPath(UIDir);
	if (!FPaths::DirectoryExists(Dir))
	{
		UE_LOG(LogWorldDivide, Error, TEXT("RTSUI: %s does not exist"), *Dir);
		return 1;
	}
	Context.AddFolder(Dir);
	TArray<FString> Errors;
	RTSLayout::ValidateFolder(Dir, Context, Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogWorldDivide, Display, TEXT("UICHECK: %s"), *Error);
	}
	UE_LOG(LogWorldDivide, Display, TEXT("RTSUI: %d problem(s) in the layouts of %s"), Errors.Num(), *Dir);
	return Errors.IsEmpty() ? 0 : 1;
}
