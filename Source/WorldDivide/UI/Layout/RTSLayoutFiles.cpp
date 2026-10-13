#include "UI/Layout/RTSLayoutFiles.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const TCHAR* RTSLayoutFiles::Suffix = TEXT(".layout.json");

namespace
{
	TMap<FString, TSharedPtr<FJsonObject>> Cache;
}

TArray<FString> RTSLayoutFiles::GetRoots()
{
	TArray<FString> Roots = { FPaths::Combine(FPaths::ProjectDir(), TEXT("UI"), TEXT("Layouts")) };

	const FString ModsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Mods"));
	TArray<FString> Mods;
	IFileManager::Get().FindFiles(Mods, *FPaths::Combine(ModsDir, TEXT("*")), false, true);
	Mods.Sort();
	for (const FString& Mod : Mods)
	{
		Roots.Add(FPaths::Combine(ModsDir, Mod, TEXT("UI"), TEXT("Layouts")));
	}
	return Roots;
}

TMap<FString, FString> RTSLayoutFiles::FindFiles(TConstArrayView<FString> Roots)
{
	TMap<FString, FString> Files;
	for (const FString& Root : Roots)
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(Root, FString(TEXT("*")) + Suffix), true, false);
		Names.Sort();
		for (const FString& Name : Names)
		{
			Files.Add(Name.LeftChop(FCString::Strlen(Suffix)), FPaths::Combine(Root, Name));
		}
	}
	return Files;
}

TSharedPtr<FJsonObject> RTSLayoutFiles::Parse(const FString& Text, FString& OutError)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		OutError = FString::Printf(TEXT("does not parse: %s"), *Reader->GetErrorMessage());
		return nullptr;
	}
	return Object;
}

TSharedPtr<FJsonObject> RTSLayoutFiles::Load(TConstArrayView<FString> Roots, const FString& Name, FString& OutError)
{
	const TMap<FString, FString> Files = FindFiles(Roots);
	const FString* File = Files.Find(Name);
	FString Text;
	if (!File || !FFileHelper::LoadFileToString(Text, **File))
	{
		OutError = FString::Printf(TEXT("%s%s: not found"), *Name, Suffix);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Object = Parse(Text, OutError);
	if (!Object)
	{
		OutError = FString::Printf(TEXT("%s: %s"), **File, *OutError);
	}
	return Object;
}

TSharedPtr<FJsonObject> RTSLayoutFiles::Load(const FString& Name, FString& OutError)
{
	if (const TSharedPtr<FJsonObject>* Cached = Cache.Find(Name))
	{
		return *Cached;
	}
	TSharedPtr<FJsonObject> Object = Load(GetRoots(), Name, OutError);
	if (Object)
	{
		Cache.Add(Name, Object);
	}
	return Object;
}

void RTSLayoutFiles::ClearCache()
{
	Cache.Reset();
}
