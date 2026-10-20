// URTSEditorScripting::BuildMuzzleFlashSystem (#220, #294), BuildMuzzleSmokeSystem (#221, #303), BuildExhaustSystem (#222)
// and BuildDustSystem (#224), on their own so that setup_testmap.py rebuilds NS_MuzzleFlash, NS_MuzzleSmoke, NS_Exhaust and
// NS_Dust only when this file changes.
#include "Core/RTSEditorScripting.h"

#if WITH_EDITOR
#include "Combat/RTSShotSubsystem.h"
#include "Data/RTSUnitData.h"
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

	/**
	 * Emitter's particles are born in a sphere of Radius cm (an expression) around Offset from the system (an expression), in
	 * the module's own coordinate space or, with Space ("Local"), in that one: Local turns and tilts with the component.
	 */
	void SetSpawnSphere(UNiagaraSystem* System, FName Emitter, const FString& Radius, const FString& Offset,
		FNiagaraExternalEditContext& Context, const TCHAR* Space = nullptr)
	{
		const FName Sphere = AddModule(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"),
			TEXT("/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Sphere, TEXT("Sphere Radius")), Radius, Context);
		SetExpression(ModuleInput(System, Emitter, ParticleSpawn, Sphere, TEXT("Offset")), Offset, Context);
		if (Space)
		{
			SetEnum(ModuleInput(System, Emitter, ParticleSpawn, Sphere, TEXT("Coordinate Space")), Space, Context);
		}
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

	/**
	 * Adds a continuous emitter from the SimpleSpriteBurst template: while the system is active, Rate sprites a second (an HLSL
	 * expression, spawned interpolated) instead of the template's burst, living Lifetime seconds, sized and coloured by Size
	 * and Color at birth and drawn with Material in world space, left where they were born, inside Bounds around the system.
	 */
	void AddSpriteStream(UNiagaraSystem* System, FName Name, const FString& Rate, const FString& Lifetime, const FString& Size,
		const FString& Color, UMaterialInterface* Material, const FBox& Bounds, FNiagaraExternalEditContext& Context)
	{
		AddSpriteBurst(System, Name, TEXT("0"), 1.f, Lifetime, Size, Color, Material, Bounds, false, Context);
		const FName EmitterUpdate(TEXT("EmitterUpdateScript"));
		SetEnum(ModuleInput(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior")), TEXT("Infinite"), Context);
		FNiagaraEdit::RemoveModule(FNiagaraExt_StackItemReference(System, Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous")),
			Context);
		const FName Spawn = AddModule(System, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"),
			Context);
		SetExpression(ModuleInput(System, Name, EmitterUpdate, Spawn, TEXT("SpawnRate")), Rate, Context);
		// A frame's sprites are spread along the system's move over that frame, so a fast hull or a slow frame leaves a trail
		// rather than clumps.
		FNiagaraExt_EmitterData EmitterData;
		EmitterData.PropertyValues = TEXT("{\"InterpolatedSpawnMode\": \"Interpolation\"}");
		FNiagaraEdit::SetEmitterData(FNiagaraExt_StackItemReference(System, Name), EmitterData, Context);
	}

	/** Emitter's sprites resized each update by the HLSL expression Size, a float2, which may read Particles.NormalizedAge or Particles.Age. */
	void SetSpriteSizeOverLife(UNiagaraSystem* System, FName Emitter, const FString& Size, FNiagaraExternalEditContext& Context)
	{
		const FName SpriteSize(TEXT("Particles.SpriteSize"));
		FNiagaraExt_SetParameterEntry Entry;
		Entry.Variable.Name = SpriteSize;
		Entry.Variable.Type = FNiagaraTypeDefinition::GetVec2Def();
		const FVector2f DefaultSize(100.f, 100.f);
		Entry.DefaultValue.Set(Entry.Variable.Type, FNiagaraVariant(&DefaultSize, sizeof(DefaultSize)));
		FNiagaraExt_ScriptStackTopology Stack;
		FNiagaraEdit::GetScriptStackTopology(FNiagaraExt_StackItemReference(System, Emitter, ParticleUpdate), Stack, Context);
		FNiagaraExt_ModuleTopology Topology;
		FNiagaraEdit::AddSetParametersModule(FNiagaraExt_StackItemReference(System, Emitter, ParticleUpdate,
			Stack.Modules.IsEmpty() ? NAME_None : Stack.Modules.Last().ModuleName), { Entry }, Topology, Context);
		SetExpression(ModuleInput(System, Emitter, ParticleUpdate, Topology.ModuleName, SpriteSize), Size, Context);
	}

	/**
	 * Sets the attribute Name of Emitter's particles, of Type with Default, to the HLSL expression Value in Script after the
	 * module After.
	 */
	template <typename TValue>
	void SetAttribute(UNiagaraSystem* System, FName Emitter, FName Script, FName After, FName Name, const FNiagaraTypeDefinition& Type,
		const TValue& Default, const FString& Value, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_SetParameterEntry Entry;
		Entry.Variable.Name = Name;
		Entry.Variable.Type = Type;
		Entry.DefaultValue.Set(Type, FNiagaraVariant(&Default, sizeof(TValue)));
		FNiagaraExt_ModuleTopology Topology;
		FNiagaraEdit::AddSetParametersModule(FNiagaraExt_StackItemReference(System, Emitter, Script, After), { Entry }, Topology, Context);
		SetExpression(ModuleInput(System, Emitter, Script, Topology.ModuleName, Name), Value, Context);
	}

	/**
	 * Emitter's particles keep what the HLSL expression Value gives at their birth as the attribute Name (such as
	 * Particles.BirthColor), so that what they do later does not change with the user parameters it reads (#336). Their own
	 * attributes will not do: a module setting Particles.Color reads none of its own.
	 */
	template <typename TValue>
	void SetBirthAttribute(UNiagaraSystem* System, FName Emitter, FName Name, const FNiagaraTypeDefinition& Type, const TValue& Default,
		const FString& Value, FNiagaraExternalEditContext& Context)
	{
		SetAttribute(System, Emitter, ParticleSpawn, TEXT("InitializeParticle"), Name, Type, Default, Value, Context);
	}

	/** Emitter's particles recoloured each update by the HLSL expression Color, a float4, which may read their birth attributes. */
	void SetColorOverLife(UNiagaraSystem* System, FName Emitter, const FString& Color, FNiagaraExternalEditContext& Context)
	{
		FNiagaraExt_ScriptStackTopology Stack;
		FNiagaraEdit::GetScriptStackTopology(FNiagaraExt_StackItemReference(System, Emitter, ParticleUpdate), Stack, Context);
		SetAttribute(System, Emitter, ParticleUpdate, Stack.Modules.IsEmpty() ? NAME_None : Stack.Modules.Last().ModuleName,
			TEXT("Particles.Color"), FNiagaraTypeDefinition::GetColorDef(), FLinearColor::White, Color, Context);
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

UNiagaraSystem* URTSEditorScripting::BuildExhaustSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// Set by URTSExhaustSubsystem from the unit's FRTSVehicleExhaust: puffs a second for the engine's state, the quality
		// and the distance, the colour and how opaque a puff is born (#325), how wide in cm, and which way the outlet faces in
		// world space.
		const float DefaultRate = 10.f;
		const FLinearColor DefaultColor(0.42f, 0.41f, 0.4f);
		const float DefaultOpacity = 0.45f;
		const float DefaultSize = 60.f;
		const FVector3f DefaultDirection(-1.f, 0.f, 0.f);
		const float DefaultLifetime = 1.5f;
		const float DefaultLift = 0.f;
		AddUserParameter(System, TEXT("User.Rate"), FNiagaraTypeDefinition::GetFloatDef(), DefaultRate, Context);
		AddUserParameter(System, TEXT("User.Direction"), FNiagaraTypeDefinition::GetVec3Def(), DefaultDirection, Context);
		AddUserParameter(System, TEXT("User.Color"), FNiagaraTypeDefinition::GetColorDef(), DefaultColor, Context);
		AddUserParameter(System, TEXT("User.Opacity"), FNiagaraTypeDefinition::GetFloatDef(), DefaultOpacity, Context);
		AddUserParameter(System, TEXT("User.Size"), FNiagaraTypeDefinition::GetFloatDef(), DefaultSize, Context);
		AddUserParameter(System, TEXT("User.Lifetime"), FNiagaraTypeDefinition::GetFloatDef(), DefaultLifetime, Context);
		AddUserParameter(System, TEXT("User.Lift"), FNiagaraTypeDefinition::GetFloatDef(), DefaultLift, Context);

		// Thin puffs in world space, so a driving hull leaves them behind, each keeping the colour, life and lift it was born
		// with: blown half a metre or so out of the outlet along User.Direction and User.Lift cm/s upward, slowed by the air
		// within a second, rising as they cool and thinning out over 0.8 to 1.2 times User.Lifetime. They grow with their age
		// in seconds, to three times their size at 1.5 s, so a short-lived cruising puff (#340) stays narrow too. The bounds
		// hold a plume left by a hull at full speed.
		const FBox Bounds(FVector(-900.0, -900.0, -400.0), FVector(900.0, 900.0, 700.0));
		AddSpriteStream(System, TEXT("Exhaust"), TEXT("User.Rate"), TEXT("User.Lifetime * (0.8 + rand(0.4))"), TEXT("User.Size"),
			TEXT("float4(User.Color.r, User.Color.g, User.Color.b, User.Opacity)"), Material, Bounds, Context);
		SetRandomRotation(System, TEXT("Exhaust"), Context);
		SetSpawnSphere(System, TEXT("Exhaust"), TEXT("User.Size * 0.2"), TEXT("float3(0.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Exhaust"),
			TEXT("User.Direction * (90.0 + rand(60.0)) + float3(rand(30.0) - 15.0, rand(30.0) - 15.0, rand(30.0) - 15.0 + User.Lift)"), 2.f,
			TEXT("float3(0.0, 0.0, 50.0)"), Context);
		SetSpriteSizeOverLife(System, TEXT("Exhaust"), TEXT("float2(1.0, 1.0) * User.Size * (1.0 + 1.33 * Particles.Age)"),
			Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildExhaustSystem"), Context);
}

UNiagaraSystem* URTSEditorScripting::BuildDustSystem(const FString& AssetPath, UMaterialInterface* HazeMaterial,
	UMaterialInterface* ChunkMaterial)
{
	using namespace RTSMuzzleFlashBuilder;
	TOptional<FNiagaraExternalEditContext> Edit;
	UNiagaraSystem* System = CreateSystem(AssetPath, Edit);
	FNiagaraExternalEditContext& Context = *Edit;
	if (System)
	{
		// Set by URTSDustSubsystem from the unit's FRTSVehicleDust: puffs a second from each contact point for the speed, the
		// turn, the ground, the quality and the distance, how opaque and how wide in cm a haze puff is born, and which way the
		// tracks or wheels throw the dust in world space. One system serves User.Points contact points (#336): itself, or two
		// at User.Side and its opposite from it in its own space, in cm, as the two tracks or the two rear wheels. Then the ground's look (FRTSDustSurface, #336), sent when the ground changes, by
		// default as on dry dirt: the haze's colour, its share of the rate, life, spread, lift, sink and size, and the chunks'
		// share of the rate, size and colour.
		const float DefaultRate = 10.f;
		const float DefaultOpacity = 0.6f;
		const float DefaultSize = 140.f;
		const FVector3f DefaultDirection(-1.f, 0.f, 0.f);
		const float DefaultPoints = 2.f;
		const FVector3f DefaultSide(0.f, 150.f, 0.f);
		const FRTSDustSurface Ground;
		AddUserParameter(System, TEXT("User.Rate"), FNiagaraTypeDefinition::GetFloatDef(), DefaultRate, Context);
		AddUserParameter(System, TEXT("User.Direction"), FNiagaraTypeDefinition::GetVec3Def(), DefaultDirection, Context);
		AddUserParameter(System, TEXT("User.Opacity"), FNiagaraTypeDefinition::GetFloatDef(), DefaultOpacity, Context);
		AddUserParameter(System, TEXT("User.Size"), FNiagaraTypeDefinition::GetFloatDef(), DefaultSize, Context);
		AddUserParameter(System, TEXT("User.Points"), FNiagaraTypeDefinition::GetFloatDef(), DefaultPoints, Context);
		AddUserParameter(System, TEXT("User.Side"), FNiagaraTypeDefinition::GetVec3Def(), DefaultSide, Context);
		AddUserParameter(System, TEXT("User.Color"), FNiagaraTypeDefinition::GetColorDef(), Ground.Color, Context);
		AddUserParameter(System, TEXT("User.HazeScale"), FNiagaraTypeDefinition::GetFloatDef(), Ground.HazeScale, Context);
		AddUserParameter(System, TEXT("User.OpacityScale"), FNiagaraTypeDefinition::GetFloatDef(), Ground.OpacityScale, Context);
		AddUserParameter(System, TEXT("User.Lifetime"), FNiagaraTypeDefinition::GetFloatDef(), Ground.Lifetime, Context);
		AddUserParameter(System, TEXT("User.Spread"), FNiagaraTypeDefinition::GetFloatDef(), Ground.Spread, Context);
		AddUserParameter(System, TEXT("User.Lift"), FNiagaraTypeDefinition::GetFloatDef(), Ground.Lift, Context);
		AddUserParameter(System, TEXT("User.Sink"), FNiagaraTypeDefinition::GetFloatDef(), Ground.Sink, Context);
		AddUserParameter(System, TEXT("User.SizeScale"), FNiagaraTypeDefinition::GetFloatDef(), Ground.SizeScale, Context);
		AddUserParameter(System, TEXT("User.ChunkScale"), FNiagaraTypeDefinition::GetFloatDef(), Ground.ChunkScale, Context);
		AddUserParameter(System, TEXT("User.ChunkSize"), FNiagaraTypeDefinition::GetFloatDef(), Ground.ChunkSize, Context);
		AddUserParameter(System, TEXT("User.ChunkColor"), FNiagaraTypeDefinition::GetColorDef(), Ground.ChunkColor, Context);

		// All in world space, born on the ground at the contact point, so a driving hull leaves a trail. The bounds hold a
		// trail left at full speed and the chunks' arcs.
		const FBox Bounds(FVector(-1500.0, -1500.0, -300.0), FVector(1500.0, 1500.0, 900.0));

		// The haze: low, heavy dust, not smoke. Each puff is born low and is thrown out flat along the ground, behind and to
		// the sides at about User.Spread, with a short rise of User.Lift that the air brakes within half a second; then it
		// sinks at User.Sink and spreads out wide rather than up, so its lower half stays in the ground and fades against it
		// (the material's depth fade). No spin. It keeps the colour, opacity, size and sink of the ground it was born on, so a
		// trail does not change as the vehicle drives onto other ground, and thins out over about User.Lifetime.
		AddSpriteStream(System, TEXT("Haze"), TEXT("User.Rate * User.Points * User.HazeScale"), TEXT("User.Lifetime * (0.7 + rand(0.6))"),
			TEXT("User.Size * User.SizeScale"), TEXT("float4(User.Color.r, User.Color.g, User.Color.b, User.Opacity)"), HazeMaterial,
			Bounds, Context);
		SetSpawnSphere(System, TEXT("Haze"), TEXT("User.Size * User.SizeScale * 0.2"),
			TEXT("User.Side * sign(rand(2.0) - 1.0) + float3(0.0, 0.0, User.Size * User.SizeScale * 0.12)"), Context, TEXT("Local"));
		SetMotion(System, TEXT("Haze"),
			TEXT("User.Direction * (User.Spread * (0.5 + rand(0.7))) + float3((rand(2.0) - 1.0) * User.Spread * 0.7, (rand(2.0) - 1.0) * User.Spread * 0.7, User.Lift * (0.5 + rand(1.0)))"),
			2.f, TEXT("float3(0.0, 0.0, -Particles.BirthShape.y)"), Context);
		SetBirthAttribute(System, TEXT("Haze"), TEXT("Particles.BirthColor"), FNiagaraTypeDefinition::GetColorDef(), FLinearColor::White,
			TEXT("float4(User.Color.r, User.Color.g, User.Color.b, User.OpacityScale)"), Context);
		SetBirthAttribute(System, TEXT("Haze"), TEXT("Particles.BirthShape"), FNiagaraTypeDefinition::GetVec2Def(), FVector2f(1.f, 0.f),
			TEXT("float2(User.SizeScale, User.Sink)"), Context);
		SetSpriteSizeOverLife(System, TEXT("Haze"),
			TEXT("float2(1.2 + 2.4 * Particles.NormalizedAge, 0.8 + 1.0 * Particles.NormalizedAge) * (User.Size * Particles.BirthShape.x)"), Context);
		SetColorOverLife(System, TEXT("Haze"),
			TEXT("float4(Particles.BirthColor.r, Particles.BirthColor.g, Particles.BirthColor.b, User.Opacity * Particles.BirthColor.a * saturate(Particles.NormalizedAge * 10.0) * (1.0 - Particles.NormalizedAge * Particles.NormalizedAge))"),
			Context);

		// The chunks: clods, bits of torn grass and grit, User.ChunkSize across, thrown back off the tracks or wheels and out
		// to the sides in short arcs, and falling back to the ground within a second. Turned at random, as clods lie.
		AddSpriteStream(System, TEXT("Chunks"), TEXT("User.Rate * User.Points * User.ChunkScale"), TEXT("0.45 + rand(0.4)"),
			TEXT("User.ChunkSize * (0.6 + rand(0.8))"), TEXT("float4(User.ChunkColor.r, User.ChunkColor.g, User.ChunkColor.b, 1.0)"),
			ChunkMaterial, Bounds, Context);
		SetRandomRotation(System, TEXT("Chunks"), Context);
		SetSpawnSphere(System, TEXT("Chunks"), TEXT("20.0"), TEXT("User.Side * sign(rand(2.0) - 1.0) + float3(0.0, 0.0, 10.0)"), Context,
			TEXT("Local"));
		SetMotion(System, TEXT("Chunks"),
			TEXT("User.Direction * (150.0 + rand(250.0)) + float3((rand(2.0) - 1.0) * 180.0, (rand(2.0) - 1.0) * 180.0, 120.0 + rand(260.0))"),
			0.6f, TEXT("float3(0.0, 0.0, -980.0)"), Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildDustSystem"), Context);
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

UNiagaraSystem* URTSEditorScripting::BuildExhaustSystem(const FString& AssetPath, UMaterialInterface* Material)
{
	return nullptr;
}

UNiagaraSystem* URTSEditorScripting::BuildDustSystem(const FString& AssetPath, UMaterialInterface* HazeMaterial,
	UMaterialInterface* ChunkMaterial)
{
	return nullptr;
}
#endif
