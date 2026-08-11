// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "TempoBoundsHeightClampInterface.h"

#include "Components/InstancedStaticMeshComponent.h"
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
			Box.Max.Z = FMath::Min(Box.Max.Z, MaxRelevantHeight.GetValue());
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

	FBox LocalBounds;

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

TArray<FBox> UTempoCoreUtils::GetActorLocalInstanceBounds(const AActor* Actor, bool bIncludeHiddenComponents)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	const TOptional<float> MaxRelevantHeight = FindMaxRelevantBoundsHeight(Actor);

	TArray<FBox> InstanceBounds;

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent->IsVisible() && !bIncludeHiddenComponents)
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
				const FTransform InstanceToActor = InstanceTransform * ComponentToActor;

				FBoxSphereBounds Bounds;
				BodySetup->AggGeom.CalcBoxSphereBounds(Bounds, InstanceToActor);
				FBox InstanceBox = Bounds.GetBox();
				ClampBoxHeight(InstanceBox, MaxRelevantHeight);
				InstanceBounds.Add(InstanceBox);
			}
			continue;
		}

		auto AddComponentBounds = [Actor, &InstanceBounds, &MaxRelevantHeight](const FKAggregateGeom& AggGeom, const FTransform& WorldTransform)
		{
			const FTransform RelativeTransform = WorldTransform.GetRelativeTransform(Actor->GetTransform());
			FBoxSphereBounds Bounds;
			AggGeom.CalcBoxSphereBounds(Bounds, RelativeTransform);
			FBox ComponentBox = Bounds.GetBox();
			ClampBoxHeight(ComponentBox, MaxRelevantHeight);
			InstanceBounds.Add(ComponentBox);
		};

		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			if (const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* SkeletalBodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					AddComponentBounds(SkeletalBodySetup->AggGeom, SkeletalMeshComponent->GetBoneTransform(SkeletalBodySetup->BoneName));
				}
			}
		}
		else if (const UBodySetup* BodySetup = PrimitiveComponent->BodyInstance.GetBodySetup())
		{
			AddComponentBounds(BodySetup->AggGeom, PrimitiveComponent->GetComponentTransform());
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
