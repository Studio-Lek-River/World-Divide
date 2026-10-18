#include "Units/Debug/RTSBenchGateCommandlet.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSUnitData.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticMeshResources.h"
#include "Units/Debug/RTSBenchGate.h"
#include "WorldDivide.h"

#if !UE_BUILD_SHIPPING

namespace
{
	using namespace RTSBenchGate;

	const TCHAR* UnitDataPath = TEXT("/Game/Data/Units");

	FString ToGatePath(const FString& Path)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}

	TArray<TSharedPtr<FJsonValue>> ToGateJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const FString& Value : Values)
		{
			Array.Add(MakeShared<FJsonValueString>(Value));
		}
		return Array;
	}

	bool WriteJson(const TSharedRef<FJsonObject>& Root, const FString& File)
	{
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
		return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> ReadGateJson(const FString& File)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *File))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	int64 GetTriangles(const UStaticMesh* Mesh)
	{
		const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		return RenderData && RenderData->LODResources.Num() > 0 ? RenderData->LODResources[0].GetNumTriangles() : 0;
	}

	/** Every /Game package Package is made of, itself included. */
	TSet<FString> GetPackages(const IAssetRegistry& Registry, const FName Package)
	{
		TSet<FString> Packages;
		TArray<FName> Open = { Package };
		while (!Open.IsEmpty())
		{
			const FName Next = Open.Pop();
			if (Packages.Contains(Next.ToString()))
			{
				continue;
			}
			Packages.Add(Next.ToString());
			TArray<FName> Dependencies;
			Registry.GetDependencies(Next, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
			for (const FName Dependency : Dependencies)
			{
				if (Dependency.ToString().StartsWith(TEXT("/Game/")))
				{
					Open.Add(Dependency);
				}
			}
		}
		return Packages;
	}

	/** The 240 moving units' game thread of a valid screen baseline in Dir, or -1. */
	double ReadScreen(const FString& Name, const FString& Dir)
	{
		const FRun Run = LoadRun(Name, FPaths::Combine(Dir, Name));
		if (!Run.bComplete || !Run.bHasValidity || !Run.Flags.IsEmpty())
		{
			return -1.0;
		}
		const FRow* Row = Run.Rows.FindByPredicate([](const FRow& R) { return R.Count == Budgets::GateCount && R.Scenario == TEXT("move"); });
		return Row ? Row->GameMs : -1.0;
	}

	/** Whether Dir holds a valid long baseline of Name. */
	bool HasBaseline(const FString& Name, const FString& Dir)
	{
		const FRun Run = LoadRun(Name, FPaths::Combine(Dir, Name));
		return Run.bComplete && Run.bHasValidity && Run.Flags.IsEmpty();
	}

	TArray<FUnitInfo> ReadRoster(const FString& ScreenDir, const FString& LongDir)
	{
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		Registry.SearchAllAssets(/*bSynchronousSearch*/ true);
		FARFilter Filter;
		Filter.PackagePaths.Add(UnitDataPath);
		Filter.ClassPaths.Add(URTSUnitData::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

		TArray<FUnitInfo> Roster;
		for (const FAssetData& Asset : Assets)
		{
			const URTSUnitData* Data = Cast<URTSUnitData>(Asset.GetAsset());
			if (!Data)
			{
				continue;
			}
			FUnitInfo Unit;
			Unit.Name = Asset.AssetName.ToString();
			Unit.Class = Data->IsA<URTSAircraftData>() ? EUnitClass::Air
				: Data->Locomotion.Type == ERTSLocomotionType::Tracked ? EUnitClass::Tracked
				: Data->Locomotion.Type == ERTSLocomotionType::Wheeled ? EUnitClass::Wheeled : EUnitClass::Infantry;
			for (const UStaticMesh* Mesh : { Data->Mesh.Get(), Data->TurretMesh.Get(), Data->GunMesh.Get() })
			{
				Unit.Parts += Mesh ? 1 : 0;
				Unit.Triangles += GetTriangles(Mesh);
			}
			for (const FRTSVehiclePart& Part : Data->Parts)
			{
				++Unit.Parts;
				Unit.Wheels += Part.bWheel ? 1 : 0;
				Unit.Triangles += GetTriangles(Part.Mesh);
			}
			if (Data->Mesh)
			{
				const FVector Size = Data->Mesh->GetBounds().BoxExtent * 2.0 * Data->MeshScale.GetAbs();
				Unit.FootprintM2 = Size.X * Size.Y / 10000.0;
			}
			Unit.Packages = GetPackages(Registry, Asset.PackageName);
			Unit.ScreenGameMs = ScreenDir.IsEmpty() ? -1.0 : ReadScreen(Unit.Name, ScreenDir);
			Unit.bHasLongBaseline = !LongDir.IsEmpty() && HasBaseline(Unit.Name, LongDir);
			Roster.Add(MoveTemp(Unit));
		}
		return Roster;
	}

	int32 RunPlan(const FString& Params, const FString& GateDir)
	{
		FString ChangesFile;
		FString ScreenDir;
		FString LongDir;
		FParse::Value(*Params, TEXT("Changes="), ChangesFile);
		FParse::Value(*Params, TEXT("Screens="), ScreenDir);
		FParse::Value(*Params, TEXT("Longs="), LongDir);
		const bool bBroad = FParse::Param(*Params, TEXT("Broad"));
		TArray<FString> Changed;
		if (!ChangesFile.IsEmpty() && !FFileHelper::LoadFileToStringArray(Changed, *ToGatePath(ChangesFile)))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: cannot read %s"), *ChangesFile);
			return URTSBenchGateCommandlet::ExitFail;
		}
		Changed.RemoveAll([](const FString& Line) { return Line.TrimStartAndEnd().IsEmpty(); });

		const TArray<FUnitInfo> Roster = ReadRoster(ScreenDir.IsEmpty() ? FString() : ToGatePath(ScreenDir),
			LongDir.IsEmpty() ? FString() : ToGatePath(LongDir));
		if (Roster.IsEmpty())
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: no unit data under %s"), UnitDataPath);
			return URTSBenchGateCommandlet::ExitFail;
		}
		const FPlan Plan = MakePlan(Roster, Changed, bBroad);

		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("broad"), Plan.bBroad);
		Root->SetBoolField(TEXT("bench_method"), Plan.bBenchMethod);
		Root->SetArrayField(TEXT("representatives"), ToGateJson(Plan.Representatives));
		Root->SetStringField(TEXT("mixed"), Plan.Mixed);
		Root->SetArrayField(TEXT("screen"), ToGateJson(Plan.Screen));
		Root->SetArrayField(TEXT("long"), ToGateJson(Plan.Long));
		const TSharedRef<FJsonObject> Reasons = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Reason : Plan.Reasons)
		{
			Reasons->SetStringField(Reason.Key, Reason.Value);
		}
		Root->SetObjectField(TEXT("reasons"), Reasons);
		Root->SetArrayField(TEXT("changes"), ToGateJson(Plan.Changes));
		TArray<TSharedPtr<FJsonValue>> Units;
		for (const FUnitInfo& Unit : Roster)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Unit.Name);
			Object->SetStringField(TEXT("class"), LexUnitClass(Unit.Class));
			Object->SetNumberField(TEXT("parts"), Unit.Parts);
			Object->SetNumberField(TEXT("wheels"), Unit.Wheels);
			Object->SetNumberField(TEXT("triangles"), static_cast<double>(Unit.Triangles));
			Object->SetNumberField(TEXT("footprint_m2"), FMath::RoundToDouble(Unit.FootprintM2 * 10.0) / 10.0);
			Object->SetNumberField(TEXT("screen_game_ms"), Unit.ScreenGameMs);
			Object->SetBoolField(TEXT("long_baseline"), Unit.bHasLongBaseline);
			TArray<FString> Packages = Unit.Packages.Array();
			Packages.Sort();
			Object->SetArrayField(TEXT("packages"), ToGateJson(Packages));
			Units.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("roster"), Units);

		const FString File = FPaths::Combine(GateDir, TEXT("plan.json"));
		if (!WriteJson(Root, File))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: could not write %s"), *File);
			return URTSBenchGateCommandlet::ExitFail;
		}
		UE_LOG(LogWorldDivide, Display, TEXT("RTSBenchGate: %d units, %d representatives; screen %d, long %d%s to %s"), Roster.Num(),
			Plan.Representatives.Num(), Plan.Screen.Num(), Plan.Long.Num(), Plan.bBroad ? TEXT(" (broad)") : TEXT(""), *File);
		return URTSBenchGateCommandlet::ExitPass;
	}

	TSharedRef<FJsonObject> ToGateJson(const FSetVerdict& Verdict)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("set"), Verdict.Set);
		Object->SetStringField(TEXT("method"), Verdict.Method);
		Object->SetStringField(TEXT("verdict"), LexVerdict(Verdict.Verdict));
		Object->SetBoolField(TEXT("escalate"), Verdict.bEscalate);
		Object->SetArrayField(TEXT("reasons"), ToGateJson(Verdict.Reasons));
		Object->SetArrayField(TEXT("notes"), ToGateJson(Verdict.Notes));
		Object->SetStringField(TEXT("next"), Verdict.Next);
		return Object;
	}

	FSetVerdict Judge(const FString& Set, const FString& Method, const FString& GateDir, const FString& BaselineDir)
	{
		const FString NowDir = FPaths::Combine(GateDir, Method, Set);
		const FString BaseDir = FPaths::Combine(BaselineDir, Method, Set);
		const FRun Now = LoadRun(Set, NowDir);
		const FRun Base = LoadRun(Set, BaseDir);
		TArray<FString> Incompatible;
		FString NowJson;
		FString BaseJson;
		if (Base.bComplete && FFileHelper::LoadFileToString(NowJson, *FPaths::Combine(NowDir, TEXT("bench.json")))
			&& FFileHelper::LoadFileToString(BaseJson, *FPaths::Combine(BaseDir, TEXT("bench.json"))))
		{
			Incompatible = CompareMetadata(BaseJson, NowJson);
		}
		const FRun* BasePtr = Base.bComplete ? &Base : nullptr;
		FSetVerdict Verdict = Method == TEXT("screen") ? JudgeScreen(Now, BasePtr, Incompatible) : JudgeLong(Now, BasePtr, Incompatible);
		Verdict.Notes.Insert(BasePtr ? FString::Printf(TEXT("baseline %s"), *FPaths::ConvertRelativePathToFull(BaseDir))
			: FString::Printf(TEXT("no baseline at %s"), *FPaths::ConvertRelativePathToFull(BaseDir)), 0);
		return Verdict;
	}

	TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field)
	{
		TArray<FString> Values;
		if (Root.IsValid())
		{
			Root->TryGetStringArrayField(Field, Values);
		}
		return Values;
	}

	FString ToMarkdown(EVerdict Overall, const FString& BaselineDir, const TSharedPtr<FJsonObject>& Plan, const TArray<FSetVerdict>& Verdicts,
		const TArray<FString>& Next)
	{
		FString Text = FString::Printf(TEXT("# Performance gate: %s\n\n- Baselines: %s\n"), LexVerdict(Overall), *FPaths::ConvertRelativePathToFull(BaselineDir));
		Text += FString::Printf(TEXT("- Scope: %s; representatives: %s; mixed set: %s\n"), Plan->GetBoolField(TEXT("broad")) ? TEXT("broad, every unit")
			: TEXT("the changed units"), *FString::Join(ReadStrings(Plan, TEXT("representatives")), TEXT(", ")), *Plan->GetStringField(TEXT("mixed")));
		if (Plan->GetBoolField(TEXT("bench_method")))
		{
			Text += TEXT("- **The bench's own method or scene changed:** regenerate every baseline (-SaveBaseline) once this is committed.\n");
		}
		Text += TEXT("- A representative subset covers kinds of cost, not every unit: a unit screened only with the screen method has no gate evidence of its own.\n");
		Text += TEXT("\n## Changes\n\n");
		for (const FString& Change : ReadStrings(Plan, TEXT("changes")))
		{
			Text += FString::Printf(TEXT("- %s\n"), *Change);
		}
		Text += TEXT("\n## Sets\n\n| Set | Method | Verdict | Why | Next |\n|---|---|---|---|---|\n");
		for (const FSetVerdict& Verdict : Verdicts)
		{
			Text += FString::Printf(TEXT("| %s | %s | %s%s | %s | %s |\n"), *Verdict.Set, *Verdict.Method, LexVerdict(Verdict.Verdict),
				Verdict.bEscalate ? TEXT(" (escalated)") : TEXT(""), Verdict.Reasons.IsEmpty() ? TEXT("-") : *FString::Join(Verdict.Reasons, TEXT("<br>")),
				Verdict.Next.IsEmpty() ? TEXT("-") : *Verdict.Next);
		}
		Text += TEXT("\n## Checks passed\n\n");
		for (const FSetVerdict& Verdict : Verdicts)
		{
			Text += FString::Printf(TEXT("- **%s (%s):** %s\n"), *Verdict.Set, *Verdict.Method, *FString::Join(Verdict.Notes, TEXT("; ")));
		}
		if (!Next.IsEmpty())
		{
			Text += TEXT("\n## Next\n\n");
			for (const FString& Line : Next)
			{
				Text += FString::Printf(TEXT("- %s\n"), *Line);
			}
		}
		return Text;
	}

	int32 RunJudge(const FString& Params, const FString& GateDir)
	{
		FString BaselineDir;
		FString Stage;
		FParse::Value(*Params, TEXT("Baselines="), BaselineDir);
		FParse::Value(*Params, TEXT("Stage="), Stage);
		BaselineDir = ToGatePath(BaselineDir);
		const TSharedPtr<FJsonObject> Plan = ReadGateJson(FPaths::Combine(GateDir, TEXT("plan.json")));
		if (!Plan.IsValid() || BaselineDir.IsEmpty() || (Stage != TEXT("screen") && Stage != TEXT("final")))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: -Judge needs plan.json in -Gate, -Baselines=<dir> and -Stage=screen|final"));
			return URTSBenchGateCommandlet::ExitFail;
		}

		TArray<FSetVerdict> Screens;
		for (const FString& Set : ReadStrings(Plan, TEXT("screen")))
		{
			Screens.Add(Judge(Set, TEXT("screen"), GateDir, BaselineDir));
		}
		if (Stage == TEXT("screen"))
		{
			TArray<FString> Escalate;
			TArray<FString> Rerun;
			TArray<TSharedPtr<FJsonValue>> Sets;
			for (const FSetVerdict& Verdict : Screens)
			{
				if (Verdict.bEscalate)
				{
					Escalate.Add(Verdict.Set);
				}
				else if (Verdict.Verdict != EVerdict::Pass)
				{
					Rerun.Add(Verdict.Set);
				}
				Sets.Add(MakeShared<FJsonValueObject>(ToGateJson(Verdict)));
				UE_LOG(LogWorldDivide, Display, TEXT("RTSBenchGate: screen %s %s%s%s"), *Verdict.Set, LexVerdict(Verdict.Verdict),
					Verdict.bEscalate ? TEXT(", escalated: ") : Verdict.Reasons.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Verdict.Reasons, TEXT("; ")));
			}
			const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetArrayField(TEXT("sets"), Sets);
			Root->SetArrayField(TEXT("escalate"), ToGateJson(Escalate));
			Root->SetArrayField(TEXT("rerun"), ToGateJson(Rerun));
			return WriteJson(Root, FPaths::Combine(GateDir, TEXT("screen.json"))) ? URTSBenchGateCommandlet::ExitPass : URTSBenchGateCommandlet::ExitFail;
		}

		// The final stage: every long set the plan and the screen asked for, and the screens no long run answers.
		TArray<FString> LongSets = ReadStrings(Plan, TEXT("long"));
		for (const FString& Set : ReadStrings(ReadGateJson(FPaths::Combine(GateDir, TEXT("screen.json"))), TEXT("escalate")))
		{
			LongSets.AddUnique(Set);
		}
		TArray<FSetVerdict> Verdicts;
		for (const FSetVerdict& Screen : Screens)
		{
			// An escalated screen is answered by its long run; the screen stays in the report as the reason.
			FSetVerdict Shown = Screen;
			if (Screen.bEscalate && LongSets.Contains(Screen.Set))
			{
				Shown.Verdict = EVerdict::Pass;
				Shown.Notes.Add(TEXT("answered by its long run"));
			}
			Verdicts.Add(Shown);
		}
		for (const FString& Set : LongSets)
		{
			Verdicts.Add(Judge(Set, TEXT("long"), GateDir, BaselineDir));
		}
		const EVerdict Overall = Combine(Verdicts);
		TArray<FString> Next;
		for (const FSetVerdict& Verdict : Verdicts)
		{
			if (Verdict.Verdict != EVerdict::Pass && !Verdict.Next.IsEmpty())
			{
				Next.AddUnique(FString::Printf(TEXT("%s: %s"), *Verdict.Set, *Verdict.Next));
			}
		}
		if (Verdicts.IsEmpty())
		{
			Next.Add(TEXT("nothing the bench runs changed; the nightly or milestone run (-Scope broad) still measures every unit"));
		}

		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("verdict"), LexVerdict(Overall));
		Root->SetStringField(TEXT("baselines"), FPaths::ConvertRelativePathToFull(BaselineDir));
		Root->SetBoolField(TEXT("broad"), Plan->GetBoolField(TEXT("broad")));
		Root->SetBoolField(TEXT("bench_method"), Plan->GetBoolField(TEXT("bench_method")));
		Root->SetArrayField(TEXT("representatives"), ToGateJson(ReadStrings(Plan, TEXT("representatives"))));
		Root->SetArrayField(TEXT("changes"), ToGateJson(ReadStrings(Plan, TEXT("changes"))));
		TArray<TSharedPtr<FJsonValue>> Sets;
		for (const FSetVerdict& Verdict : Verdicts)
		{
			Sets.Add(MakeShared<FJsonValueObject>(ToGateJson(Verdict)));
		}
		Root->SetArrayField(TEXT("sets"), Sets);
		Root->SetArrayField(TEXT("next"), ToGateJson(Next));
		const FString Markdown = ToMarkdown(Overall, BaselineDir, Plan, Verdicts, Next);
		if (!WriteJson(Root, FPaths::Combine(GateDir, TEXT("gate.json")))
			|| !FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(GateDir, TEXT("gate.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: could not write gate.json and gate.md to %s"), *GateDir);
			return URTSBenchGateCommandlet::ExitFail;
		}
		UE_LOG(LogWorldDivide, Display, TEXT("RTSBenchGate: %s, %d set(s), to %s"), LexVerdict(Overall), Verdicts.Num(), *GateDir);
		return Overall == EVerdict::Pass ? URTSBenchGateCommandlet::ExitPass
			: Overall == EVerdict::Fail ? URTSBenchGateCommandlet::ExitFail : URTSBenchGateCommandlet::ExitInconclusive;
	}
}

#endif

URTSBenchGateCommandlet::URTSBenchGateCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URTSBenchGateCommandlet::Main(const FString& Params)
{
#if UE_BUILD_SHIPPING
	return ExitFail;
#else
	FString GateDir;
	FParse::Value(*Params, TEXT("Gate="), GateDir);
	const bool bPlan = FParse::Param(*Params, TEXT("Plan"));
	const bool bJudge = FParse::Param(*Params, TEXT("Judge"));
	if (GateDir.IsEmpty() || bPlan == bJudge)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("RTSBenchGate: give -Gate=<dir> and either -Plan or -Judge"));
		return ExitFail;
	}
	GateDir = ToGatePath(GateDir);
	return bPlan ? RunPlan(Params, GateDir) : RunJudge(Params, GateDir);
#endif
}
