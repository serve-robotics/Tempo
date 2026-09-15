// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TempoDateTimeSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDateTimeChanged, const FDateTime&, DateTime);

UCLASS(BlueprintType, Blueprintable)
class TEMPOGEOGRAPHIC_API ATempoDateTimeSystem : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FDateTimeChanged DateTimeChangedEvent;

	ATempoDateTimeSystem();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "TempoGeographic", meta = (WorldContext = "WorldContextObject"))
	static ATempoDateTimeSystem* GetTempoDateTimeSystem(UObject* WorldContextObject);

protected:
	const FDateTime& GetSimDateTime() const { return SimDateTime; }

	void AdvanceSimDateTime(const FTimespan& Timespan);

	void BroadcastDateTimeChanged() const;

	void SetDayCycleRelativeRate(float DayCycleRelativeRateIn) { DayCycleRelativeRate = DayCycleRelativeRateIn; }

	// The rate the geographic world time advances faster than real time
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DayCycleRelativeRate = 1.0;

	// The current geographic world date and time
	UPROPERTY(EditAnywhere)
	FDateTime SimDateTime = FDateTime(2024, 4, 3, 11, 0, 0, 0);

	// Explicitly re-capture the captured-scene sky lights so their ambient cubemap matches the current
	// sun position. Needed for headless/offscreen (-RenderOffscreen) datagen runs: the real-time
	// sky-capture path only runs inside the main deferred view render, which never happens offscreen,
	// and the static sky-capture queue is only pumped from UGameEngine::Tick, which is skipped offscreen
	// too. Without this, sensor SceneCaptures sample an empty sky cubemap and shadow sides render
	// near-black. The level SkyLight ships real-time-capture-enabled, so we first force it to static
	// capture at runtime (SetRealTimeCaptureEnabled(false)) — otherwise the static queue pump drops it —
	// then dirty it and pump USkyLightComponent::UpdateSkyCaptureContents here. Safe on-screen (redundant
	// one-time capture there).
	void RecaptureStaticSkyLights();

	// Whether to periodically re-capture static sky lights so the ambient term tracks the moving sun.
	// A single capture at load is enough for short (~30s) episodes; enable this for long day-cycle runs.
	UPROPERTY(EditAnywhere, Category = "TempoGeographic|SkyCapture")
	bool bRecaptureSkyOnSunChange = true;

	// Seconds between periodic static sky re-captures when bRecaptureSkyOnSunChange is true.
	// Ignored for the first capture, which always happens once the sun has been positioned.
	UPROPERTY(EditAnywhere, Category = "TempoGeographic|SkyCapture", meta = (EditCondition = "bRecaptureSkyOnSunChange", ClampMin = "0.0"))
	float SkyRecaptureIntervalSeconds = 5.0f;

	// True once the sun has been positioned (i.e. at least one DateTimeChangedEvent broadcast happened),
	// so the first sky capture reflects a real sun rotation rather than the pre-BeginPlay default.
	bool bSunHasBeenPositioned = false;

	// True once we've done the initial post-load sky capture.
	bool bHasCapturedSkyOnce = false;

	// Real (world) seconds since the last static sky capture, for periodic re-capture.
	float TimeSinceLastSkyCapture = 0.0f;

	friend class UTempoGeographicServiceSubsystem;
};
