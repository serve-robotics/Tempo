// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "TempoBoundsHeightClampInterface.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 4
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

namespace
{
	// Smallest height declared by any ITempoBoundsHeightClampInterface component on Actor, or
	// unset if none declare one.
	TOptional<float> FindMaxRelevantBoundsHeight(const AActor* Actor)
	{
		const TArray<UActorComponent*> HeightClampComponents = Actor->GetComponentsByInterface(UTempoBoundsHeightClampInterface::StaticClass());
		if (HeightClampComponents.IsEmpty())
		{
			return {};
		}

		float MaxRelevantHeight = TNumericLimits<float>::Max();
		for (const UActorComponent* HeightClampComponent : HeightClampComponents)
		{
			MaxRelevantHeight = FMath::Min(MaxRelevantHeight, Cast<ITempoBoundsHeightClampInterface>(HeightClampComponent)->GetMaxRelevantBoundsHeight());
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
