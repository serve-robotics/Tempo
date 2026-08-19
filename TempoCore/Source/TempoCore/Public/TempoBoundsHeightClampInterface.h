// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoBoundsHeightClampInterface.generated.h"

UINTERFACE()
class TEMPOCORE_API UTempoBoundsHeightClampInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by components that want their owning Actor's UTempoCoreUtils::GetActorLocalBounds
// (and therefore the bounds Tempo reports for that Actor, e.g. via TempoWorld's ActorState) clamped
// to only the portion of the Actor that could plausibly interact with a ground robot, rather than
// the Actor's full physical extent. A tree's canopy, for example, is well above anything a ground
// robot can touch, so its reported bounds shouldn't extend to the treetop.
class TEMPOCORE_API ITempoBoundsHeightClampInterface
{
	GENERATED_BODY()

public:
	// Maximum height, in cm above the Actor's local origin (Unreal native units), that
	// GetActorLocalBounds should report for this Actor. Assumes the Actor's local Z origin sits at
	// (or near) its base on the ground, as is typical for placed static props; if multiple components
	// on the same Actor implement this interface, the smallest value wins.
	virtual float GetMaxRelevantBoundsHeight() const = 0;
};
