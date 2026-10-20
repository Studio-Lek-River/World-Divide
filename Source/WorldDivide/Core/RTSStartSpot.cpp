#include "Core/RTSStartSpot.h"

#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/RTSMaps.h"

namespace
{
	/** Big enough to read from the editor's top-down view of a whole map. */
	constexpr float NumberWorldSize = 600.f;
	/** Above the capsule, so the number is not hidden in the HQ that spawns here. */
	constexpr float NumberHeight = 1500.f;
}

ARTSStartSpot::ARTSStartSpot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NumberText = CreateEditorOnlyDefaultSubobject<UTextRenderComponent>(TEXT("NumberText"));
	if (NumberText)
	{
		NumberText->SetupAttachment(GetCapsuleComponent());
		NumberText->SetHiddenInGame(true);
		NumberText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NumberText->SetCanEverAffectNavigation(false);
		NumberText->SetHorizontalAlignment(EHTA_Center);
		NumberText->SetVerticalAlignment(EVRTA_TextCenter);
		NumberText->SetWorldSize(NumberWorldSize);
		NumberText->SetTextRenderColor(FColor::Yellow);
		// Laid flat and facing up, readable from above; absolute so it stays flat whichever way the spot faces.
		NumberText->SetUsingAbsoluteRotation(true);
		NumberText->SetRelativeLocationAndRotation(FVector(0.f, 0.f, NumberHeight), FRotator(90.f, 180.f, 0.f));
	}
#endif
}

void ARTSStartSpot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SpotIndex = FMath::Max(SpotIndex, 0);
	PlayerStartTag = RTSMaps::MakeTeamTag(SpotIndex);
#if WITH_EDITORONLY_DATA
	if (NumberText)
	{
		NumberText->SetText(FText::AsNumber(SpotIndex + 1));
	}
#endif
}
