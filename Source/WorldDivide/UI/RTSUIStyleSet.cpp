#include "UI/RTSUIStyleSet.h"

FName URTSUIStyleSet::MakeTextStyleKey(FName Font, FName Color)
{
	return FName(*FString::Printf(TEXT("%s.%s"), *Font.ToString(), *Color.ToString()));
}
