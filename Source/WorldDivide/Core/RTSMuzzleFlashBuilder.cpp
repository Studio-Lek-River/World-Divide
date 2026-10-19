// URTSEditorScripting::BuildMuzzleFlashSystem (#220), on its own so that setup_testmap.py rebuilds NS_MuzzleFlash only when
// this file changes.
#include "Core/RTSEditorScripting.h"

#if WITH_EDITOR
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
	 * Adds an emitter from the engine's SimpleSpriteBurst template: once per activation, Count sprites in the system's space
	 * (+X along the bore) that live Lifetime seconds, sized and coloured by the HLSL expressions Size and Color and drawn
	 * with Material, inside Bounds.
	 */
	void AddSpriteBurst(UNiagaraSystem* System, FName Name, int32 Count, float Lifetime, const FString& Size, const FString& Color,
		UMaterialInterface* Material, const FBox& Bounds, FNiagaraExternalEditContext& Context)
	{
		UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"));
		FNiagaraExt_EmitterTopology Topology;
		FNiagaraEdit::AddEmitter(Template, Name, Topology, Context);

		const FName EmitterUpdate(TEXT("EmitterUpdateScript"));
		const FName ParticleSpawn(TEXT("ParticleSpawnScript"));
		SetEnum(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Life Cycle Mode")), TEXT("Self"), Context);
		SetEnum(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior")), TEXT("Once"), Context);
		SetFloat(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration")), Lifetime, Context);
		SetInt(ModuleInput(System, Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count")), Count, Context);
		SetFloat(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime")), Lifetime, Context);
		SetExpression(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Color")), Color, Context);
		SetExpression(ModuleInput(System, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size")), Size, Context);

		FNiagaraExt_EmitterData EmitterData;
		EmitterData.PropertyValues = FString::Printf(
			TEXT("{\"bLocalSpace\": true, \"CalculateBoundsMode\": \"Fixed\", \"FixedBounds\": {\"min\": {\"x\": %g, \"y\": %g, \"z\": %g}, \"max\": {\"x\": %g, \"y\": %g, \"z\": %g}, \"isValid\": true}}"),
			Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z);
		FNiagaraEdit::SetEmitterData(FNiagaraExt_StackItemReference(System, Name), EmitterData, Context);

		FNiagaraExt_StackItemReference Renderer(System, Name);
		Renderer.RendererIndex = 0;
		FNiagaraExt_RendererData RendererData;
		RendererData.PropertyValues = FString::Printf(TEXT("{\"Material\": \"%s\", \"bCastShadows\": false}"),
			Material ? *Material->GetPathName() : TEXT("None"));
		FNiagaraEdit::SetRendererData(Renderer, RendererData, Context);
	}
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	using namespace RTSMuzzleFlashBuilder;
	// Built new each time: a system whose emitters are removed and added again in place plays nothing. The one there moves
	// out of the way, so the new one takes its package and the save replaces the file; whatever pointed at the old one is
	// pointed at the new one by the caller.
	const FString Name = FPackageName::GetShortName(AssetPath);
	if (UNiagaraSystem* Old = LoadObject<UNiagaraSystem>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Old->ClearFlags(RF_Public | RF_Standalone);
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	FNiagaraExternalEditContext CreateContext;
	UNiagaraSystem* System = FNiagaraEdit::CreateNiagaraSystem(Name, FPackageName::GetLongPackagePath(AssetPath), nullptr, CreateContext);
	// Each edit goes through a view model of the system, which the context makes from it.
	FNiagaraExternalEditContext Context = System ? FNiagaraExternalEditContext(System) : MoveTemp(CreateContext);
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
		AddSpriteBurst(System, TEXT("Core"), 1, 0.15f, TEXT("User.Scale * 200.0"), TEXT("User.Color"), Material, Bounds, Context);
		AddSpriteBurst(System, TEXT("Spray"), 4, 0.15f, TEXT("User.Scale * 90.0"), TEXT("User.Color * 0.7"), Material, Bounds, Context);
		UNiagaraScript* AddVelocity = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"));
		FNiagaraExt_ModuleTopology Velocity;
		FNiagaraEdit::AddModule(FNiagaraExt_StackItemReference(System, TEXT("Spray"), TEXT("ParticleSpawnScript"), TEXT("InitializeParticle")),
			AddVelocity, Velocity, Context);
		SetExpression(ModuleInput(System, TEXT("Spray"), TEXT("ParticleSpawnScript"), Velocity.ModuleName, TEXT("Velocity")),
			TEXT("float3((700.0 + rand(900.0)) * User.Scale, rand(300.0) - 150.0, rand(300.0) - 150.0)"), Context);

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
		UE_LOG(LogWorldDivide, Error, TEXT("BuildMuzzleFlashSystem %s: %s"), *AssetPath, *Error.ToString());
	}
	return Context.Errors.IsEmpty() ? System : nullptr;
}
#else
UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	return nullptr;
}
#endif
