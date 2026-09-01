// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoSegmentedSplineMeshBoundsInterface.generated.h"

UINTERFACE()
class TEMPOCORE_API UTempoSegmentedSplineMeshBoundsInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by an Actor (or a component on it) that wants each of its USplineMeshComponents
// reported to UTempoCoreUtils::GetActorLocalInstanceBounds as SEVERAL oriented boxes -- one per
// straight sub-segment of the bent mesh -- instead of the single actor-axis-aligned box a deformed
// spline mesh would otherwise collapse to. A 20m fence rail bent around a block corner has an AABB
// covering the whole corner's interior, which reads downstream as a solid obstacle across ground a
// robot can actually drive over; a chain of chord-fitted cuboids traces the real geometry instead.
//
// Segmentation is ON BY DEFAULT for every Actor -- an Actor need only implement this interface to
// TURN IT OFF, or to tighten/loosen the tolerance from the global default (see the
// Tempo.SplineMeshBounds.* console variables in TempoCoreUtils.cpp). In that sense this is really an
// OVERRIDE interface, not an opt-in one; named for what it reports rather than for its (uncommon)
// off state, to match ITempoBoundsHeightClampInterface's naming.
class TEMPOCORE_API ITempoSegmentedSplineMeshBoundsInterface
{
	GENERATED_BODY()

public:
	// False to report each USplineMeshComponent as a single box, as GetActorLocalInstanceBounds did
	// before this interface existed.
	virtual bool ShouldReportSegmentedSplineMeshBounds() const = 0;

	// Maximum sagitta -- chord-to-centreline deviation, in cm -- tolerated within one reported
	// sub-segment: a bent spline mesh is subdivided until every sub-segment's straight chord stays
	// within this distance of the mesh's true deformed centreline (roll twist and cross-section scale
	// drift are folded into the same cm budget -- see AppendSplineMeshSegmentBounds). Smaller means
	// more, tighter-fitting boxes. If several implementers on the same Actor disagree, the smallest
	// (finest) value wins, matching ITempoBoundsHeightClampInterface's min-wins rule.
	virtual float GetSegmentedSplineMeshBoundsChordToleranceCm() const = 0;
};
