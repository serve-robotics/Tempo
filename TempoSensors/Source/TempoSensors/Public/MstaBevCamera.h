// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoCamera.h"
#include "MstaBevCamera.generated.h"

struct FMstaColorImageRequest
{
	TempoSensors::ColorImageRequest Request;
	TResponseDelegate<TempoSensors::ColorImage> ResponseContinuation;
};

struct FMstaLabelImageRequest
{
	TempoSensors::LabelImageRequest Request;
	TResponseDelegate<TempoSensors::LabelImage> ResponseContinuation;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOSENSORS_API UMstaBevCamera : public UTempoCamera
{
	GENERATED_BODY()

public:

	// The material instance of this BEV for outputting correct region of MSTA tile
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MSTA|Bev")
	TObjectPtr<UMaterialInstanceDynamic> BevOutputMaterialInstance;

	// The master material BevOuputMaterialInstance is instantiated from, pans and rotates based on component orientation
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MSTA|Bev")
	TObjectPtr<UMaterialInterface> BevOutputMasterMaterial;

public:
	// Sets default values for this component's properties
	UMstaBevCamera();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	FIntPoint CurrentMSTATile = FIntPoint(-99,-99);

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	class AMSTARenderBounds* GetRenderBounds();

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	FIntPoint GetCurrentMSTATile() const {return CurrentMSTATile;};

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	void UpdateMSTATile(FIntPoint TileXY, UTexture2D* TileImage);

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	UMaterialInstanceDynamic* GetBevOutputMaterialInstance();

	// Updates rander target texture with BEV view
	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	void BakeBEV();

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	TArray<FColor> BakeBEVToPixelArray();

	UFUNCTION(BlueprintCallable, Category="MSTA|Bev")
	UTexture2D* BakeBEVToTexture2D();

private:

	UPROPERTY(Transient)
	TObjectPtr<AMSTARenderBounds> RenderBoundsActor;

	void UpdateBevMaterial();

	// TEMPO

public:

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
	#else
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder) override;
	#endif

private:
	TArray<FMstaColorImageRequest> PendingMstaColorRequests;
	TArray<FMstaLabelImageRequest> PendingMstaLabelRequests;

};
