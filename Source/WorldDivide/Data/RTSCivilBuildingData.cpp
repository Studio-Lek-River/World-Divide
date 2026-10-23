#include "Data/RTSCivilBuildingData.h"

#include "Engine/StaticMesh.h"

UStaticMesh* URTSCivilBuildingData::GetMesh(ERTSCivilBuildingState State) const
{
	UStaticMesh* Damaged = DamagedMesh ? DamagedMesh.Get() : PristineMesh.Get();
	switch (State)
	{
	case ERTSCivilBuildingState::Rubble:
		return RubbleMesh ? RubbleMesh.Get() : Damaged;
	case ERTSCivilBuildingState::Damaged:
		return Damaged;
	default:
		return PristineMesh;
	}
}
