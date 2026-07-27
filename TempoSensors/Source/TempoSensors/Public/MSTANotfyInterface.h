// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MSTANotfyInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UMSTANotfyInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 *
 */
class TEMPOSENSORS_API IMSTANotfyInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	/** Called just before an MSTA tile (or batch) is rendered. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="MSTA")
	void NotifyPreMSTARender();

	/** Called right after an MSTA tile (or batch) has been rendered. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="MSTA")
	void NotifyPostMSTARender();
};
