#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Algo/Find.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/RTSSettings.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Tests/RTSTestFlags.h"
#include "UObject/UnrealType.h"

/**
 * The cook rules of DefaultGame.ini (#226, guideline 32): a cook takes the maps in Content/Maps and what they and URTSSettings
 * reference, and never the git-ignored marketplace packs, which would add gigabytes and make the cook differ from one machine
 * to the next.
 */
namespace
{
	using RTSTests::TestFlags;

	const FName CookedMapDir(TEXT("/Game/Maps"));

	/** The top-level Content folders that hold the project's own content; any other folder must be in DirectoriesToNeverCook. */
	const TCHAR* ProjectFolders[] = { TEXT("Art"), TEXT("Collections"), TEXT("Data"), TEXT("Developers"), TEXT("Maps"), TEXT("Materials"), TEXT("UI") };

	FString GetPackageName(const FString& Path)
	{
		FString Package = Path;
		Package.Split(TEXT("."), &Package, nullptr);
		Package.RemoveFromEnd(TEXT("/"));
		return Package;
	}

	bool IsUnder(const FString& Package, const TArray<FString>& Roots)
	{
		return Roots.ContainsByPredicate([&Package](const FString& Root) { return Package == Root || Package.StartsWith(Root + TEXT("/")); });
	}

	/** The top-level Content folders of Folders that are neither a project folder nor never cooked. */
	TArray<FString> FindUncoveredFolders(const TArray<FString>& Folders, const TArray<FString>& NeverCook)
	{
		TArray<FString> Uncovered;
		for (const FString& Folder : Folders)
		{
			const bool bProject = Algo::FindByPredicate(ProjectFolders, [&Folder](const TCHAR* Project) { return Folder == Project; }) != nullptr;
			if (!bProject && !NeverCook.Contains(TEXT("/Game/") + Folder))
			{
				Uncovered.Add(Folder);
			}
		}
		return Uncovered;
	}

	/** Every /Game package Roots hard or soft reference, Roots among them, each with the package that first referenced it. */
	TMap<FName, FName> GetPackages(const IAssetRegistry& Registry, const TArray<FName>& Roots)
	{
		TMap<FName, FName> Referencers;
		TArray<FName> Open;
		for (const FName Root : Roots)
		{
			Referencers.Add(Root, NAME_None);
			Open.Add(Root);
		}
		while (!Open.IsEmpty())
		{
			const FName Next = Open.Pop();
			TArray<FName> Dependencies;
			Registry.GetDependencies(Next, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
			for (const FName Dependency : Dependencies)
			{
				if (Dependency.ToString().StartsWith(TEXT("/Game/")) && !Referencers.Contains(Dependency))
				{
					Referencers.Add(Dependency, Next);
					Open.Add(Dependency);
				}
			}
		}
		return Referencers;
	}

	/** The packages URTSSettings references: the menus, factions, materials and skirmish maps the game loads by path. */
	TArray<FName> GetSettingsPackages()
	{
		TArray<FName> Packages;
		const URTSSettings* Settings = GetDefault<URTSSettings>();
		for (TPropertyValueIterator<FObjectPropertyBase> It(URTSSettings::StaticClass(), Settings); It; ++It)
		{
			FString Path;
			if (CastField<FSoftObjectProperty>(It.Key()))
			{
				Path = static_cast<const FSoftObjectPtr*>(It.Value())->ToSoftObjectPath().GetLongPackageName();
			}
			else if (const UObject* Object = It.Key()->GetObjectPropertyValue(It.Value()))
			{
				Path = Object->GetPackage()->GetName();
			}
			if (Path.StartsWith(TEXT("/Game/")))
			{
				Packages.AddUnique(FName(Path));
			}
		}
		return Packages;
	}

	/** The packages' size on disk in bytes, with their .uexp and .ubulk files: the source size, not the cooked size. */
	int64 GetDiskSize(const TArray<FName>& Packages)
	{
		int64 Size = 0;
		for (const FName Package : Packages)
		{
			FString File;
			if (!FPackageName::DoesPackageExist(Package.ToString(), &File))
			{
				continue;
			}
			for (const TCHAR* Extension : { TEXT(""), TEXT(".uexp"), TEXT(".ubulk") })
			{
				const int64 FileSize = IFileManager::Get().FileSize(*(*Extension ? FPaths::ChangeExtension(File, Extension) : File));
				Size += FMath::Max<int64>(FileSize, 0);
			}
		}
		return Size;
	}

	FString DescribeSize(const TArray<FName>& Packages)
	{
		return FString::Printf(TEXT("%d packages, %.1f MB on disk"), Packages.Num(), GetDiskSize(Packages) / (1024.0 * 1024.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCookRulesTest, "WorldDivide.Config.Cook", TestFlags)

bool FRTSCookRulesTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
	TestFalse(TEXT("The cook takes the listed maps, not all content"), Packaging->bCookAll);

	// Every map in Content/Maps is cooked, and nothing else is listed.
	TArray<FAssetData> MapAssets;
	Registry.GetAssetsByPath(CookedMapDir, MapAssets, true);
	MapAssets.RemoveAll([](const FAssetData& Asset) { return Asset.AssetClassPath != UWorld::StaticClass()->GetClassPathName(); });
	TSet<FString> Maps;
	for (const FAssetData& Asset : MapAssets)
	{
		Maps.Add(Asset.PackageName.ToString());
	}
	TSet<FString> MapsToCook;
	for (const FFilePath& Map : Packaging->MapsToCook)
	{
		MapsToCook.Add(GetPackageName(Map.FilePath));
	}
	for (const FString& Map : Maps.Difference(MapsToCook))
	{
		AddError(FString::Printf(TEXT("%s is not in MapsToCook"), *Map));
	}
	for (const FString& Map : MapsToCook.Difference(Maps))
	{
		AddError(FString::Printf(TEXT("MapsToCook lists %s, which is not a map in %s"), *Map, *CookedMapDir.ToString()));
	}

	TArray<FString> NeverCook;
	for (const FDirectoryPath& Directory : Packaging->DirectoriesToNeverCook)
	{
		NeverCook.Add(GetPackageName(Directory.Path));
	}

	// Every top-level Content folder is the project's own or never cooked: a new pack cannot slip into the cook.
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("*")), false, true);
	for (const FString& Folder : FindUncoveredFolders(Folders, NeverCook))
	{
		AddError(FString::Printf(TEXT("Content/%s is neither a project folder nor in DirectoriesToNeverCook"), *Folder));
	}

	// The check itself: without its never-cook entry, each pack folder is reported, also on a machine without the packs.
	for (const FString& Root : NeverCook)
	{
		FString Folder = Root;
		if (!Folder.RemoveFromStart(TEXT("/Game/")) || Folder.Contains(TEXT("/")))
		{
			continue;
		}
		TArray<FString> Without = NeverCook;
		Without.Remove(Root);
		TestTrue(*FString::Printf(TEXT("Without %s the folder check reports it"), *Root), FindUncoveredFolders({ Folder }, Without) == TArray<FString>{ Folder });
	}

	// Nothing the cooked maps or the settings reference, hard or soft, lies under a never-cook root.
	TArray<FName> Roots;
	for (const FString& Map : MapsToCook)
	{
		Roots.Add(FName(Map));
	}
	const TArray<FName> SettingsPackages = GetSettingsPackages();
	TestTrue(TEXT("URTSSettings references the game's content"), SettingsPackages.Num() > 0);
	for (const FName Package : SettingsPackages)
	{
		Roots.AddUnique(Package);
	}
	const TMap<FName, FName> Cooked = GetPackages(Registry, Roots);
	for (const TPair<FName, FName>& Package : Cooked)
	{
		if (IsUnder(Package.Key.ToString(), NeverCook))
		{
			AddError(FString::Printf(TEXT("%s is never cooked, but %s references it"), *Package.Key.ToString(),
				Package.Value.IsNone() ? TEXT("the cook rules") : *Package.Value.ToString()));
		}
	}

	// The cook without rules took every map under /Game and what it references; with them it takes the set walked above.
	TArray<FAssetData> AllMaps;
	Registry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AllMaps);
	TArray<FName> AllMapPackages;
	for (const FAssetData& Asset : AllMaps)
	{
		if (Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))
		{
			AllMapPackages.AddUnique(Asset.PackageName);
		}
	}
	TArray<FName> Before;
	GetPackages(Registry, AllMapPackages).GetKeys(Before);
	TArray<FName> After;
	Cooked.GetKeys(After);
	AddInfo(FString::Printf(TEXT("Cook without rules: %d maps, %s"), AllMapPackages.Num(), *DescribeSize(Before)));
	AddInfo(FString::Printf(TEXT("Cook with rules: %d maps, %s"), MapsToCook.Num(), *DescribeSize(After)));
	return true;
}

#endif
