// URTSEditorScripting::BuildMuzzleFlashSystem (#220, #294, #354), BuildMuzzleSmokeSystem (#221, #303, #355), BuildExhaustSystem (#222)
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

	/**
	 * Emitter's particles churn as smoke does (#361): each turns at its own rate of up to Spin degrees a second, slowing as it
	 * ages, and curl noise of Strength (an expression, which may read Particles.Age) pushes them unevenly, before the solver
	 * SetMotion added.
	 */
	void SetChurn(UNiagaraSystem* System, FName Emitter, float Spin, const FString& Strength, FNiagaraExternalEditContext& Context)
	{
		SetBirthAttribute(System, Emitter, TEXT("Particles.SpinRate"), FNiagaraTypeDefinition::GetFloatDef(), 0.f,
			FString::Printf(TEXT("(rand(2.0) - 1.0) * %g"), Spin), Context);
		bool bHasSolver = false;
		const FName Before = ModuleBefore(System, Emitter, ParticleUpdate, TEXT("SolveForcesAndVelocity"), bHasSolver, Context);
		const FName Curl = AddModule(System, Emitter, ParticleUpdate, Before, TEXT("/Niagara/Modules/Update/Forces/CurlNoiseForce.CurlNoiseForce"),
			Context);
		SetExpression(ModuleInput(System, Emitter, ParticleUpdate, Curl, TEXT("Noise Strength")), Strength, Context);
		FNiagaraExt_ScriptStackTopology Stack;
		FNiagaraEdit::GetScriptStackTopology(FNiagaraExt_StackItemReference(System, Emitter, ParticleUpdate), Stack, Context);
		const FName Rotation = AddModule(System, Emitter, ParticleUpdate, Stack.Modules.IsEmpty() ? NAME_None : Stack.Modules.Last().ModuleName,
			TEXT("/Niagara/Modules/Update/Orientation/SpriteRotationRate.SpriteRotationRate"), Context);
		SetExpression(ModuleInput(System, Emitter, ParticleUpdate, Rotation, TEXT("Rotation Rate")),
			TEXT("Particles.SpinRate * exp(-Particles.Age * 0.6)"), Context);
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
	UMaterialInterface* FireMaterial, UMaterialInterface* GlareMaterial)
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

		// The layers of a real firing (docs/art/refs/muzzle_flash) as World in Conflict's tanks show them at 60 fps (#354),
		// along the bore (+X), all gone by 0.2 s; sizes are for a scale of 1, so twice them on the M20's gun (scale 2). The
		// bounds hold the heaviest gun (scale 2.5) and its sparks' flight.
		const FBox Bounds(FVector(-300.0, -700.0, -700.0), FVector(2000.0, 700.0, 700.0));
		const FString Stretched(TEXT(", \"Alignment\": \"VelocityAligned\""));
		const float LoopSeconds = 0.22f;
		// The brightest channel of the shot's colour, for the layers that burn whiter than it.
		const FString Heat(TEXT("max(User.Color.r, max(User.Color.g, User.Color.b))"));

		// The tongue: a short white-hot jet right at the muzzle, there for the first frame or two only.
		AddSpriteBurst(System, TEXT("Tongue"), TEXT("1"), LoopSeconds, TEXT("0.035"), TEXT("User.Scale * 60.0"),
			FString::Printf(TEXT("float4(float3(1.0, 0.95, 0.85) * %s, User.Color.a) * 0.5"), *Heat), GlowMaterial, Bounds, true, Context,
			Stretched);
		SetSpriteSize2D(System, TEXT("Tongue"), TEXT("float2(User.Scale * 20.0, User.Scale * 70.0)"), Context);
		SetSpawnSphere(System, TEXT("Tongue"), TEXT("0.0"), TEXT("float3(User.Scale * 35.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Tongue"), TEXT("float3(User.Scale * 300.0, 0.0, 0.0)"), 10.f, FString(), Context);

		// The blast cone: two tongues of flame stretched along the bore, 1.5 to 2.2 m long, their back ends at the muzzle so
		// they shoot out of it without lying over the barrel, thrown out and gone in 0.05 to 0.08 s.
		AddSpriteBurst(System, TEXT("Cone"), TEXT("2"), LoopSeconds, TEXT("0.05 + rand(0.03)"), TEXT("User.Scale * 60.0"), TEXT("User.Color * 0.5"),
			GlowMaterial, Bounds, true, Context, Stretched);
		SetSpriteSize2D(System, TEXT("Cone"), TEXT("float2(User.Scale * (40.0 + rand(15.0)), User.Scale * (150.0 + rand(70.0)))"), Context);
		SetSpawnSphere(System, TEXT("Cone"), TEXT("0.0"), TEXT("float3(User.Scale * 90.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Cone"), TEXT("float3(User.Scale * (900.0 + rand(500.0)), 0.0, 0.0)"), 25.f, FString(), Context);

		// The brake jets: on a gun with a muzzle brake, a tongue of flame thrown out to each side and a little forward.
		for (const TPair<const TCHAR*, const TCHAR*>& Jet : { TPair<const TCHAR*, const TCHAR*>(TEXT("JetLeft"), TEXT("-")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("JetRight"), TEXT("")) })
		{
			AddSpriteBurst(System, Jet.Key, TEXT("User.Brake"), LoopSeconds, TEXT("0.05 + rand(0.02)"), TEXT("User.Scale * 30.0"),
				TEXT("User.Color * 0.8"), GlowMaterial, Bounds, true, Context, Stretched);
			SetSpriteSize2D(System, Jet.Key, TEXT("float2(User.Scale * 30.0, User.Scale * (90.0 + rand(30.0)))"), Context);
			SetMotion(System, Jet.Key, FString::Printf(TEXT("float3(User.Scale * 150.0, %sUser.Scale * (600.0 + rand(250.0)), 0.0)"), Jet.Value),
				25.f, FString(), Context);
		}

		// The fireball: four puffs of the baked fire (T_MuzzleFire), 1.9 to 2.6 m across, thrown 5 to 30 m/s along the bore and
		// braked by the air to a few percent within 0.1 s, so together they make a ball as big as the turret just in front of
		// the muzzle. White-yellow, fading to yellow (M_MuzzleFire), gone in one frame after 0.15 to 0.2 s.
		AddSpriteBurst(System, TEXT("Fireball"), TEXT("4"), LoopSeconds, TEXT("0.15 + rand(0.05)"), TEXT("User.Scale * (95.0 + rand(35.0))"),
			TEXT("User.Color"), FireMaterial, Bounds, true, Context);
		SetRandomRotation(System, TEXT("Fireball"), Context);
		// Born at the mouth of the barrel at two fifths of their size and swelling to full in the first quarter of their life
		// while the air brakes them, so the ball grows out of the muzzle and its back stays on it.
		SetSpawnSphere(System, TEXT("Fireball"), TEXT("User.Scale * 10.0"), TEXT("float3(User.Scale * 25.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Fireball"),
			TEXT("float3(User.Scale * (150.0 + rand(900.0)), User.Scale * (rand(120.0) - 60.0), User.Scale * (rand(120.0) - 60.0))"), 28.f,
			FString(), Context);
		SetBirthAttribute(System, TEXT("Fireball"), TEXT("Particles.BirthSize"), FNiagaraTypeDefinition::GetFloatDef(), 100.f,
			TEXT("User.Scale * (95.0 + rand(35.0))"), Context);
		SetSpriteSizeOverLife(System, TEXT("Fireball"),
			TEXT("float2(1.0, 1.0) * Particles.BirthSize * (0.4 + 0.6 * saturate(Particles.NormalizedAge * 4.0))"), Context);

		// The sparks: one streak flung 40 to 70 m/s down the bore, white turning blue-white for three quarters of its 0.2 s,
		// then swelling to three times its size as it fades. It and the glare draw with GlareMaterial, which leaves their
		// brightness over life to the colour set here.
		AddSpriteBurst(System, TEXT("Sparks"), TEXT("1"), LoopSeconds, TEXT("0.2"), TEXT("User.Scale * 30.0"), TEXT("User.Color"),
			GlareMaterial, Bounds, true, Context, Stretched);
		SetSpawnSphere(System, TEXT("Sparks"), TEXT("0.0"), TEXT("float3(User.Scale * 60.0, 0.0, 0.0)"), Context);
		SetMotion(System, TEXT("Sparks"), TEXT("float3(User.Scale * (2000.0 + rand(1500.0)), 0.0, 0.0)"), 0.5f, FString(), Context);
		const FString Late(TEXT("saturate((Particles.NormalizedAge - 0.75) / 0.25)"));
		SetSpriteSizeOverLife(System, TEXT("Sparks"),
			FString::Printf(TEXT("float2(User.Scale * 6.0, User.Scale * 25.0) * (1.0 + 2.0 * %s)"), *Late), Context);
		SetColorOverLife(System, TEXT("Sparks"),
			FString::Printf(TEXT("float4(lerp(float3(1.0, 1.0, 1.0), float3(0.7, 0.82, 1.0), saturate(Particles.NormalizedAge / 0.75)) * %s * 0.4, User.Color.a * (1.0 - %s))"),
				*Heat, *Late), Context);

		// The glare: one wide, soft, warm glow over the gun and the ground around it that floods the view for 0.2 s, flaring
		// within a frame and fading fast. It faces the camera and fades where it meets the ground, so it lights the picture,
		// not a disc on the ground (#312), and takes no light from the budget (#227).
		AddSpriteBurst(System, TEXT("Glare"), TEXT("1"), LoopSeconds, TEXT("0.2"), TEXT("User.Scale * 300.0"), TEXT("User.Color"), GlareMaterial,
			Bounds, true, Context);
		SetSpawnSphere(System, TEXT("Glare"), TEXT("0.0"), TEXT("float3(User.Scale * 40.0, 0.0, 0.0)"), Context);
		SetColorOverLife(System, TEXT("Glare"),
			FString::Printf(TEXT("float4(float3(1.0, 0.93, 0.38) * %s * 0.012, User.Color.a * saturate(Particles.NormalizedAge * 12.0) * pow(1.0 - Particles.NormalizedAge, 3.0))"),
				*Heat), Context);
	}
	return FinishSystem(System, AssetPath, TEXT("BuildMuzzleFlashSystem"), Context);
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* BlastMaterial,
	UMaterialInterface* VeilMaterial, UMaterialInterface* DustMaterial)
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

		// World in Conflict's tank smoke as its data and a 60 fps recording show it (#355): clouds thrown fast
		// down the bore that stop almost at once, dark at first, swelling and turning grey, then rising and thinning out over
		// 3 to 7 s. All in world space, so the smoke stays where the shot was fired as the turret turns. Sizes and speeds are
		// for a scale of 1, twice them on the M20's gun; the bounds hold the heaviest gun's clouds and their rise.
		const FBox Bounds(FVector(-2500.0, -2500.0, -800.0), FVector(2500.0, 2500.0, 3500.0));
		const float LoopSeconds = URTSShotSubsystem::MuzzleSmokeSeconds;
		// The air brakes the clouds to 30 % of their speed in 0.1 s, so they stop 1 to 4 m a unit of scale out.
		const float Brake = 12.f;
		// From about a second on they rise, at 1.25 m/s a unit of scale once the drag holds them.
		const FString Rise(TEXT("float3(0.0, 0.0, 1500.0 * User.Scale * saturate((Particles.Age - 0.8) / 0.6))"));
		// A cloud's colour by its age in seconds: born inside the fireball, it glows orange from it (a colour above 1 glows in
		// the smoke material) and cools to dark charcoal at Dark s as the fireball dies, then mid grey at Mid s and light grey
		// from Light s on, a little brown as cordite smoke is. The material's Brightness calibrates it (#327).
		const auto Greys = [](float Dark, float Mid, float Light)
		{
			return FString::Printf(TEXT("lerp(float3(6.0, 2.6, 0.8), float3(1.0, 0.97, 0.92) * lerp(lerp(0.03, 0.12, saturate((Particles.Age - %g) / %g)), 0.4, saturate((Particles.Age - %g) / %g)), pow(saturate(Particles.Age / %g), 0.6))"),
				Dark, Mid - Dark, Mid, Light - Mid, Dark);
		};
		// Thin while the fireball burns over it, so the fire shows through and the smoke grows out of it, dense once it has
		// cooled at 0.15 s, thinner as it swells, faded out over the end of its life.
		const FString Thinning(TEXT("lerp(1.0, 0.55, saturate((Particles.Age - 0.2) / 1.5)) * lerp(0.25, 1.0, saturate((Particles.Age - 0.03) / 0.12)) * (1.0 - Particles.NormalizedAge * Particles.NormalizedAge)"));

		// Many smaller, thinner sprites rather than a few large ones (#361), sorted back to front so their overlaps blend into
		// one rolling cloud, each churning (SetChurn) from about 0.3 s on as the blast's push dies.
		const FString Sorted(TEXT(", \"SortMode\": \"ViewDepth\""));
		const FString Churn(TEXT("User.Scale * 90.0 * saturate((Particles.Age - 0.3) / 0.6)"));

		// The blast clouds: four in ten of User.Count (16 at epic up close), born at the mouth of the barrel where the fireball
		// is, thrown 6 to 36 m/s a unit of scale along the bore and spread sideways, so the air stops them anywhere from 0.5 to
		// 3 m out: the slow ones stay at the muzzle and the fast ones carry the cloud out, one cloud from the barrel to a
		// tank-length out on the M20 (#355). The size of the fireball's puffs at birth, 2 to 3 m across by 0.15 s, swelling to
		// 4.5 to 7 m. Gone after 3 to 7 s.
		AddSpriteBurst(System, TEXT("Blast"), TEXT("max(User.Count * 4 / 10, 2)"), LoopSeconds, TEXT("3.0 + rand(4.0)"),
			TEXT("User.Scale * 250.0"), TEXT("float4(0.3, 0.22, 0.14, 0.6)"), BlastMaterial, Bounds, false, Context, Sorted);
		SetRandomRotation(System, TEXT("Blast"), Context);
		SetSpawnSphere(System, TEXT("Blast"), TEXT("User.Scale * 10.0"), TEXT("User.Direction * (User.Scale * 25.0)"), Context);
		SetMotion(System, TEXT("Blast"),
			TEXT("User.Direction * (User.Scale * (600.0 + rand(3000.0))) + float3(rand(500.0) - 250.0, rand(500.0) - 250.0, rand(240.0) - 100.0) * User.Scale"),
			Brake, Rise, Context);
		SetBirthAttribute(System, TEXT("Blast"), TEXT("Particles.CloudSize"), FNiagaraTypeDefinition::GetVec2Def(), FVector2f(250.f, 575.f),
			TEXT("float2(200.0 + rand(100.0), 450.0 + rand(250.0)) * User.Scale"), Context);
		SetSpriteSizeOverLife(System, TEXT("Blast"),
			TEXT("float2(1.0, 1.0) * (lerp(User.Scale * 50.0, Particles.CloudSize.x, saturate(Particles.Age / 0.15)) + (Particles.CloudSize.y - Particles.CloudSize.x) * (1.0 - exp(-Particles.Age * 1.6)))"),
			Context);
		SetColorOverLife(System, TEXT("Blast"), FString::Printf(TEXT("float4(%s, 0.6 * %s)"), *Greys(0.15f, 0.25f, 1.2f), *Thinning), Context);
		SetChurn(System, TEXT("Blast"), 35.f, Churn, Context);

		// The bore puffs: three in ten of User.Count (12 at epic up close) and at least one, born at the muzzle and thrown 2 to
		// 20 m/s down the bore, so they fill the way from the barrel to the blast clouds. 0.5 m
		// growing to 1.5 m by 0.06 s and 1.7 to 2 m by 0.16 s, then swelling to 3.5 to 4.5 m. Gone after about 5 s.
		AddSpriteBurst(System, TEXT("Puffs"), TEXT("max(User.Count * 3 / 10, 1)"), LoopSeconds, TEXT("4.5 + rand(0.5)"), TEXT("User.Scale * 50.0"),
			TEXT("float4(0.3, 0.22, 0.14, 0.6)"), BlastMaterial, Bounds, false, Context, Sorted);
		SetRandomRotation(System, TEXT("Puffs"), Context);
		SetSpawnSphere(System, TEXT("Puffs"), TEXT("User.Scale * 10.0"), TEXT("User.Direction * (User.Scale * 25.0)"), Context);
		SetMotion(System, TEXT("Puffs"),
			TEXT("User.Direction * (User.Scale * (200.0 + rand(1800.0))) + float3(rand(200.0) - 100.0, rand(200.0) - 100.0, rand(160.0) - 60.0) * User.Scale"),
			Brake, Rise, Context);
		SetBirthAttribute(System, TEXT("Puffs"), TEXT("Particles.CloudSize"), FNiagaraTypeDefinition::GetVec2Def(), FVector2f(185.f, 400.f),
			TEXT("float2(170.0 + rand(30.0), 350.0 + rand(100.0)) * User.Scale"), Context);
		SetSpriteSizeOverLife(System, TEXT("Puffs"),
			TEXT("float2(1.0, 1.0) * lerp(lerp(lerp(50.0 * User.Scale, 150.0 * User.Scale, saturate(Particles.Age / 0.06)), Particles.CloudSize.x, saturate((Particles.Age - 0.06) / 0.1)), Particles.CloudSize.y, 1.0 - exp(-max(Particles.Age - 0.16, 0.0) * 1.2))"),
			Context);
		SetColorOverLife(System, TEXT("Puffs"), FString::Printf(TEXT("float4(%s, 0.6 * %s)"), *Greys(0.1f, 0.2f, 1.2f), *Thinning), Context);
		SetChurn(System, TEXT("Puffs"), 25.f, Churn, Context);

		// The veil: one wide, thin grey sheet over the gun and the view close to it, there from 0.1 to 0.6 s after the shot,
		// thickest at about 0.2 s. Left out far away at the lowest quality.
		AddSpriteBurst(System, TEXT("Veil"), TEXT("User.Count >= 3 ? 1 : 0"), LoopSeconds, TEXT("0.6"), TEXT("User.Scale * 450.0"),
			TEXT("float4(0.15, 0.145, 0.14, 0.0)"), VeilMaterial, Bounds, false, Context);
		SetRandomRotation(System, TEXT("Veil"), Context);
		SetSpawnSphere(System, TEXT("Veil"), TEXT("0.0"), TEXT("User.Direction * (User.Scale * 150.0)"), Context);
		SetMotion(System, TEXT("Veil"), TEXT("User.Direction * (User.Scale * 200.0)"), 3.f, FString(), Context);
		SetColorOverLife(System, TEXT("Veil"),
			TEXT("float4(0.15, 0.145, 0.14, 0.4 * saturate((Particles.Age - 0.1) / 0.08) * (1.0 - saturate((Particles.Age - 0.3) / 0.3)))"), Context);

		// The wisp: after the blast, thin smoke goes on leaking from the muzzle for 1.5 s, fewer puffs as it runs out, each
		// drifting slowly down the bore and up, swelling from 0.4 to 1.5 m and fading over 1.5 to 2.5 s. Left out where the
		// veil is.
		AddSpriteStream(System, TEXT("Wisp"), TEXT("User.Count >= 3 ? 10.0 * (1.0 - saturate(Emitter.Age / 1.5)) : 0.0"),
			TEXT("1.5 + rand(1.0)"), TEXT("User.Scale * 20.0"), TEXT("float4(0.5, 0.49, 0.47, 0.0)"), VeilMaterial, Bounds, Context);
		const FName EmitterUpdate(TEXT("EmitterUpdateScript"));
		SetEnum(ModuleInput(System, TEXT("Wisp"), EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior")), TEXT("Once"), Context);
		SetFloat(ModuleInput(System, TEXT("Wisp"), EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration")), 1.5f, Context);
		SetRandomRotation(System, TEXT("Wisp"), Context);
		SetSpawnSphere(System, TEXT("Wisp"), TEXT("User.Scale * 5.0"), TEXT("User.Direction * (User.Scale * 20.0)"), Context);
		SetMotion(System, TEXT("Wisp"), TEXT("User.Direction * (User.Scale * (20.0 + rand(30.0))) + float3(0.0, 0.0, User.Scale * 30.0)"), 0.5f,
			FString(), Context);
		SetSpriteSizeOverLife(System, TEXT("Wisp"), TEXT("float2(1.0, 1.0) * User.Scale * lerp(20.0, 75.0, Particles.NormalizedAge)"), Context);
		SetColorOverLife(System, TEXT("Wisp"),
			TEXT("float4(0.5, 0.49, 0.47, 0.3 * saturate(Particles.Age * 8.0) * (1.0 - Particles.NormalizedAge))"), Context);
		SetChurn(System, TEXT("Wisp"), 20.f, TEXT("User.Scale * 30.0"), Context);

		// The ground blast: when the muzzle is less than 3 m a unit of scale above the ground, the pressure wave kicks dust off
		// it under and ahead of the muzzle, thrown out flat along the bore and to the sides, settling in 0.6 to 1 s.
		AddSpriteBurst(System, TEXT("Dust"), TEXT("User.GroundHeight < 300.0 * User.Scale ? max(User.Count * 4 / 10, 2) : 0"),
			LoopSeconds, TEXT("0.6 + rand(0.4)"), TEXT("User.Scale * (100.0 + rand(70.0))"),
			TEXT("float4(0.6, 0.5, 0.38, 0.6)"), DustMaterial, Bounds, false, Context, Sorted);
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
	UMaterialInterface* FireMaterial, UMaterialInterface* GlareMaterial)
{
	return nullptr;
}

UNiagaraSystem* URTSEditorScripting::BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* BlastMaterial,
	UMaterialInterface* VeilMaterial, UMaterialInterface* DustMaterial)
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
