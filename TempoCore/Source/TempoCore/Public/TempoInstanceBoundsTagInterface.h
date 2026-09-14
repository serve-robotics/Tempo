// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoInstanceBoundsTagInterface.generated.h"

class UPrimitiveComponent;

UINTERFACE()
class TEMPOCORE_API UTempoInstanceBoundsTagInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by an Actor (or a component on it) that can supply its own semantic label for a
// specific reported UTempoCoreUtils::GetActorLocalInstanceBounds entry, keyed by that entry's
// SOURCE component (a UInstancedStaticMeshComponent for every instance placed by it, or a
// USplineMeshComponent for its segment(s)) -- instead of leaving a downstream consumer (e.g.
// genesis) to infer a label after the fact from the Actor's class or name. A returned tag rides
// along on every FTempoInstanceBounds entry that component contributes (see
// UTempoCoreUtils::GetActorLocalInstanceBounds / FTempoInstanceBounds::Tag) and, once serialized to
// TempoWorld.InstanceBounds.tag, is authoritative for that cuboid.
//
// Unlike ITempoInstanceBoundsFilterInterface/ITempoSegmentedSplineMeshBoundsInterface, this is a
// genuinely OPT-IN interface: the default (no implementer, or an implementer returning an empty
// string for a given component) means "no tag", and a caller receiving an empty tag falls back to
// whatever labeling it already does. Implementing this interface never changes what's reported,
// only what it's labeled.
class TEMPOCORE_API ITempoInstanceBoundsTagInterface
{
	GENERATED_BODY()

public:
	// Semantic tag for cuboids sourced from SourceComponent, or an empty string for "no tag, use the
	// caller's own fallback labeling". SourceComponent is exactly the component
	// GetActorLocalInstanceBounds is currently decomposing (a UInstancedStaticMeshComponent or a
	// USplineMeshComponent) -- never a component this Actor doesn't render.
	virtual FString GetInstanceBoundsTag(const UPrimitiveComponent* SourceComponent) const { return FString(); }
};
