// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoCustomInstanceBoundsInterface.generated.h"

struct FTempoInstanceBounds;

UINTERFACE()
class TEMPOCORE_API UTempoCustomInstanceBoundsInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by an Actor (or a component on it) that wants to fully own the decomposition of its
// own USplineMeshComponents into GetActorLocalInstanceBounds entries, instead of Tempo's generic
// per-component AppendSplineMeshSegmentBounds pass (see ITempoSegmentedSplineMeshBoundsInterface).
// That generic pass operates strictly within ONE USplineMeshComponent's own undeformed mesh domain
// -- it has no visibility into how several components chain together into one continuous placed
// run (a machine-city-only concept, e.g. ASplinePropLine's FSplinePropRun), so it can never produce
// a cuboid coarser than one component's own length. This interface lets an Actor that DOES have
// that context (its own run/spline data) do the decomposition itself, run-wide.
//
// Called ONCE per Actor, batched across every USplineMeshComponent at once -- NOT once per
// component -- so the implementer can freely group/split across component boundaries. Only affects
// the SplineMesh category: an Actor's InstancedStaticMeshComponent instances (and everything else
// GetActorLocalInstanceBounds reports) are completely unaffected and continue through the normal
// per-component path, respecting ITempoInstanceBoundsFilterInterface exactly as before. An Actor
// that doesn't implement this (the overwhelming majority) sees no change at all.
class TEMPOCORE_API ITempoCustomInstanceBoundsInterface
{
	GENERATED_BODY()

public:
	// Return true and append this Actor's own SplineMesh-derived entries to OutInstanceBounds to
	// take over that category entirely -- GetActorLocalInstanceBounds then skips its generic
	// per-USplineMeshComponent segmentation pass for every USplineMeshComponent on this Actor.
	// GetActorLocalInstanceBounds height-clamps whatever entries this call appends the same way it
	// clamps every other entry, so the implementer does not need to duplicate that itself.
	// Return false (leaving OutInstanceBounds untouched) to fall back to the generic pass instead --
	// e.g. while this Actor's own run data isn't ready yet.
	virtual bool GetCustomSplineMeshInstanceBounds(TArray<FTempoInstanceBounds>& OutInstanceBounds) const = 0;
};
