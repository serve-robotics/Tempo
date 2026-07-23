// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "MSTARenderBounds.h"

#include "EngineUtils.h"
#include "ImageUtils.h"
#include "MstaBevCamera.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MSTANotfyInterface.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMstaBake, Log, All);


// Sets default values
AMSTARenderBounds::AMSTARenderBounds()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	CaptureMode = EMSTACapureMode::MSTA_Precache;

	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	SetRootComponent(BoundsBox);
	BoundsBox->SetBoxExtent(FVector(20000,20000, 1000));

	CaptureRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CaptureRoot"));
	CaptureRoot->SetupAttachment(BoundsBox);
	CaptureRoot->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(CaptureRoot);
	SceneCapture->SetRelativeRotation(FRotator(-90.0f, 90.0f, 0.0f));
	SceneCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	SceneCapture->OrthoWidth = 2048;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PostProcessMatOb(
	   TEXT("Material'/TempoSensors/Materials/PP_MSTACustomStencil.PP_MSTACustomStencil'")
	);


	if (PostProcessMatOb.Succeeded())
	{
		SceneCapture->AddOrUpdateBlendable(PostProcessMatOb.Object, 1.0);
	}
	else
	{
		UE_LOG(LogMstaBake, Warning,
			TEXT("Failed to load PP_MSTACustomStencil material from MSTA plugin content"));
	}
}

// Called when the game starts or when spawned
void AMSTARenderBounds::BeginPlay()
{
	Super::BeginPlay();

	FIntPoint TileDimensions = GetNumTiles();
	int32 NumTiles = TileDimensions.X * TileDimensions.Y;
	if (CaptureMode == EMSTACapureMode::MSTA_Precache)
	{
		if (TileCache.Num() != NumTiles)
		{
			RenderAllTiles();
		}
	}
}

// Called every frame
void AMSTARenderBounds::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TArray<FIntPoint> TilesInUse;

	for (UMstaBevCamera* BevComponent : BevComponents)
	{
		if (BevComponent)
		{
			FIntPoint BevTile = GetTileFromWorldLocation(BevComponent->GetComponentLocation());
			if (BevTile != BevComponent->GetCurrentMSTATile())
			{
				//Update it to being in a new tile
				BevComponent->UpdateMSTATile(BevTile, GetTileImage(BevTile));
			}
			TilesInUse.AddUnique(BevTile);
		}
		else
		{
			RemoveBev(BevComponent);
		}
	}

	if (CaptureMode == EMSTACapureMode::MSTA_OnDemand)
	{
		TArray<FIntPoint> TilesToRemove;
		TArray<FIntPoint> TilesInCache;
		TileCache.GenerateKeyArray(TilesInCache);
		for (FIntPoint TileInCache : TilesInCache)
		{
			if (!TilesInUse.Contains(TileInCache))
			{
				TilesToRemove.Add(TileInCache);
			}
		}

		for (FIntPoint TileToRemove : TilesToRemove)
		{
			TileCache.Remove(TileToRemove);
			UE_LOG(LogMstaBake, Log, TEXT("GetNumTiles: Removing MSTA tile at %d , %d"), TileToRemove.X, TileToRemove.Y);
		}
	}
}

void AMSTARenderBounds::RenderTile(int32 TileX, int32 TileY)
{
	// Based on the X and Y, position CaptureRoot

	FVector Origin = GetActorLocation() - BoundsBox->GetScaledBoxExtent();
	Origin.Z = GetActorLocation().Z;

	const FVector2D TileCenter = (FVector2D(TileX, TileY) + FVector2D(0.5, 0.5))* (TileRenderSettings.GetTileSizeUU());

	const float CaptureZ = BoundsBox->GetUnscaledBoxExtent().Z;
	const FVector CaptureLocation = Origin + FVector(TileCenter.X, TileCenter.Y, CaptureZ);
	CaptureRoot->SetWorldLocation(CaptureLocation);

	SceneCapture->OrthoWidth = TileRenderSettings.GetTileSizeUU(true);

	EnsureRenderTarget();
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->CaptureScene();
	SceneCapture->bCaptureEveryFrame = false;

	// Ex. MSTA/Town_06/
	FString CachePath = "MSTA/" + GetWorld()->GetName() + "/";
	// Ex. MSTA_x4_y8
	FString TileName = "MSTA_x" + FString::FromInt(TileX) + "_y" + FString::FromInt(TileY);
	BakeRenderTargetToTexture(FIntPoint(TileX, TileY), CachePath, TileName, CaptureMode == EMSTACapureMode::MSTA_Precache);
}

void AMSTARenderBounds::RenderAllTiles()
{
	TArray<TScriptInterface<IMSTANotfyInterface>> NotifyThese;
	GetMstaNotifyReceivers(NotifyThese);

	for (TScriptInterface<IMSTANotfyInterface> NotifyThis : NotifyThese)
	{
		NotifyThis->Execute_NotifyPreMSTARender(NotifyThis.GetObject());
	}

	FIntPoint NumTile = GetNumTiles();
	for (int32 X = 0; X <NumTile.X; X++ )
	{
		for (int32 Y = 0; Y < NumTile.Y; Y++ )
		{
			RenderTile(X, Y);
		}
	}

	for (TScriptInterface<IMSTANotfyInterface> NotifyThis : NotifyThese)
	{
		NotifyThis->Execute_NotifyPostMSTARender(NotifyThis.GetObject());
	}
}

void AMSTARenderBounds::RenderSingleTile(int32 TileX, int32 TileY)
{
	TArray<TScriptInterface<IMSTANotfyInterface>> NotifyThese;
	GetMstaNotifyReceivers(NotifyThese);

	for (TScriptInterface<IMSTANotfyInterface> NotifyThis : NotifyThese)
	{
		NotifyThis->Execute_NotifyPreMSTARender(NotifyThis.GetObject());
	}

	RenderTile( TileX, TileY);

	for (TScriptInterface<IMSTANotfyInterface> NotifyThis : NotifyThese)
	{
		NotifyThis->Execute_NotifyPostMSTARender(NotifyThis.GetObject());
	}
}

FIntPoint AMSTARenderBounds::GetNumTiles() const
{
	const FVector BoxSize = (BoundsBox->GetScaledBoxExtent() * 2.0);
	const FVector2D BoxXY = FVector2D(BoxSize.X, BoxSize.Y);

	FVector2D Tiles = BoxXY / (TileRenderSettings.GetTileSizeUU());
	FIntPoint NumTiles = FIntPoint(FMath::CeilToInt32(Tiles.X), FMath::CeilToInt32(Tiles.Y));

	UE_LOG(LogMstaBake, Log,
			TEXT("GetNumTiles: Number of MSTA Tiles %d x %d"), NumTiles.X, NumTiles.Y);

	return NumTiles;
}

UTexture2D* AMSTARenderBounds::GetTileImage(int32 X, int32 Y)
{
	if (TileCache.Contains(FIntPoint(X, Y)))
	{
		return *TileCache.Find(FIntPoint(X, Y));
	}
	else if (CaptureMode == EMSTACapureMode::MSTA_OnDemand)
	{
		RenderSingleTile(X, Y);
		return *TileCache.Find(FIntPoint(X, Y));
	}

	UE_LOG(LogMstaBake, Log,
			TEXT("BakeRenderTargetToTexture: Could not find texture for %dx%d"), X, Y);
	return nullptr;
}

FIntPoint AMSTARenderBounds::GetTileFromWorldLocation(FVector WorldLocation)
{
	FVector2D Tile = GetTileLocationFromWorldLocation(WorldLocation);

	return FIntPoint(FMath::Floor(Tile.X), FMath::Floor(Tile.Y));
}

FVector2D AMSTARenderBounds::GetTileLocationFromWorldLocation(FVector WorldLocation)
{
	FVector Origin = GetActorLocation() - BoundsBox->GetScaledBoxExtent();
	FVector WorldDelta = WorldLocation - Origin;

	float TileX = (WorldDelta.X / TileRenderSettings.TileSizeMeters) / 100.0;
	float TileY = (WorldDelta.Y / TileRenderSettings.TileSizeMeters) / 100.0;

	return FVector2D(TileX, TileY);
}

FVector2D AMSTARenderBounds::GetTileBottomLeftLocation(FIntPoint TileXY, bool bWithPadding)
{
	FVector Origin = GetActorLocation() - BoundsBox->GetScaledBoxExtent();
	FVector2D Corner =  FVector2D(TileXY.X * TileRenderSettings.GetTileSizeUU(), TileXY.Y * TileRenderSettings.GetTileSizeUU());
	if (bWithPadding)
	{
		Corner.X -= TileRenderSettings.TileOverlapMeters * 50.0;
		Corner.Y -= TileRenderSettings.TileOverlapMeters * 50.0;
	}

	return Corner;
}

void AMSTARenderBounds::AddBev(class UMstaBevCamera* BevComponent)
{
	if (BevComponent)
	{
		BevComponents.AddUnique(BevComponent);
	}
}

void AMSTARenderBounds::RemoveBev(class UMstaBevCamera* BevComponent)
{
	if (BevComponent)
	{
		BevComponents.Remove(BevComponent);
	}
}

void AMSTARenderBounds::SetModePrecache(bool bCaptureNow)
{
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Modify(); // marks level dirty & records for undo
	}
#endif

	CaptureMode = EMSTACapureMode::MSTA_Precache;

	if (bCaptureNow)
	{
		RenderAllTiles();
	}
}

void AMSTARenderBounds::SetModeOnDemand(bool bFlushCache)
{
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Modify(); // marks level dirty & records for undo
	}
#endif

	CaptureMode = EMSTACapureMode::MSTA_OnDemand;

	if (bFlushCache)
	{
		TileCache.Empty();
	}
}

void AMSTARenderBounds::EnsureRenderTarget()
{
	int32 NumPixels = TileRenderSettings.TileSizePixels;

	if (!TileRenderTarget)
	{
		TileRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MstaTileRT"));
		TileRenderTarget->RenderTargetFormat = RTF_RGBA8;
		TileRenderTarget->bAutoGenerateMips = false;
		TileRenderTarget->InitAutoFormat(NumPixels, NumPixels);
		TileRenderTarget->UpdateResourceImmediate(true);
	}
	else
	{
		if (TileRenderTarget->SizeX != NumPixels ||
			TileRenderTarget->SizeY != NumPixels)
		{
			TileRenderTarget->ResizeTarget(NumPixels, NumPixels);
		}
	}

	if (SceneCapture)
	{
		SceneCapture->TextureTarget = TileRenderTarget;
	}
}

UTexture2D* AMSTARenderBounds::BakeRenderTargetToTexture(const FIntPoint& TileKey, const FString& PackagePath,
	const FString& AssetName, bool bSaveAsAsset)
{
	if (!TileRenderTarget)
    {
        UE_LOG(LogMstaBake, Warning, TEXT("BakeRenderTargetToTexture: TileRenderTarget is null"));
        return nullptr;
    }

    // 1. Read pixels from render target
    FTextureRenderTargetResource* RTResource = TileRenderTarget->GameThread_GetRenderTargetResource();
    if (!RTResource)
    {
        UE_LOG(LogMstaBake, Warning, TEXT("BakeRenderTargetToTexture: RTResource is null"));
        return nullptr;
    }

    const int32 Width  = TileRenderTarget->SizeX;
    const int32 Height = TileRenderTarget->SizeY;

    TArray<FColor> SurfaceData;
    SurfaceData.Reset();
    SurfaceData.AddUninitialized(Width * Height);

    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false);

    if (!RTResource->ReadPixels(SurfaceData, ReadFlags))
    {
        UE_LOG(LogMstaBake, Warning, TEXT("BakeRenderTargetToTexture: ReadPixels failed"));
        return nullptr;
    }

    // 2. Create the UTexture2D
    UTexture2D* NewTexture = nullptr;

#if WITH_EDITOR
    if (bSaveAsAsset)
    {
    	// ---------- EDITOR / ASSET PATH ----------
    	FString CleanPackagePath = PackagePath;
    	if (!CleanPackagePath.StartsWith(TEXT("/Game")))
    	{
    		CleanPackagePath = TEXT("/Game") / CleanPackagePath;
    	}

    	const FString FullPackageName = CleanPackagePath / AssetName;
    	UPackage* Package = CreatePackage(*FullPackageName);
    	if (!Package)
    	{
    		UE_LOG(LogMstaBake, Error,
				TEXT("BakeRenderTargetToTexture: Failed to create package '%s'"),
				*FullPackageName);
    		return nullptr;
    	}

    	Package->FullyLoad();

    	// Use FImageUtils to build the texture (handles platform data & mips)
    	FCreateTexture2DParameters Params;
    	Params.bDeferCompression = false;
    	Params.bSRGB = false;           // semantics, not color
    	Params.CompressionSettings = TC_Default;
    	Params.MipGenSettings = TMGS_NoMipmaps;

    	NewTexture = FImageUtils::CreateTexture2D(
			Width,
			Height,
			SurfaceData,
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			Params
		);

    	if (!NewTexture)
    	{
    		UE_LOG(LogMstaBake, Error,
				TEXT("BakeRenderTargetToTexture: Failed to create UTexture2D '%s'"),
				*FullPackageName);
    		return nullptr;
    	}

    	// Register & save asset
    	FAssetRegistryModule::AssetCreated(NewTexture);
    	bool bDirty = Package->MarkPackageDirty();

    	const FString FilePath = FPackageName::LongPackageNameToFilename(
			FullPackageName,
			FPackageName::GetAssetPackageExtension()
		);

    	FSavePackageArgs SaveArgs;

        UPackage::SavePackage(
            Package,
            NewTexture,
            *FilePath,
            SaveArgs
        );

        UE_LOG(LogMstaBake, Log,
            TEXT("BakeRenderTargetToTexture: Saved MSTA tile asset '%s' to '%s'"),
            *FullPackageName, *FilePath);
    }
    else
#endif // WITH_EDITOR
    {
        // Runtime / transient texture (no asset)
        NewTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (!NewTexture)
        {
            UE_LOG(LogMstaBake, Error,
                TEXT("BakeRenderTargetToTexture: Failed to create transient UTexture2D"));
            return nullptr;
        }

        NewTexture->SRGB = false;
#if WITH_EDITORONLY_DATA
    	NewTexture->MipGenSettings = TMGS_NoMipmaps;
    	NewTexture->CompressionSettings = TC_VectorDisplacementmap;
#endif

        // Fill first mip
        FTexture2DMipMap& Mip = NewTexture->GetPlatformData()->Mips[0];
        void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, SurfaceData.GetData(), Width * Height * sizeof(FColor));
        Mip.BulkData.Unlock();

        NewTexture->UpdateResource();

        UE_LOG(LogMstaBake, Log,
            TEXT("BakeRenderTargetToTexture: Created transient texture %dx%d"), Width, Height);
    }

    // 3. Cache in TileCache
    if (NewTexture)
    {
        TileCache.Add(TileKey, NewTexture);
    }

    return NewTexture;
}

void AMSTARenderBounds::GetMstaNotifyReceivers(TArray<TScriptInterface<IMSTANotfyInterface>>& OutReceivers) const
{
	OutReceivers.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->GetClass()->ImplementsInterface(UMSTANotfyInterface::StaticClass()))
		{
			TScriptInterface<IMSTANotfyInterface> Entry;
			Entry.SetObject(Actor);
			Entry.SetInterface(Cast<IMSTANotfyInterface>(Actor));
			OutReceivers.Add(Entry);
		}
	}
}

UTexture2D* AMSTARenderBounds::GetTileImage(FIntPoint TileXY)
{
	return GetTileImage(TileXY.X, TileXY.Y);
}
