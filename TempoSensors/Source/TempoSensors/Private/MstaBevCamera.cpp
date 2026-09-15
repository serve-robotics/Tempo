// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "MstaBevCamera.h"
#include "MSTARenderBounds.h"
#include "TempoSensorsSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogMstaBev, Log, All);

// Sets default values for this component's properties
UMstaBevCamera::UMstaBevCamera()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BevMatObj(
	   TEXT("Material'/TempoSensors/Materials/M_BevRender.M_BevRender'")
   );

	if (BevMatObj.Succeeded() && !BevOutputMasterMaterial)
	{
		BevOutputMasterMaterial = BevMatObj.Object;
	}
	else
	{
		UE_LOG(LogMstaBev, Warning,
			TEXT("Failed to load M_BevRender material from MSTA plugin content"));
	}

	// TEMPO
	// Only images we actually plan to serve from MSTA
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE };

	// We don’t want the built‑in depth pipeline
	SetDepthEnabled(false);

	//TODO: set up something to turn this on/off for full headless vs debugging
	bCaptureEveryFrame = true;

	SizeXY = FIntPoint(256, 256);
}


// Called when the game starts
void UMstaBevCamera::BeginPlay()
{
	Super::BeginPlay();

	if (GetRenderBounds())
	{
		UpdateBevMaterial();
	}

}


// Called every frame
void UMstaBevCamera::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bCaptureEveryFrame)
	{
		if (GetRenderBounds())
		{
			UpdateBevMaterial();
		}
	}
}

AMSTARenderBounds* UMstaBevCamera::GetRenderBounds()
{
	if (RenderBoundsActor)
	{
		return RenderBoundsActor;
	}
	else
	{
		RenderBoundsActor = Cast<AMSTARenderBounds>(UGameplayStatics::GetActorOfClass(GetWorld(), AMSTARenderBounds::StaticClass()));
		if (RenderBoundsActor)
		{
			// Lets the render bounds know this bev needs access to its tile data
			RenderBoundsActor->AddBev(this);
		}
	}

	return RenderBoundsActor;
}

void UMstaBevCamera::UpdateBevMaterial()
{
	if (GetBevOutputMaterialInstance() && GetRenderBounds() && TextureTarget)
	{
		// Using -X as North Up
		GetBevOutputMaterialInstance()->SetScalarParameterValue("Yaw", GetComponentRotation().Yaw - 180.0);

		// Set the size of the tile
		GetBevOutputMaterialInstance()->SetScalarParameterValue("TileSize", GetRenderBounds()->TileRenderSettings.TileSizeMeters);
		GetBevOutputMaterialInstance()->SetScalarParameterValue("TilePadding", GetRenderBounds()->TileRenderSettings.TileOverlapMeters);

		// Set location in tile 0 - 1 space
		FVector2D TileLocation = GetRenderBounds()->GetTileLocationFromWorldLocation(GetComponentLocation());
		TileLocation.X = FMath::Frac(TileLocation.X);
		TileLocation.Y = FMath::Frac(TileLocation.Y);
		FLinearColor Location = FLinearColor(TileLocation.X, TileLocation.Y, 0.0f);
		GetBevOutputMaterialInstance()->SetVectorParameterValue("TileLocation", Location);

		// FString S = "Bev location in tile X_" + FString::SanitizeFloat(TileLocation.X) + " Y_" + FString::SanitizeFloat(TileLocation.Y) + " ";
		// UE_LOG(LogMstaBev, Log, TEXT("%s"), *S);
		const float BevSizeMeters = GetRenderBounds()->BevSettings.SizeMeters;

		// Set the size of the BEV
		GetBevOutputMaterialInstance()->SetScalarParameterValue("BevSize", BevSizeMeters);
	}
}

UMaterialInstanceDynamic* UMstaBevCamera::GetBevOutputMaterialInstance()
{
	if (BevOutputMasterMaterial && !BevOutputMaterialInstance)
	{
		BevOutputMaterialInstance = UMaterialInstanceDynamic::Create(BevOutputMasterMaterial, this);
	}

	return BevOutputMaterialInstance;
}

void UMstaBevCamera::UpdateMSTATile(FIntPoint TileXY, UTexture2D* TileImage)
{
	CurrentMSTATile = TileXY;
	UpdateBevMaterial();
	if (BevOutputMaterialInstance)
	{
		BevOutputMaterialInstance->SetTextureParameterValue("TileImage", TileImage);
	}
}

void UMstaBevCamera::BakeBEV()
{
	// 2. Draw the material into the render target
	UKismetRenderingLibrary::ClearRenderTarget2D(this, TextureTarget, FLinearColor::Transparent);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, TextureTarget, BevOutputMaterialInstance);
}

TArray<FColor> UMstaBevCamera::BakeBEVToPixelArray()
{
	TArray<FColor> SurfaceData;

	if (!BevOutputMaterialInstance || !TextureTarget)
	{
		return SurfaceData;
	}

	SurfaceData.AddUninitialized(TextureTarget->SizeX * TextureTarget->SizeY);

	UWorld* World = GetWorld();
	if (!World)
	{
		return SurfaceData;
	}


	if (!TextureTarget)
	{
		return SurfaceData;
	}

/*
	TextureTarget->RenderTargetFormat = RTF_RGBA8;
	TextureTarget->bAutoGenerateMips = false;
	TextureTarget->InitAutoFormat(Width, Height);
	TextureTarget->UpdateResourceImmediate(true);*/



	// Read back pixels from the render target
	FTextureRenderTargetResource* RTResource = TextureTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return SurfaceData;
	}

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);

	if (!RTResource->ReadPixels(SurfaceData, ReadFlags))
	{
		return SurfaceData;
	}

	return SurfaceData;
}

UTexture2D* UMstaBevCamera::BakeBEVToTexture2D()
{
	TArray<FColor> SurfaceData = BakeBEVToPixelArray();

    // Create the transient UTexture2D and copy pixel data
    UTexture2D* NewTexture = UTexture2D::CreateTransient(TextureTarget->SizeX, TextureTarget->SizeY, PF_B8G8R8A8);
    if (!NewTexture)
    {
        return nullptr;
    }

    NewTexture->SRGB = false;
#if WITH_EDITOR
    NewTexture->CompressionSettings = TC_Default;
    NewTexture->MipGenSettings = TMGS_NoMipmaps;
#endif
    FTexturePlatformData* PlatformData = NewTexture->GetPlatformData();
    if (!PlatformData || PlatformData->Mips.Num() == 0)
    {
        return nullptr;
    }

    FTexture2DMipMap& Mip = PlatformData->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    if (!MipData)
    {
        Mip.BulkData.Unlock();
        return nullptr;
    }

    const SIZE_T NumBytes = static_cast<SIZE_T>(SurfaceData.Num()) * sizeof(FColor);
    FMemory::Memcpy(MipData, SurfaceData.GetData(), NumBytes);
    Mip.BulkData.Unlock();

    NewTexture->UpdateResource();

    return NewTexture;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMstaBevCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UMstaBevCamera::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
    TextureInitFence.Wait();

    if (!TextureTarget)
    {
        return;
    }

    if (TextureTarget->SizeX != SizeXY.X || TextureTarget->SizeY != SizeXY.Y)
    {
        InitRenderTarget();  // same helper as base :llmCitationRef[9]
        return;
    }

    const FTextureRenderTargetResource* RenderTarget = TextureTarget->GameThread_GetRenderTargetResource();
    if (!ensureMsgf(RenderTarget && RenderTarget->IsInitialized(), TEXT("RenderTarget not initialized.")))
    {
        return;
    }

    if (ShouldManageOwnReadback())
    {
        if (!ensureMsgf(StagingTextures.Num() > 0 && StagingTextures[0].IsValid() && StagingTextures[0]->IsValid(), TEXT("StagingTextures were not valid. Skipping capture.")) ||
            !ensureMsgf(StagingTextures[0]->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and StagingTextures did not have same format. Skipping Capture.")))
        {
            return;
        }

        const int32 MaxTextureQueueSize = GetMaxTextureQueueSize();
        while (MaxTextureQueueSize > 0 && TextureReadQueue.Num() >= MaxTextureQueueSize)
        {
            UE_LOG(LogMstaBev, Warning, TEXT("MSTA camera fell behind, evicting oldest frame."));
            TextureReadQueue.EvictOldest();
        }
    }

    // --- CUSTOM PART: render BEV into TextureTarget instead of scene capture ---

	UpdateBevMaterial();
	BakeBEV();

    // --- END CUSTOM PART ---

    if (!ShouldManageOwnReadback())
    {
        return;
    }

    SequenceId++;

    TSharedPtr<FTextureRead> NewRead(MakeTextureRead());
    NewRead->StagingTexture = AcquireNextStagingTexture();

    ENQUEUE_RENDER_COMMAND(SetTempoSceneCaptureRenderFence)(
        [NewRead](FRHICommandList& RHICmdList)
        {
            NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
            RHICmdList.WriteGPUFence(NewRead->RenderFence);
        });

    TextureReadQueue.Enqueue(MoveTemp(NewRead));
}
