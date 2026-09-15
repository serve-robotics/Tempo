// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MSTANotfyInterface.h"
#include "MSTARenderBounds.generated.h"

USTRUCT(BlueprintType)
struct FMstaBevSettings
{
	// Bird's eye view
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Bev")
	float SizeMeters = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Bev")
	float ResoluionPixels = 256.0f;

	float GetMetersPerPixel() const { return SizeMeters / ResoluionPixels;};

	FMstaBevSettings() {};
};

//Waking wind, but don't want stutter

USTRUCT(BlueprintType)
struct FMstaTileRenderSettings
{
	GENERATED_BODY()

public:

	// Assuming square tiles
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Tile")
	float TileSizeMeters = 200.0;

	// Meters of tile padding per side
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Tile")
	float TileOverlapMeters = 40.f;

	// Size of tile image
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Tile")
	int32 TileSizePixels = 2048;

	// This isn't the final output so it doesn't technically need the same meters per pixel
	//float TileMetrsPerPixel = (30.f / 256.f);

	FMstaTileRenderSettings() {};

	float GetTileSizeUU(bool bWithPadding = false) const
	{
		if (bWithPadding)
		{
			return (TileSizeMeters + TileOverlapMeters) * 100.0;
		}

		return TileSizeMeters * 100.0;
	};
};

UENUM(BlueprintType)
enum EMSTACapureMode
{
	MSTA_Precache	UMETA(DisplayName="Precache All", ToolTip="Render all MSTA tiles of the world in advance and save for reuse"),
	MSTA_OnDemand	UMETA(DisplayName="Tile On Demand", ToolTip="Render a single MSTA tile per BEV upon request"),

};


UCLASS()
class TEMPOSENSORS_API AMSTARenderBounds : public AActor
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "MSTA|Render")
	TEnumAsByte<EMSTACapureMode> CaptureMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Render")
	FMstaBevSettings BevSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Render")
	FMstaTileRenderSettings TileRenderSettings;

	// If true, draw debug shapes in editor to display
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MSTA|Render")
	bool ShowDebug = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category= "MSTA|Copmonents")
	TObjectPtr<class UBoxComponent> BoundsBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category= "MSTA|Copmonents")
	TObjectPtr<class USceneComponent> CaptureRoot;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category= "MSTA|Copmonents")
	TObjectPtr<class USceneCaptureComponent2D> SceneCapture;

public:
	// Sets default values for this actor's properties
	AMSTARenderBounds();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** Render a single tile at integer tile indices (TileX, TileY). */
	void RenderTile(int32 TileX, int32 TileY);

	void RenderTile(FIntPoint TileXY) {RenderTile(TileXY.X, TileXY.Y);} ;

	/** Render all tiles in sequence (can be called in editor or at runtime). */
	UFUNCTION(BlueprintCallable, Category = "MSTA|Render")
	void RenderAllTiles();

	UFUNCTION(BlueprintCallable, Category = "MSTA|Render")
	void RenderSingleTile(int32 TileX, int32 TileY);

	FIntPoint GetNumTiles() const;

	UTexture2D* GetTileImage(int32 X, int32 Y);

	FIntPoint GetTileFromWorldLocation(FVector WorldLocation);

	FVector2D GetTileLocationFromWorldLocation(FVector WorldLocation);

	FVector2D GetTileBottomLeftLocation(FIntPoint TileXY, bool bWithPadding = false);

	UFUNCTION(BlueprintCallable, Category = "MSTA|Bev")
	void AddBev(class UMstaBevCamera* BevComponent);

	UFUNCTION(BlueprintCallable, Category = "MSTA|Bev")
	void RemoveBev(class UMstaBevCamera* BevComponent);

	UFUNCTION(BlueprintCallable, Category = "MSTA|Render")
	void SetModePrecache(bool bCaptureNow = true);

	UFUNCTION(BlueprintCallable, Category = "MSTA|Render")
	void SetModeOnDemand(bool bFlushCache = false);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MSTA|Render")
	TMap<FIntPoint, class UTexture2D*> TileCache;

protected:

	void EnsureRenderTarget();
	UTexture2D* BakeRenderTargetToTexture(
	const FIntPoint& TileKey,
	const FString& PackagePath,
	const FString& AssetName,
	bool bSaveAsAsset);

private:

	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> TileRenderTarget;

	UPROPERTY(Transient)
	TArray<UMstaBevCamera*> BevComponents;

	/** Collect all actors in this world that implement UMSTANotifyInterface. */
	UFUNCTION(BlueprintCallable, Category="MSTA|Notify")
	void GetMstaNotifyReceivers(TArray<TScriptInterface<IMSTANotfyInterface>>& OutReceivers) const;

	UTexture2D* GetTileImage(FIntPoint TileXY);
};

