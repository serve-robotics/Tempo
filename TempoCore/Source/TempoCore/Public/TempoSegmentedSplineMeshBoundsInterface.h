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

	// Target sub-segment DENSITY (cuboids per meter of the mesh's own undeformed domain length,
	// interpreted the same way as the ChordToleranceCm-driven subdivision's own length terms -- see
	// AppendSplineMeshSegmentBounds) that the bend-dependent (chord/roll/twist error) subdivision above
	// is driven toward, in WHICHEVER direction is needed -- this is a target to LAND CLOSE TO, not a
	// floor: if the bend-driven result already has fewer sub-segments than the target implies, more are
	// added (splitting the currently-largest one first, repeatedly); if it already has MORE than the
	// target implies (e.g. a tightly bent run whose own chord tolerance alone demanded a fine
	// subdivision), it is COARSENED back down toward the target too (repeatedly merging whichever
	// adjacent pair would produce the smallest combined length -- i.e. undoing the least-impactful
	// split first). On by default (0.5/meter); 0 disables this entirely, leaving the bend-driven result
	// exactly as ChordToleranceCm alone produced it, in either direction. Useful for e.g. an obstacle
	// consumer that wants a roughly PREDICTABLE spatial resolution along a whole spline -- both a
	// guaranteed minimum along a dead-straight run chord tolerance alone wouldn't subdivide much, and a
	// bound on how fine a tightly bent run's own chord tolerance would otherwise make it. A straight
	// run's own reliability floor (see MinSubSegmentsForStraightRuns in AppendSplineMeshSegmentBounds)
	// is never coarsened below, regardless of this setting. If several implementers on the same Actor
	// disagree, the LARGEST (most demanding, i.e. denser) target wins.
	virtual float GetSegmentedSplineMeshBoundsTargetCuboidsPerMeter() const { return 0.5f; }

	// Hard cap (cm) on any single reported sub-segment's length -- independent of, and enforced on top
	// of, both the chord-tolerance pass and the TargetCuboidsPerMeter top-up above: whichever of those
	// two would otherwise leave a sub-segment longer than this is split further (always bisecting the
	// currently-largest sub-segment, same as the density top-up) until every sub-segment is at or under
	// this length. On by default (2000cm / 20m) -- a very long, dead-straight run would otherwise still
	// produce a handful of very long boxes even with a sensible chord tolerance and density target, if
	// neither of those ever demanded a split that far apart. <= 0 disables this cap. Subject to the
	// same MaxSubSegments hard ceiling as the rest of this subdivision (see AppendSplineMeshSegmentBounds)
	// -- an extremely long component may still exceed this length per sub-segment once that ceiling is
	// hit; this is an accepted trade-off, not a bug, matching the existing safety-valve philosophy. If
	// several implementers on the same Actor disagree, the SMALLEST (most restrictive) value wins,
	// matching ChordToleranceCm's min-wins rule.
	virtual float GetSegmentedSplineMeshBoundsMaxCuboidLengthCm() const { return 2000.0f; }
};
