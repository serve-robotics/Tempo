// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "TempoBoundsHeightClampInterface.h"
#include "TempoSegmentedSplineMeshBoundsInterface.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Math/RotationMatrix.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 4
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

namespace
{
	// Global defaults for segmented spline-mesh bounds (see ITempoSegmentedSplineMeshBoundsInterface),
	// used for any Actor that doesn't implement that interface. Segmentation is on by default -- these
	// exist as the kill switch/tuning knob for Tempo content elsewhere that generates its own
	// USplineMeshComponents without implementing the interface itself (this repo's only consumer,
	// ASplinePropLine, always does implement it, but Tempo is shared across projects).
	int32 GDefaultSegmentSplineMeshBounds = 1;
	FAutoConsoleVariableRef CVarDefaultSegmentSplineMeshBounds(
		TEXT("Tempo.SplineMeshBounds.Segmented"),
		GDefaultSegmentSplineMeshBounds,
		TEXT("Whether GetActorLocalInstanceBounds decomposes a bent USplineMeshComponent into several\n")
		TEXT("chord-fitted sub-segment boxes (1, default) or reports it as a single actor-axis-aligned\n")
		TEXT("box (0), for any Actor that doesn't implement ITempoSegmentedSplineMeshBoundsInterface.")
		);

	float GDefaultSplineMeshBoundsChordToleranceCm = 5.0f;
	FAutoConsoleVariableRef CVarDefaultSplineMeshBoundsChordToleranceCm(
		TEXT("Tempo.SplineMeshBounds.ChordToleranceCm"),
		GDefaultSplineMeshBoundsChordToleranceCm,
		TEXT("Default chord-deviation tolerance (cm) for segmented spline-mesh bounds -- see\n")
		TEXT("ITempoSegmentedSplineMeshBoundsInterface::GetSegmentedSplineMeshBoundsChordToleranceCm.\n")
		TEXT("Used for any Actor that doesn't implement that interface.")
		);

	// Every implementer of TInterface on Actor: the Actor itself, if its class implements the
	// interface, plus every component that does. The Actor-level case matters for Actors that generate
	// their own sub-geometry (e.g. ASplinePropLine) and would rather expose a setting next to the
	// properties that produce that geometry than carry a second, nearly-empty component just to hold it.
	template <typename UInterfaceType, typename IInterfaceType>
	TArray<const IInterfaceType*> FindBoundsInterfaceImplementers(const AActor* Actor)
	{
		TArray<const IInterfaceType*> Implementers;
		if (Actor->GetClass()->ImplementsInterface(UInterfaceType::StaticClass()))
		{
			if (const IInterfaceType* ActorImplementer = Cast<IInterfaceType>(Actor))
			{
				Implementers.Add(ActorImplementer);
			}
		}
		for (const UActorComponent* Component : Actor->GetComponentsByInterface(UInterfaceType::StaticClass()))
		{
			if (const IInterfaceType* ComponentImplementer = Cast<IInterfaceType>(Component))
			{
				Implementers.Add(ComponentImplementer);
			}
		}
		return Implementers;
	}

	// Smallest height declared by any ITempoBoundsHeightClampInterface implementer on Actor (Actor
	// class or component), or unset if none declare one.
	TOptional<float> FindMaxRelevantBoundsHeight(const AActor* Actor)
	{
		const TArray<const ITempoBoundsHeightClampInterface*> Implementers =
			FindBoundsInterfaceImplementers<UTempoBoundsHeightClampInterface, ITempoBoundsHeightClampInterface>(Actor);
		if (Implementers.IsEmpty())
		{
			return {};
		}

		float MaxRelevantHeight = TNumericLimits<float>::Max();
		for (const ITempoBoundsHeightClampInterface* Implementer : Implementers)
		{
			MaxRelevantHeight = FMath::Min(MaxRelevantHeight, Implementer->GetMaxRelevantBoundsHeight());
		}
		return MaxRelevantHeight;
	}

	void ClampBoxHeight(FBox& Box, const TOptional<float>& MaxRelevantHeight)
	{
		if (MaxRelevantHeight.IsSet() && Box.IsValid)
		{
			// Clamp relative to this box's OWN base (Min.Z), not an absolute Z in the Actor's local
			// frame. For a single placed prop those coincide (the Actor's origin sits at its base), but
			// GetActorLocalInstanceBounds applies this same clamp to every per-instance box of a
			// multi-instance Actor (e.g. each tree along a HISM-based ASplinePropLine); those instances
			// sit away from the Actor's origin, at whatever height their own placement/terrain gives
			// them, so clamping to an absolute Actor-local Z can push Max.Z below that instance's own
			// Min.Z -- an inverted, degenerate box that fails to render downstream. Clamping relative to
			// the box's own Min.Z keeps Max.Z >= Min.Z always, while still cutting off canopies/tall
			// extents beyond MaxRelevantHeight above each instance's own base.
			Box.Max.Z = FMath::Min(Box.Max.Z, Box.Min.Z + MaxRelevantHeight.GetValue());
		}
	}

	// Whether to segment SplineMeshComponent's bounds, and the chord tolerance to use if so. See
	// ITempoSegmentedSplineMeshBoundsInterface. Several disagreeing implementers on the same Actor
	// resolve conservatively: the bool is a logical AND (any "off" wins) and the tolerance is the
	// MINIMUM (finest wins) -- mirroring FindMaxRelevantBoundsHeight's min-wins rule above.
	struct FSplineMeshBoundsSettings
	{
		bool bSegmented = true;
		float ChordToleranceCm = 5.0f;
	};

	FSplineMeshBoundsSettings ResolveSplineMeshBoundsSettings(const AActor* Actor)
	{
		const TArray<const ITempoSegmentedSplineMeshBoundsInterface*> Implementers =
			FindBoundsInterfaceImplementers<UTempoSegmentedSplineMeshBoundsInterface, ITempoSegmentedSplineMeshBoundsInterface>(Actor);
		if (Implementers.IsEmpty())
		{
			return { GDefaultSegmentSplineMeshBounds != 0, FMath::Max(GDefaultSplineMeshBoundsChordToleranceCm, 0.1f) };
		}

		FSplineMeshBoundsSettings Settings;
		Settings.bSegmented = true;
		Settings.ChordToleranceCm = TNumericLimits<float>::Max();
		for (const ITempoSegmentedSplineMeshBoundsInterface* Implementer : Implementers)
		{
			Settings.bSegmented = Settings.bSegmented && Implementer->ShouldReportSegmentedSplineMeshBounds();
			Settings.ChordToleranceCm = FMath::Min(Settings.ChordToleranceCm, Implementer->GetSegmentedSplineMeshBoundsChordToleranceCm());
		}
		Settings.ChordToleranceCm = FMath::Max(Settings.ChordToleranceCm, 0.1f);
		return Settings;
	}

	// The static mesh's own collision cross-section, in the mesh's UNDEFORMED local space. Reads the
	// STATIC MESH's BodySetup (Mesh->GetBodySetup()), never the SplineMeshComponent's own --
	// USplineMeshComponent::RecreateCollision mutates the component's BodySetup into ALREADY-DEFORMED
	// geometry (hull verts pushed through CalcSliceTransform), so reading it here would deform twice.
	// Falls back to the mesh's RENDER bounds when the collision BodySetup has no elements at all --
	// true whenever the mesh's collision complexity is CTF_UseComplexAsSimple, which RecreateCollision
	// empties outright (AggGeom.EmptyElements()) -- so a complex-as-simple mesh still gets a fallback
	// box here instead of silently contributing none, unlike the generic per-component branch this
	// function's caller replaces for SplineMeshComponents.
	FBox UndeformedCrossSectionBox(const UStaticMesh* Mesh)
	{
		if (const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr)
		{
			if (BodySetup->AggGeom.GetElementCount() > 0)
			{
				FBoxSphereBounds Bounds;
				BodySetup->AggGeom.CalcBoxSphereBounds(Bounds, FTransform::Identity);
				if (Bounds.GetBox().IsValid)
				{
					return Bounds.GetBox();
				}
			}
		}
		return Mesh ? Mesh->GetBounds().GetBox() : FBox(ForceInit);
	}

	// Which of CalcSliceTransform's three orthonormal basis vectors is the cross-section's "up" axis
	// (frenet-frame YVec, the axis roll rotates around) -- depends on ForwardAxis, because
	// FSplineMeshSceneProxyDesc::CalcSliceTransformAtSplineOffset permutes which of its own X/Y/Z
	// columns holds SplineDir/XVec/YVec per ForwardAxis (SplineMeshSceneProxyDesc.cpp): X-forward ->
	// (SplineDir, XVec, YVec), so YVec is the transform's Z column; Y-forward -> (YVec, SplineDir,
	// XVec), so YVec is the X column; Z-forward -> (XVec, YVec, SplineDir), so YVec is the Y column.
	FVector SliceUpAxis(const FTransform& SliceTransform, ESplineMeshAxis::Type ForwardAxis)
	{
		switch (ForwardAxis)
		{
		case ESplineMeshAxis::X:
			return SliceTransform.TransformVectorNoScale(FVector::UpVector);
		case ESplineMeshAxis::Y:
			return SliceTransform.TransformVectorNoScale(FVector::ForwardVector);
		default:
			return SliceTransform.TransformVectorNoScale(FVector::RightVector);
		}
	}

	// The 4 corners of CrossSectionBox's cross-section rectangle (the two axes orthogonal to
	// ForwardAxis vary independently between Min/Max; ForwardAxis itself contributes nothing, mirroring
	// the engine's own vertex/collision recipe -- PropagateSplineDeformationToMesh,
	// GetPhysicsTriMeshData, RecreateCollision all compute
	// CalcSliceTransform(GetAxisValueRef(V,Axis)).TransformPosition(V * GetAxisMask(Axis)), and
	// GetAxisMask zeroes exactly the forward component; its own header comment claims the opposite,
	// verified wrong against the implementation), placed at DistanceAlong via
	// SplineMeshComponent's own slice transform. Result is in the SplineMeshComponent's own component space.
	void AppendSliceCorners(const USplineMeshComponent* SplineMeshComponent, const FBox& CrossSectionBox,
		float DistanceAlong, TArray<FVector>& OutPoints)
	{
		const ESplineMeshAxis::Type Axis = SplineMeshComponent->ForwardAxis;
		const FTransform SliceTransform = SplineMeshComponent->CalcSliceTransform(DistanceAlong);
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			FVector Corner = FVector::ZeroVector;
			switch (Axis)
			{
			case ESplineMeshAxis::X:
				Corner.Y = (CornerIndex & 1) ? CrossSectionBox.Max.Y : CrossSectionBox.Min.Y;
				Corner.Z = (CornerIndex & 2) ? CrossSectionBox.Max.Z : CrossSectionBox.Min.Z;
				break;
			case ESplineMeshAxis::Y:
				Corner.X = (CornerIndex & 1) ? CrossSectionBox.Max.X : CrossSectionBox.Min.X;
				Corner.Z = (CornerIndex & 2) ? CrossSectionBox.Max.Z : CrossSectionBox.Min.Z;
				break;
			default: // Z
				Corner.X = (CornerIndex & 1) ? CrossSectionBox.Max.X : CrossSectionBox.Min.X;
				Corner.Y = (CornerIndex & 2) ? CrossSectionBox.Max.Y : CrossSectionBox.Min.Y;
				break;
			}
			OutPoints.Add(SliceTransform.TransformPosition(Corner));
		}
	}

	// Perpendicular distance from Point to the line SEGMENT [SegStart, SegEnd] (clamped, not the
	// infinite line) -- used to measure how far a sampled interior slice's centreline bulges from the
	// straight chord a sub-segment's box would otherwise assume.
	float PointToSegmentDistance(const FVector& Point, const FVector& SegStart, const FVector& SegEnd)
	{
		const FVector SegDelta = SegEnd - SegStart;
		const float SegLenSq = static_cast<float>(SegDelta.SizeSquared());
		if (SegLenSq <= UE_SMALL_NUMBER)
		{
			return static_cast<float>((Point - SegStart).Size());
		}
		const float Alpha = FMath::Clamp(static_cast<float>(FVector::DotProduct(Point - SegStart, SegDelta) / SegLenSq), 0.0f, 1.0f);
		const FVector ClosestPointOnSeg = SegStart + Alpha * SegDelta;
		return static_cast<float>((Point - ClosestPointOnSeg).Size());
	}

	// True if the deformation between [DomainMin, DomainMax] is close enough to a straight, untwisted,
	// uniformly-scaled stretch that a single box already fits it tightly -- the fast path for the
	// overwhelmingly common case (ASplinePropLine fragments a bent rail into one SplineMeshComponent per
	// SpacingRange step already, so most individual components are only mildly bent) and a guarantee
	// that straight content reports exactly as many boxes as it did before segmentation existed.
	bool IsEffectivelyStraight(const USplineMeshComponent* SplineMeshComponent, float DomainMin, float DomainMax)
	{
		const FSplineMeshParams& Params = SplineMeshComponent->SplineParams;
		if (!FMath::IsNearlyEqual(Params.StartRoll, Params.EndRoll, 0.001f)) { return false; }
		if (!Params.StartScale.Equals(Params.EndScale, 0.001)) { return false; }
		if (!Params.StartOffset.Equals(Params.EndOffset, 0.01)) { return false; }

		const FTransform StartTransform = SplineMeshComponent->CalcSliceTransform(DomainMin);
		const FTransform EndTransform = SplineMeshComponent->CalcSliceTransform(DomainMax);
		const FVector Chord = EndTransform.GetTranslation() - StartTransform.GetTranslation();
		if (Chord.IsNearlyZero()) { return true; } // degenerate-length mesh; nothing to subdivide anyway

		const ESplineMeshAxis::Type Axis = SplineMeshComponent->ForwardAxis;
		const FVector StartForward = StartTransform.TransformVectorNoScale(
			Axis == ESplineMeshAxis::X ? FVector::ForwardVector : Axis == ESplineMeshAxis::Y ? FVector::RightVector : FVector::UpVector);
		const FVector EndForward = EndTransform.TransformVectorNoScale(
			Axis == ESplineMeshAxis::X ? FVector::ForwardVector : Axis == ESplineMeshAxis::Y ? FVector::RightVector : FVector::UpVector);
		const FVector ChordDir = Chord.GetSafeNormal();
		// ~0.5 degrees: cos(0.5deg) ~ 0.99996.
		constexpr float ParallelDotThreshold = 0.99996f;
		return FVector::DotProduct(StartForward, ChordDir) >= ParallelDotThreshold
			&& FVector::DotProduct(EndForward, ChordDir) >= ParallelDotThreshold;
	}
}

UWorldSubsystem* UTempoCoreUtils::GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UWorldSubsystem* SubsystemImplementingInterface = nullptr;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
		TArray<UWorldSubsystem*> Subsystems = World->GetSubsystemArray<UWorldSubsystem>();
		for (UWorldSubsystem* Subsystem : Subsystems)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				SubsystemImplementingInterface = Subsystem;
			}
		}
#else
		World->ForEachSubsystem<UWorldSubsystem>([&Interface, &SubsystemImplementingInterface](UWorldSubsystem* Subsystem)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				SubsystemImplementingInterface = Subsystem;
			}
		});
#endif
		return SubsystemImplementingInterface;
	}

	return nullptr;
}

bool UTempoCoreUtils::IsGameWorld(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE;
}

FBox UTempoCoreUtils::GetActorLocalBounds(const AActor* Actor, bool bIncludeHiddenComponents)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// ForceInit, NOT the default FBox() constructor: that one sets only IsValid=0 and leaves Min/Max
	// deliberately uninitialized (see TBox's constructor comment in Box.h). Every accumulation below
	// goes through operator+=, which correctly ignores an invalid box's contents, so garbage Min/Max
	// is harmless as long as SOMETHING is accumulated. But an Actor whose components contribute no
	// collision geometry at all -- a HISM-based ASplinePropLine whose instances live on the component
	// rather than on the Actor, or a component with collision disabled and hence no BodySetup --
	// leaves LocalBounds untouched, and then the raw stack garbage in Min/Max escapes this function.
	// Callers that check only IsValid (or don't check at all) then read it as +/-inf or NaN.
	FBox LocalBounds(ForceInit);

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent->IsVisible() && !bIncludeHiddenComponents)
		{
			continue;
		}

		auto AddAggGeomToBounds = [Actor, &LocalBounds](const FKAggregateGeom& AggGeom, const FTransform& WorldTransform)
		{
			FBoxSphereBounds Bounds;
			const FTransform RelativeTransform = WorldTransform.GetRelativeTransform(Actor->GetTransform());
			AggGeom.CalcBoxSphereBounds(Bounds, RelativeTransform);
			LocalBounds += Bounds.GetBox();
		};

		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			if (const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* SkeletalBodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					AddAggGeomToBounds(SkeletalBodySetup->AggGeom, SkeletalMeshComponent->GetBoneTransform(SkeletalBodySetup->BoneName));
				}
			}
		}
		else if(const UBodySetup* BodySetup = PrimitiveComponent->BodyInstance.GetBodySetup())
		{
			AddAggGeomToBounds(BodySetup->AggGeom, PrimitiveComponent->GetComponentTransform());
		}
	}

	// If any component implements ITempoBoundsHeightClampInterface, clamp the reported bounds to only
	// the portion of the Actor near its base that could plausibly interact with a ground robot (e.g. a
	// tree's canopy is well above anything a robot can touch).
	ClampBoxHeight(LocalBounds, FindMaxRelevantBoundsHeight(Actor));

	return LocalBounds;
}

void UTempoCoreUtils::AppendSplineMeshSegmentBounds(const USplineMeshComponent* SplineMeshComponent, const AActor* Actor,
	float ChordToleranceCm, const TOptional<float>& MaxRelevantHeight, TArray<FTempoInstanceBounds>& OutInstanceBounds)
{
	const UStaticMesh* Mesh = SplineMeshComponent->GetStaticMesh();
	if (!Mesh)
	{
		return;
	}

	const ESplineMeshAxis::Type Axis = SplineMeshComponent->ForwardAxis;
	const FBox CrossSectionBox = UndeformedCrossSectionBox(Mesh);
	if (!CrossSectionBox.IsValid)
	{
		return;
	}

	// Domain = the undeformed cross-section's own extent along ForwardAxis -- the same source
	// FSplineMeshSceneProxyDesc::ComputeRatioAlongSpline uses to map a forward-axis coordinate to an
	// alpha along the spline, when the component has no custom SplineBoundaryMin/Max override (matched
	// here via CalcSliceTransform itself, which already applies that override internally -- this
	// function only needs the DOMAIN to sample over, not to replicate the alpha mapping).
	const double DomainMinD = USplineMeshComponent::GetAxisValueRef(CrossSectionBox.Min, Axis);
	const double DomainMaxD = USplineMeshComponent::GetAxisValueRef(CrossSectionBox.Max, Axis);
	const float DomainMin = UE::SplineMesh::RealToFloatChecked(DomainMinD);
	const float DomainMax = UE::SplineMesh::RealToFloatChecked(DomainMaxD);
	if (DomainMax - DomainMin <= UE_SMALL_NUMBER)
	{
		return;
	}

	// The cross-section's circumradius (scaled by the larger of the two ends' cross-section scale),
	// used below to convert roll twist into an equivalent cm error -- a corner of a cross-section of
	// radius R, twisted by dRoll around the centreline, sweeps roughly R*(1-cos(dRoll/2)) away from
	// where a non-twisted box would place it.
	FVector2D CrossSectionExtent2D;
	switch (Axis)
	{
	case ESplineMeshAxis::X:
		CrossSectionExtent2D = FVector2D(CrossSectionBox.Max.Y - CrossSectionBox.Min.Y, CrossSectionBox.Max.Z - CrossSectionBox.Min.Z);
		break;
	case ESplineMeshAxis::Y:
		CrossSectionExtent2D = FVector2D(CrossSectionBox.Max.X - CrossSectionBox.Min.X, CrossSectionBox.Max.Z - CrossSectionBox.Min.Z);
		break;
	default: // Z
		CrossSectionExtent2D = FVector2D(CrossSectionBox.Max.X - CrossSectionBox.Min.X, CrossSectionBox.Max.Y - CrossSectionBox.Min.Y);
		break;
	}
	const float CrossSectionCircumradius = static_cast<float>(CrossSectionExtent2D.Size()) * 0.5f;
	const FVector2D MaxCrossSectionScale = FVector2D(
		FMath::Max(FMath::Abs(SplineMeshComponent->SplineParams.StartScale.X), FMath::Abs(SplineMeshComponent->SplineParams.EndScale.X)),
		FMath::Max(FMath::Abs(SplineMeshComponent->SplineParams.StartScale.Y), FMath::Abs(SplineMeshComponent->SplineParams.EndScale.Y)));
	const float ScaledCircumradius = CrossSectionCircumradius * FMath::Max(MaxCrossSectionScale.X, MaxCrossSectionScale.Y);

	// Fast path: an untwisted, unscaled, straight stretch is exactly one box at zero extra cost, and is
	// the overwhelmingly common case (ASplinePropLine already fragments a bent rail into one
	// SplineMeshComponent per SpacingRange step). Guarantees straight content reports exactly as many
	// boxes as it did before segmentation existed.
	TArray<float> Boundaries;
	if (IsEffectivelyStraight(SplineMeshComponent, DomainMin, DomainMax))
	{
		Boundaries = { DomainMin, DomainMax };
	}
	else
	{
		// Roll is stored in radians (FSplineMeshParams::StartRoll/EndRoll); interpolated linearly here
		// to match FSplineMeshSceneProxyDesc::CalcSliceTransformAtSplineOffset's own (non-smoothstep,
		// unless bSmoothInterpRollScale) lerp closely enough for an error ESTIMATE -- exactness isn't
		// needed since ChordToleranceCm is itself a tolerance, not an exact bound.
		auto RollAt = [SplineMeshComponent, DomainMin, DomainMax](float DistanceAlong) -> float
		{
			const float Alpha = (DomainMax - DomainMin) > UE_SMALL_NUMBER
				? (DistanceAlong - DomainMin) / (DomainMax - DomainMin) : 0.0f;
			return FMath::Lerp(SplineMeshComponent->SplineParams.StartRoll, SplineMeshComponent->SplineParams.EndRoll, Alpha);
		};

		// Sagitta (centreline chord deviation) + twist error, folded into one cm budget -- see the
		// ITempoSegmentedSplineMeshBoundsInterface doc comment. 5 interior samples, not midpoint-only:
		// an S-shaped cubic can put its midpoint exactly on the chord while both halves bulge, which a
		// midpoint-only test would wrongly declare converged.
		auto SegmentError = [SplineMeshComponent, &RollAt, ScaledCircumradius](float D0, float D1) -> float
		{
			const FVector P0 = SplineMeshComponent->CalcSliceTransform(D0).GetTranslation();
			const FVector P1 = SplineMeshComponent->CalcSliceTransform(D1).GetTranslation();
			float Sagitta = 0.0f;
			for (const float Alpha : { 0.2f, 0.4f, 0.5f, 0.6f, 0.8f })
			{
				const FVector Mid = SplineMeshComponent->CalcSliceTransform(FMath::Lerp(D0, D1, Alpha)).GetTranslation();
				Sagitta = FMath::Max(Sagitta, PointToSegmentDistance(Mid, P0, P1));
			}
			const float DeltaRoll = FMath::Abs(RollAt(D1) - RollAt(D0));
			const float TwistError = ScaledCircumradius * (1.0f - FMath::Cos(DeltaRoll * 0.5f));
			return FMath::Max(Sagitta, TwistError);
		};

		// Iterative bisection over a work list (not naive recursion) so MaxSubSegments is an exact,
		// enforceable cap -- GetActorLocalInstanceBounds runs per world-state query and a fence line can
		// hold hundreds of SplineMeshComponents, so an unbounded subdivision here would be a real cost.
		constexpr int32 MaxSubSegments = 32;
		const float MinSubSegmentLength = FMath::Max(1.0f, (DomainMax - DomainMin) / MaxSubSegments);

		Boundaries = { DomainMin, DomainMax };
		TArray<TPair<float, float>> Queue = { { DomainMin, DomainMax } };
		while (Queue.Num() > 0 && Boundaries.Num() < MaxSubSegments + 1)
		{
			const TPair<float, float> Range = Queue.Pop();
			const float D0 = Range.Key;
			const float D1 = Range.Value;
			if (D1 - D0 <= 2.0f * MinSubSegmentLength || SegmentError(D0, D1) <= ChordToleranceCm)
			{
				continue;
			}
			const float Mid = 0.5f * (D0 + D1);
			Boundaries.Add(Mid);
			Queue.Add({ D0, Mid });
			Queue.Add({ Mid, D1 });
		}
		Boundaries.Sort();
	}

	const FTransform ComponentToActor = SplineMeshComponent->GetComponentTransform().GetRelativeTransform(Actor->GetTransform());

	for (int32 SegmentIndex = 0; SegmentIndex + 1 < Boundaries.Num(); ++SegmentIndex)
	{
		const float D0 = Boundaries[SegmentIndex];
		const float D1 = Boundaries[SegmentIndex + 1];
		const FTransform SliceTransform0 = SplineMeshComponent->CalcSliceTransform(D0);
		const FTransform SliceTransform1 = SplineMeshComponent->CalcSliceTransform(D1);

		const FVector P0 = ComponentToActor.TransformPosition(SliceTransform0.GetTranslation());
		const FVector P1 = ComponentToActor.TransformPosition(SliceTransform1.GetTranslation());
		FVector Forward = P1 - P0;
		if (!Forward.Normalize())
		{
			continue; // degenerate sub-segment (coincident slices); skip rather than report a garbage box
		}

		// "Up" for the box frame follows the mesh's own roll rather than world Z: average the two
		// slices' cross-section "up" basis vector (see SliceUpAxis for the ForwardAxis-dependent
		// permutation this reads back), then carry it into the Actor's frame the same way the
		// translations above were.
		const FVector UpRef = ComponentToActor.TransformVectorNoScale(
			(SliceUpAxis(SliceTransform0, Axis) + SliceUpAxis(SliceTransform1, Axis)).GetSafeNormal());
		// Forward stays exact, up gets forced perpendicular to it -- the same MakeFromXZ convention
		// ASplinePropLine::ComputeInstanceTransform already documents and relies on.
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, UpRef).ToQuat();
		const FVector Center = 0.5 * (P0 + P1);

		// Hull = both end slices' cross-section corners PLUS the midpoint slice's -- included so a
		// sub-segment left slightly bent by the tolerance still has its bulge INSIDE the reported box
		// (conservative, never smaller than the true geometry, which is what an obstacle consumer wants)
		// rather than merely close to it.
		TArray<FVector> ComponentSpaceHull;
		AppendSliceCorners(SplineMeshComponent, CrossSectionBox, D0, ComponentSpaceHull);
		AppendSliceCorners(SplineMeshComponent, CrossSectionBox, D1, ComponentSpaceHull);
		AppendSliceCorners(SplineMeshComponent, CrossSectionBox, 0.5f * (D0 + D1), ComponentSpaceHull);

		FBox LocalBox(ForceInit);
		for (const FVector& ComponentSpacePoint : ComponentSpaceHull)
		{
			const FVector ActorSpacePoint = ComponentToActor.TransformPosition(ComponentSpacePoint);
			LocalBox += Rotation.UnrotateVector(ActorSpacePoint - Center);
		}
		if (!LocalBox.IsValid)
		{
			continue;
		}
		ClampBoxHeight(LocalBox, MaxRelevantHeight);

		FTempoInstanceBounds Entry;
		Entry.LocalBounds = LocalBox;
		Entry.Transform = FTransform(Rotation, Center);
		OutInstanceBounds.Add(Entry);
	}
}

TArray<FTempoInstanceBounds> UTempoCoreUtils::GetActorLocalInstanceBounds(const AActor* Actor, bool bIncludeHiddenComponents)
{
	const TOptional<float> MaxRelevantHeight = FindMaxRelevantBoundsHeight(Actor);

	// An Actor with a movement CapsuleComponent (every ACharacter, including every pedestrian) is
	// reported as exactly one instance box derived from the capsule's own shape, oriented to the
	// capsule's own rotation -- instead of decomposing per skeletal-mesh sub-component below. A
	// multi-part character rig can have several independently physics-asset-bearing skeletal meshes
	// (body, head, clothing layers), which would otherwise report several overlapping boxes for what is
	// visually one pedestrian. The capsule is used regardless of its own visibility (a Character's
	// capsule is normally bHiddenInGame, which the bVisible-based skip below doesn't consider hidden
	// anyway) since it's the actor's authoritative footprint whether or not it renders.
	if (const UCapsuleComponent* CapsuleComponent = Actor->GetComponentByClass<UCapsuleComponent>())
	{
		const FTransform Placement = CapsuleComponent->GetComponentTransform().GetRelativeTransform(Actor->GetTransform());
		const float Radius = CapsuleComponent->GetScaledCapsuleRadius();
		const float HalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();

		FBox LocalBox(FVector(-Radius, -Radius, -HalfHeight), FVector(Radius, Radius, HalfHeight));
		ClampBoxHeight(LocalBox, MaxRelevantHeight);

		FTempoInstanceBounds Entry;
		Entry.LocalBounds = LocalBox;
		Entry.Transform = FTransform(Placement.GetRotation(), Placement.GetTranslation());
		return { Entry };
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	TArray<FTempoInstanceBounds> InstanceBounds;

	// Computes Placement's box in ITS OWN local frame (scale baked in, but NOT rotated by Placement's
	// own rotation) plus the location+rotation needed to place and orient it relative to Actor --
	// mirroring ActorState's local_bounds + transform, just per-instance, so a rotated instance is
	// reported as an oriented box rather than an Actor-axis-aligned one. Clamping happens on the
	// UN-rotated local box, where the box's own Min.Z is still that instance's own base, so the clamp
	// is unaffected by whatever rotation the instance carries.
	auto AddInstanceBounds = [&InstanceBounds, &MaxRelevantHeight](const FKAggregateGeom& AggGeom, const FTransform& Placement)
	{
		FBoxSphereBounds Bounds;
		AggGeom.CalcBoxSphereBounds(Bounds, FTransform(FQuat::Identity, FVector::ZeroVector, Placement.GetScale3D()));
		FBox LocalBox = Bounds.GetBox();
		if (!LocalBox.IsValid)
		{
			return;
		}
		ClampBoxHeight(LocalBox, MaxRelevantHeight);

		FTempoInstanceBounds Entry;
		Entry.LocalBounds = LocalBox;
		Entry.Transform = FTransform(Placement.GetRotation(), Placement.GetTranslation());
		InstanceBounds.Add(Entry);
	};

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent->IsVisible() && !bIncludeHiddenComponents)
		{
			continue;
		}

		// Pure collision proxies (e.g. a Character's movement CapsuleComponent) don't render anything
		// of their own -- they duplicate whatever mesh they're attached to rather than representing a
		// distinct placed sub-object, so they shouldn't get their own instance box here (they'd
		// otherwise report a redundant near-duplicate box alongside that mesh's own contribution).
		if (PrimitiveComponent->IsA<UShapeComponent>())
		{
			continue;
		}

		if (const UInstancedStaticMeshComponent* InstancedMeshComponent = Cast<UInstancedStaticMeshComponent>(PrimitiveComponent))
		{
			const UStaticMesh* Mesh = InstancedMeshComponent->GetStaticMesh();
			const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
			if (!BodySetup)
			{
				continue;
			}

			const FTransform ComponentToActor = InstancedMeshComponent->GetComponentTransform().GetRelativeTransform(Actor->GetTransform());
			const int32 NumInstances = InstancedMeshComponent->GetInstanceCount();
			for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!InstancedMeshComponent->GetInstanceTransform(InstanceIndex, InstanceTransform))
				{
					continue;
				}
				AddInstanceBounds(BodySetup->AggGeom, InstanceTransform * ComponentToActor);
			}
			continue;
		}

		// Cast checked BEFORE the generic BodySetup branch below -- USplineMeshComponent derives from
		// UStaticMeshComponent (not UInstancedStaticMeshComponent, so this can never double-fire with
		// the ISM branch above), and its own BodySetup (read by that generic branch) is DEFORMED
		// geometry that collapses to one actor-axis-aligned box, which is exactly what this branch
		// exists to replace with several chord-fitted sub-segment boxes instead. Falls through to the
		// generic branch (by not `continue`ing) only when segmentation resolves nothing usable, so this
		// path is never worse than what existed before it.
		if (const USplineMeshComponent* SplineMeshComponent = Cast<USplineMeshComponent>(PrimitiveComponent))
		{
			const FSplineMeshBoundsSettings BoundsSettings = ResolveSplineMeshBoundsSettings(Actor);
			if (BoundsSettings.bSegmented)
			{
				const int32 CountBefore = InstanceBounds.Num();
				UTempoCoreUtils::AppendSplineMeshSegmentBounds(SplineMeshComponent, Actor,
					BoundsSettings.ChordToleranceCm, MaxRelevantHeight, InstanceBounds);
				if (InstanceBounds.Num() > CountBefore)
				{
					continue;
				}
				// Else nothing usable was resolved (e.g. no static mesh) -- fall through below.
			}
		}

		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			// Union every bone's body bounds into a single box for this component, rather than adding
			// one box per bone -- a ragdoll-capable character's PhysicsAsset can have 20+ SkeletalBodySetups
			// (one per bone), and this function's contract is one box PER COMPONENT (mirroring
			// GetActorLocalBounds's per-component contributions), not per body within a component. Bones
			// on the same skeleton can each carry a different rotation, so there's no single instance
			// rotation to factor out here (unlike the ISM/other-component cases) -- this box stays
			// axis-aligned to the Actor's own frame, reported with an identity Transform.
			if (const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset())
			{
				FBox SkeletalMeshBox(ForceInit);
				for (const USkeletalBodySetup* SkeletalBodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					const FTransform RelativeTransform = SkeletalMeshComponent->GetBoneTransform(SkeletalBodySetup->BoneName).GetRelativeTransform(Actor->GetTransform());
					FBoxSphereBounds Bounds;
					SkeletalBodySetup->AggGeom.CalcBoxSphereBounds(Bounds, RelativeTransform);
					SkeletalMeshBox += Bounds.GetBox();
				}
				if (SkeletalMeshBox.IsValid)
				{
					ClampBoxHeight(SkeletalMeshBox, MaxRelevantHeight);
					FTempoInstanceBounds Entry;
					Entry.LocalBounds = SkeletalMeshBox;
					Entry.Transform = FTransform::Identity;
					InstanceBounds.Add(Entry);
				}
			}
		}
		else if (const UBodySetup* BodySetup = PrimitiveComponent->BodyInstance.GetBodySetup())
		{
			AddInstanceBounds(BodySetup->AggGeom, PrimitiveComponent->GetComponentTransform().GetRelativeTransform(Actor->GetTransform()));
		}
	}

	return InstanceBounds;
}

FString UTempoCoreUtils::GetActorIdentifier(const AActor* Actor)
{
#if WITH_EDITOR
	// Materialize the actor label now so it matches every later GetActorNameOrLabel() call,
	// including GetActorWithName lookups. See header for details.
	(void)Actor->GetActorLabel();
#endif
	return Actor->GetActorNameOrLabel();
}
