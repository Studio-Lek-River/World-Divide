#pragma once

#include "CoreMinimal.h"
#include "Buildings/RTSBuildingStateComponent.h"
#include "Combat/RTSTargetable.h"
#include "Data/RTSKitPieceData.h"
#include "GameFramework/Actor.h"
#include "RTSBuilding.generated.h"

class ARTSUnit;
class UBoxComponent;
class UInstancedStaticMeshComponent;
class ULocalLightComponent;
class UStaticMeshComponent;
class URTSBuildingData;
class URTSGarageComponent;
class URTSHealthComponent;
class URTSProductionComponent;
class URTSSuperweaponComponent;
class URTSUnitData;
class URTSWeaponComponent;
struct FRTSBuildingFootprint;
struct FRTSBuildingLight;
struct FNavAgentProperties;

/**
 * A structure on the build grid. Stats come from URTSBuildingData. It blocks the navmesh from the moment it is placed,
 * constructs over BuildTime and stays inert, weapon and production included, until construction completes. Completion also
 * spawns the data's SpawnOnComplete unit, such as a depot's harvester, which sets off on a Harvest order.
 * Construction runs on the server; clients see the progress through replication.
 */
UCLASS()
class WORLDDIVIDE_API ARTSBuilding : public AActor, public IRTSTargetable
{
	GENERATED_BODY()

public:
	ARTSBuilding();

	/** Server only, between SpawnActorDeferred and FinishSpawning. The actor origin is the footprint centre on the ground. */
	void InitFromData(URTSBuildingData* InBuildingData, int32 InTeamId);

	/** Server only. Sets weapon damage and maximum health from the data and the upgrades the owner has researched. */
	void ApplyUpgrades();

	/**
	 * Server only. Advances construction, slower while the owner is low on power; completing it arms the weapon, adds the
	 * building to the owner's power grid, spawns the SpawnOnComplete unit, puts the rally point in front of the exit and,
	 * for a superweapon, starts its charge and warns the opponents.
	 * Public so tests and builders can drive it.
	 */
	void UpdateConstruction(float DeltaSeconds);

	/**
	 * Server only. Arms the weapon once constructed, unless the data requires power and the owner is low on power, and
	 * records the owner's power for every machine (IsOwnerLowPower).
	 */
	void RefreshWeapon();

	/**
	 * Whether the owner was low on power when the grid last changed (ARTSPlayerState::RecalculatePower), on every machine:
	 * the player's power replicates to that player alone, but everyone sees a building go dark (URTSBuildingStateComponent).
	 */
	bool IsOwnerLowPower() const { return bOwnerLowPower; }

	/**
	 * Server only. Fire at Target, of any team, until it dies or leaves range; when it is null, at the ground at Location until
	 * StopFiring. Refused, returning false, unless the building is constructed, its weapon can force-fire there
	 * (URTSWeaponComponent::CanForceFireAt) and the target or spot is already in range: a building cannot close in.
	 */
	bool ForceFire(AActor* Target, const FVector& Location);

	/** Server only. Drops a force-fire order; the weapon engages enemies in range on its own again. */
	void StopFiring();

	URTSBuildingData* GetBuildingData() const { return BuildingData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	URTSProductionComponent* GetProductionComponent() const { return ProductionComponent; }
	URTSSuperweaponComponent* GetSuperweaponComponent() const { return SuperweaponComponent; }
	URTSGarageComponent* GetGarageComponent() const { return GarageComponent; }
	URTSBuildingStateComponent* GetStateComponent() const { return StateComponent; }

	/**
	 * Where units leave the building: outside its front edge (the data's +X edge, turned with the building), Clearance beyond
	 * it, on the exit lane (RTSBuildPlacement::GetExitLaneOffset: the garage's for a building with one, else the middle of
	 * the edge), nudged onto Agent's navmesh, or the narrowest agent's without one, when it exists; on a garage's lane only
	 * along the lane, so it stays in it.
	 */
	FVector GetExitLocation(float Clearance, const FNavAgentProperties* Agent = nullptr) const;

	/** Server only. Spawns a unit of Data for the building's team at the exit. */
	ARTSUnit* SpawnUnitAtExit(URTSUnitData* Data);

	/**
	 * Server only. Sends out a unit of Data that production completed: through the garage for a vehicle when the building has
	 * one (URTSGarageComponent), else spawned at the exit and sent off at once (URTSProductionComponent::SendOut).
	 */
	void DeliverUnit(URTSUnitData& Data);

	/**
	 * The component that draws the part Name (URTSBuildingData::Parts) and, when it draws several, the part's instance in it;
	 * INDEX_NONE for a part drawn on its own. False when the data has no part of that name.
	 */
	bool FindPart(FName Name, UStaticMeshComponent*& OutComponent, int32& OutInstance) const;

	/**
	 * Writes the lens state (RTSKitLensData::Floats values) of the mounted kit piece Part to its instance alone, with one
	 * update of its component's instances. False when Part is not a mounted piece.
	 */
	bool SetKitInstanceData(FName Part, TConstArrayView<float> Values);

	/** The mounted kit pieces as RTSKit::Expand resolved their mounts (FRTSKitExpansion::PartInfo), in mount order. */
	const TArray<FRTSKitPart>& GetKitParts() const { return KitParts; }

	/**
	 * Shows Looks, one per GetKitParts (URTSBuildingStateComponent, BUILDING_KIT.md, States): each lens's RTSKitLensData with
	 * one MarkRenderInstancesDirty per component, and each real light's intensity and colour with it.
	 */
	void SetKitLooks(TConstArrayView<FRTSKitLook> Looks);
	const TArray<FRTSKitLook>& GetKitLooks() const { return KitLooks; }

	/**
	 * URTSBuildingLightSubsystem, each frame this building's lights are shown: switches each kit light with its lens's flash and
	 * fault phase at Time (game seconds), turns and fades the Rotate ones with their spin (RTSKit::GetSpin, #459), and keeps out those beyond their MaxDrawDistance from the
	 * nearest of Cameras (or Override), the Secondary ones unless bSecondary, and every one past Budget, which it counts down.
	 */
	void UpdateKitLights(double Time, TConstArrayView<FVector> Cameras, const TOptional<FVector>& Override, bool bSecondary, int32& Budget);

	/** The kit lights lit now, of GetLightComponents. */
	int32 GetLitKitLightCount() const;

	/**
	 * Draws every mesh of the building with LODs, shell, parts and kit pieces, at LOD, or lets the renderer pick again for
	 * INDEX_NONE: one decision for the whole building (BUILDING_KIT.md, option C2), from URTSBuildingLightSubsystem.
	 * Touches the components only when it changes.
	 */
	void SetForcedLOD(int32 LOD);
	int32 GetForcedLOD() const { return ForcedLOD; }

	/**
	 * How fast the parts that turn on their own run (URTSBuildingMotionSubsystem, #351): 1 at their data's rates, 0 stopped
	 * where they are, between them slower. Set by URTSBuildingStateComponent (#196), which stops the fans and radar on low power;
	 * this machine only, it does not replicate.
	 */
	void SetEquipmentMotionScale(float Scale) { EquipmentMotionScale = FMath::Max(Scale, 0.f); }
	float GetEquipmentMotionScale() const { return EquipmentMotionScale; }

	/** Aircraft pads, from the data; 0 for a building that is not an airfield. */
	int32 GetPadCount() const;

	/** Where an aircraft stands on pad Index: on the roof, pads side by side across the footprint along the data's Y. */
	FVector GetPadLocation(int32 Index) const;

	/** The aircraft bound to pad Index, or null while it is free. */
	ARTSUnit* GetPadAircraft(int32 Index) const;

	/** Pads neither bound to an aircraft nor kept for an aircraft in the production queue. */
	int32 GetFreePadCount() const;

	/**
	 * Server only. Binds Aircraft to a free pad and returns its index, or the pad it already has here; INDEX_NONE when none is
	 * free.
	 */
	int32 ClaimPad(ARTSUnit& Aircraft);

	/** Server only. Frees Aircraft's pad here, if it has one. */
	void ReleasePad(const ARTSUnit& Aircraft);

	/**
	 * The data's footprint in world cells, turned with the building (RTSBuildPlacement::GetRotatedFootprint): only for a
	 * building turned in quarter turns. Anything that looks at the ground it covers takes GetGroundFootprint.
	 */
	FIntPoint GetFootprint() const;

	/** The footprint on the ground, turned with the building whatever its yaw. */
	FRTSBuildingFootprint GetGroundFootprint() const;

	/** From clamped to the turned footprint rectangle. Under construction or not, the building can be shot. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/**
	 * The meshes drawn with Mesh for a building of several (URTSBuildingData::Parts, then its mounted kit pieces), in the
	 * order of each mesh's first part: one component per part, or one UInstancedStaticMeshComponent for the parts that share
	 * a mesh and how it draws (the HQ's door sections), an instance per part in the data's order (#358). Kit pieces always
	 * draw instanced, even alone.
	 */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetPartComponents() const { return PartComponents; }

	/**
	 * The real lights at the building's lamps (URTSBuildingData::Lights, #195) but the garage's beacons, which the garage
	 * flashes itself; hidden until URTSBuildingLightSubsystem shows them in the dark. None on a dedicated server.
	 */
	const TArray<TObjectPtr<ULocalLightComponent>>& GetLightComponents() const { return LightComponents; }

	/**
	 * A light of the data at its lamp, attached to Mesh and tagged Light.<Group>, hidden, casting no shadow; null on a
	 * dedicated server. For the building's own lights and the garage's beacons.
	 */
	ULocalLightComponent* CreateLight(const FRTSBuildingLight& Light);

	/** The tag of the light components of lighting group Group, Light.<Group>. */
	static FName GetLightGroupTag(FName Group);

	/**
	 * Switches lighting group Group (Windows, Fixtures, Accents, GarageLamps) on or off: its lights and the glow of the
	 * materials of that group, if any (RTSPrimitiveData::LightWindows..). All are on by default; the garage switches
	 * GarageLamps with its door. URTSBuildingStateComponent switches the other three (#196); this machine
	 * only, it does not replicate.
	 */
	void SetLightGroupOn(FName Group, bool bOn);
	bool IsLightGroupOn(FName Group) const { return !LightGroupsOff.Contains(Group); }

	/** URTSBuildingLightSubsystem: the dark raises the glow to each material's NightGain; Show lights its lamps' lights. */
	void SetLightsNight(bool bNight);
	void ShowLights(bool bShow);
	bool AreLightsShown() const { return bLightsShown; }

	/**
	 * The boxes that block units and cut the navmesh: the footprint box alone, or the data's CollisionBoxes, the first of them
	 * in that box.
	 */
	TArray<UBoxComponent*> GetCollisionComponents() const;

	/** 0 when placed, 1 when complete. */
	float GetConstructionProgress() const { return ConstructionProgress; }
	bool IsConstructed() const { return ConstructionProgress >= 1.f; }

	/** Credits the owner paid for the building; 0 for one placed for free, such as by the map. */
	int32 GetPaidCost() const { return PaidCost; }

	/** Server only. Set right after a purchase, while the building is still in its initial replication. */
	void SetPaidCost(int32 InPaidCost);

	/**
	 * What cancelling returns now: the unbuilt share of PaidCost, rounded down, so nothing once constructed.
	 * A construction site that is destroyed refunds nothing.
	 */
	int32 GetCancelRefund() const;

	/**
	 * Server only. True from placement until the navmesh hole under the footprint is built. Meanwhile units whose path leads
	 * into the footprint wait at its edge, and route around it once the hole is there.
	 */
	bool IsNavCutPending() const { return bNavCutPending; }

	/**
	 * Server only. While set the building constructs itself each tick, as one from the debug build menu does; a builder unit
	 * clears it on the site it places and drives UpdateConstruction while it stands there.
	 */
	UPROPERTY(EditAnywhere, Category = "Construction")
	bool bAutoConstruct = true;

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_BuildingData();

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_ConstructionProgress();

	/** Server only. Warns the team's players unless the instigator is on the building's own team. */
	UFUNCTION()
	void HandleDamaged(float Amount, AActor* DamageInstigator);

	/** Server only. Leaves a ruin (ARTSBuildingRuin) and goes at once. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void ApplyBuildingData();
	void ApplyTeamColor();

	/**
	 * Makes the data's Parts and its mounted kit pieces (Kit), replacing any made before, attached to Mesh so they rise and
	 * scale with it; the flag part flies the data's FlagType, and the parts that turn on their own go to
	 * URTSBuildingMotionSubsystem.
	 */
	void ApplyParts(const FRTSKitExpansion& Kit);

	/**
	 * Makes the data's Lights and its kit pieces' (Kit) but the garage's beacons, replacing any made before, and registers
	 * with URTSBuildingLightSubsystem.
	 */
	void ApplyLights(const FRTSKitExpansion& Kit);

	/** Writes the lighting groups and the night (RTSPrimitiveData::LightWindows..LightNight) to Mesh and every part. */
	void ApplyLightData();

	/** Sizes the collision: one box over the footprint up to Height, or the data's CollisionBoxes. */
	void ApplyCollision(float Height);

	/** The mesh rises out of the ground as construction progresses. */
	void ApplyConstructionVisual();

	/** Server only. Spawns SpawnOnComplete at the exit and sends a harvester off to work. */
	void SpawnCompletionUnit();

	/** Server only, while the navmesh hole is pending: holds units headed into the footprint, releases them once it is built. */
	void UpdateNavCut(float DeltaSeconds);

	void ReleaseHeldUnits();

	/** Server only, each tick once constructed: drops a force-fired target that is gone or, while armed, out of range. */
	void UpdateForcedFire();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Sized to the footprint. Blocks mouse traces for picking and cuts a hole in the navmesh. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** The data's Parts, on Mesh (GetPartComponents). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PartComponents;

	/** Each part's index in PartComponents and its instance there, INDEX_NONE for a part drawn on its own. */
	TMap<FName, TPair<int32, int32>> PartIndex;

	/** The data's Lights but the garage's beacons (GetLightComponents). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULocalLightComponent>> LightComponents;

	/** The data's own lights lead LightComponents; the kit pieces' follow, which their looks switch, not their groups. */
	int32 OwnLightCount = 0;

	/** A kit piece's light: its part (KitParts), its place in LightComponents and how it shines at full. */
	struct FKitLight
	{
		int32 Part = INDEX_NONE;
		int32 Component = INDEX_NONE;
		float Candelas = 0.f;
		FLinearColor Color = FLinearColor::White;
		FRotator Rotation = FRotator::ZeroRotator;
	};
	TArray<FKitLight> KitLights;
	TArray<FRTSKitPart> KitParts;
	TArray<FRTSKitLook> KitLooks;

	/**
	 * A turning beacon's switch (#459), one per KitParts, 0 and 0 for the rest: the game seconds it last came on and went off
	 * (RTSKit::GetSpin, RTSKitLensData::SpinOn) and its look's level when last on, which it keeps while it coasts down.
	 */
	struct FKitSpin
	{
		float On = 0.f;
		float Off = 0.f;
		float Level = 0.f;
	};
	TArray<FKitSpin> KitSpins;

	/** Part's look's level; a turning beacon's level when last on, as its lamp fades by its spin (KitSpins). */
	float GetKitLevel(int32 Part) const;

	/** Where in its cycle the building's blinking lenses and their halos are (#460), 0 to 1, from where it stands: the same on every machine. */
	float GetKitBlinkPhase() const;

	/** The kit pieces' beams and glare halos (FRTSKitExpansion::Glows), one instanced component per mesh, shown at night (#440). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> GlowComponents;

	/** A beam or halo: its part (KitParts), its component and instance there, and its colour at full. */
	struct FKitGlow
	{
		int32 Part = INDEX_NONE;
		int32 Component = INDEX_NONE;
		int32 Instance = INDEX_NONE;
		FLinearColor Color = FLinearColor::White;
		/** FRTSKitGlow::BlinkShift: -1 steady. */
		float BlinkShift = -1.f;
		/** FRTSKitGlow::Point. */
		float Point = 0.f;
	};
	TArray<FKitGlow> KitGlows;
	/** Which of GlowComponents draw beams, which rts.Fx.KitBeams can turn off; the rest draw halos. */
	TBitArray<> GlowIsBeam;

	/** Makes GlowComponents from Kit's glows; none on a dedicated server. */
	void ApplyKitGlows(const FRTSKitExpansion& Kit);

	/** Shows KitLooks on the kit lights' intensity and colour, and on their beams and halos. */
	void ApplyKitLightLooks();

	/** Shows the beams and halos in the dark only. */
	void ShowKitGlows();

	/** The lighting groups switched off (SetLightGroupOn); the rest are on. */
	TSet<FName> LightGroupsOff;
	bool bLightsNight = false;
	bool bLightsShown = false;

	/** The data's CollisionBoxes after the first, which Collision takes. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ExtraCollision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSProductionComponent> ProductionComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSSuperweaponComponent> SuperweaponComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSGarageComponent> GarageComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSBuildingStateComponent> StateComponent;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingData)
	TObjectPtr<URTSBuildingData> BuildingData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ConstructionProgress)
	float ConstructionProgress = 0.f;

	UPROPERTY(Replicated)
	int32 PaidCost = 0;

	UPROPERTY(Replicated)
	bool bOwnerLowPower = false;

	float MeshBaseOffset = 0.f;

	/** Where UpdateNavCut looks for the navmesh hole, in the actor's frame: inside a collision box. */
	FVector NavProbeOffset = FVector::ZeroVector;

	/** Server only. */
	bool bNavCutPending = false;
	float NavCutWaited = 0.f;
	TArray<TWeakObjectPtr<ARTSUnit>> HeldUnits;

	/** Server only. The actor a force-fire order is at; unset for none or an order at the ground. */
	TWeakObjectPtr<AActor> ForcedTarget;

	/** Server only. The aircraft bound to each pad; one destroyed frees its pad by itself. */
	TArray<TWeakObjectPtr<ARTSUnit>> Pads;

	float EquipmentMotionScale = 1.f;

	/** The LOD SetForcedLOD forces on every mesh, INDEX_NONE while the renderer picks. */
	int32 ForcedLOD = INDEX_NONE;
};
