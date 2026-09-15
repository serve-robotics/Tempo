// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoBoundsHeightClampInterface.h"
#include "TempoHeightClampedBoundsComponent.generated.h"

// Drop-in component implementing ITempoBoundsHeightClampInterface with an editable height, for the
// common case of an Actor that just needs its Tempo-reported bounds clamped to a fixed height (e.g. a
// spawned tree, street lamp, or other tall static prop that only obstructs a ground robot near its
// base). Actors with more specific needs can implement the interface directly instead.
UCLASS(ClassGroup = (Tempo), meta = (BlueprintSpawnableComponent))
class TEMPOCORE_API UTempoHeightClampedBoundsComponent : public UActorComponent, public ITempoBoundsHeightClampInterface
{
	GENERATED_BODY()

public:
	UTempoHeightClampedBoundsComponent();

	// Maximum height, in cm above each reported box's own base, to report in its Tempo bounds (see
	// ITempoBoundsHeightClampInterface::GetMaxRelevantBoundsHeight). Defaults to 250cm (2.5m), the top
	// of the range a ground robot could plausibly interact with.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	float MaxRelevantHeight = 250.0f;

	virtual float GetMaxRelevantBoundsHeight() const override { return MaxRelevantHeight; }
};
