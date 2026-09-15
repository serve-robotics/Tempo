// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorUtils.h"

#include "TempoAgentsWorldSubsystem.h"
#include "TempoRoadLaneGraphSubsystem.h"

#include "Editor.h"

bool UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline()
{
	// Disabled: use the GIS tools panel "Build Zone Graph" button instead.
	// That button runs SpawnTempoIntersectionActors first, which is required to
	// produce correct results for GIS-imported roads.
	return false;
}
