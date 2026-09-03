// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoInstanceBoundsFilterInterface.generated.h"

UINTERFACE()
class TEMPOCORE_API UTempoInstanceBoundsFilterInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by an Actor (or a component on it) that wants to EXCLUDE one whole category of its own
// sub-geometry -- UInstancedStaticMeshComponent instances, or USplineMeshComponent segments -- from
// UTempoCoreUtils::GetActorLocalInstanceBounds entirely, while still rendering it normally. Useful
// for an Actor that mixes both kinds for different visual purposes (e.g. a grass-strip prop line
// whose SplineMesh entries are the ground surface and whose InstancedStaticMesh entries are loose
// decorative tufts that shouldn't themselves read as separate obstacles) and wants only one kind
// counted as this Actor's Tempo obstacle geometry.
//
// Both kinds report BY DEFAULT for every Actor -- an Actor need only implement this interface to
// EXCLUDE one. In that sense this is an override interface, like ITempoSegmentedSplineMeshBoundsInterface,
// not an opt-in one.
class TEMPOCORE_API ITempoInstanceBoundsFilterInterface
{
	GENERATED_BODY()

public:
	// False to exclude every UInstancedStaticMeshComponent's instances on this Actor from
	// GetActorLocalInstanceBounds (they still render; they're just not reported as obstacle boxes).
	virtual bool ShouldReportInstancedMeshBounds() const = 0;

	// False to exclude every USplineMeshComponent's segments on this Actor from
	// GetActorLocalInstanceBounds (they still render). Independent of
	// ITempoSegmentedSplineMeshBoundsInterface, which only controls HOW a spline mesh that DOES
	// report is subdivided, not whether it reports at all.
	virtual bool ShouldReportSplineMeshBounds() const = 0;
};
