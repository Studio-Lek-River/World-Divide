// URTSEditorScripting::BuildMuzzleFlashSystem (#220) and BuildMuzzleSmokeSystem (#221), on their own so that setup_testmap.py
// rebuilds NS_MuzzleFlash and NS_MuzzleSmoke only when this file changes.
#include "Core/RTSEditorScripting.h"

#if WITH_EDITOR
#include "Combat/RTSShotSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraEmitter.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/Package.h"
#include "WorldDivide.h"

// Named, not anonymous: the unity build shares one scope among several files.
namespace RTSMuzzleFlashBuilder
{
	using FNiagaraEdit = UNiagaraExternalEditUtilities;

	/** A module input of Emitter's stack Script (such as ParticleSpawnScript), for FNiagaraEdit::SetStackInputData. */
	FNiagaraExt_StackItemReference ModuleInput(UNiagaraSystem* System, FName Emitter, FName Script, FName Module, FName Input)
	{
		FNiagaraExt_StackItemReference Reference(System, Emitter, Script, Module);
		Reference.InputNameStack.Add(Input);
		return Reference;
	}

	template <typename TValue>
	void SetInput(const FNiagaraExt_StackItemReference& Input, const TValue& Value, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_StackInputValue Data;
		Data.InitializeAs<TValue>(Value);
		FNiagaraEdit::SetStackInputData(Input, Data, Context);
	}

	void SetFloat(const FNiagaraExt_StackItemReference& Input, float Value, FNiagaraExternalEditContext& Context)
	{
		FNiagaraFloat Data;
		Data.Value = Value;
		SetInput(Input, Data, Context);
	}

	void SetInt(const FNiagaraExt_StackItemReference& Input, int32 Value, FNiagaraExternalEditContext& Context)
	{
		FNiagaraInt32 Data;
		Data.Value = Value;
		SetInput(Input, Data, Context);
	}

	/** Sets Input to an HLSL expression, which may read the system's user parameters (User.Scale). */
	void SetExpression(const FNiagaraExt_StackItemReference& Input, const FString& Expression, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_StackInputData_HlslExpression Data;
		Data.HlslExpression = Expression;
		SetInput(Input, Data, Context);
	}

	/** Sets the enum Input to the entry the stack shows as DisplayName ("Once", "Self"). */
	void SetEnum(const FNiagaraExt_StackItemReference& Input, const FString& DisplayName, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_StackInputTopology Topology;
		FNiagaraEdit::GetStackInputTopology(Input, Topology, Context);
		UEnum* Enum = Topology.Type.GetEnum();
		for (int32 Index = 0; Enum && Index < Enum->NumEnums(); ++Index)
		{
			if (Enum->GetDisplayNameTextByIndex(Index).ToString() == DisplayName)
			{
				FNiagaraExt_StackInputData_Enum Data;
				Data.Enum = Enum;
				Data.EnumName = Enum->GetNameByIndex(Index);
				SetInput(Input, Data, Context);
				return;
			}
		}
		Context.Error(FText::FromString(FString::Printf(TEXT("%s has no entry %s"), *Input.InputNameStack.Last().ToString(), *DisplayName)));
	}

	template <typename TValue>
	void AddUserParameter(UNiagaraSystem* System, FName Name, const FNiagaraTypeDefinition& Type, const TValue& Default,
		FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_UserVariable Variable;
		Variable.Name = Name;
		Variable.Type = Type;
		Variable.DefaultValue.Set(Type, FNiagaraVariant(&Default, sizeof(TValue)));
		FNiagaraEdit::AddUserVariable(System, Variable, Context);
	}

	/**
	 * Adds an emitter from the engine's SimpleSpriteBurst template: once per activation, Count sprites (an HLSL expression)
	 * that live Lifetime seconds (an expression too; the emitter runs for LoopSeconds), sized and coloured by the HLSL
	 * expressions Size and Color and drawn with Material, inside Bounds around the system (+X along the bore). In the
	 * system's space when bLocalSpace, so they move with it; in world space otherwise, left where they were born.
	 */
	void AddSpriteBurst(UNiagaraSystem* System, FName Name, const FString& Count, float LoopSeconds, const FString& Lifetime,
		const FString& Size, const FString& Color, UMaterialInterface* Material, const FBox& Bounds, bool bLocalSpace,
		FNiagaraExternalEditContext& Context)
	{
		UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"));
		FNiagaraExt_EmitterTopology Topology;
		FNiagaraEdit::AddEmitter(Template, Name, Topology, Context);

		const FName EmitterUpdate(TEXT("EmitterUpdateScript"));
		const FName ParticleSpawn(TEXT("ParticleSpawnScript"));
		SetEnum(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Life Cycle Mode")), TEXT("Self"), Context);
		SetEnum(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior")), TEXT("Once"), Context);
		SetFloat(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration")), LoopSeconds, Context);
		SetExpression(ModuleInput(System, Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count")), Count, Context);
		SetExpression(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime")), Lifetime, Context);
		SetExpression(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Color")), Color, Context);
		SetExpression(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size")), Size, Context);

		FNiagaraExt_EmitterData EmitterData;
		EmitterData.PropertyValues = FString::Printf(
			TEXT("{\"bLocalSpace\": %s, \"CalculateBoundsMode\": \"Fixed\", \"FixedBounds\": {\"min\": {\"x\": %g, \"y\": %g, \"z\": %g}, \"max\": {\"x\": %g, \"y\": %g, \"z\": %g}, \"isValid\": true}}"),
			bLocalSpace ? TEXT("true") : TEXT("false"), Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z);
		FNiagaraEdit::SetEmitterData(FNiagaraExt_StackItemReference(System, Name), EmitterData, Context);

		FNiagaraExt_StackItemReference Renderer(System, Name);
		Renderer.RendererIndex = 0;
		FNiagaraExt_RendererData RendererData;
		RendererData.PropertyValues = FString::Printf(TEXT("{\"Material\": \"%s\", \"bCastShadows\": false}"),
			Material ? *Material->GetPathName() : TEXT("None"));
		FNiagaraEdit::SetRendererData(Renderer, RendererData, Context);
	}

	/**
	 * Makes the Niagara system AssetPath new, with the context to edit it through. Built new each time: a system whose
	 * emitters are removed and added again in place plays nothing. The one there moves out of the way, so the new one takes
	 * its package and the save replaces the file; whatever pointed at the old one is pointed at the new one by the caller.
	 */
	UNiagaraSystem* CreateSystem(const FString& AssetPath, TOptional<FNiagaraExternalEditContext>& OutContext)
	{
		const FString Name = FPackageName::GetShortName(AssetPath);
		if (UNiagaraSystem* Old = LoadObject<UNiagaraSystem>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Old->ClearFlags(RF_Public | RF_Standalone);
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
		FNiagaraExternalEditContext CreateContext;
		UNiagaraSystem* System = FNiagaraEdit::CreateNiagaraSystem(Name, FPackageName::GetLongPackagePath(AssetPath), nullptr, CreateContext);
		// Each edit goes through a view model of the system, which the context makes from it.
		if (System)
		{
			OutContext.Emplace(System);
		}
		else
		{
			OutContext.Emplace(MoveTemp(CreateContext));
		}
		return System;
	}

	/** Adds the module script ModulePath to Emitter's Script stack after the module After (none: at its end); returns its name. */
	FName AddModule(UNiagaraSystem* System, FName Emitter, FName Script, FName After, const TCHAR* ModulePath, FNiagaraExternalEditContext& Context)
	{
		const UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, ModulePath);
		FNiagaraExt_ModuleTopology Topology;
		FNiagaraEdit::AddModule(FNiagaraExt_StackItemReference(System, Emitter, Script, After), Module, Topology, Context);
		return Topology.ModuleName;
	}

	/** The module of Emitter's Script stack that runs just before Before; none when Before is first or missing. */
	FName ModuleBefore(UNiagaraSystem* System, FName Emitter, FName Script, FName Before, bool& bOutFound, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_ScriptStackTopology Stack;
		FNiagaraEdit::GetScriptStackTopology(FNiagaraExt_StackItemReference(System, Emitter, Script), Stack, Context);
		FName Previous;
		for (const FNiagaraExt_ModuleTopology& Module : Stack.Modules)
		{
			if (Module.ModuleName == Before)
			{
				bOutFound = true;
				return Previous;
			}
			Previous = Module.ModuleName;
		}
		bOutFound = false;
		return Previous;
	}

	/** Compiles System and returns it, or logs each error and returns null. */
	UNiagaraSystem* FinishSystem(UNiagaraSystem* System, const FString& AssetPath, const TCHAR* Builder, FNiagaraExternalEditContext& Context)
	{
		if (System)
		{
			System->RequestCompile(false);
			System->WaitForCompilationComplete(false, false);
			FNiagaraExt_SystemCompileState Compile;
			FNiagaraEdit::GetSystemCompileState(System, Compile, Context);
			for (const FNiagaraExt_ScriptCompileInfo& Script : Compile.Scripts)
			{
				if (!Script.ErrorSummary.IsEmpty())
				{
					Context.Error(FText::FromString(FString::Printf(TEXT("%s %s: %s"), *Script.EmitterName.ToString(), *Script.ScriptName.ToString(),
						*Script.ErrorSummary)));
				}
			}
			if (Compile.bHasErrors && Context.Errors.IsEmpty())
			{
				Context.Error(FText::FromString(TEXT("the system does not compile")));
			}
			System->MarkPackageDirty();
		}
		for (const FText& Error : Context.Errors)
		{
			UE_LOG(LogWorldDivide, Error, TEXT("%s %s: %s"), Builder, *AssetPath, *Error.ToString());
		}
		return Context.Errors.IsEmpty() ? System : nullptr;
	}
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// The shot's colour and size: URTSWeaponData's ShellColor and EffectScale, set by ARTSShotEffect.
		const FLinearColor DefaultColor(8.f, 4.f, 1.f);
		const float DefaultScale = 1.f;
		AddUserParameter(System, TEXT("User.Color"), FNiagaraTypeDefinition::GetColorDef(), DefaultColor, Context);
		AddUserParameter(System, TEXT("User.Scale"), FNiagaraTypeDefinition::GetFloatDef(), DefaultScale, Context);

		// A ball of fire at the muzzle, 4 m across on the M20's gun (scale 2), and a spray of smaller ones blown 2 to 5 m out
		// along the bore, all gone in 0.15 s: big and long enough to read from the RTS camera. The bounds hold the spray of
		// the heaviest gun (scale 2.5).
		const FBox Bounds(FVector(-300.0, -300.0, -300.0), FVector(900.0, 300.0, 300.0));
		AddSpriteBurst(System, TEXT("Core"), TEXT("1"), 0.15f, TEXT("0.15"), TEXT("User.Scale * 200.0"), TEXT("User.Color"), Material, Bounds,
			true, Context);
		AddSpriteBurst(System, TEXT("Spray"), TEXT("4"), 0.15f, TEXT("0.15"), TEXT("User.Scale * 90.0"), TEXT("User.Color * 0.7"), Material,
			Bounds, true, Context);
		const FName Velocity = AddModule(System, TEXT("Spray"), TEXT("ParticleSpawnScript"), TEXT("InitializeParticle"),
			TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"), Context);
		SetExpression(ModuleInput(System, TEXT("Spray"), TEXT("ParticleSpawnScript"), Velocity, TEXT("Velocity")),
			TEXT("float3((700.0 + rand(900.0)) * User.Scale, rand(300.0) - 150.0, rand(300.0) - 150.0)"), Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildMuzzleFlashSystem"), Context);
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// Set by URTSShotSubsystem: the particles for the quality and the distance, the bore in world space, EffectScale.
		const int32 DefaultCount = 10;
		const FVector3f DefaultDirection(1.f, 0.f, 0.f);
		const float DefaultScale = 1.f;
		AddUserParameter(System, TEXT("User.Count"), FNiagaraTypeDefinition::GetIntDef(), DefaultCount, Context);
		AddUserParameter(System, TEXT("User.Direction"), FNiagaraTypeDefinition::GetVec3Def(), DefaultDirection, Context);
		AddUserParameter(System, TEXT("User.Scale"), FNiagaraTypeDefinition::GetFloatDef(), DefaultScale, Context);

		// Grey puffs in world space, so they stay where the shot was fired as the turret turns. They leave the muzzle along the
		// bore at 0.4 to 2.6 m/s a unit of scale, so they string out into a plume, drag stops them within half a second, 0.3
		// to 1.7 m out on the M20's gun (scale 2), and they rise a little as they spread. M_MuzzleSmoke grows each puff from
		// a third of its sprite to all of it over its life and fades it out, so they end 2 to 3.2 m across on the M20's gun
		// after 2.2 to 3 s. The bounds hold the heaviest gun's drift.
		const FName Emitter(TEXT("Puffs"));
		const FName ParticleSpawn(TEXT("ParticleSpawnScript"));
		const FName ParticleUpdate(TEXT("ParticleUpdateScript"));
		const FBox Bounds(FVector(-1500.0, -1500.0, -500.0), FVector(1500.0, 1500.0, 1500.0));
		AddSpriteBurst(System, Emitter, TEXT("User.Count"), URTSShotSubsystem::MuzzleSmokeSeconds, TEXT("2.2 + rand(0.8)"),
			TEXT("User.Scale * (100.0 + rand(60.0))"), TEXT("float4(0.52, 0.5, 0.47, 1.0)"), Material, Bounds, false, Context);
		const FName Velocity = AddModule(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"),
			TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Velocity, TEXT("Velocity")),
			TEXT("User.Direction * ((40.0 + rand(220.0)) * User.Scale) + float3(rand(40.0) - 20.0, rand(40.0) - 20.0, rand(30.0))"), Context);

		// The forces go before the solver that applies them; the solver is added at the end when the template has none.
		const FName Solver(TEXT("SolveForcesAndVelocity"));
		bool bHasSolver = false;
		FName BeforeSolver = ModuleBefore(System, Emitter, ParticleUpdate, Solver, bHasSolver, Context);
		const FName Drag = AddModule(System, Emitter, ParticleUpdate, BeforeSolver, TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), Context);
		SetFloat(ModuleInput(System, Emitter, ParticleUpdate, Drag, TEXT("Drag")), 3.f, Context);
		const FName Rise = AddModule(System, Emitter, ParticleUpdate, Drag,
			TEXT("/Niagara/Modules/Update/Forces/AccelerationForce.AccelerationForce"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleUpdate, Rise, TEXT("Acceleration")), TEXT("float3(0.0, 0.0, 40.0 * User.Scale)"), Context);
		if (!bHasSolver)
		{
			AddModule(System, Emitter, ParticleUpdate, Rise, TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), Context);
		}
	}
	return FinishSystem(System, AssetPath, TEXT("BuildMuzzleSmokeSystem"), Context);
}
#else
UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	return nullptr;
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	return nullptr;
}
#endif
