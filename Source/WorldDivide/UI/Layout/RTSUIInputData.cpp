#include "UI/Layout/RTSUIInputData.h"

#include "CommonUITypes.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"

namespace
{
	const FName ClickRow(TEXT("Click"));
	const FName BackRow(TEXT("Back"));

	/** A row bound to Key on mouse and keyboard; its key is protected, so it is set through reflection. */
	FCommonInputActionDataBase MakeAction(const FText& DisplayName, const FKey& Key)
	{
		FCommonInputActionDataBase Action;
		Action.DisplayName = DisplayName;
		const FStructProperty* Keyboard = FindFProperty<FStructProperty>(FCommonInputActionDataBase::StaticStruct(), TEXT("KeyboardInputTypeInfo"));
		Keyboard->ContainerPtrToValuePtr<FCommonInputTypeInfo>(&Action)->SetKey(Key);
		return Action;
	}
}

URTSUIInputData::URTSUIInputData()
{
	Actions = CreateDefaultSubobject<UDataTable>(TEXT("Actions"));
	Actions->RowStruct = FCommonInputActionDataBase::StaticStruct();
	Actions->AddRow(ClickRow, MakeAction(NSLOCTEXT("RTSUI", "Click", "Select"), EKeys::LeftMouseButton));
	Actions->AddRow(BackRow, MakeAction(NSLOCTEXT("RTSUI", "Back", "Back"), EKeys::Escape));

	DefaultClickAction.DataTable = Actions;
	DefaultClickAction.RowName = ClickRow;
	DefaultBackAction.DataTable = Actions;
	DefaultBackAction.RowName = BackRow;
}
