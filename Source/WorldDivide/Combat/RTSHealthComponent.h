#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRTSDeathSignature, AActor*, Killer);

/** Replicated hit points. Only the server changes them; clients read them for health bars. */
UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class WORLDDIVIDE_API URTSHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSHealthComponent();

	/** Server only. Fills health up to the new maximum. */
	void InitHealth(float InMaxHealth);

	/** Server only. Clamps at zero and broadcasts OnDeath once; damage to the dead is ignored. */
	void ApplyDamage(float Amount, AActor* DamageInstigator);

	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	float GetHealthFraction() const { return MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f; }
	bool IsDead() const { return Health <= 0.f; }

	/** Server only. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FRTSDeathSignature OnDeath;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	float Health = 1.f;

	UPROPERTY(Replicated)
	float MaxHealth = 1.f;
};
