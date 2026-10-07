#include "Combat/RTSHealthComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

URTSHealthComponent::URTSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URTSHealthComponent::InitHealth(float InMaxHealth)
{
	check(GetOwner()->HasAuthority());
	MaxHealth = FMath::Max(InMaxHealth, 1.f);
	Health = MaxHealth;
}

void URTSHealthComponent::SetMaxHealth(float InMaxHealth)
{
	check(GetOwner()->HasAuthority());
	const float NewMaxHealth = FMath::Max(InMaxHealth, 1.f);
	Health = MaxHealth > 0.f ? Health * NewMaxHealth / MaxHealth : NewMaxHealth;
	MaxHealth = NewMaxHealth;
}

void URTSHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator)
{
	check(GetOwner()->HasAuthority());
	if (IsDead() || Amount <= 0.f)
	{
		return;
	}

	Health = FMath::Max(Health - Amount, 0.f);
	if (IsDead())
	{
		OnDeath.Broadcast(DamageInstigator);
	}
}

void URTSHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URTSHealthComponent, Health);
	DOREPLIFETIME(URTSHealthComponent, MaxHealth);
}
