// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreUtils.generated.h"

// One "instance" of a multi-instance Actor's decomposed bounds (see GetActorLocalInstanceBounds): a
// box in that instance's own local frame (scaled, but NOT rotated by the instance's own placement)
// plus the Transform (location + rotation, relative to the Actor) needed to place and orient it.
// Mirrors ActorState's local_bounds + transform pattern, just per-instance instead of per-actor.
USTRUCT(BlueprintType)
struct FTempoInstanceBounds
{
	GENERATED_BODY()

	// Axis-aligned box in this instance's own local frame (scale baked in, not rotated).
	UPROPERTY(BlueprintReadOnly, Category = "TempoCoreUtils")
	FBox LocalBounds = FBox(ForceInit);

	// This instance's transform (location + rotation only, no scale -- already baked into LocalBounds)
	// relative to the Actor.
	UPROPERTY(BlueprintReadOnly, Category = "TempoCoreUtils")
	FTransform Transform = FTransform::Identity;
};

UCLASS()
class TEMPOCORE_API UTempoCoreUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	template <typename TEnum>
	static FString GetEnumValueAsString(const TEnum Value, bool bQualified=false)
	{
		FString ValueString = UEnum::GetValueAsString(Value);
		if (!bQualified)
		{
			if (int32 LastColonIdx; ValueString.FindLastChar(':', LastColonIdx))
			{
				ValueString.RightChopInline(LastColonIdx + 1);
			}
		}
		return ValueString;
	}

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject", DeterminesOutputType="Interface"))
	static UWorldSubsystem* GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface);

	// Is the world owning this object a PIE or Game world?
	// Note that UWorld::GetWorld() considers GamePreview and GameRPC worlds to be Game worlds, which we do not.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject"))
	static bool IsGameWorld(const UObject* WorldContextObject);

	// Calculates a tight bounding box of all the Actor's components,
	// axis-aligned with the Actor's local coordinates.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils")
	static FBox GetActorLocalBounds(const AActor* Actor, bool bIncludeHiddenComponents);

	// Like GetActorLocalBounds, but returns one entry per "instance" instead of a single union: one
	// per instance of each InstancedStaticMeshComponent (e.g. each tree in a HISM-based prop line)
	// plus one per other primitive component (mirroring GetActorLocalBounds's per-component
	// contributions, just not unioned together). Each entry's LocalBounds is axis-aligned in THAT
	// INSTANCE's own frame, not the Actor's -- combine it with the entry's Transform to recover the
	// instance's actual oriented box, the same way ActorState's local_bounds + transform do for the
	// whole Actor. (The one exception is a skeletal mesh's per-bone union, which has no single instance
	// rotation to factor out and so reports an identity Transform with its box already in the Actor's
	// frame.) For Actors composed of many placed sub-objects rendered under one Actor for efficiency,
	// this lets each sub-object still report a distinguishable, correctly-oriented bounds. Empty for
	// Actors with no primitive components.
	//
	// SPECIAL CASE: an Actor with a movement CapsuleComponent (every ACharacter, including every
	// pedestrian) always returns exactly one entry derived from the capsule's own shape and oriented to
	// the capsule's own rotation, regardless of how many skeletal mesh sub-components it has -- a
	// multi-part character rig would otherwise report one overlapping box per physics-asset-bearing
	// skeletal mesh for what is visually one pedestrian.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils")
	static TArray<FTempoInstanceBounds> GetActorLocalInstanceBounds(const AActor* Actor, bool bIncludeHiddenComponents);

	// Returns a stable, round-trippable name for an actor, suitable for handing to an external
	// client and using later to look the same actor back up (e.g. via GetActorWithName).
	//
	// AActor::GetActorNameOrLabel() is not stable over an actor's lifetime in editor builds: it
	// returns the FName (which keeps the Blueprint "_C" suffix, e.g. "BP_Foo_C_0") until an actor
	// label is lazily created, after which it returns the de-"_C"'d label ("BP_Foo_0"). A name
	// returned to a client before its label exists therefore fails a later lookup. Materializing
	// the label here pins the value so every subsequent GetActorNameOrLabel() call agrees. This is
	// a no-op in cooked builds, which have no labels (GetActorNameOrLabel() is always GetName()).
	static FString GetActorIdentifier(const AActor* Actor);

	template <typename BaseClass>
	static bool IsMostDerivedSubclass(UClass* Class)
	{
		// RF_NoFlags to include CDO
		for (TObjectIterator<BaseClass> DerivedClass(EObjectFlags::RF_NoFlags); DerivedClass; ++DerivedClass)
		{
			if (DerivedClass->GetClass() != Class && DerivedClass->IsA(Class))
			{
				// There is a more derived version of Class
				return false;
			}
		}

		return true;
	}

	// Wraps all BP calls in FEditorScriptExecutionGuard when in the Editor, which prevents early termination (and lurking
	// bugs due to calls silently being cancelled and returning default values) due to erroneous runaway loop detection.
	template <typename ObjectType, typename FuncType, typename... ArgTypes>
	static auto CallBlueprintFunction(ObjectType* Object, FuncType Function, ArgTypes&&... Args)
	{
		if (Object->GetWorld()->WorldType != EWorldType::Editor)
		{
			return Function(Object, Args...);
		}

		using RetValType = decltype(Function(Object, std::forward<ArgTypes>(Args)...)); // Deduce return type
		if constexpr (std::is_void_v<RetValType>)
		{
			if (!ensureMsgf(IsValid(Object), TEXT("Tried to call Blueprint function on invalid object")))
			{
				return;
			}
			{
				FEditorScriptExecutionGuard ScriptExecutionGuard;
				Function(Object, std::forward<ArgTypes>(Args)...);
			}
		}
		else
		{
			if (!ensureMsgf(IsValid(Object), TEXT("Tried to call Blueprint function on invalid object")))
			{
				return RetValType();
			}
			RetValType RetVal;
			{
				FEditorScriptExecutionGuard ScriptExecutionGuard;
				RetVal = Function(Object, std::forward<ArgTypes>(Args)...);
			}
			return RetVal;
		}
	}
};
