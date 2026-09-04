// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoSegmentedSplineMeshBoundsInterface.h"
#include "TempoSegmentedSplineMeshBoundsComponent.generated.h"

// Drop-in component implementing ITempoSegmentedSplineMeshBoundsInterface with editable settings, for
// an Actor that just needs to retune or disable segmented spline-mesh bounds without implementing the
// interface on its own class (see UTempoHeightClampedBoundsComponent for the analogous drop-in for
// height clamping). Actors that generate their own spline-mesh geometry, and so already have a natural
// home for these settings among their own properties, may prefer to implement the interface directly
// instead -- ASplinePropLine does this.
UCLASS(ClassGroup = (Tempo), meta = (BlueprintSpawnableComponent))
class TEMPOCORE_API UTempoSegmentedSplineMeshBoundsComponent : public UActorComponent, public ITempoSegmentedSplineMeshBoundsInterface
{
	GENERATED_BODY()

public:
	UTempoSegmentedSplineMeshBoundsComponent();

	// See ITempoSegmentedSplineMeshBoundsInterface::ShouldReportSegmentedSplineMeshBounds. On by
	// default -- this component exists to retune the tolerance or to turn segmentation OFF, not to
	// turn it on (it's already on for every Actor without this component at all).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	bool bReportSegmentedSplineMeshBounds = true;

	// See ITempoSegmentedSplineMeshBoundsInterface::GetSegmentedSplineMeshBoundsChordToleranceCm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta = (ClampMin = "0.1", Units = "Centimeters",
		EditCondition = "bReportSegmentedSplineMeshBounds", EditConditionHides))
	float ChordToleranceCm = 5.0f;

	virtual bool ShouldReportSegmentedSplineMeshBounds() const override { return bReportSegmentedSplineMeshBounds; }
	virtual float GetSegmentedSplineMeshBoundsChordToleranceCm() const override { return ChordToleranceCm; }
};
