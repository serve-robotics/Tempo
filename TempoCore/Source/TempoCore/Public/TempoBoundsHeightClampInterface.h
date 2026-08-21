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
	// Maximum height, in cm above its own base (Unreal native units), that GetActorLocalBounds (or,
	// per-instance, GetActorLocalInstanceBounds) should report. Applied relative to each reported box's
	// own Min.Z -- not a single absolute Z in the Actor's local frame -- so it clamps correctly both for
	// an ordinary single-box Actor (whose local origin sits at, or near, its base) and for a
	// multi-instance Actor (e.g. each tree along a HISM-based prop line), where every instance has its
	// own base at a different height. If multiple components on the same Actor implement this
	// interface, the smallest value wins.
	virtual float GetMaxRelevantBoundsHeight() const = 0;
};
