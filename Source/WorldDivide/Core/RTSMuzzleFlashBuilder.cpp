// URTSEditorScripting::BuildMuzzleFlashSystem (#220, #294) and BuildMuzzleSmokeSystem (#221, #303), on their own so that setup_testmap.py
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
	 * RendererExtras adds more sprite renderer properties to its JSON, each led by a comma.
	 */
	void AddSpriteBurst(UNiagaraSystem* System, FName Name, const FString& Count, float LoopSeconds, const FString& Lifetime,
		const FString& Size, const FString& Color, UMaterialInterface* Material, const FBox& Bounds, bool bLocalSpace,
		FNiagaraExternalEditContext& Context, const FString& RendererExtras = FString())
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
		RendererData.PropertyValues = FString::Printf(TEXT("{\"Material\": \"%s\", \"bCastShadows\": false%s}"),
			Material ? *Material->GetPathName() : TEXT("None"), *RendererExtras);
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

	const FName ParticleSpawn(TEXT("ParticleSpawnScript"));
	const FName ParticleUpdate(TEXT("ParticleUpdateScript"));

	/**
	 * Sprites of Emitter sized by the HLSL expression Size, a float2 of width and length, instead of uniformly: set after
	 * InitializeParticle, whose own non-uniform size stays hidden behind its static switch until the graph recompiles.
	 */
	void SetSpriteSize2D(UNiagaraSystem* System, FName Emitter, const FString& Size, FNiagaraExternalEditContext& Context)
	{
		const FName SpriteSize(TEXT("Particles.SpriteSize"));
		FNiagaraExt_SetParameterEntry Entry;
		Entry.Variable.Name = SpriteSize;
		Entry.Variable.Type = FNiagaraTypeDefinition::GetVec2Def();
		const FVector2f DefaultSize(100.f, 100.f);
		Entry.DefaultValue.Set(Entry.Variable.Type, FNiagaraVariant(&DefaultSize, sizeof(DefaultSize)));
		FNiagaraExt_ModuleTopology Topology;
		FNiagaraEdit::AddSetParametersModule(FNiagaraExt_StackItemReference(System, Emitter, ParticleSpawn, TEXT("InitializeParticle")), { Entry },
			Topology, Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Topology.ModuleName, SpriteSize), Size, Context);
	}

	/** Sprites of Emitter turned to a random angle each, so neighbours look different. */
	void SetRandomRotation(UNiagaraSystem* System, FName Emitter, FNiagaraExternalEditContext& Context)
	{
		SetEnum(ModuleInput(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Sprite Rotation Mode")), TEXT("Random"), Context);
	}

	/** Emitter's particles are born in a sphere of Radius cm (an expression) around Offset from the system (an expression). */
	void SetSpawnSphere(UNiagaraSystem* System, FName Emitter, const FString& Radius, const FString& Offset,
		FNiagaraExternalEditContext& Context)
	{
		const FName Sphere = AddModule(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"),
			TEXT("/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Sphere, TEXT("Sphere Radius")), Radius, Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Sphere, TEXT("Offset")), Offset, Context);
	}

	/**
	 * Emitter's particles leave at Velocity (an expression), slowed by Drag; Acceleration (an expression, none when empty)
	 * pushes them on. The forces go before the solver that applies them; the solver is added at the end when the template
	 * has none.
	 */
	void SetMotion(UNiagaraSystem* System, FName Emitter, const FString& Velocity, float Drag, const FString& Acceleration,
		FNiagaraExternalEditContext& Context)
	{
		const FName AddVelocity = AddModule(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"),
			TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, AddVelocity, TEXT("Velocity")), Velocity, Context);

		const FName Solver(TEXT("SolveForcesAndVelocity"));
		bool bHasSolver = false;
		FName Last = AddModule(System, Emitter, ParticleUpdate, ModuleBefore(System, Emitter, ParticleUpdate, Solver, bHasSolver, Context),
			TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), Context);
		SetFloat(ModuleInput(System, Emitter, ParticleUpdate, Last, TEXT("Drag")), Drag, Context);
		if (!Acceleration.IsEmpty())
		{
			Last = AddModule(System, Emitter, ParticleUpdate, Last, TEXT("/Niagara/Modules/Update/Forces/AccelerationForce.AccelerationForce"), Context);
			SetExpression(ModuleInput(System, Emitter, ParticleUpdate, Last, TEXT("Acceleration")), Acceleration, Context);
		}
		if (!bHasSolver)
		{
			AddModule(System, Emitter, ParticleUpdate, Last, TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), Context);
		}
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

UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* GlowMaterial,
	UMaterialInterface* FireMaterial)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// The shot's colour and size, URTSWeaponData's ShellColor and EffectScale, and whether its gun has a muzzle brake
		// (bMuzzleBrake, 1 or 0): set by ARTSShotEffect.
		const FLinearColor DefaultColor(8.f, 4.f, 1.f);
		const float DefaultScale = 1.f;
		const int32 DefaultBrake = 0;
		AddUserParameter(System, TEXT("User.Color"), FNiagaraTypeDefinition::GetColorDef(), DefaultColor, Context);
		AddUserParameter(System, TEXT("User.Scale"), FNiagaraTypeDefinition::GetFloatDef(), DefaultScale, Context);
		AddUserParameter(System, TEXT("User.Brake"), FNiagaraTypeDefinition::GetIntDef(), DefaultBrake, Context);

		// The layers of a real firing (docs/art/refs/muzzle_flash), along the bore (+X), all gone within 0.15 s; sizes are for
		// a scale of 1, so twice them on the M20's gun (scale 2). The bounds hold the heaviest gun (scale 2.5).
		const FBox Bounds(FVector(-300.0, -700.0, -700.0), FVector(1100.0, 700.0, 700.0));
		const FString Stretched(TEXT(", \"Alignment\": \"VelocityAligned\""));

		// A white-hot core right at the muzzle: flares in a frame or two and burns out in 0.08 s.
		AddSpriteBurst(System, TEXT("Core"), TEXT("1"), 0.15f, TEXT("0.08"), TEXT("User.Scale * 60.0"), TEXT("User.Color * 0.5"), GlowMaterial,
			Bounds, true, Context);
		SetSpawnSphere(System, TEXT("Core"), TEXT("0.0"), TEXT("float3(User.Scale * 20.0, 0.0, 0.0)"), Context);

		// The blast cone: two tongues of flame stretched along the bore, 1.5 to 2.2 m long, born clear of the muzzle so none
		// lies over the barrel, thrown out and gone in 0.06 to 0.09 s.
		AddSpriteBurst(System, TEXT("Cone"), TEXT("2"), 0.15f, TEXT("0.06 + rand(0.03)"), TEXT("User.Scale * 60.0"), TEXT("User.Color * 0.9"),
			GlowMaterial, Bounds, true, Context, Stretched);
		SetSpriteSize2D(System, TEXT("Cone"), TEXT("float2(User.Scale * (40.0 + rand(15.0)), User.Scale * (150.0 + rand(70.0)))"), Context);
		SetSpawnSphere(System, TEXT("Cone"), TEXT("0.0"), TEXT("float3(User.Scale * 140.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Cone"), TEXT("float3(User.Scale * (900.0 + rand(500.0)), 0.0, 0.0)"), 25.f, FString(), Context);

		// The brake jets: on a gun with a muzzle brake, a tongue of flame thrown out to each side and a little forward.
		for (const TPair<const TCHAR*, const TCHAR*>& Jet : { TPair<const TCHAR*, const TCHAR*>(TEXT("JetLeft"), TEXT("-")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("JetRight"), TEXT("")) })
		{
			AddSpriteBurst(System, Jet.Key, TEXT("User.Brake"), 0.15f, TEXT("0.05 + rand(0.02)"), TEXT("User.Scale * 30.0"),
				TEXT("User.Color * 0.8"), GlowMaterial, Bounds, true, Context, Stretched);
			SetSpriteSize2D(System, Jet.Key, TEXT("float2(User.Scale * 30.0, User.Scale * (90.0 + rand(30.0)))"), Context);
			SetMotion(System, Jet.Key, FString::Printf(TEXT("float3(User.Scale * 150.0, %sUser.Scale * (600.0 + rand(250.0)), 0.0)"), Jet.Value),
				25.f, FString(), Context);
		}

		// The fireball: six billows of the baked fire (T_MuzzleFire), 1.1 to 1.7 m across, thrown out along the bore and
		// slowed hard by the air, cooling from white-yellow to dark red in 0.11 to 0.16 s.
		AddSpriteBurst(System, TEXT("Fireball"), TEXT("6"), 0.2f, TEXT("0.11 + rand(0.05)"), TEXT("User.Scale * (110.0 + rand(60.0))"),
			TEXT("User.Color"), FireMaterial, Bounds, true, Context);
		SetRandomRotation(System, TEXT("Fireball"), Context);
		SetSpawnSphere(System, TEXT("Fireball"), TEXT("User.Scale * 20.0"), TEXT("float3(User.Scale * 60.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Fireball"),
			TEXT("float3(User.Scale * (500.0 + rand(700.0)), User.Scale * (rand(240.0) - 120.0), User.Scale * (rand(240.0) - 120.0))"), 14.f,
			FString(), Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildMuzzleFlashSystem"), Context);
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* BlastMaterial,
	UMaterialInterface* DriftMaterial, UMaterialInterface* DustMaterial)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// Set by URTSShotSubsystem: the particles for the quality and the distance, the bore in world space, EffectScale and
		// how high the muzzle is above the ground in cm.
		const int32 DefaultCount = 10;
		const FVector3f DefaultDirection(1.f, 0.f, 0.f);
		const float DefaultScale = 1.f;
		const float DefaultGroundHeight = 200.f;
		AddUserParameter(System, TEXT("User.Count"), FNiagaraTypeDefinition::GetIntDef(), DefaultCount, Context);
		AddUserParameter(System, TEXT("User.Direction"), FNiagaraTypeDefinition::GetVec3Def(), DefaultDirection, Context);
		AddUserParameter(System, TEXT("User.Scale"), FNiagaraTypeDefinition::GetFloatDef(), DefaultScale, Context);
		AddUserParameter(System, TEXT("User.GroundHeight"), FNiagaraTypeDefinition::GetFloatDef(), DefaultGroundHeight, Context);

		// All in world space, so the smoke stays where the shot was fired as the turret turns. Sizes are for a scale of 1,
		// twice them on the M20's gun; the bounds hold the heaviest gun's drift and the ground under it.
		const FBox Bounds(FVector(-1500.0, -1500.0, -800.0), FVector(1500.0, 1500.0, 1500.0));

		// The blast cloud: six tenths of the particles, never fewer than three, rolling out of the fireball along the bore and
		// sideways, 1.5 to 2.3 m across, braked by the air within a second and thinning out over 1 to 1.6 s.
		AddSpriteBurst(System, TEXT("Blast"), TEXT("max(User.Count * 6 / 10, 3)"), URTSShotSubsystem::MuzzleSmokeSeconds, TEXT("1.0 + rand(0.6)"),
			TEXT("User.Scale * (150.0 + rand(80.0))"), TEXT("float4(0.46, 0.44, 0.41, 0.9)"), BlastMaterial, Bounds, false, Context);
		SetRandomRotation(System, TEXT("Blast"), Context);
		SetSpawnSphere(System, TEXT("Blast"), TEXT("User.Scale * 30.0"), TEXT("User.Direction * (User.Scale * 70.0)"), Context);
		SetMotion(System, TEXT("Blast"),
			TEXT("User.Direction * (User.Scale * (150.0 + rand(350.0))) + float3(rand(160.0) - 80.0, rand(160.0) - 80.0, rand(60.0)) * User.Scale"),
			3.f, TEXT("float3(0.0, 0.0, 30.0 * User.Scale)"), Context);

		// The drifting cloud: the rest, at least one, larger and thinner, 2.4 to 3.6 m across, drifting on and rising slowly,
		// gone in 2.4 to 3 s.
		AddSpriteBurst(System, TEXT("Drift"), TEXT("max(User.Count * 4 / 10, 1)"), URTSShotSubsystem::MuzzleSmokeSeconds, TEXT("2.4 + rand(0.6)"),
			TEXT("User.Scale * (240.0 + rand(120.0))"), TEXT("float4(0.54, 0.52, 0.49, 0.55)"), DriftMaterial, Bounds, false, Context);
		SetRandomRotation(System, TEXT("Drift"), Context);
		SetSpawnSphere(System, TEXT("Drift"), TEXT("User.Scale * 40.0"), TEXT("User.Direction * (User.Scale * 150.0)"), Context);
		SetMotion(System, TEXT("Drift"),
			TEXT("User.Direction * (User.Scale * (60.0 + rand(80.0))) + float3(rand(40.0) - 20.0, rand(40.0) - 20.0, 20.0 + rand(20.0)) * User.Scale"),
			0.8f, TEXT("float3(0.0, 0.0, 25.0 * User.Scale)"), Context);

		// The ground blast: when the muzzle is less than 3 m a unit of scale above the ground, the pressure wave kicks dust off
		// it under and ahead of the muzzle, thrown out flat along the bore and to the sides, settling in 0.6 to 1 s.
		AddSpriteBurst(System, TEXT("Dust"), TEXT("User.GroundHeight < 300.0 * User.Scale ? max(User.Count * 6 / 10, 2) : 0"),
			URTSShotSubsystem::MuzzleSmokeSeconds, TEXT("0.6 + rand(0.4)"), TEXT("User.Scale * (120.0 + rand(80.0))"),
			TEXT("float4(0.6, 0.5, 0.38, 0.75)"), DustMaterial, Bounds, false, Context);
		SetRandomRotation(System, TEXT("Dust"), Context);
		SetSpawnSphere(System, TEXT("Dust"), TEXT("User.Scale * 40.0"),
			TEXT("float3(User.Direction.x, User.Direction.y, 0.0) * (User.Scale * 120.0) + float3(0.0, 0.0, 30.0 - User.GroundHeight)"), Context);
		SetMotion(System, TEXT("Dust"),
			TEXT("float3(User.Direction.x, User.Direction.y, 0.0) * (User.Scale * (100.0 + rand(300.0))) + float3(rand(400.0) - 200.0, rand(400.0) - 200.0, 20.0 + rand(40.0)) * User.Scale"),
			4.f, FString(), Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildMuzzleSmokeSystem"), Context);
}
#else
UNiagaraSystem* URTSEditorScripting::BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* GlowMaterial,
	UMaterialInterface* FireMaterial)
{
	return nullptr;
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* BlastMaterial,
	UMaterialInterface* DriftMaterial, UMaterialInterface* DustMaterial)
{
	return nullptr;
}
#endif
