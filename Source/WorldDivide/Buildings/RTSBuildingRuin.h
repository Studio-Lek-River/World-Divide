#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSBuildingRuin.generated.h"

class ARTSBuilding;
class UStaticMeshComponent;
class URTSBuildingData;

/**
 * The rubble a destroyed building leaves (#196): purely cosmetic, so it has no collision, cannot be selected or shot, is not
 * a building of any team and never holds up placement or a rebuild. The building itself goes at once (ARTSBuilding::HandleDeath)
 * and this takes its place. Every machine that draws collapses the building's meshes flat and dark over the data's
 * RuinCollapseSeconds (or shows its RuinMesh once collapsed), leaves the rubble lying RuinSeconds, then sinks it into the
 * ground over RuinFadeSeconds; the server removes it then. Its BeginPlay is the Destroyed moment the VFX and audio hook (#197).
 */
UCLASS()
class WORLDDIVIDE_API ARTSBuildingRuin : public AActor
{
	GENERATED_BODY()

public:
	ARTSBuildingRuin();

	/** Server only. Spawns the ruin of Building where it stands; null for a building without data. */
	static ARTSBuildingRuin* SpawnFor(const ARTSBuilding& Building);

	URTSBuildingData* GetBuildingData() const { return BuildingData; }

	/** Seconds from the collapse until the ruin is gone. */
	float GetLifetime() const;

	/** Every machine that draws: poses the ruin Age seconds after the collapse began. Public for the tests. */
	void Pose(float Age);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Makes the meshes from the data; nothing on a dedicated server. */
	void BuildMeshes();

	/** Ticks only while something moves: the collapse and the sinking. */
	void ResumeTick();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** The building's mesh, its parts attached, collapsing with it. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** The data's RuinMesh, shown once the collapse is done; null without one. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Rubble;

	UPROPERTY(Replicated)
	TObjectPtr<URTSBuildingData> BuildingData;

	UPROPERTY(Replicated)
	int32 TeamId = 0;

	/** The building's construction progress when it fell; a site falls as the sliver it was. */
	UPROPERTY(Replicated)
	float ConstructionFraction = 1.f;

	FVector MeshScale = FVector::OneVector;
	float MeshBaseOffset = 0.f;
	float MeshHeight = 0.f;
	float Age = 0.f;
	FTimerHandle FadeTimer;
};
