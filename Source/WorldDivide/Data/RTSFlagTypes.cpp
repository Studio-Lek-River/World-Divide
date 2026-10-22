#include "Data/RTSFlagTypes.h"

#include "Materials/MaterialInterface.h"

const FRTSFlagType* URTSFlagTypes::Find(FName Key) const
{
	const FRTSFlagType* Flag = Flags.Find(Key);
	return Flag && Flag->Material ? Flag : nullptr;
}
